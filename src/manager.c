#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <fmac.h>

#define TARGET_PACKAGE "me.nekosu.aqnya"
#define TARGET_HASH                                                                                                    \
    "\x98\xd2\x19\x85\x2e\xc3\xd2\x35\x80\xd1\x25\xb7\xb2\x71\x46\x79\x19\x38\xbd\x30\xa9\x9a\xbb\x42\xc9\xfc\xbf\xac\x98\x9e\xd8\xe6"

#define PACKAGES_XML_PATH "/data/system/packages.xml"
#define MAX_PACKAGES_XML_SIZE (8 * 1024 * 1024)
#define MAX_INTERNED_STRINGS 512
#define BUF_SIZE 65536

static kuid_t manager_kuid;

bool is_manager(void)
{
    return uid_valid(manager_kuid) && uid_eq(current_uid(), manager_kuid);
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
 * Locate <package name="TARGET_PACKAGE"><sigs><cert key="..."/></sigs></package>
 * in /data/system/packages.xml and compare the SHA-256 of the DER certificate
 * stored in the key attribute against TARGET_HASH.
 */
static bool verify_package_signature(void)
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

            if (is_pkg)
                pkg_depth = depth;
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
    ssize_t read_size;

    buf = kmalloc(BUF_SIZE, GFP_KERNEL);
    if (!buf)
        return (uid_t)-1;

    file = filp_open("/data/system/packages.list", O_RDONLY, 0);
    if (IS_ERR(file)) {
        kfree(buf);
        return (uid_t)-1;
    }

    read_size = kernel_read(file, buf, BUF_SIZE - 1, &pos);
    if (read_size > 0) {
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
    }

    filp_close(file, NULL);
    kfree(buf);
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

static int scan_and_apply(void)
{
    uid_t uid;
    int ret = -1;

    uid = get_uid_from_packages_list(TARGET_PACKAGE);
    if (uid == (uid_t)-1) {
        pr_err("[manager] Could not find UID for %s\n", TARGET_PACKAGE);
        return -1;
    }

    if (verify_package_signature()) {
        pr_info("[manager] Verification passed. "
                "Granting privileges to UID %u\n",
                uid);
        nksu_profile_set_default(uid);
        manager_kuid = make_kuid(current_user_ns(), uid);
#ifndef CONFIG_NKSU_SYSCALL
        mark_zygote();
#endif
        ret = 0;
    } else {
        pr_err("[manager] Signature mismatch!\n");
    }

    return ret;
}

int appscan_init(void)
{
    pr_info("[manager] Module starting scan...\n");
    return scan_and_apply();
}
