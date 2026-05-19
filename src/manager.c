#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/kernel.h>
#include <linux/dirent.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/version.h>
#include <linux/sched/signal.h>
#include <fmac.h>

#define TARGET_PACKAGE "me.nekosu.aqnya"
#define TARGET_HASH "\x44\xad\x9d\x68\x78\xb7\x23\xa6\x77\x7e\x95\x48\x64\x4b\x73\x44\xf9\x91\x15\xa1\x32\x81\x88\xf6\x60\x9e\x8d\x4a\xa1\x1d\x6c\x33"

#define APK_PATH_MAX 512
#define BUF_SIZE 65536
#define EOCD_SEARCH_SIZE 65557

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
#define FILLDIR_RETURN_TYPE      bool
#define FILLDIR_ACTOR_CONTINUE   true
#define FILLDIR_ACTOR_STOP       false
#else
#define FILLDIR_RETURN_TYPE      int
#define FILLDIR_ACTOR_CONTINUE   0
#define FILLDIR_ACTOR_STOP       (-EINVAL)
#endif

#define DATA_PATH_LEN APK_PATH_MAX

static kuid_t manager_kuid;

struct data_path {
	char dirpath[DATA_PATH_LEN];
	int depth;
	struct list_head list;
};

struct apk_scan_ctx {
	struct dir_context ctx;
	struct list_head *data_path_list;
	char *parent_dir;
	const char *target_pkg;
	char *found_path;
	int depth;
	int *stop;
};

bool is_manager(void)
{
	return uid_valid(manager_kuid) && uid_eq(current_uid(), manager_kuid);
}

static FILLDIR_RETURN_TYPE apk_actor(struct dir_context *ctx,
				     const char *name, int namelen,
				     loff_t off, u64 ino, unsigned int d_type)
{
	struct apk_scan_ctx *s = container_of(ctx, struct apk_scan_ctx, ctx);
	char fullpath[DATA_PATH_LEN];

	if (!s)
		return FILLDIR_ACTOR_STOP;

	if (s->stop && *s->stop)
		return FILLDIR_ACTOR_STOP;

	if (!strncmp(name, ".", namelen) || !strncmp(name, "..", namelen))
		return FILLDIR_ACTOR_CONTINUE;

	if (d_type == DT_DIR && namelen >= 8 &&
	    !strncmp(name, "vmdl", 4) &&
	    !strncmp(name + namelen - 4, ".tmp", 4))
		return FILLDIR_ACTOR_CONTINUE;

	if (snprintf(fullpath, DATA_PATH_LEN, "%s/%.*s",
		     s->parent_dir, namelen, name) >= DATA_PATH_LEN) {
		pr_err("[manager] path too long: %s/%.*s\n",
		       s->parent_dir, namelen, name);
		return FILLDIR_ACTOR_CONTINUE;
	}

	if (d_type == DT_DIR && s->depth == 2) {
		struct data_path *dp = kzalloc(sizeof(*dp), GFP_ATOMIC);
		if (!dp)
			return FILLDIR_ACTOR_CONTINUE;
		strscpy(dp->dirpath, fullpath, DATA_PATH_LEN);
		dp->depth = 1;
		list_add_tail(&dp->list, s->data_path_list);
	} else if (d_type == DT_DIR && s->depth == 1) {
		if (strnstr(name, s->target_pkg, namelen)) {
			struct data_path *dp = kzalloc(sizeof(*dp), GFP_ATOMIC);
			if (!dp)
				return FILLDIR_ACTOR_CONTINUE;
			strscpy(dp->dirpath, fullpath, DATA_PATH_LEN);
			dp->depth = 0;
			list_add_tail(&dp->list, s->data_path_list);
		}
	} else if (d_type == DT_REG && s->depth == 0) {
		if (namelen == 8 && !strncmp(name, "base.apk", 8)) {
			strscpy(s->found_path, fullpath, DATA_PATH_LEN);
			if (s->stop)
				*s->stop = 1;
		}
	}

	return FILLDIR_ACTOR_CONTINUE;
}

static int find_apk_path(const char *package_name, char *apk_path)
{
	int i, stop = 0;
	unsigned long data_app_magic = 0;
	struct list_head data_path_list;
	struct data_path root_entry;

	INIT_LIST_HEAD(&data_path_list);
	strscpy(root_entry.dirpath, "/data/app", DATA_PATH_LEN);
	root_entry.depth = 2;
	list_add_tail(&root_entry.list, &data_path_list);

	for (i = 2; i >= 0; i--) {
		struct data_path *pos, *n;

		list_for_each_entry_safe(pos, n, &data_path_list, list) {
			struct apk_scan_ctx ctx = {
				.ctx.actor = apk_actor,
				.data_path_list = &data_path_list,
				.parent_dir = pos->dirpath,
				.target_pkg = package_name,
				.found_path = apk_path,
				.depth = pos->depth,
				.stop = &stop,
			};
			struct file *dir;

			if (stop)
				goto del;

			if (pos->depth != i)
				continue;

			dir = filp_open(pos->dirpath, O_RDONLY | O_NOFOLLOW, 0);
			if (IS_ERR(dir)) {
				pr_err("[manager] open failed: %s (%ld)\n",
				       pos->dirpath, PTR_ERR(dir));
				goto del;
			}

			if (!data_app_magic) {
				data_app_magic = dir->f_inode->i_sb->s_magic;
				pr_info("[manager] /data/app fs magic: 0x%lx\n",
					data_app_magic);
			} else if (dir->f_inode->i_sb->s_magic !=
				   data_app_magic) {
				pr_info("[manager] skipping cross-fs dir: %s\n",
					pos->dirpath);
				filp_close(dir, NULL);
				goto del;
			}

			iterate_dir(dir, &ctx.ctx);
			filp_close(dir, NULL);
del:
			list_del(&pos->list);
			if (pos != &root_entry)
				kfree(pos);
		}

		if (stop)
			break;
	}

	{
		struct data_path *pos, *n;
		list_for_each_entry_safe(pos, n, &data_path_list, list) {
			list_del(&pos->list);
			if (pos != &root_entry)
				kfree(pos);
		}
	}

	return stop ? 0 : -1;
}

static uid_t get_uid_from_packages_list(const char *package_name)
{
	struct file *file;
	char *buf, *line, *p, *token;
	loff_t pos = 0;
	uid_t target_uid = (uid_t) - 1;
	ssize_t read_size;

	buf = kmalloc(BUF_SIZE, GFP_KERNEL);
	if (!buf)
		return (uid_t) - 1;

	file = filp_open("/data/system/packages.list", O_RDONLY, 0);
	if (IS_ERR(file)) {
		kfree(buf);
		return (uid_t) - 1;
	}

	read_size = kernel_read(file, buf, BUF_SIZE - 1, &pos);
	if (read_size > 0) {
		buf[read_size] = '\0';
		p = buf;
		while ((line = strsep(&p, "\n")) != NULL) {
			token = strsep(&line, " ");
			if (token && strcmp(token, package_name) == 0) {
				token = strsep(&line, " ");
				if (token
				    && kstrtouint(token, 10, &target_uid) == 0)
					break;
			}
		}
	}

	filp_close(file, NULL);
	kfree(buf);
	return target_uid;
}

static bool verify_apk_signature(const char *path, const u8 *expected_hash)
{
	struct file *fp;
	loff_t pos;
	u32 size4;
	u64 size8, size_of_block;
	u8 buffer[0x11] = { 0 };
	u8 *cert = NULL;
	int loop;

	bool v2_valid = false;
	int v2_count = 0;
	bool v3_exist = false;
	bool v3_1_exist = false;

	fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp))
		return false;

	fp->f_mode |= FMODE_NONOTIFY;

	for (int i = 0;; i++) {
		u16 n;
		pos = generic_file_llseek(fp, -i - 2, SEEK_END);
		if (kernel_read(fp, &n, 2, &pos) != 2)
			goto out;

		if (n == i) {
			pos -= 22;
			if (kernel_read(fp, &size4, 4, &pos) != 4)
				goto out;

			if (size4 == 0x06054b50u)
				break;
		}

		if (i == 0xffff)
			goto out;
	}

	pos += 12;
	if (kernel_read(fp, &size4, 4, &pos) != 4)
		goto out;

	pos = size4 - 0x18;

	if (kernel_read(fp, &size8, 8, &pos) != 8)
		goto out;

	if (kernel_read(fp, buffer, 0x10, &pos) != 0x10)
		goto out;

	if (memcmp(buffer, "APK Sig Block 42", 16))
		goto out;

	pos = size4 - (size8 + 8);

	if (kernel_read(fp, &size_of_block, 8, &pos) != 8)
		goto out;

	if (size_of_block != size8)
		goto out;

	loop = 0;

	while (loop++ < 10) {
		u32 id;
		u32 offset = 0;

		if (kernel_read(fp, &size8, 8, &pos) != 8)
			break;

		if (size8 == size_of_block)
			break;

		if (kernel_read(fp, &id, 4, &pos) != 4)
			break;

		offset = 4;

		if (id == 0x7109871a) {
			u32 seq_len, signer_len;
			u32 certs_seq_len, cert_len;

			v2_count++;

			if (kernel_read(fp, &seq_len, 4, &pos) != 4)
				break;
			if (kernel_read(fp, &signer_len, 4, &pos) != 4)
				break;

			if (seq_len != signer_len + 4) {
				pr_warn
				    ("[manager] V2 Reject: Multiple signers detected!\n");
				break;
			}

			if (kernel_read(fp, &size4, 4, &pos) != 4)
				break;

			offset += 12;

			if (kernel_read(fp, &size4, 4, &pos) != 4)
				break;
			pos += size4;
			offset += 4 + size4;

			if (kernel_read(fp, &certs_seq_len, 4, &pos) != 4)
				break;
			if (kernel_read(fp, &cert_len, 4, &pos) != 4)
				break;

			offset += 8;

			if (certs_seq_len != cert_len + 4) {
				pr_warn
				    ("[manager] V2 Reject: Multiple certificates detected!\n");
				break;
			}

			if (cert_len == 0 || cert_len > BUF_SIZE)
				break;

			cert = kmalloc(cert_len, GFP_KERNEL);
			if (!cert)
				break;

			if (kernel_read(fp, cert, cert_len, &pos) != cert_len) {
				kfree(cert);
				break;
			}

			{
				struct crypto_shash *tfm;
				struct shash_desc *desc;
				u8 hash[32];

				tfm = crypto_alloc_shash("sha256", 0, 0);
				if (!IS_ERR(tfm)) {
					desc =
					    kmalloc(sizeof(*desc) +
						    crypto_shash_descsize(tfm),
						    GFP_KERNEL);
					if (desc) {
						desc->tfm = tfm;
						crypto_shash_init(desc);
						crypto_shash_update(desc, cert,
								    cert_len);
						crypto_shash_final(desc, hash);

						if (!memcmp
						    (hash, expected_hash, 32))
							v2_valid = true;

						kfree(desc);
					}
					crypto_free_shash(tfm);
				}
			}

			kfree(cert);

		} else if (id == 0xf05368c0) {
			v3_exist = true;
		} else if (id == 0x1b93ad61) {
			v3_1_exist = true;
		}

		pos += (size8 - offset);
	}

	if (v2_count != 1)
		v2_valid = false;

out:
	filp_close(fp, NULL);

	if (v3_exist || v3_1_exist)
		return false;

	return v2_valid;
}

static int get_task_cmdline(struct task_struct *task, char *buffer, int buflen)
{
	struct mm_struct *mm;
	unsigned long arg_start, arg_end, len;
	int res = 0;

	mm = get_task_mm(task);
	if (!mm)
		return 0;

	down_read(&mm->mmap_lock);
	arg_start = mm->arg_start;
	arg_end = mm->arg_end;
	up_read(&mm->mmap_lock);

	len = arg_end - arg_start;
	if (len > buflen - 1)
		len = buflen - 1;

	if (len > 0) {
		res = access_process_vm(task, arg_start, buffer, len, 0);
		if (res > 0)
			buffer[res] = '\0';
		else
			buffer[0] = '\0';
	}

	mmput(mm);
	return res;
}

static int mark_zygote(void)
{
	struct task_struct *p;
	char *cmdline_buf;

	cmdline_buf = kmalloc(256, GFP_KERNEL);
	if (!cmdline_buf)
		return -ENOMEM;

	rcu_read_lock();
	for_each_process(p) {
		if (p->flags & PF_KTHREAD)
			continue;

		if (get_task_cmdline(p, cmdline_buf, 256) > 0) {
			if (strncmp(cmdline_buf, "zygote", 6) == 0 ||
			    strncmp(cmdline_buf, "zygote64", 8) == 0 ||
			    strstr(cmdline_buf, "app_process")) {
				mark_threads_by_pid(p->pid);
				pr_info
				    ("[manager] : marked %s (pid=%d, uid=%u)\n",
				     cmdline_buf, p->pid, task_uid(p).val);
			}
		}
	}
	rcu_read_unlock();

	kfree(cmdline_buf);
	return 0;
}

static int scan_and_apply(void)
{
	uid_t uid;
	char *apk_path;
	int ret = -1;

	uid = get_uid_from_packages_list(TARGET_PACKAGE);
	if (uid == (uid_t) - 1) {
		pr_err("[manager] Could not find UID for %s\n", TARGET_PACKAGE);
		return -1;
	}

	apk_path = kmalloc(APK_PATH_MAX, GFP_KERNEL);
	if (!apk_path)
		return -ENOMEM;

	if (find_apk_path(TARGET_PACKAGE, apk_path) == 0) {
		pr_info("[manager] Found APK: %s\n", apk_path);
		if (verify_apk_signature(apk_path, (const u8 *)TARGET_HASH)) {
			pr_info("[manager] Verification passed. "
				"Granting privileges to UID %u\n", uid);
			nksu_profile_set_default(uid);
			manager_kuid = make_kuid(current_user_ns(), uid);
#ifndef CONFIG_NKSU_SYSCALL
			mark_zygote();
#endif
			ret = 0;
		} else {
			pr_err("[manager] Signature mismatch!\n");
		}
	} else {
		pr_err("[manager] Could not find APK for %s\n", TARGET_PACKAGE);
	}

	kfree(apk_path);
	return ret;
}

int appscan_init(void)
{
	pr_info("[manager] Module starting scan...\n");
	return scan_and_apply();
}
