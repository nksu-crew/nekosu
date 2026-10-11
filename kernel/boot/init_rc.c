// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * nksu -- KernelSU-style init.rc injection.
 *
 * KernelSU hooks init's read() of /system/etc/init/hw/init.rc through the
 * syscall table (__NR_read / __NR_fstat) and appends a static rc that execs
 * its userspace daemon ksud at the well-defined boot stages.  nksu does the
 * same, exec'ing ncore instead:
 *
 *   on post-fs-data
 *       exec u:r:nksu:s0 root -- /data/adb/nksu/ncore post-fs-data
 *   on nonencrypted
 *   on property:vold.decrypt=trigger_restart_framework
 *       exec u:r:nksu:s0 root -- /data/adb/nksu/ncore services
 *   on property:sys.boot_completed=1
 *       exec u:r:nksu:s0 root -- /data/adb/nksu/ncore boot-completed
 *
 * ncore (userspace/ of the ncore project, shipped by the manager as
 * /data/adb/nksu/ncore) is the KernelSU-compatible module runtime.  The kernel
 * only edits init's view of init.rc.  Because init's `exec` is synchronous, the
 * post-fs-data hooks (metamodule mount included) finish before init continues,
 * so modules are mounted before zygote/system_server start.
 *
 * The read proxy is a straight port of ksu_install_rc_hook(): the first read()
 * of /system/etc/init/hw/init.rc replaces the file's file_operations with a
 * proxy that, once the original read hits EOF, appends the static rc.  fstat
 * is hooked too so the reported file size includes the appended text.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include <linux/uaccess.h>
#include <linux/uio.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <asm/current.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>

#include <fmac.h>
#include "boot/init_rc.h"
#include "hook/syscall.h"

#define NKSU_RC_INIT_PATH "/system/etc/init/hw/init.rc"
#define NKSU_RC_INIT_PATH_LEGACY "/system/etc/init.rc"

/* nksu domain; see selinux/selinux.h */
#define NKSU_RC_CONTEXT DOMAIN_CTX

/* The rc that init parses; it just reaches ncore at each stage. */
static const char nksu_rc[] =
    "\n"
    "on post-fs-data\n"
    "    exec " NKSU_RC_CONTEXT " root -- " NKSU_NCORE_PATH " post-fs-data\n"
    "\n"
    "on nonencrypted\n"
    "    exec " NKSU_RC_CONTEXT " root -- " NKSU_NCORE_PATH " services\n"
    "\n"
    "on property:vold.decrypt=trigger_restart_framework\n"
    "    exec " NKSU_RC_CONTEXT " root -- " NKSU_NCORE_PATH " services\n"
    "\n"
    "on property:sys.boot_completed=1\n"
    "    exec " NKSU_RC_CONTEXT " root -- " NKSU_NCORE_PATH " boot-completed\n"
    "\n";

static const size_t nksu_rc_len = sizeof(nksu_rc) - 1;
static ssize_t nksu_rc_pos;
static bool nksu_rc_hooked;

/* f_op proxy state, mirroring KernelSU's fops_proxy */
static struct file_operations nksu_fops_proxy;
static ssize_t (*nksu_orig_read)(struct file *, char __user *, size_t, loff_t *);
static ssize_t (*nksu_orig_read_iter)(struct kiocb *, struct iov_iter *);

/* saved originals for the two syscall-table hooks */
static syscall_fn_t nksu_orig_read_sys;
static syscall_fn_t nksu_orig_fstat_sys;

/* Same match as KernelSU's is_init_rc(): only init, only the hw init.rc. */
static bool nksu_rc_is_init_rc(struct file *file)
{
    char buf[256];
    char *dpath;
    const char *short_name;

    if (strcmp(current->comm, "init"))
        return false;

    if (!file->f_path.dentry || !d_is_reg(file->f_path.dentry))
        return false;

    short_name = file->f_path.dentry->d_name.name;
    if (!short_name || strcmp(short_name, "init.rc"))
        return false;

    dpath = d_path(&file->f_path, buf, sizeof(buf));
    if (IS_ERR(dpath))
        return false;

    return !strcmp(dpath, NKSU_RC_INIT_PATH) ||
           !strcmp(dpath, NKSU_RC_INIT_PATH_LEGACY);
}

/* append the rc once the original read has reached EOF (KernelSU read_proxy) */
static ssize_t nksu_rc_read_proxy(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    ssize_t ret = 0;
    size_t append_count;

    if (nksu_rc_pos && nksu_rc_pos < (ssize_t)nksu_rc_len)
        goto append_rc;

    ret = nksu_orig_read(file, buf, count, pos);
    if (ret != 0)
        return ret;
    if (nksu_rc_pos >= (ssize_t)nksu_rc_len)
        return ret;

append_rc:
    if (nksu_rc_pos < (ssize_t)nksu_rc_len) {
        append_count = nksu_rc_len - (size_t)nksu_rc_pos;
        if (append_count > count - (size_t)ret)
            append_count = count - (size_t)ret;
        if (copy_to_user(buf + ret, nksu_rc + nksu_rc_pos, append_count))
            return ret;
        nksu_rc_pos += append_count;
        ret += append_count;
    }

    return ret;
}

static ssize_t nksu_rc_read_iter_proxy(struct kiocb *iocb, struct iov_iter *to)
{
    ssize_t ret = 0;
    size_t append_count;

    if (nksu_rc_pos && nksu_rc_pos < (ssize_t)nksu_rc_len)
        goto append_rc;

    ret = nksu_orig_read_iter(iocb, to);
    if (ret != 0)
        return ret;
    if (nksu_rc_pos >= (ssize_t)nksu_rc_len)
        return ret;

append_rc:
    if (nksu_rc_pos < (ssize_t)nksu_rc_len) {
        append_count = copy_to_iter(nksu_rc + nksu_rc_pos,
                                    nksu_rc_len - (size_t)nksu_rc_pos, to);
        if (!append_count)
            return ret;
        nksu_rc_pos += append_count;
        ret += append_count;
    }

    return ret;
}

/* install the read proxy on the first init.rc read (KernelSU ksu_install_rc_hook) */
static void nksu_rc_install(struct file *file)
{
    if (!nksu_rc_is_init_rc(file))
        return;

    if (nksu_rc_hooked)
        return;
    nksu_rc_hooked = true;

    memcpy(&nksu_fops_proxy, file->f_op, sizeof(struct file_operations));

    nksu_orig_read = file->f_op->read;
    if (nksu_orig_read)
        nksu_fops_proxy.read = nksu_rc_read_proxy;

    nksu_orig_read_iter = file->f_op->read_iter;
    if (nksu_orig_read_iter)
        nksu_fops_proxy.read_iter = nksu_rc_read_iter_proxy;

    file->f_op = &nksu_fops_proxy;

    pr_info("nksu: init.rc hooked, appending %zu bytes\n", nksu_rc_len);
}

/* __NR_read: detect init reading init.rc before the real read runs */
static long nksu_sys_read(const struct pt_regs *regs)
{
    unsigned int fd = (unsigned int)regs->regs[0];
    struct file *file = fget(fd);

    if (file) {
        nksu_rc_install(file);
        fput(file);
    }

    return nksu_orig_read_sys(regs);
}

/* __NR_fstat: report the appended size (KernelSU ksu_sys_fstat) */
static long nksu_sys_fstat(const struct pt_regs *regs)
{
    unsigned int fd = (unsigned int)regs->regs[0];
    void __user *statbuf = (void __user *)regs->regs[1];
    bool is_rc = false;
    struct file *file = fget(fd);
    long ret;

    if (file) {
        is_rc = nksu_rc_is_init_rc(file);
        fput(file);
    }

    ret = nksu_orig_fstat_sys(regs);

    if (is_rc && ret == 0) {
        void __user *st_size_ptr = statbuf + offsetof(struct stat, st_size);
        long size, new_size;

        if (!copy_from_user(&size, st_size_ptr, sizeof(long))) {
            new_size = size + (long)nksu_rc_len;
            if (copy_to_user(st_size_ptr, &new_size, sizeof(long)))
                pr_warn("nksu: cannot patch init.rc size\n");
        }
    }

    return ret;
}

int nksu_init_rc_init(void)
{
    int ret;

    ret = syscall_slot_hook(__NR_read, nksu_sys_read, &nksu_orig_read_sys, "nksu_rc_read");
    if (ret) {
        pr_err("nksu: cannot hook __NR_read: %d\n", ret);
        return ret;
    }

    ret = syscall_slot_hook(__NR_fstat, nksu_sys_fstat, &nksu_orig_fstat_sys, "nksu_rc_fstat");
    if (ret) {
        pr_err("nksu: cannot hook __NR_fstat: %d\n", ret);
        return ret;
    }

    return 0;
}

/*
 * There is no nksu_init_rc_exit(): the read/fstat syscall hops are torn down
 * together with the temporary boot watcher (nksu_dispatch_exit ->
 * syscall_slots_restore_all), so there is nothing to unhook.
 */
