// SPDX-License-Identifier: GPL-2.0
/*
 * SELinux hiding, ported from KernelSU's feature/selinux_hide.c.
 *
 * su checkers read /sys/fs/selinux/{context,access} and /sys/fs/selinux/status;
 * those would expose the domain and the rules NekoSU injects.  When enabled,
 * requests from app uids (uid >= 10000) are answered from the untouched policy
 * kept aside by sepolicy_dup_and_apply(), and /sys/fs/selinux/status serves a
 * snapshot taken before the policy was modified, so the injected state is
 * invisible to userspace.
 *
 * It is a normal kernel feature: flip it with IOC_FEATURE_SET through the
 * manager's JNI interface, and /data/adb/nksu/feature turns it on at boot.
 *
 * Linux 6.6 dropped the struct selinux_state argument from the policy helpers;
 * the older kernels answer through a throwaway state pointing at the untouched
 * policy, and 6.6+ through the vendored policy engine at the bottom of the
 * file (see the #if).
 */
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include <security.h>
#include "ss/policydb.h"
#include "ss/context.h"
#include "ss/conditional.h"
#include "ss/mls.h"
#include "ss/services.h"
#include "avc.h"
#include "objsec.h"

#include "hook/lsm_hook.h"
#include "hook/patch.h"
#include "manager/feature.h"
#include "selinux/hide.h"
#include "selinux/policy.h"
#include "selinux/selinux.h"
#include "symbol/symbol.h"

/*
 * symbol_compat.h redirects `selinux_state` to a resolved pointer, and the
 * macro would also rewrite the `struct selinux_state` tag; capture the real
 * type before it is in scope.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
typedef struct selinux_state nksu_fake_state_t;
#endif

/* Last: redirects unexported symbols (selinux_state, ...) through pointers. */
#include "symbol/symbol_compat.h"

/*
 * This file calls several resolved-but-unexported kernel functions through
 * pointers (policydb_write, avc_has_perm, security_*_to_sid, and the saved
 * f_op handlers), exactly like selinux.c / policy.c / rule.c / manager.c, so it
 * disables CFI for its own functions the same way they do.  Without it the
 * first indirect call panics with "CFI failure (target: policydb_write)" when
 * the module was built by a different clang than the running kernel.
 *
 * Note this only drops the checks at *our* call sites: the compiler still
 * emits the .cfi_jt stub for address-taken functions, so the handlers nksu
 * installs into selinuxfs f_ops remain valid targets for the kernel's own
 * indirect calls.
 */
#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

/* Its presence at boot turns the feature on. */
#define NKSU_FEATURE_FLAG "/data/adb/nksu/feature"

/* selinuxfs caps one transaction at a page. */
#define NKSU_SIMPLE_TRANSACTION_LIMIT 4096

/*
 * The flag lives under /data/adb, which only the nksu domain can reach.  Like
 * profile_store, borrow a cred switched to that domain for the lookup; the
 * feature-init stage (zygote) is already after /data is mounted.  Own cred,
 * not the manager scan's.
 */
static struct cred *hide_cred;

static const struct cred *hide_creds_begin(void)
{
    if (!hide_cred) {
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
        hide_cred = cred;
    }

    return override_creds(hide_cred);
}

static void hide_creds_end(const struct cred *old)
{
    if (old)
        revert_creds(old);
}

static bool nksu_feature_flag_present(void)
{
    const struct cred *old;
    struct file *fp;
    bool present;

    old = hide_creds_begin();
    if (!old)
        return false;

    fp = filp_open(NKSU_FEATURE_FLAG, O_RDONLY, 0);
    present = !IS_ERR(fp);
    if (!IS_ERR(fp))
        filp_close(fp, NULL);

    hide_creds_end(old);
    return present;
}

/* Unlink one leaf whose parent is a directory (mirrors kdir_mkdir in manager.c). */
static int kfile_unlink(const char *path)
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
        err = vfs_unlink(mnt_idmap(p.mnt), dir, dentry, NULL);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
        err = vfs_unlink(mnt_user_ns(p.mnt), dir, dentry, NULL);
#else
        err = vfs_unlink(dir, dentry, NULL);
#endif
        dput(dentry);
    }
    inode_unlock(dir);
    path_put(&p);
    return err;
}

/*
 * Persist the switch so the kernel turns the feature on/off by itself next
 * boot.  The flag file is the boot-time source of truth (see
 * nksu_feature_flag_present), so the manager does not touch it.
 */
static void nksu_feature_flag_store(bool enabled)
{
    const struct cred *old = hide_creds_begin();
    struct file *fp;
    int err;

    if (!old)
        return;

    if (enabled) {
        fp = filp_open(NKSU_FEATURE_FLAG, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (IS_ERR(fp))
            pr_warn("[selinux_hide] cannot write %s: %ld\n", NKSU_FEATURE_FLAG,
                    PTR_ERR(fp));
        else
            filp_close(fp, NULL);
    } else {
        err = kfile_unlink(NKSU_FEATURE_FLAG);
        if (err && err != -ENOENT)
            pr_warn("[selinux_hide] cannot remove %s: %d\n", NKSU_FEATURE_FLAG,
                    err);
    }

    hide_creds_end(old);
}

/* Index into selinuxfs's `write_op` table, mirroring sel_inos in selinuxfs.c. */
enum sel_inos {
    SEL_ROOT_INO = 2,
    SEL_LOAD,
    SEL_ENFORCE,
    SEL_CONTEXT,
    SEL_ACCESS,
    SEL_CREATE,
    SEL_RELABEL,
    SEL_USER,
    SEL_POLICYVERS,
    SEL_COMMIT_BOOLS,
    SEL_MLS,
    SEL_DISABLE,
    SEL_MEMBER,
    SEL_CHECKREQPROT,
    SEL_COMPAT_NET,
    SEL_REJECT_UNKNOWN,
    SEL_DENY_UNKNOWN,
    SEL_STATUS,
    SEL_POLICY,
    SEL_VALIDATE_TRANS,
    SEL_INO_NEXT,
};

typedef ssize_t (*write_op_fn)(struct file *, char *, size_t);

static DEFINE_MUTEX(nksu_hide_mutex);
static bool nksu_hide_running;

static write_op_fn *context_write_slot;
static write_op_fn *access_write_slot;
static write_op_fn orig_context_write;
static write_op_fn orig_access_write;

static int (**status_open_slot)(struct inode *, struct file *);
static int (*orig_status_open)(struct inode *, struct file *);

static struct page *fake_status;

/* One lazy retry if the page was not ready when the feature was enabled. */
static bool fake_status_retry_done;

/* The clean policy, serialised once when the feature is enabled. */
static void *clean_policy_data;
static size_t clean_policy_len;

static ssize_t (**policy_read_slot)(struct file *, char __user *, size_t, loff_t *);
static ssize_t (*orig_policy_read)(struct file *, char __user *, size_t, loff_t *);

/* Bound the capture; a loaded policy blob is a few hundred KiB. */
#define NKSU_POLICY_BLOB_MAX (16 * 1024 * 1024)

/* core.c: false for a first-stage (vendor_boot) load, true for a late insmod. */
extern bool late_load;

static void nksu_fake_status_prepare(void)
{
    struct selinux_kernel_status *cur, *copy;
    struct page *page;

    mutex_lock(&selinux_state.status_lock);
    if (fake_status)
        goto out;
    if (!selinux_state.status_page) {
        pr_warn("[selinux_hide] status page not ready\n");
        goto out;
    }

    cur = page_address(selinux_state.status_page);
    if (!cur->enforcing && !late_load) {
        pr_warn("[selinux_hide] permissive, fake status skipped\n");
        goto out;
    }

    page = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!page)
        goto out;

    copy = page_address(page);
    memcpy(copy, cur, sizeof(*copy));

    if (late_load) {
        /*
         * A loader may have reloaded sepolicy before us, so the captured page
         * is not stock.  Serve what a stock boot ends with instead: the
         * creation sentinel below 6.10, one load plus one setenforce above.
         */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
        copy->sequence = 4;
        copy->policyload = 1;
#else
        copy->sequence = 0;
        copy->policyload = 0;
#endif
        if (!copy->enforcing)
            copy->enforcing = 1;
    }

    fake_status = page;
    pr_info("[selinux_hide] fake status ready\n");
out:
    mutex_unlock(&selinux_state.status_lock);
}

static int nksu_status_open(struct inode *inode, struct file *filp)
{
    if (likely(current_uid().val >= 10000 && nksu_hide_running)) {
        void *page;
        int ret;

        mutex_lock(&selinux_state.status_lock);
        page = fake_status;
        mutex_unlock(&selinux_state.status_lock);

        if (page) {
            filp->private_data = page;
            return 0;
        }

        /*
         * The status page is allocated lazily by the stock handler on the
         * first open, which can happen after the feature is enabled.  Run the
         * original first so the page exists, then snapshot it for the next
         * call (mirrors KernelSU's fake_status_initialize_key).
         */
        ret = orig_status_open(inode, filp);
        if (!ret && !fake_status_retry_done) {
            fake_status_retry_done = true;
            nksu_fake_status_prepare();
        }
        return ret;
    }
    return orig_status_open(inode, filp);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)

static nksu_fake_state_t fake_state;

#else /* >= 6.6: the policy helpers dropped the selinux_state argument */

static int security_context_to_sid_with_policy(struct selinux_policy *policy, const char *scontext,
                                               u32 scontext_len, u32 *sid, u32 def_sid, gfp_t gfp_flags);
static int security_sid_to_context_with_policy(struct selinux_policy *policy, u32 sid, char **scontext,
                                               u32 *scontext_len);
static void security_compute_av_user_with_policy(struct selinux_policy *policy, u32 ssid, u32 tsid,
                                                 u16 tclass, struct av_decision *avd);
static void (*security_dump_masked_av_fn)(struct policydb *policydb, struct context *scontext,
                                          struct context *tcontext, u16 tclass, u32 permissions,
                                          const char *reason) = NULL;
static void (*context_struct_compute_av_fn)(struct policydb *policydb, struct context *scontext,
                                            struct context *tcontext, u16 tclass,
                                            struct av_decision *avd,
                                            struct extended_perms *xperms) = NULL;

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0) */

/*
 * Kernel-side setprocattr hook, ported from KernelSU's my_setprocattr.
 *
 * A su checker (or anything else) writes its SELinux context to
 * /proc/self/attr/current; the kernel routes that through the LSM setprocattr
 * hook to selinux_setprocattr().  We answer app uids from the clean policy the
 * same way the context/access writes above do, while keeping the live
 * permission checks (SETCURRENT, DYNTRANSITION, ptrace) intact.
 *
 * The working policy shares the original policy's sidtab (see
 * nksu_policy_dup), so the SID resolved here is already valid in the live
 * policy.  KernelSU needs a second resolve into a separate sidtab for this;
 * NekoSU does not.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
#define NKSU_AVC_HAS_PERM(...) avc_has_perm(__VA_ARGS__)
#define NKSU_BOUNDED_TRANSITION(old, new) nksu_bounded_transition_fn(old, new)
#else
#define NKSU_AVC_HAS_PERM(...) avc_has_perm(&selinux_state, __VA_ARGS__)
#define NKSU_BOUNDED_TRANSITION(old, new) \
	nksu_bounded_transition_fn(&selinux_state, old, new)
#endif

typedef int (*nksu_setprocattr_fn)(const char *name, void *value, size_t size);

/* security_bounded_transition(), resolved in nksu_hide_enable(); optional. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static int (*nksu_bounded_transition_fn)(u32 old_sid, u32 new_sid);
#else
static int (*nksu_bounded_transition_fn)(nksu_fake_state_t *state,
					  u32 old_sid, u32 new_sid);
#endif

static u32 nksu_cred_sid(const struct cred *cred)
{
	return nksu_cred_security(cred)->sid;
}

static u32 nksu_task_sid_obj(const struct task_struct *task)
{
	u32 sid;

	rcu_read_lock();
	sid = nksu_cred_sid(__task_cred(task));
	rcu_read_unlock();
	return sid;
}

static u32 nksu_ptrace_parent_sid(void)
{
	u32 sid = 0;
	struct task_struct *tracer;

	rcu_read_lock();
	tracer = ptrace_parent(current);
	if (tracer)
		sid = nksu_task_sid_obj(tracer);
	rcu_read_unlock();
	return sid;
}

static int __nocfi nksu_setprocattr(const char *name, void *value, size_t size);

static struct nksu_lsm_hook nksu_setprocattr_hook = NKSU_LSM_HOOK_INIT(
	setprocattr, "selinux_setprocattr", nksu_setprocattr, 0);

static int __nocfi nksu_setprocattr(const char *name, void *value, size_t size)
{
	struct task_security_struct *tsec;
	struct cred *new;
	u32 mysid, sid = 0, ptsid;
	int error;
	char *str = value;

	if (likely(current_uid().val < 10000))
		goto call_orig;

	if (strcmp(name, "current"))
		goto call_orig;

	mysid = nksu_current_sid();
	error = NKSU_AVC_HAS_PERM(mysid, mysid, SECCLASS_PROCESS,
				  PROCESS__SETCURRENT, NULL);
	if (error)
		return error;

	/* Obtain a SID for the context, if one was specified. */
	if (size && str[0] && str[0] != '\n') {
		if (str[size - 1] == '\n') {
			str[size - 1] = 0;
			size--;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		error = security_context_to_sid_with_policy(
			nksu_orig_policy_get(), value, size, &sid, SECSID_NULL,
			GFP_KERNEL);
#else
		error = security_context_to_sid(&fake_state, value, size, &sid,
						GFP_KERNEL);
#endif
		if (error)
			return error;
	}

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	/*
	 * Permission checking based on the specified context is performed
	 * during the actual operation (execve, open/mkdir/...), when the full
	 * context is known; see selinux_bprm_creds_for_exec and may_create.
	 */
	tsec = nksu_cred_security(new);
	error = -EINVAL;
	if (sid == 0)
		goto abort_change;

	if (!current_is_single_threaded() && nksu_bounded_transition_fn) {
		error = NKSU_BOUNDED_TRANSITION(tsec->sid, sid);
		if (error)
			goto abort_change;
	}

	/* Check permissions for the transition. */
	error = NKSU_AVC_HAS_PERM(tsec->sid, sid, SECCLASS_PROCESS,
				  PROCESS__DYNTRANSITION, NULL);
	if (error)
		goto abort_change;

	/* Check for ptracing, and update the task SID if ok.  Otherwise leave
	 * the SID unchanged and fail. */
	ptsid = nksu_ptrace_parent_sid();
	if (ptsid != 0) {
		error = NKSU_AVC_HAS_PERM(ptsid, sid, SECCLASS_PROCESS,
					  PROCESS__PTRACE, NULL);
		if (error)
			goto abort_change;
	}

	tsec->sid = sid;
	commit_creds(new);
	return size;

abort_change:
	abort_creds(new);
	return error;

call_orig:
	return ((nksu_setprocattr_fn)nksu_setprocattr_hook.original)(name, value,
								     size);
}

static ssize_t nksu_write_context(struct file *file, char *buf, size_t size)
{
    char *canon = NULL;
    u32 sid, len;
    ssize_t length;

    if (likely(current_uid().val < 10000))
        return orig_context_write(file, buf, size);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    length = avc_has_perm(nksu_current_sid(), SECINITSID_SECURITY, SECCLASS_SECURITY,
                          SECURITY__CHECK_CONTEXT, NULL);
    if (length)
        goto out;

    length = security_context_to_sid_with_policy(nksu_orig_policy_get(), buf, size, &sid,
                                                 SECSID_NULL, GFP_KERNEL);
    if (length)
        goto out;

    length = security_sid_to_context_with_policy(nksu_orig_policy_get(), sid, &canon, &len);
    if (length)
        goto out;
#else
    length = avc_has_perm(&selinux_state, nksu_current_sid(), SECINITSID_SECURITY,
                          SECCLASS_SECURITY, SECURITY__CHECK_CONTEXT, NULL);
    if (length)
        goto out;

    length = security_context_to_sid(&fake_state, buf, size, &sid, GFP_KERNEL);
    if (length)
        goto out;

    length = security_sid_to_context(&fake_state, sid, &canon, &len);
    if (length)
        goto out;
#endif

    length = -ERANGE;
    if (len > NKSU_SIMPLE_TRANSACTION_LIMIT)
        goto out;

    memcpy(buf, canon, len);
    length = len;
out:
    kfree(canon);
    return length;
}

static ssize_t nksu_write_access(struct file *file, char *buf, size_t size)
{
    char *scon = NULL, *tcon = NULL;
    u32 ssid, tsid;
    u16 tclass;
    struct av_decision avd;
    ssize_t length;

    if (likely(current_uid().val < 10000))
        return orig_access_write(file, buf, size);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    length = avc_has_perm(nksu_current_sid(), SECINITSID_SECURITY, SECCLASS_SECURITY,
                          SECURITY__COMPUTE_AV, NULL);
#else
    length = avc_has_perm(&selinux_state, nksu_current_sid(), SECINITSID_SECURITY,
                          SECCLASS_SECURITY, SECURITY__COMPUTE_AV, NULL);
#endif
    if (length)
        goto out;

    length = -ENOMEM;
    scon = kzalloc(size + 1, GFP_KERNEL);
    if (!scon)
        goto out;
    tcon = kzalloc(size + 1, GFP_KERNEL);
    if (!tcon)
        goto out;

    length = -EINVAL;
    if (sscanf(buf, "%s %s %hu", scon, tcon, &tclass) != 3)
        goto out;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    length = security_context_to_sid_with_policy(nksu_orig_policy_get(), scon, strlen(scon),
                                                 &ssid, SECSID_NULL, GFP_KERNEL);
    if (length)
        goto out;

    length = security_context_to_sid_with_policy(nksu_orig_policy_get(), tcon, strlen(tcon),
                                                 &tsid, SECSID_NULL, GFP_KERNEL);
    if (length)
        goto out;

    security_compute_av_user_with_policy(nksu_orig_policy_get(), ssid, tsid, tclass, &avd);
#else
    length = security_context_str_to_sid(&fake_state, scon, &ssid, GFP_KERNEL);
    if (length)
        goto out;

    length = security_context_str_to_sid(&fake_state, tcon, &tsid, GFP_KERNEL);
    if (length)
        goto out;

    security_compute_av_user(&fake_state, ssid, tsid, tclass, &avd);
#endif

    /* stock reads 1 here */
    avd.seqno = 1;
    length = scnprintf(buf, NKSU_SIMPLE_TRANSACTION_LIMIT, "%x %x %x %x %u %x",
                       avd.allowed, 0xffffffff, avd.auditallow, avd.auditdeny,
                       avd.seqno, avd.flags);
out:
    kfree(tcon);
    kfree(scon);
    return length;
}

/*
 * Serialise the clean policy the way security_read_policy() does:
 * policydb_write() into a buffer sized for the loaded blob, with the per-type
 * slack policydb_read() needs for the attribute maps (see nksu_policy_dup()).
 */
static int nksu_clean_policy_prepare(void)
{
    struct selinux_policy *pol = nksu_orig_policy_get();
    struct policy_file fp;
    size_t len;
    void *data;
    int ret;

    if (clean_policy_data)
        return 0;
    if (!pol)
        return -EAGAIN;

    len = pol->policydb.len +
          (size_t)pol->policydb.p_types.nprim * (sizeof(u32) + sizeof(u64));
    if (!len || len > NKSU_POLICY_BLOB_MAX)
        return -EINVAL;

    data = vmalloc(len);
    if (!data)
        return -ENOMEM;

    fp.data = data;
    fp.len = len;

    ret = policydb_write(&pol->policydb, &fp);
    if (ret) {
        vfree(data);
        return ret;
    }

    clean_policy_len = len - fp.len;
    clean_policy_data = data;
    pr_info("[selinux_hide] captured %zu-byte clean policy\n", clean_policy_len);
    return 0;
}

static void nksu_clean_policy_release(void)
{
    if (clean_policy_data) {
        vfree(clean_policy_data);
        clean_policy_data = NULL;
        clean_policy_len = 0;
    }
}

/* /sys/fs/selinux/policy: hand apps the clean policy instead of the live one. */
static ssize_t nksu_read_policy(struct file *filp, char __user *buf, size_t count,
                                loff_t *ppos)
{
    if (likely(current_uid().val < 10000) || !nksu_hide_running ||
        !clean_policy_data)
        return orig_policy_read(filp, buf, count, ppos);

    return simple_read_from_buffer(buf, count, ppos, clean_policy_data,
                                   clean_policy_len);
}

static void nksu_hide_unhook(void)
{
    if (context_write_slot) {
        nksu_patch_text(context_write_slot, &orig_context_write,
                        sizeof(orig_context_write));
        context_write_slot = NULL;
        orig_context_write = NULL;
    }
    if (access_write_slot) {
        nksu_patch_text(access_write_slot, &orig_access_write,
                        sizeof(orig_access_write));
        access_write_slot = NULL;
        orig_access_write = NULL;
    }
    if (status_open_slot && orig_status_open) {
        nksu_patch_text(status_open_slot, &orig_status_open,
                        sizeof(orig_status_open));
        status_open_slot = NULL;
        orig_status_open = NULL;
    }
    if (policy_read_slot && orig_policy_read) {
        nksu_patch_text(policy_read_slot, &orig_policy_read,
                        sizeof(orig_policy_read));
        policy_read_slot = NULL;
        orig_policy_read = NULL;
    }

    nksu_lsm_unhook(&nksu_setprocattr_hook);
}

static int nksu_hide_enable(void)
{
    struct file_operations *policy_ops, *status_ops;
    write_op_fn *write_op, new_write;
    int ret;

    if (!nksu_orig_policy_get()) {
        pr_err("[selinux_hide] no original policy available\n");
        return -EAGAIN;
    }

    write_op = (write_op_fn *)nksu_ksym_lookup("write_op");
    if (!write_op) {
        pr_err("[selinux_hide] symbol 'write_op' not found\n");
        return -ENOSYS;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    security_dump_masked_av_fn =
        (typeof(security_dump_masked_av_fn))nksu_ksym_lookup("security_dump_masked_av");
    if (!security_dump_masked_av_fn)
        pr_warn("[selinux_hide] security_dump_masked_av not found\n");

    context_struct_compute_av_fn =
        (typeof(context_struct_compute_av_fn))nksu_ksym_lookup("context_struct_compute_av");
    if (!context_struct_compute_av_fn)
        pr_warn("[selinux_hide] context_struct_compute_av not found\n");
#else
    fake_state.initialized = true;
    fake_state.policy = nksu_orig_policy_get();
#endif

    nksu_bounded_transition_fn = (typeof(nksu_bounded_transition_fn))
        nksu_ksym_lookup("security_bounded_transition");
    if (!nksu_bounded_transition_fn)
        pr_warn("[selinux_hide] security_bounded_transition not found\n");

    ret = nksu_clean_policy_prepare();
    if (ret) {
        pr_err("[selinux_hide] cannot prepare the clean policy: %d\n", ret);
        return ret;
    }

    policy_ops = (struct file_operations *)nksu_ksym_lookup("sel_policy_ops");
    if (policy_ops) {
        ssize_t (*new_read)(struct file *, char __user *, size_t, loff_t *) =
            nksu_read_policy;

        policy_read_slot = &policy_ops->read;
        orig_policy_read = *policy_read_slot;
        ret = nksu_patch_text(policy_read_slot, &new_read, sizeof(new_read));
        if (ret) {
            pr_err("[selinux_hide] patch policy read: %d\n", ret);
            policy_read_slot = NULL;
            orig_policy_read = NULL;
        }
    } else {
        pr_warn("[selinux_hide] sel_policy_ops not found\n");
    }

    context_write_slot = &write_op[SEL_CONTEXT];
    orig_context_write = *context_write_slot;
    new_write = nksu_write_context;
    ret = nksu_patch_text(context_write_slot, &new_write, sizeof(new_write));
    if (ret) {
        pr_err("[selinux_hide] patch context write: %d\n", ret);
        context_write_slot = NULL;
        goto fail;
    }

    access_write_slot = &write_op[SEL_ACCESS];
    orig_access_write = *access_write_slot;
    new_write = nksu_write_access;
    ret = nksu_patch_text(access_write_slot, &new_write, sizeof(new_write));
    if (ret) {
        pr_err("[selinux_hide] patch access write: %d\n", ret);
        access_write_slot = NULL;
        goto fail;
    }

    status_ops = (struct file_operations *)nksu_ksym_lookup("sel_handle_status_ops");
    if (status_ops) {
        int (*new_open)(struct inode *, struct file *) = nksu_status_open;

        status_open_slot = &status_ops->open;
        orig_status_open = *status_open_slot;
        ret = nksu_patch_text(status_open_slot, &new_open, sizeof(new_open));
        if (ret) {
            pr_err("[selinux_hide] patch status open: %d\n", ret);
            status_open_slot = NULL;
            orig_status_open = NULL;
        }
    } else {
        pr_warn("[selinux_hide] sel_handle_status_ops not found\n");
    }

    fake_status_retry_done = false;
    nksu_fake_status_prepare();

    ret = nksu_lsm_hook(&nksu_setprocattr_hook);
    if (ret) {
        pr_err("[selinux_hide] setprocattr hook: %d\n", ret);
        goto fail;
    }

    pr_info("[selinux_hide] enabled\n");
    return 0;

fail:
    nksu_hide_unhook();
    return ret;
}

static void nksu_hide_disable(void)
{
    if (!nksu_hide_running)
        return;
    nksu_hide_unhook();
    pr_info("[selinux_hide] disabled\n");
}

static int nksu_hide_feature_get(u64 *value)
{
    *value = nksu_hide_running ? 1 : 0;
    return 0;
}

/*
 * Switch the feature.  `persist` is false for the boot-time enable: the flag
 * file is what asked for it, so rewriting it there would be pointless.
 */
static int nksu_hide_apply(u64 value, bool persist)
{
    int ret = 0;

    mutex_lock(&nksu_hide_mutex);
    if (value) {
        if (!nksu_hide_running) {
            ret = nksu_hide_enable();
            if (!ret)
                nksu_hide_running = true;
        }
    } else if (nksu_hide_running) {
        nksu_hide_disable();
        nksu_hide_running = false;
    }
    mutex_unlock(&nksu_hide_mutex);

    if (!ret && persist)
        nksu_feature_flag_store(value != 0);

    return ret;
}

static int nksu_hide_feature_set(u64 value) { return nksu_hide_apply(value, true); }

static const struct nksu_feature nksu_hide_feature = {
    .id = NKSU_FEATURE_SELINUX_HIDE,
    .name = "selinux_hide",
    .get = nksu_hide_feature_get,
    .set = nksu_hide_feature_set,
};

int nksu_selinux_hide_init(void)
{
    int ret;

    ret = nksu_feature_register(&nksu_hide_feature);
    if (ret)
        pr_warn("[selinux_hide] feature register failed: %d\n", ret);

    if (!nksu_feature_flag_present()) {
        pr_info("[selinux_hide] %s absent, off\n", NKSU_FEATURE_FLAG);
        return 0;
    }

    ret = nksu_hide_apply(1, false);
    if (ret)
        pr_warn("[selinux_hide] enable from %s failed: %d\n",
                NKSU_FEATURE_FLAG, ret);

    return 0;
}

void nksu_selinux_hide_exit(void)
{
    mutex_lock(&nksu_hide_mutex);
    nksu_hide_disable();
    nksu_hide_running = false;
    mutex_unlock(&nksu_hide_mutex);

    nksu_clean_policy_release();

    mutex_lock(&selinux_state.status_lock);
    if (fake_status) {
        __free_page(fake_status);
        fake_status = NULL;
    }
    mutex_unlock(&selinux_state.status_lock);

    if (hide_cred) {
        put_cred(hide_cred);
        hide_cred = NULL;
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
/* --- vendored from KernelSU (kernel/feature/selinux_hide.c) --- */
/*
 * Caveat:  Mutates scontext.
 */
static int string_to_context_struct(struct policydb *pol, struct sidtab *sidtabp, char *scontext, struct context *ctx,
                                    u32 def_sid)
{
    struct role_datum *role;
    struct type_datum *typdatum;
    struct user_datum *usrdatum;
    char *scontextp, *p, oldc;
    int rc = 0;

    context_init(ctx);

    /* Parse the security context. */

    rc = -EINVAL;
    scontextp = scontext;

    /* Extract the user. */
    p = scontextp;
    while (*p && *p != ':')
        p++;

    if (*p == 0)
        goto out;

    *p++ = 0;

    usrdatum = symtab_search(&pol->p_users, scontextp);
    if (!usrdatum)
        goto out;

    ctx->user = usrdatum->value;

    /* Extract role. */
    scontextp = p;
    while (*p && *p != ':')
        p++;

    if (*p == 0)
        goto out;

    *p++ = 0;

    role = symtab_search(&pol->p_roles, scontextp);
    if (!role)
        goto out;
    ctx->role = role->value;

    /* Extract type. */
    scontextp = p;
    while (*p && *p != ':')
        p++;
    oldc = *p;
    *p++ = 0;

    typdatum = symtab_search(&pol->p_types, scontextp);
    if (!typdatum || typdatum->attribute)
        goto out;

    ctx->type = typdatum->value;

    rc = mls_context_to_sid(pol, oldc, p, ctx, sidtabp, def_sid);
    if (rc)
        goto out;

    /* Check the validity of the new context. */
    rc = -EINVAL;
    if (!policydb_context_isvalid(pol, ctx))
        goto out;
    rc = 0;
out:
    if (rc)
        context_destroy(ctx);
    return rc;
}

static int security_context_to_sid_with_policy(struct selinux_policy *policy, const char *scontext, u32 scontext_len,
                                               u32 *sid, u32 def_sid, gfp_t gfp_flags)
{
    struct policydb *policydb;
    struct sidtab *sidtab;
    char *scontext2, *str = NULL;
    struct context context;
    int rc = 0;

    /* An empty security context is never valid. */
    if (!scontext_len)
        return -EINVAL;

    /* Copy the string to allow changes and ensure a NUL terminator */
    scontext2 = kmemdup_nul(scontext, scontext_len, gfp_flags);
    if (!scontext2)
        return -ENOMEM;

    // removed: if (!selinux_initialized())
    *sid = SECSID_NULL;

    // removed: if (force)
    // removed: rcu lock
    policydb = &policy->policydb;
    sidtab = policy->sidtab;
    rc = string_to_context_struct(policydb, sidtab, scontext2, &context, def_sid);
    if (rc)
        goto out;
    rc = sidtab_context_to_sid(sidtab, &context, sid);
    // rc should not be frozen
    if (rc)
        goto out;
    // removed: if (rc == -ESTALE)
    context_destroy(&context);
out:
    kfree(scontext2);
    kfree(str);
    return rc;
}

/*
 * Write the security context string representation of
 * the context structure `context' into a dynamically
 * allocated string of the correct size.  Set `*scontext'
 * to point to this string and set `*scontext_len' to
 * the length of the string.
 */
static int context_struct_to_string(struct policydb *p, struct context *context, char **scontext, u32 *scontext_len)
{
    char *scontextp;

    if (scontext)
        *scontext = NULL;
    *scontext_len = 0;

    if (context->len) {
        *scontext_len = context->len;
        if (scontext) {
            *scontext = kstrdup(context->str, GFP_ATOMIC);
            if (!(*scontext))
                return -ENOMEM;
        }
        return 0;
    }

    /* Compute the size of the context. */
    *scontext_len += strlen(sym_name(p, SYM_USERS, context->user - 1)) + 1;
    *scontext_len += strlen(sym_name(p, SYM_ROLES, context->role - 1)) + 1;
    *scontext_len += strlen(sym_name(p, SYM_TYPES, context->type - 1)) + 1;
    *scontext_len += mls_compute_context_len(p, context);

    if (!scontext)
        return 0;

    /* Allocate space for the context; caller must free this space. */
    scontextp = kmalloc(*scontext_len, GFP_ATOMIC);
    if (!scontextp)
        return -ENOMEM;
    *scontext = scontextp;

    /*
     * Copy the user name, role name and type name into the context.
     */
    scontextp += sprintf(scontextp, "%s:%s:%s", sym_name(p, SYM_USERS, context->user - 1),
                         sym_name(p, SYM_ROLES, context->role - 1), sym_name(p, SYM_TYPES, context->type - 1));

    mls_sid_to_context(p, context, &scontextp);

    *scontextp = 0;

    return 0;
}

static int sidtab_entry_to_string(struct policydb *p, struct sidtab *sidtab, struct sidtab_entry *entry,
                                  char **scontext, u32 *scontext_len)
{
    int rc = sidtab_sid2str_get(sidtab, entry, scontext, scontext_len);

    if (rc != -ENOENT)
        return rc;

    rc = context_struct_to_string(p, &entry->context, scontext, scontext_len);
    if (!rc && scontext)
        sidtab_sid2str_put(sidtab, entry, *scontext, *scontext_len);
    return rc;
}

static int security_sid_to_context_with_policy(struct selinux_policy *policy, u32 sid, char **scontext,
                                               u32 *scontext_len)
{
    struct policydb *policydb;
    struct sidtab *sidtab;
    struct sidtab_entry *entry;
    int rc = 0;

    if (scontext)
        *scontext = NULL;
    *scontext_len = 0;

    // removed: if (!selinux_initialized())
    // removed: rcu lock
    policydb = &policy->policydb;
    sidtab = policy->sidtab;

    // removed: force
    entry = sidtab_search_entry(sidtab, sid);
    if (!entry) {
        pr_err("SELinux: %s:  unrecognized SID %d\n", __func__, sid);
        rc = -EINVAL;
        goto out_unlock;
    }
    // removed: only_invalid

    rc = sidtab_entry_to_string(policydb, sidtab, entry, scontext, scontext_len);

out_unlock:
    return rc;
}

static void avd_init(struct selinux_policy *policy, struct av_decision *avd)
{
    avd->allowed = 0;
    avd->auditallow = 0;
    avd->auditdeny = 0xffffffff;
    if (policy)
        avd->seqno = policy->latest_granting;
    else
        avd->seqno = 0;
    avd->flags = 0;
}

static void context_struct_compute_av(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                      u16 tclass, struct av_decision *avd, struct extended_perms *xperms);

/*
 * security_boundary_permission - drops violated permissions
 * on boundary constraint.
 */
static void __nocfi type_attribute_bounds_av(struct policydb *policydb, struct context *scontext,
                                             struct context *tcontext, u16 tclass, struct av_decision *avd)
{
    struct context lo_scontext;
    struct context lo_tcontext, *tcontextp = tcontext;
    struct av_decision lo_avd;
    struct type_datum *source;
    struct type_datum *target;
    u32 masked = 0;

    source = policydb->type_val_to_struct[scontext->type - 1];
    BUG_ON(!source);

    if (!source->bounds)
        return;

    target = policydb->type_val_to_struct[tcontext->type - 1];
    BUG_ON(!target);

    memset(&lo_avd, 0, sizeof(lo_avd));

    memcpy(&lo_scontext, scontext, sizeof(lo_scontext));
    lo_scontext.type = source->bounds;

    if (target->bounds) {
        memcpy(&lo_tcontext, tcontext, sizeof(lo_tcontext));
        lo_tcontext.type = target->bounds;
        tcontextp = &lo_tcontext;
    }

    context_struct_compute_av(policydb, &lo_scontext, tcontextp, tclass, &lo_avd, NULL);

    masked = ~lo_avd.allowed & avd->allowed;

    if (likely(!masked))
        return; /* no masked permission */

    /* mask violated permissions */
    avd->allowed &= ~masked;

    /* audit masked permissions */
    if (security_dump_masked_av_fn)
        security_dump_masked_av_fn(policydb, scontext, tcontext, tclass, masked, "bounds");
}

/*
 * Return the boolean value of a constraint expression
 * when it is applied to the specified source and target
 * security contexts.
 *
 * xcontext is a special beast...  It is used by the validatetrans rules
 * only.  For these rules, scontext is the context before the transition,
 * tcontext is the context after the transition, and xcontext is the context
 * of the process performing the transition.  All other callers of
 * constraint_expr_eval should pass in NULL for xcontext.
 */
static int constraint_expr_eval(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                struct context *xcontext, struct constraint_expr *cexpr)
{
    u32 val1, val2;
    struct context *c;
    struct role_datum *r1, *r2;
    struct mls_level *l1, *l2;
    struct constraint_expr *e;
    int s[CEXPR_MAXDEPTH];
    int sp = -1;

    for (e = cexpr; e; e = e->next) {
        switch (e->expr_type) {
        case CEXPR_NOT:
            BUG_ON(sp < 0);
            s[sp] = !s[sp];
            break;
        case CEXPR_AND:
            BUG_ON(sp < 1);
            sp--;
            s[sp] &= s[sp + 1];
            break;
        case CEXPR_OR:
            BUG_ON(sp < 1);
            sp--;
            s[sp] |= s[sp + 1];
            break;
        case CEXPR_ATTR:
            if (sp == (CEXPR_MAXDEPTH - 1))
                return 0;
            switch (e->attr) {
            case CEXPR_USER:
                val1 = scontext->user;
                val2 = tcontext->user;
                break;
            case CEXPR_TYPE:
                val1 = scontext->type;
                val2 = tcontext->type;
                break;
            case CEXPR_ROLE:
                val1 = scontext->role;
                val2 = tcontext->role;
                r1 = policydb->role_val_to_struct[val1 - 1];
                r2 = policydb->role_val_to_struct[val2 - 1];
                switch (e->op) {
                case CEXPR_DOM:
                    s[++sp] = ebitmap_get_bit(&r1->dominates, val2 - 1);
                    continue;
                case CEXPR_DOMBY:
                    s[++sp] = ebitmap_get_bit(&r2->dominates, val1 - 1);
                    continue;
                case CEXPR_INCOMP:
                    s[++sp] =
                        (!ebitmap_get_bit(&r1->dominates, val2 - 1) && !ebitmap_get_bit(&r2->dominates, val1 - 1));
                    continue;
                default:
                    break;
                }
                break;
            case CEXPR_L1L2:
                l1 = &(scontext->range.level[0]);
                l2 = &(tcontext->range.level[0]);
                goto mls_ops;
            case CEXPR_L1H2:
                l1 = &(scontext->range.level[0]);
                l2 = &(tcontext->range.level[1]);
                goto mls_ops;
            case CEXPR_H1L2:
                l1 = &(scontext->range.level[1]);
                l2 = &(tcontext->range.level[0]);
                goto mls_ops;
            case CEXPR_H1H2:
                l1 = &(scontext->range.level[1]);
                l2 = &(tcontext->range.level[1]);
                goto mls_ops;
            case CEXPR_L1H1:
                l1 = &(scontext->range.level[0]);
                l2 = &(scontext->range.level[1]);
                goto mls_ops;
            case CEXPR_L2H2:
                l1 = &(tcontext->range.level[0]);
                l2 = &(tcontext->range.level[1]);
                goto mls_ops;
            mls_ops:
                switch (e->op) {
                case CEXPR_EQ:
                    s[++sp] = mls_level_eq(l1, l2);
                    continue;
                case CEXPR_NEQ:
                    s[++sp] = !mls_level_eq(l1, l2);
                    continue;
                case CEXPR_DOM:
                    s[++sp] = mls_level_dom(l1, l2);
                    continue;
                case CEXPR_DOMBY:
                    s[++sp] = mls_level_dom(l2, l1);
                    continue;
                case CEXPR_INCOMP:
                    s[++sp] = mls_level_incomp(l2, l1);
                    continue;
                default:
                    BUG();
                    return 0;
                }
                break;
            default:
                BUG();
                return 0;
            }

            switch (e->op) {
            case CEXPR_EQ:
                s[++sp] = (val1 == val2);
                break;
            case CEXPR_NEQ:
                s[++sp] = (val1 != val2);
                break;
            default:
                BUG();
                return 0;
            }
            break;
        case CEXPR_NAMES:
            if (sp == (CEXPR_MAXDEPTH - 1))
                return 0;
            c = scontext;
            if (e->attr & CEXPR_TARGET)
                c = tcontext;
            else if (e->attr & CEXPR_XTARGET) {
                c = xcontext;
                if (!c) {
                    BUG();
                    return 0;
                }
            }
            if (e->attr & CEXPR_USER)
                val1 = c->user;
            else if (e->attr & CEXPR_ROLE)
                val1 = c->role;
            else if (e->attr & CEXPR_TYPE)
                val1 = c->type;
            else {
                BUG();
                return 0;
            }

            switch (e->op) {
            case CEXPR_EQ:
                s[++sp] = ebitmap_get_bit(&e->names, val1 - 1);
                break;
            case CEXPR_NEQ:
                s[++sp] = !ebitmap_get_bit(&e->names, val1 - 1);
                break;
            default:
                BUG();
                return 0;
            }
            break;
        default:
            BUG();
            return 0;
        }
    }

    BUG_ON(sp != 0);
    return s[0];
}

/*
 * Compute access vectors and extended permissions based on a context
 * structure pair for the permissions in a particular class.
 */
static void context_struct_compute_av(struct policydb *policydb, struct context *scontext, struct context *tcontext,
                                      u16 tclass, struct av_decision *avd, struct extended_perms *xperms)
{
    struct constraint_node *constraint;
    struct role_allow *ra;
    struct avtab_key avkey;
    struct avtab_node *node;
    struct class_datum *tclass_datum;
    struct ebitmap *sattr, *tattr;
    struct ebitmap_node *snode, *tnode;
    unsigned int i, j;

    avd->allowed = 0;
    avd->auditallow = 0;
    avd->auditdeny = 0xffffffff;
    if (xperms) {
        memset(&xperms->drivers, 0, sizeof(xperms->drivers));
        xperms->len = 0;
    }

    if (unlikely(!tclass || tclass > policydb->p_classes.nprim)) {
        pr_warn_ratelimited("SELinux:  Invalid class %u\n", tclass);
        return;
    }

    tclass_datum = policydb->class_val_to_struct[tclass - 1];

    /*
     * If a specific type enforcement rule was defined for
     * this permission check, then use it.
     */
    avkey.target_class = tclass;
    avkey.specified = AVTAB_AV | AVTAB_XPERMS;
    sattr = &policydb->type_attr_map_array[scontext->type - 1];
    tattr = &policydb->type_attr_map_array[tcontext->type - 1];
    ebitmap_for_each_positive_bit(sattr, snode, i)
    {
        ebitmap_for_each_positive_bit(tattr, tnode, j)
        {
            avkey.source_type = i + 1;
            avkey.target_type = j + 1;
            for (node = avtab_search_node(&policydb->te_avtab, &avkey); node;
                 node = avtab_search_node_next(node, avkey.specified)) {
                if (node->key.specified == AVTAB_ALLOWED)
                    avd->allowed |= node->datum.u.data;
                else if (node->key.specified == AVTAB_AUDITALLOW)
                    avd->auditallow |= node->datum.u.data;
                else if (node->key.specified == AVTAB_AUDITDENY)
                    avd->auditdeny &= node->datum.u.data;
                else if (xperms && (node->key.specified & AVTAB_XPERMS))
                    services_compute_xperms_drivers(xperms, node);
            }

            /* Check conditional av table for additional permissions */
            cond_compute_av(&policydb->te_cond_avtab, &avkey, avd, xperms);
        }
    }

    /*
     * Remove any permissions prohibited by a constraint (this includes
     * the MLS policy).
     */
    constraint = tclass_datum->constraints;
    while (constraint) {
        if ((constraint->permissions & (avd->allowed)) &&
            !constraint_expr_eval(policydb, scontext, tcontext, NULL, constraint->expr)) {
            avd->allowed &= ~(constraint->permissions);
        }
        constraint = constraint->next;
    }

    /*
     * If checking process transition permission and the
     * role is changing, then check the (current_role, new_role)
     * pair.
     */
    if (tclass == policydb->process_class && (avd->allowed & policydb->process_trans_perms) &&
        scontext->role != tcontext->role) {
        for (ra = policydb->role_allow; ra; ra = ra->next) {
            if (scontext->role == ra->role && tcontext->role == ra->new_role)
                break;
        }
        if (!ra)
            avd->allowed &= ~policydb->process_trans_perms;
    }

    /*
     * If the given source and target types have boundary
     * constraint, lazy checks have to mask any violated
     * permission and notice it to userspace via audit.
     */
    type_attribute_bounds_av(policydb, scontext, tcontext, tclass, avd);
}

static void __nocfi security_compute_av_user_with_policy(struct selinux_policy *policy, u32 ssid, u32 tsid, u16 tclass,
                                                         struct av_decision *avd)
{
    struct policydb *policydb;
    struct sidtab *sidtab;
    struct context *scontext = NULL, *tcontext = NULL;

    // remove: rcu lock
    avd_init(policy, avd);
    // remove: if (!selinux_initialized())

    policydb = &policy->policydb;
    sidtab = policy->sidtab;

    scontext = sidtab_search(sidtab, ssid);
    if (!scontext) {
        pr_err("SELinux: %s:  unrecognized SID %d\n", __func__, ssid);
        goto out;
    }

    /* permissive domain? */
    if (ebitmap_get_bit(&policydb->permissive_map, scontext->type))
        avd->flags |= AVD_FLAGS_PERMISSIVE;

    tcontext = sidtab_search(sidtab, tsid);
    if (!tcontext) {
        pr_err("SELinux: %s:  unrecognized SID %d\n", __func__, tsid);
        goto out;
    }

    if (unlikely(!tclass)) {
        if (policydb->allow_unknown)
            goto allow;
        goto out;
    }

    if (context_struct_compute_av_fn) {
        context_struct_compute_av_fn(policydb, scontext, tcontext, tclass, avd, NULL);
    } else {
        context_struct_compute_av(policydb, scontext, tcontext, tclass, avd, NULL);
    }
out:
    return;
allow:
    avd->allowed = 0xffffffff;
    goto out;
}

#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0) */

#if defined(__clang__)
#pragma clang attribute pop
#endif
