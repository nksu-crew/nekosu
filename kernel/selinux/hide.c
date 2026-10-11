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
 * Only the pre-6.6 path is implemented.  Linux 6.6 removed the
 * struct selinux_state argument from the policy helpers, and KernelSU answers
 * that by vendoring the policy engine; that has not been ported here, so on
 * 6.6+ the feature reports itself as unavailable.
 */
#include <linux/types.h>
#include <linux/version.h>

#include "manager/feature.h"
#include "manager/manager.h"
#include "selinux/hide.h"

/* Its presence at boot turns the feature on. */
#define NKSU_FEATURE_FLAG "/data/adb/nksu/feature"

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)

#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <security.h>
#include "ss/context.h"
#include "ss/services.h"
#include "avc.h"
#include "objsec.h"

#include "hook/patch.h"
#include "selinux/policy.h"
#include "symbol/symbol.h"

/*
 * symbol_compat.h redirects `selinux_state` to a resolved pointer, and the
 * macro would also rewrite the `struct selinux_state` tag; capture the real
 * type before it is in scope.
 */
typedef struct selinux_state nksu_fake_state_t;

/* Last: redirects unexported symbols (selinux_state, ...) through pointers. */
#include "symbol/symbol_compat.h"

/* selinuxfs caps one transaction at a page. */
#define NKSU_SIMPLE_TRANSACTION_LIMIT 4096

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

/*
 * The policy helpers still take a struct selinux_state on < 6.6, so a
 * throwaway state pointing at the untouched policy answers exactly as if
 * NekoSU had injected nothing.
 */
static nksu_fake_state_t fake_state;

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
    if (!cur->enforcing) {
        pr_warn("[selinux_hide] permissive, fake status skipped\n");
        goto out;
    }

    page = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (!page)
        goto out;

    copy = page_address(page);
    memcpy(copy, cur, sizeof(*copy));
    /* A stock boot never reloaded the policy. */
    copy->sequence = 0;
    copy->policyload = 0;
    fake_status = page;
    pr_info("[selinux_hide] fake status ready\n");
out:
    mutex_unlock(&selinux_state.status_lock);
}

static int nksu_status_open(struct inode *inode, struct file *filp)
{
    if (likely(current_uid().val >= 10000)) {
        void *page;

        mutex_lock(&selinux_state.status_lock);
        page = fake_status;
        mutex_unlock(&selinux_state.status_lock);
        if (page) {
            filp->private_data = page;
            return 0;
        }
    }
    return orig_status_open(inode, filp);
}

static ssize_t nksu_write_context(struct file *file, char *buf, size_t size)
{
    char *canon = NULL;
    u32 sid, len;
    ssize_t length;

    if (likely(current_uid().val < 10000))
        return orig_context_write(file, buf, size);

    length = avc_has_perm(&selinux_state, current_sid(), SECINITSID_SECURITY,
                          SECCLASS_SECURITY, SECURITY__CHECK_CONTEXT, NULL);
    if (length)
        goto out;

    length = security_context_to_sid(&fake_state, buf, size, &sid, GFP_KERNEL);
    if (length)
        goto out;

    length = security_sid_to_context(&fake_state, sid, &canon, &len);
    if (length)
        goto out;

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

    length = avc_has_perm(&selinux_state, current_sid(), SECINITSID_SECURITY,
                          SECCLASS_SECURITY, SECURITY__COMPUTE_AV, NULL);
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

    length = security_context_str_to_sid(&fake_state, scon, &ssid, GFP_KERNEL);
    if (length)
        goto out;

    length = security_context_str_to_sid(&fake_state, tcon, &tsid, GFP_KERNEL);
    if (length)
        goto out;

    security_compute_av_user(&fake_state, ssid, tsid, tclass, &avd);

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
}

static int nksu_hide_enable(void)
{
    struct file_operations *status_ops;
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

    fake_state.initialized = true;
    fake_state.policy = nksu_orig_policy_get();

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

    nksu_fake_status_prepare();
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

static int nksu_hide_feature_set(u64 value)
{
    int ret = 0;

    mutex_lock(&nksu_hide_mutex);
    if (value) {
        if (nksu_hide_running) {
            ret = 0;
        } else {
            ret = nksu_hide_enable();
            if (!ret)
                nksu_hide_running = true;
        }
    } else if (nksu_hide_running) {
        nksu_hide_disable();
        nksu_hide_running = false;
    }
    mutex_unlock(&nksu_hide_mutex);
    return ret;
}

static const struct nksu_feature nksu_hide_feature = {
    .id = NKSU_FEATURE_SELINUX_HIDE,
    .name = "selinux_hide",
    .get = nksu_hide_feature_get,
    .set = nksu_hide_feature_set,
};

#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0) */

static int nksu_hide_feature_get(u64 *value)
{
    *value = 0;
    return 0;
}

/* 6.6 moved the policy helpers onto the global selinux_state; KernelSU answers
 * that by vendoring the policy engine, which is not ported here yet, so the
 * feature is read-only and reports itself as off. */
static const struct nksu_feature nksu_hide_feature = {
    .id = NKSU_FEATURE_SELINUX_HIDE,
    .name = "selinux_hide",
    .get = nksu_hide_feature_get,
    .set = NULL,
};

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0) */

int nksu_selinux_hide_init(void)
{
    int ret;

    ret = nksu_feature_register(&nksu_hide_feature);
    if (ret)
        pr_warn("[selinux_hide] feature register failed: %d\n", ret);

    if (!nksu_file_exists(NKSU_FEATURE_FLAG)) {
        pr_info("[selinux_hide] %s absent, off\n", NKSU_FEATURE_FLAG);
        return 0;
    }

    ret = nksu_feature_set(NKSU_FEATURE_SELINUX_HIDE, 1);
    if (ret)
        pr_warn("[selinux_hide] enable from %s failed: %d\n",
                NKSU_FEATURE_FLAG, ret);

    return 0;
}

void nksu_selinux_hide_exit(void)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 6, 0)
    mutex_lock(&nksu_hide_mutex);
    nksu_hide_disable();
    nksu_hide_running = false;
    mutex_unlock(&nksu_hide_mutex);

    mutex_lock(&selinux_state.status_lock);
    if (fake_status) {
        __free_page(fake_status);
        fake_status = NULL;
    }
    mutex_unlock(&selinux_state.status_lock);
#endif
}
