// SPDX-License-Identifier: GPL-2.0
/*
 * SELinux hiding, read side only.
 *
 * Detectors read selinuxfs to learn what a root solution injected.  We keep a
 * clean copy of the policy -- nksu_orig_policy, the untouched policy that
 * sepolicy_dup_and_apply() put aside before load_policy() added the nksu domain
 * and rules -- and serve it from the read interfaces:
 *
 *   /sys/fs/selinux/policy   -> the serialised clean policy (captured once with
 *                               policydb_write(), the same call the stock
 *                               security_read_policy() uses)
 *   /sys/fs/selinux/status   -> a frozen status page, so a policy reload is not
 *                               visible in policyload/sequence
 *
 * Only app uids (uid >= 10000) are affected.  The write side
 * (/sys/fs/selinux/{context,access}) is deliberately not intercepted.
 *
 * It is a normal kernel feature: flip it with IOC_FEATURE_SET through the
 * manager's JNI interface, and /data/adb/nksu/feature turns it on at boot.
 */
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include <security.h>
#include "ss/policydb.h"

#include "hook/patch.h"
#include "manager/feature.h"
#include "selinux/hide.h"
#include "selinux/policy.h"
#include "selinux/selinux.h"
#include "symbol/symbol.h"
/* Last: redirects policydb_write (and friends) through resolved pointers. */
#include "symbol/symbol_compat.h"

/* Its presence at boot turns the feature on. */
#define NKSU_FEATURE_FLAG "/data/adb/nksu/feature"

/* Bound the policy capture; a loaded policy blob is a few hundred KiB. */
#define NKSU_POLICY_BLOB_MAX (16 * 1024 * 1024)

/*
 * The flag lives under /data/adb, which only the nksu domain can reach.  Like
 * profile_store, borrow a cred switched to that domain for the lookup; the
 * feature-init stage (zygote) is already after /data is mounted.  Own cred, not
 * the manager scan's.
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

static DEFINE_MUTEX(nksu_hide_mutex);
static bool nksu_hide_running;

/* The clean policy, serialised once when the feature is enabled. */
static void *clean_policy_data;
static size_t clean_policy_len;

static ssize_t (*policy_read_slot)(struct file *, char __user *, size_t, loff_t *);
static ssize_t (*orig_policy_read)(struct file *, char __user *, size_t, loff_t *);

static int (*status_open_slot)(struct inode *, struct file *);
static int (*orig_status_open)(struct inode *, struct file *);

static struct page *fake_status;

/* core.c: false for a first-stage (vendor_boot) load, true for a late insmod. */
extern bool late_load;

/*
 * Serialise the clean policy the way security_read_policy() does: policydb_write
 * into a buffer sized for the loaded blob.  The extra slack per type covers the
 * attribute-map entries policydb_read() adds, exactly like nksu_policy_dup().
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

static void nksu_hide_unhook(void)
{
    if (policy_read_slot && orig_policy_read) {
        nksu_patch_text(policy_read_slot, &orig_policy_read,
                        sizeof(orig_policy_read));
        policy_read_slot = NULL;
        orig_policy_read = NULL;
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
    struct file_operations *policy_ops, *status_ops;
    int ret;

    ret = nksu_clean_policy_prepare();
    if (ret) {
        pr_err("[selinux_hide] cannot prepare the clean policy: %d\n", ret);
        return ret;
    }

    policy_ops = (struct file_operations *)nksu_ksym_lookup("sel_policy_ops");
    if (!policy_ops) {
        pr_err("[selinux_hide] symbol 'sel_policy_ops' not found\n");
        return -ENOSYS;
    }

    {
        ssize_t (*new_read)(struct file *, char __user *, size_t, loff_t *) =
            nksu_read_policy;

        policy_read_slot = &policy_ops->read;
        orig_policy_read = *policy_read_slot;
        ret = nksu_patch_text(policy_read_slot, &new_read, sizeof(new_read));
        if (ret) {
            pr_err("[selinux_hide] patch policy read: %d\n", ret);
            policy_read_slot = NULL;
            orig_policy_read = NULL;
            goto fail;
        }
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
    return ret;
}

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

    ret = nksu_feature_set(NKSU_FEATURE_SELINUX_HIDE, 1);
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
