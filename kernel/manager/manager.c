#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/fsnotify_backend.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <fmac.h>

/* Last: redirects vfs_mkdir/lookup_one_len to the resolved-symbol pointers. */
#include "symbol/symbol_compat.h"

/*
 * vfs_mkdir()/lookup_one_len() are reached through resolved pointers, so
 * disable CFI for this file: the type-hash the module emits for the indirect
 * calls may differ from the running kernel's.
 */
#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

#define TARGET_PACKAGE "me.nekosu.aqnya"
#define TARGET_HASH                                                                                                    \
    "\x98\xd2\x19\x85\x2e\xc3\xd2\x35\x80\xd1\x25\xb7\xb2\x71\x46\x79\x19\x38\xbd\x30\xa9\x9a\xbb\x42\xc9\xfc\xbf\xac\x98\x9e\xd8\xe6"

#define PACKAGES_XML_PATH "/data/system/packages.xml"
#define MAX_PACKAGES_XML_SIZE (8 * 1024 * 1024)
#define MAX_INTERNED_STRINGS 512
#define BUF_SIZE 65536

static kuid_t manager_kuid = INVALID_UID;

bool is_manager(void)
{
    return uid_valid(manager_kuid) && uid_eq(current_uid(), manager_kuid);
}

bool is_manager_uid(uid_t uid)
{
    return uid_valid(manager_kuid) && __kuid_val(manager_kuid) == uid;
}

/*
 * Whether /data/adb/nksu/ncore is known to be present.  Set by the (sleepable)
 * manager scan; read by the su tracepoint hook, which must not sleep.
 */
static bool ncore_ready;

bool is_ncore_ready(void)
{
    return READ_ONCE(ncore_ready);
}

/*
 * ABX (Android Binary XML) wire format, as produced by
 * com.android.modules.utils.BinaryXmlSerializer (AOSP modules-utils).
 *
 * Every event is a single byte: low nibble is an XmlPullParser token and
 * high nibble is an optional data type signal. Strings are written with a
 * 2-byte big-endian length followed by Modified UTF-8 bytes. Names written
 * through writeInternedUTF() are canonicalized: the first occurrence is a
 * 0xffff sentinel followed by the full string, and later occurrences are a
 * 2-byte big-endian index into the intern table.
 */
#define ABX_TOKEN_START_DOCUMENT 0
#define ABX_TOKEN_END_DOCUMENT 1
#define ABX_TOKEN_START_TAG 2
#define ABX_TOKEN_END_TAG 3
#define ABX_TOKEN_TEXT 4
#define ABX_TOKEN_ATTRIBUTE 15

#define ABX_TYPE_NULL 0x10
#define ABX_TYPE_STRING 0x20
#define ABX_TYPE_STRING_INTERNED 0x30
#define ABX_TYPE_BYTES_HEX 0x40
#define ABX_TYPE_BYTES_BASE64 0x50
#define ABX_TYPE_INT 0x60
#define ABX_TYPE_INT_HEX 0x70
#define ABX_TYPE_LONG 0x80
#define ABX_TYPE_LONG_HEX 0x90
#define ABX_TYPE_FLOAT 0xa0
#define ABX_TYPE_DOUBLE 0xb0
#define ABX_TYPE_BOOLEAN_TRUE 0xc0
#define ABX_TYPE_BOOLEAN_FALSE 0xd0

#define ABX_INTERNED_SENTINEL 0xffff

struct abx_reader {
    const u8 *buf;
    size_t len;
    size_t pos;
    const u8 **interned;
    size_t *interned_len;
    int interned_count;
};

static inline int abx_read_byte(struct abx_reader *r, u8 *out)
{
    if (r->pos >= r->len)
        return -1;
    *out = r->buf[r->pos++];
    return 0;
}

static inline int abx_read_u16(struct abx_reader *r, u16 *out)
{
    if (r->pos + 2 > r->len)
        return -1;
    *out = (u16)((r->buf[r->pos] << 8) | r->buf[r->pos + 1]);
    r->pos += 2;
    return 0;
}

static inline int abx_read_u64(struct abx_reader *r, u64 *out)
{
    if (r->pos + 8 > r->len)
        return -1;
    *out = ((u64)r->buf[r->pos] << 56) | ((u64)r->buf[r->pos + 1] << 48) |
           ((u64)r->buf[r->pos + 2] << 40) | ((u64)r->buf[r->pos + 3] << 32) |
           ((u64)r->buf[r->pos + 4] << 24) | ((u64)r->buf[r->pos + 5] << 16) |
           ((u64)r->buf[r->pos + 6] << 8) | (u64)r->buf[r->pos + 7];
    r->pos += 8;
    return 0;
}

/* Read a plain (non-interned) UTF-8 string; returns a view into the buffer. */
static int abx_read_utf(struct abx_reader *r, const u8 **str, size_t *len)
{
    u16 n;

    if (abx_read_u16(r, &n) < 0)
        return -1;
    if (r->pos + n > r->len)
        return -1;
    *str = r->buf + r->pos;
    *len = n;
    r->pos += n;
    return 0;
}

/*
 * Read a string that was written through writeInternedUTF(). The first
 * occurrence carries a 0xffff sentinel followed by the full string, which is
 * appended to the intern table; later occurrences are an index into the table.
 */
static int abx_read_interned(struct abx_reader *r, const u8 **str, size_t *len)
{
    u16 ref;

    if (abx_read_u16(r, &ref) < 0)
        return -1;
    if (ref == ABX_INTERNED_SENTINEL) {
        if (abx_read_utf(r, str, len) < 0)
            return -1;
        if (r->interned_count >= MAX_INTERNED_STRINGS)
            return -1;
        r->interned[r->interned_count] = *str;
        r->interned_len[r->interned_count] = *len;
        r->interned_count++;
    } else {
        if (ref >= r->interned_count)
            return -1;
        *str = r->interned[ref];
        *len = r->interned_len[ref];
    }
    return 0;
}

static inline bool abx_str_eq(const u8 *s, size_t len, const char *lit)
{
    size_t l = strlen(lit);

    return len == l && !memcmp(s, lit, l);
}

static int sha256_bytes(const u8 *data, size_t len, u8 out[32])
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret = -1;

    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm))
        return -1;

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (desc) {
        desc->tfm = tfm;
        if (crypto_shash_init(desc) == 0 && crypto_shash_update(desc, data, len) == 0 &&
            crypto_shash_final(desc, out) == 0)
            ret = 0;
        kfree(desc);
    }
    crypto_free_shash(tfm);
    return ret;
}

/*
 * The manager package's on-disk locations, captured from packages.xml while
 * looking for its signature.  nativeLibraryPath is the extracted jniLibs
 * directory (<codePath>/lib/<abi>); codePath is the fallback for images that
 * do not write it.
 */
struct manager_paths {
    char code_path[256];
    char native_lib_path[256];
};

static void copy_path_attr(char *dst, size_t cap, const u8 *src, size_t len)
{
    size_t n = 0;

    if (src && len) {
        n = len < cap - 1 ? len : cap - 1;
        memcpy(dst, src, n);
    }
    dst[n] = '\0';
}

/*
 * Locate <package name="TARGET_PACKAGE"><sigs><cert key="..."/></sigs></package>
 * in /data/system/packages.xml and compare the SHA-256 of the DER certificate
 * stored in the key attribute against TARGET_HASH.  Also fills `paths` with the
 * package's codePath / nativeLibraryPath when requested.
 */
static bool verify_package_signature(struct manager_paths *paths)
{
    struct file *fp;
    struct abx_reader r = { .interned = NULL, .interned_len = NULL };
    loff_t pos = 0;
    ssize_t rd;
    size_t fsize;
    u8 *buf;
    const u8 *name;
    size_t nlen;
    int depth = 0;
    int pkg_depth = -1;
    int sigs_depth = -1;
    bool valid = false;
    const u8 *cert = NULL;
    size_t cert_len = 0;

    fp = filp_open(PACKAGES_XML_PATH, O_RDONLY, 0);
    if (IS_ERR(fp))
        return false;

    fsize = i_size_read(fp->f_inode);
    if (fsize < 4 || fsize > MAX_PACKAGES_XML_SIZE) {
        filp_close(fp, NULL);
        return false;
    }

    buf = kvmalloc(fsize, GFP_KERNEL);
    if (!buf) {
        filp_close(fp, NULL);
        return false;
    }

    rd = kernel_read(fp, buf, fsize, &pos);
    filp_close(fp, NULL);
    if (rd != (ssize_t)fsize)
        goto out_free;

    /* ABX magic: "ABX\0" */
    if (memcmp(buf, "ABX\0", 4) != 0)
        goto out_free;

    r.buf = buf;
    r.len = fsize;
    r.pos = 4;
    r.interned_count = 0;

    r.interned = kvmalloc_array(MAX_INTERNED_STRINGS, sizeof(*r.interned), GFP_KERNEL);
    r.interned_len = kvmalloc_array(MAX_INTERNED_STRINGS, sizeof(*r.interned_len), GFP_KERNEL);
    if (!r.interned || !r.interned_len)
        goto out_free;

    while (r.pos < r.len) {
        u8 ev, tok, typ;

        if (abx_read_byte(&r, &ev) < 0)
            goto out_free;
        tok = ev & 0x0f;
        typ = ev & 0xf0;

        if (tok == ABX_TOKEN_START_DOCUMENT) {
            continue;
        } else if (tok == ABX_TOKEN_END_DOCUMENT) {
            break;
        } else if (tok == ABX_TOKEN_START_TAG) {
            bool is_sigs, is_cert, is_pkg;
            const u8 *pkg_attr = NULL;
            size_t pkg_attr_len = 0;
            const u8 *code_attr = NULL;
            size_t code_attr_len = 0;
            const u8 *nlib_attr = NULL;
            size_t nlib_attr_len = 0;

            if (abx_read_interned(&r, &name, &nlen) < 0)
                goto out_free;
            depth++;

            is_sigs = (pkg_depth > 0 && sigs_depth < 0) && depth == pkg_depth + 1 &&
                      abx_str_eq(name, nlen, "sigs");
            is_cert = (sigs_depth > 0) && depth == sigs_depth + 1 &&
                      abx_str_eq(name, nlen, "cert");

            /* Consume attributes (if any) until the next non-attribute event. */
            while (r.pos < r.len) {
                size_t save = r.pos;
                u8 ev2, typ2;
                const u8 *aname;
                size_t alen;

                if (abx_read_byte(&r, &ev2) < 0)
                    break;
                if ((ev2 & 0x0f) != ABX_TOKEN_ATTRIBUTE) {
                    r.pos = save;
                    break;
                }
                typ2 = ev2 & 0xf0;

                if (abx_read_interned(&r, &aname, &alen) < 0)
                    goto out_free;

                if (is_cert && abx_str_eq(aname, alen, "key") &&
                    (typ2 == ABX_TYPE_BYTES_HEX || typ2 == ABX_TYPE_BYTES_BASE64)) {
                    u16 blen;

                    if (abx_read_u16(&r, &blen) < 0 || r.pos + blen > r.len)
                        goto out_free;
                    cert = r.buf + r.pos;
                    cert_len = blen;
                    r.pos += blen;
                } else if (typ2 == ABX_TYPE_STRING) {
                    u16 slen;

                    if (abx_read_u16(&r, &slen) < 0 || r.pos + slen > r.len)
                        goto out_free;
                    if (abx_str_eq(aname, alen, "name")) {
                        pkg_attr = r.buf + r.pos;
                        pkg_attr_len = slen;
                    } else if (abx_str_eq(aname, alen, "codePath")) {
                        code_attr = r.buf + r.pos;
                        code_attr_len = slen;
                    } else if (abx_str_eq(aname, alen, "nativeLibraryPath")) {
                        nlib_attr = r.buf + r.pos;
                        nlib_attr_len = slen;
                    }
                    r.pos += slen;
                } else if (typ2 == ABX_TYPE_STRING_INTERNED) {
                    const u8 *v;
                    size_t vlen;

                    if (abx_read_interned(&r, &v, &vlen) < 0)
                        goto out_free;
                    if (abx_str_eq(aname, alen, "name")) {
                        pkg_attr = v;
                        pkg_attr_len = vlen;
                    } else if (abx_str_eq(aname, alen, "codePath")) {
                        code_attr = v;
                        code_attr_len = vlen;
                    } else if (abx_str_eq(aname, alen, "nativeLibraryPath")) {
                        nlib_attr = v;
                        nlib_attr_len = vlen;
                    }
                } else if (typ2 == ABX_TYPE_BYTES_HEX || typ2 == ABX_TYPE_BYTES_BASE64) {
                    u16 blen;

                    if (abx_read_u16(&r, &blen) < 0 || r.pos + blen > r.len)
                        goto out_free;
                    r.pos += blen;
                } else if (typ2 == ABX_TYPE_INT || typ2 == ABX_TYPE_INT_HEX ||
                           typ2 == ABX_TYPE_FLOAT) {
                    if (r.pos + 4 > r.len)
                        goto out_free;
                    r.pos += 4;
                } else if (typ2 == ABX_TYPE_LONG || typ2 == ABX_TYPE_LONG_HEX ||
                           typ2 == ABX_TYPE_DOUBLE) {
                    u64 v;

                    if (abx_read_u64(&r, &v) < 0)
                        goto out_free;
                } else if (typ2 == ABX_TYPE_BOOLEAN_TRUE || typ2 == ABX_TYPE_BOOLEAN_FALSE ||
                           typ2 == ABX_TYPE_NULL) {
                    /* No payload */
                } else {
                    goto out_free;
                }
            }

            is_pkg = (pkg_depth < 0) && abx_str_eq(name, nlen, "package") && pkg_attr &&
                     abx_str_eq(pkg_attr, pkg_attr_len, TARGET_PACKAGE);

            if (is_pkg) {
                pkg_depth = depth;
                if (paths) {
                    copy_path_attr(paths->code_path, sizeof(paths->code_path),
                                   code_attr, code_attr_len);
                    copy_path_attr(paths->native_lib_path,
                                   sizeof(paths->native_lib_path), nlib_attr,
                                   nlib_attr_len);
                }
            }
            if (is_sigs)
                sigs_depth = depth;
            if (cert && cert_len > 0)
                goto check;
        } else if (tok == ABX_TOKEN_END_TAG) {
            if (abx_read_interned(&r, &name, &nlen) < 0)
                goto out_free;
            if (depth == pkg_depth)
                pkg_depth = -1;
            if (depth == sigs_depth)
                sigs_depth = -1;
            depth--;
            if (depth < 0)
                goto out_free;
        } else if (tok == ABX_TOKEN_TEXT || tok == 5 || tok == 6 || tok == 7 || tok == 8 ||
                   tok == 9 || tok == 10) {
            /* TEXT(4) CDSECT(5) ENTITY_REF(6) IGNORABLE_WHITESPACE(7)
             * PROCESSING_INSTRUCTION(8) COMMENT(9) DOCDECL(10) */
            if (typ == ABX_TYPE_STRING) {
                u16 slen;

                if (abx_read_u16(&r, &slen) < 0 || r.pos + slen > r.len)
                    goto out_free;
                r.pos += slen;
            } else if (typ == ABX_TYPE_STRING_INTERNED) {
                if (abx_read_interned(&r, &name, &nlen) < 0)
                    goto out_free;
            } else if (typ == ABX_TYPE_NULL) {
                /* No payload */
            } else {
                goto out_free;
            }
        } else {
            goto out_free;
        }
    }

check:
    if (cert && cert_len > 0) {
        u8 hash[32];

        if (sha256_bytes(cert, cert_len, hash) == 0 && !memcmp(hash, TARGET_HASH, 32))
            valid = true;
    }

out_free:
    kvfree(r.interned);
    kvfree(r.interned_len);
    kvfree(buf);
    return valid;
}

static uid_t get_uid_from_packages_list(const char *package_name)
{
    struct file *file;
    char *buf, *line, *p, *token;
    loff_t pos = 0;
    uid_t target_uid = (uid_t)-1;
    size_t fsize;
    ssize_t read_size;

    file = filp_open("/data/system/packages.list", O_RDONLY, 0);
    if (IS_ERR(file))
        return (uid_t)-1;

    fsize = i_size_read(file->f_inode);
    if (fsize == 0 || fsize > MAX_PACKAGES_XML_SIZE) {
        filp_close(file, NULL);
        return (uid_t)-1;
    }

    /* The list can exceed one read buffer; read it whole. */
    buf = kvmalloc(fsize + 1, GFP_KERNEL);
    if (!buf) {
        filp_close(file, NULL);
        return (uid_t)-1;
    }

    read_size = kernel_read(file, buf, fsize, &pos);
    filp_close(file, NULL);
    if (read_size <= 0) {
        kvfree(buf);
        return (uid_t)-1;
    }
    buf[read_size] = '\0';

    p = buf;
    while ((line = strsep(&p, "\n")) != NULL) {
        token = strsep(&line, " ");
        if (token && strcmp(token, package_name) == 0) {
            token = strsep(&line, " ");
            if (token && kstrtouint(token, 10, &target_uid) == 0)
                break;
        }
    }

    kvfree(buf);
    return target_uid;
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
    for_each_process (p) {
        if (p->flags & PF_KTHREAD)
            continue;

        if (get_task_cmdline(p, cmdline_buf, 256) > 0) {
            if (strncmp(cmdline_buf, "zygote", 6) == 0 || strncmp(cmdline_buf, "zygote64", 8) == 0 ||
                strstr(cmdline_buf, "app_process")) {
                mark_threads_by_pid(p->pid);
                pr_info("[manager] : marked %s (pid=%d, uid=%u)\n", cmdline_buf, p->pid, task_uid(p).val);
            }
        }
    }
    rcu_read_unlock();

    kfree(cmdline_buf);
    return 0;
}

/*
 * The scan runs from a kthread, i.e. in the kernel SELinux domain, which is
 * not allowed to read /data/system.  Borrow a cred switched to the nksu
 * domain (load_policy() grants it allow-any-any) while touching those files,
 * the same way KernelSU wraps its package reads in override_creds().
 */
static struct cred *nksu_scan_cred;

static const struct cred *nksu_scan_creds_begin(void)
{
    if (!nksu_scan_cred) {
        struct cred *cred = prepare_creds();

        if (!cred)
            return NULL;

        cred->cap_effective = CAP_FULL_SET;
        cred->cap_permitted = CAP_FULL_SET;
        cred->cap_bset = CAP_FULL_SET;
        cred->cap_inheritable = CAP_FULL_SET;

        if (set_domain(DOMAIN_CTX, cred)) {
            abort_creds(cred);
            return NULL;
        }
        nksu_scan_cred = cred;
    }

    return override_creds(nksu_scan_cred);
}

static void nksu_scan_creds_end(const struct cred *old)
{
    if (old)
        revert_creds(old);
}

/*
 * Bootstrap /data/adb/nksu/{ncore,bin/busybox} straight from the verified
 * manager package, so root works on the very first boot without the manager
 * having to run `ncore install` from a root shell it does not have yet.
 *
 * The manager ships both binaries as jniLibs (libncore.so and libbusybox.so);
 * the kernel only sees the extracted nativeLibraryPath directory from
 * packages.xml.  This mirrors KernelSU's `is_ksud_exists()` gate and the
 * manager-provided libksud.so, but installs from the kernel so no userspace
 * round-trip is needed.
 */
#define NKSU_NCORE_BOOT_PATH    "/data/adb/nksu/ncore"
#define NKSU_NCORE_BIN_DIR      "/data/adb/nksu/bin"
#define NKSU_NCORE_BUSYBOX_PATH NKSU_NCORE_BIN_DIR "/busybox"
#define NKSU_NCORE_LIB_NAME     "libncore.so"
#define NKSU_NCORE_BUSYBOX_LIB  "libbusybox.so"

static bool kfile_exists(const char *path)
{
    struct file *f = filp_open(path, O_RDONLY, 0);

    if (IS_ERR(f))
        return false;
    filp_close(f, NULL);
    return true;
}

/* mkdir one leaf whose parent already exists. */
static int kdir_mkdir(const char *path, umode_t mode)
{
    char parent[256];
    const char *slash;
    struct path p;
    struct inode *dir;
    struct dentry *dentry;
    size_t len;
    int err;

    slash = strrchr(path, '/');
    if (!slash || slash == path)
        return -EINVAL;

    len = (size_t)(slash - path);
    if (len >= sizeof(parent))
        return -ENAMETOOLONG;
    memcpy(parent, path, len);
    parent[len] = '\0';

    err = kern_path(parent, LOOKUP_DIRECTORY | LOOKUP_FOLLOW, &p);
    if (err)
        return err;

    dir = d_inode(p.dentry);
    inode_lock_nested(dir, I_MUTEX_PARENT);
    dentry = lookup_one_len(slash + 1, p.dentry, (int)strlen(slash + 1));
    if (IS_ERR(dentry)) {
        err = PTR_ERR(dentry);
    } else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
        err = vfs_mkdir(mnt_idmap(p.mnt), dir, dentry, mode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
        err = vfs_mkdir(mnt_user_ns(p.mnt), dir, dentry, mode);
#else
        err = vfs_mkdir(dir, dentry, mode);
#endif
        if (err == -EEXIST)
            err = 0;
        dput(dentry);
    }
    inode_unlock(dir);
    path_put(&p);
    return err;
}

static int kdir_mkdir_p(const char *dir)
{
    char tmp[256];
    size_t n = strlen(dir);
    int err = 0;

    if (n == 0 || n >= sizeof(tmp))
        return -EINVAL;
    memcpy(tmp, dir, n + 1);

    for (char *q = tmp + 1;; q++) {
        if (*q == '/' || *q == '\0') {
            char saved = *q;

            *q = '\0';
            if (!kfile_exists(tmp)) {
                int e = kdir_mkdir(tmp, 0700);

                if (e && !err)
                    err = e;
            }
            *q = saved;
            if (saved == '\0')
                break;
        }
    }
    return err;
}

static int kfile_copy(const char *src, const char *dst)
{
    struct file *in, *out;
    char *buf;
    loff_t ipos = 0, opos = 0;
    ssize_t off;
    int err = 0;

    in = filp_open(src, O_RDONLY, 0);
    if (IS_ERR(in))
        return PTR_ERR(in);

    out = filp_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (IS_ERR(out)) {
        err = PTR_ERR(out);
        filp_close(in, NULL);
        return err;
    }

    buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!buf) {
        err = -ENOMEM;
    } else {
        for (;;) {
            ssize_t r = kernel_read(in, buf, PAGE_SIZE, &ipos);

            if (r < 0) {
                err = (int)r;
                break;
            }
            if (r == 0)
                break;

            off = 0;
            while (off < r) {
                ssize_t w = kernel_write(out, buf + off, (size_t)(r - off), &opos);

                if (w < 0) {
                    err = (int)w;
                    break;
                }
                off += w;
            }
            if (err)
                break;
        }
        kfree(buf);
    }

    filp_close(out, NULL);
    filp_close(in, NULL);
    return err;
}

/* codePath is either the package dir or .../base.apk; reduce it to the dir. */
static void code_path_base(const char *code, char *out, size_t cap)
{
    size_t n = strlen(code);

    if (n > 4 && strcmp(code + n - 4, ".apk") == 0) {
        const char *slash = strrchr(code, '/');

        if (slash) {
            n = (size_t)(slash - code);
            if (n >= cap)
                n = cap - 1;
            memcpy(out, code, n);
            out[n] = '\0';
            return;
        }
    }
    strscpy(out, code, cap);
}

static bool resolve_lib_dir(const struct manager_paths *paths, char *out, size_t cap)
{
    char cand[320];
    char base[256];

    if (paths->native_lib_path[0]) {
        snprintf(cand, sizeof(cand), "%s/%s", paths->native_lib_path, NKSU_NCORE_LIB_NAME);
        if (kfile_exists(cand)) {
            strscpy(out, paths->native_lib_path, cap);
            return true;
        }
    }

    if (paths->code_path[0]) {
        code_path_base(paths->code_path, base, sizeof(base));

        snprintf(cand, sizeof(cand), "%s/lib/arm64/%s", base, NKSU_NCORE_LIB_NAME);
        if (kfile_exists(cand)) {
            snprintf(out, cap, "%s/lib/arm64", base);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/lib/arm/%s", base, NKSU_NCORE_LIB_NAME);
        if (kfile_exists(cand)) {
            snprintf(out, cap, "%s/lib/arm", base);
            return true;
        }
    }
    return false;
}

static int install_ncore_from_manager(const struct manager_paths *paths)
{
    char lib_dir[256];
    char src[320];
    int err;

    if (kfile_exists(NKSU_NCORE_BOOT_PATH)) {
        /*
         * Already bootstrapped.  Re-assert the label every boot: an older
         * build created this as adb_data_file, which init cannot execute, and
         * this also repairs a file whose xattr was lost.
         */
        nksu_relabel_path(NKSU_NCORE_BOOT_PATH);
        nksu_relabel_path(NKSU_NCORE_BUSYBOX_PATH);
        WRITE_ONCE(ncore_ready, true);
        return 0;
    }

    if (!resolve_lib_dir(paths, lib_dir, sizeof(lib_dir))) {
        pr_err("[manager] cannot locate %s in the manager install\n", NKSU_NCORE_LIB_NAME);
        return -ENOENT;
    }

    err = kdir_mkdir_p("/data/adb/nksu");
    if (err && err != -EEXIST) {
        pr_err("[manager] mkdir /data/adb/nksu failed: %d\n", err);
        return err;
    }
    err = kdir_mkdir_p(NKSU_NCORE_BIN_DIR);
    if (err && err != -EEXIST) {
        pr_err("[manager] mkdir %s failed: %d\n", NKSU_NCORE_BIN_DIR, err);
        return err;
    }

    snprintf(src, sizeof(src), "%s/%s", lib_dir, NKSU_NCORE_LIB_NAME);
    err = kfile_copy(src, NKSU_NCORE_BOOT_PATH);
    if (err) {
        pr_err("[manager] copy %s -> %s failed: %d\n", src, NKSU_NCORE_BOOT_PATH, err);
        return err;
    }
    /* Label it nksu_file so init's injected init.rc can exec it. */
    nksu_relabel_path(NKSU_NCORE_BOOT_PATH);

    snprintf(src, sizeof(src), "%s/%s", lib_dir, NKSU_NCORE_BUSYBOX_LIB);
    if (kfile_exists(src)) {
        int berr = kfile_copy(src, NKSU_NCORE_BUSYBOX_PATH);

        if (berr)
            pr_warn("[manager] copy busybox failed: %d\n", berr);
        else
            nksu_relabel_path(NKSU_NCORE_BUSYBOX_PATH);
    } else {
        pr_warn("[manager] %s not found, busybox not installed\n", src);
    }

    pr_info("[manager] installed ncore from %s\n", lib_dir);
    WRITE_ONCE(ncore_ready, true);
    return 0;
}

static int scan_and_apply(void)
{
    const struct cred *old;
    struct manager_paths paths = { 0 };
    uid_t uid;
    int ret = -1;

    old = nksu_scan_creds_begin();

    uid = get_uid_from_packages_list(TARGET_PACKAGE);
    if (uid == (uid_t)-1) {
        pr_err("[manager] Could not find UID for %s\n", TARGET_PACKAGE);
        goto out;
    }

    if (verify_package_signature(&paths)) {
        pr_info("[manager] Verification passed. "
                "Granting privileges to UID %u\n",
                uid);
        /*
         * Record the UID before granting the profile so the store can leave
         * the manager's own entry out of allow.profile: it is re-created on
         * every boot from this verified scan, never restored from disk.
         */
        manager_kuid = make_kuid(current_user_ns(), uid);
        /* Install ncore/busybox before the profile store writes its file so
         * /data/adb/nksu exists for the first save. */
        install_ncore_from_manager(&paths);
        nksu_profile_set_default(uid);
#ifndef CONFIG_NKSU_SYSCALL
        mark_zygote();
#endif
        ret = 0;
    } else {
        pr_err("[manager] Signature mismatch!\n");
    }

out:
    nksu_scan_creds_end(old);
    return ret;
}

static struct task_struct *appscan_thread;
static DECLARE_WAIT_QUEUE_HEAD(appscan_wq);
static atomic_t appscan_pending = ATOMIC_INIT(0);

/*
 * packages.xml observer.
 *
 * PackageManagerService republishes /data/system/packages.xml whenever the set
 * of installed packages changes (it writes a reserve copy and renames it into
 * place), so watch the directory and re-run the manager scan as soon as the
 * manager appears or is updated. The callback runs under the fsnotify SRCU lock
 * and must not sleep, so it only flags the scan thread.
 */
#define APPSCAN_WATCH_DIR "/data/system"
#define APPSCAN_WATCH_NAME "packages.xml"
#define APPSCAN_WATCH_MASK (FS_CREATE | FS_MOVE | FS_EVENT_ON_CHILD)

static struct fsnotify_group *appscan_group;
static struct fsnotify_mark *appscan_mark;

static void appscan_notify(void)
{
    atomic_set(&appscan_pending, 1);
    wake_up_interruptible(&appscan_wq);
}

static int appscan_handle_inode_event(struct fsnotify_mark *mark, u32 mask,
                                      struct inode *inode, struct inode *dir,
                                      const struct qstr *file_name, u32 cookie)
{
    (void)mark;
    (void)mask;
    (void)inode;
    (void)dir;
    (void)cookie;

    if (!file_name || file_name->len != sizeof(APPSCAN_WATCH_NAME) - 1)
        return 0;
    if (memcmp(file_name->name, APPSCAN_WATCH_NAME,
               sizeof(APPSCAN_WATCH_NAME) - 1))
        return 0;

    appscan_notify();
    return 0;
}

static const struct fsnotify_ops appscan_ops = {
    .handle_inode_event = appscan_handle_inode_event,
};

static void appscan_observer_exit(void)
{
    if (appscan_mark) {
        fsnotify_destroy_mark(appscan_mark, appscan_group);
        fsnotify_put_mark(appscan_mark);
        appscan_mark = NULL;
    }
    if (appscan_group) {
        fsnotify_put_group(appscan_group);
        appscan_group = NULL;
    }
}

static int appscan_observer_init(void)
{
    const struct cred *old;
    struct fsnotify_mark *mark;
    struct path kpath;
    int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
    appscan_group = fsnotify_alloc_group(&appscan_ops, 0);
#else
    appscan_group = fsnotify_alloc_group(&appscan_ops);
#endif
    if (IS_ERR(appscan_group)) {
        ret = PTR_ERR(appscan_group);
        appscan_group = NULL;
        return ret;
    }

    /* /data/system is only reachable under nksu's domain + full caps. */
    old = nksu_scan_creds_begin();
    ret = kern_path(APPSCAN_WATCH_DIR, LOOKUP_FOLLOW, &kpath);
    nksu_scan_creds_end(old);
    if (ret)
        goto out_group;

    mark = kzalloc(sizeof(*mark), GFP_KERNEL);
    if (!mark) {
        ret = -ENOMEM;
        path_put(&kpath);
        goto out_group;
    }

    fsnotify_init_mark(mark, appscan_group);
    mark->mask = APPSCAN_WATCH_MASK;
    ret = fsnotify_add_inode_mark(mark, d_inode(kpath.dentry), 0);
    path_put(&kpath);
    if (ret) {
        fsnotify_put_mark(mark);
        goto out_group;
    }

    appscan_mark = mark;
    pr_info("[manager] watching %s/%s\n", APPSCAN_WATCH_DIR, APPSCAN_WATCH_NAME);
    return 0;

out_group:
    pr_warn("[manager] package observer unavailable (%d)\n", ret);
    fsnotify_put_group(appscan_group);
    appscan_group = NULL;
    return ret;
}

static int appscan_retry_thread(void *data)
{
    int tries = 120; /* first boot: up to ~60s of retries */

    while (!kthread_should_stop()) {
        if (scan_and_apply() == 0) {
            pr_info("[manager] manager profile applied\n");
            tries = 0; /* applied: park until packages.xml changes */
        } else if (tries > 0 && --tries == 0) {
            pr_info("[manager] manager not found yet, waiting for package changes\n");
        }

        if (tries > 0) {
            /*
             * First boot: retry every 500ms, but wake up immediately when
             * packages.xml changes.
             */
            wait_event_interruptible_timeout(appscan_wq,
                                             kthread_should_stop() ||
                                                 atomic_xchg(&appscan_pending, 0),
                                             msecs_to_jiffies(500));
        } else {
            /*
             * Applied, or out of retries: wait for the app list to change and
             * then re-run the scan, so the manager is picked up without a
             * reboot.
             */
            wait_event_interruptible(appscan_wq,
                                     kthread_should_stop() ||
                                         atomic_xchg(&appscan_pending, 0));
        }
    }

    /*
     * Never return on our own: a kthread that exits while the module still
     * holds its task_struct makes the later kthread_stop() touch freed
     * memory.
     */
    wait_event_interruptible(appscan_wq, kthread_should_stop());
    return 0;
}

int appscan_init(void)
{
    int ret;

    pr_info("[manager] Module starting scan...\n");

    appscan_observer_init();

    /*
     * Always keep the scan thread around: the packages.xml observer wakes it
     * whenever the app list changes. A first-stage (vendor_boot) load starts
     * before PackageManagerService has written the package list, so the first
     * successful scan usually comes from the retry loop.
     */
    appscan_thread = kthread_run(appscan_retry_thread, NULL, "nksu-appscan");
    if (IS_ERR(appscan_thread)) {
        ret = PTR_ERR(appscan_thread);
        appscan_thread = NULL;
        appscan_observer_exit();
        return ret;
    }
    return 0;
}

void appscan_exit(void)
{
    if (appscan_thread) {
        kthread_stop(appscan_thread);
        appscan_thread = NULL;
    }
    appscan_observer_exit();
    if (nksu_scan_cred) {
        put_cred(nksu_scan_cred);
        nksu_scan_cred = NULL;
    }
}

#if defined(__clang__)
#pragma clang attribute pop
#endif
