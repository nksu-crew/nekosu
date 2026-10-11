// SPDX-License-Identifier: GPL-2.0
/*
 * Policy lifecycle: duplicate the live policy, mutate it, restore it, and
 * apply NekoSU's static rule set.
 *
 * The duplication uses the same policydb_write()/policydb_read() round-trip as
 * KernelSU: it yields a fully independent policy, so the rule engine can
 * mutate types, avtab and filename_trans without touching the original.
 *
 * The static rule set mirrors KernelSU's apply_kernelsu_rules() (domain, an
 * unconstrained file type, allow-any-any, the ioctl xperms and the
 * Magisk/KernelSU-compatible allow list), with NekoSU's domain names.
 *
 * Locking: policy_mutex protects selinux_state.policy.
 */

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <fmac.h>

#include "ss/policydb.h"
#include "ss/services.h"
#include "ss/avtab.h"
#include "ss/ebitmap.h"
#include "ss/hashtab.h"

#include "symbol/symbol_compat.h"
/*
 * This file calls resolved unexported kernel functions through pointers.
 * Disable CFI for its functions so those indirect calls are not type-hash
 * checked (the module's build headers may hash differently than the running
 * kernel). Functions the kernel calls back live in other files.
 */
#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

/* the pre-hook policy, restored on unload */
static struct selinux_policy *nksu_orig_policy __read_mostly;
static struct selinux_policy *nksu_work_policy __read_mostly;

struct selinux_policy *nksu_orig_policy_get(void)
{
	return nksu_orig_policy;
}

/*
 * Independent deep-copy of a selinux_policy via the policydb (de)serializer,
 * exactly like KernelSU's ksu_dup_sepolicy.  This is what makes mutation and
 * eventual destruction of the copy safe.
 */
static struct selinux_policy *nksu_policy_dup(struct selinux_policy *old_pol)
{
	struct selinux_policy *new_pol;
	struct policy_file fp;
	size_t len;
	void *data;
	int ret;

	// policydb_read() adds each type to its own attribute map; keep one
	// spare ebitmap entry per type so the round-trip never returns -EINVAL.
	len = old_pol->policydb.len + (size_t)old_pol->policydb.p_types.nprim *
					      (sizeof(u32) + sizeof(u64));

	data = vmalloc(len);
	if (!data)
		return ERR_PTR(-ENOMEM);

	fp.data = data;
	fp.len = len;

	ret = policydb_write(&old_pol->policydb, &fp);
	if (ret) {
		pr_err("[selinux]: policydb_write: %d\n", ret);
		vfree(data);
		return ERR_PTR(ret);
	}
	len -= fp.len;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 18, 0)
	/* re-add the Android netlink config bits the writer may have dropped */
	{
		static const size_t kConfigOff = 20; /* 4*2+8+4 */

		if (len >= kConfigOff + sizeof(u32)) {
			u32 *config_ptr = (u32 *)((unsigned long)data + kConfigOff);

			if (old_pol->policydb.android_netlink_route)
				*config_ptr |= POLICYDB_CONFIG_ANDROID_NETLINK_ROUTE;
			if (old_pol->policydb.android_netlink_getneigh)
				*config_ptr |= POLICYDB_CONFIG_ANDROID_NETLINK_GETNEIGH;
		}
	}
#endif

	new_pol = kmemdup(old_pol, sizeof(*old_pol), GFP_KERNEL);
	if (!new_pol) {
		vfree(data);
		return ERR_PTR(-ENOMEM);
	}
	memset(&new_pol->policydb, 0, sizeof(new_pol->policydb));

	fp.data = data;
	fp.len = len;

	ret = policydb_read(&new_pol->policydb, &fp);
	if (ret) {
		pr_err("[selinux]: policydb_read: %d\n", ret);
		kfree(new_pol);
		vfree(data);
		return ERR_PTR(ret);
	}
	new_pol->policydb.len = len;
	vfree(data);

	return new_pol;
}

/*
 * Clone the live policy and RCU-swap it in, keeping the original aside so
 * unload can restore it.
 */
int sepolicy_dup_and_apply(void)
{
	struct selinux_policy *orig, *work;

	if (nksu_orig_policy) {
		pr_warn("[selinux]: sepolicy_dup_and_apply: already active\n");
		return -EBUSY;
	}

	mutex_lock(&selinux_state.policy_mutex);

	orig = rcu_dereference_protected(selinux_state.policy,
					 lockdep_is_held(&selinux_state.policy_mutex));
	if (!orig) {
		mutex_unlock(&selinux_state.policy_mutex);
		pr_err("[selinux]: sepolicy_dup_and_apply: no live policy\n");
		return -ENOENT;
	}

	work = nksu_policy_dup(orig);
	if (IS_ERR(work)) {
		mutex_unlock(&selinux_state.policy_mutex);
		pr_err("[selinux]: sepolicy_dup_and_apply: dup failed\n");
		return PTR_ERR(work);
	}

	nksu_orig_policy = orig;
	nksu_work_policy = work;

	rcu_assign_pointer(selinux_state.policy, work);

	mutex_unlock(&selinux_state.policy_mutex);
	synchronize_rcu();

	pr_info("[selinux]: policy duplicated, working copy installed\n");
	return 0;
}

void sepolicy_restore(void)
{
	struct selinux_policy *live;

	if (!nksu_orig_policy) {
		pr_warn("[selinux]: sepolicy_restore: nothing to restore\n");
		return;
	}

	mutex_lock(&selinux_state.policy_mutex);

	live = rcu_dereference_protected(selinux_state.policy,
					 lockdep_is_held(&selinux_state.policy_mutex));
	if (live == nksu_work_policy)
		rcu_assign_pointer(selinux_state.policy, nksu_orig_policy);
	else
		pr_warn("[selinux]: live policy already replaced, leaving it\n");

	mutex_unlock(&selinux_state.policy_mutex);
	synchronize_rcu();

	/*
	 * Deliberately leak the working copy: freeing it raced live SELinux
	 * permission checks on other CPUs.  One leaked policy per load is a
	 * small price for a safe unload, and it mirrors KernelSU, which never
	 * tears its policy down either.
	 */
	nksu_work_policy = NULL;
	nksu_orig_policy = NULL;

	avc_reset();

	pr_info("[selinux]: original policy restored (working copy kept)\n");
}

/* ---- static rule set (aligned with KernelSU's apply_kernelsu_rules) ---- */

#define ALL NULL

static void apply(const char *src, const char *tgt, const char *cls,
		  const char *perm)
{
	sepolicy_add_rule(src, tgt, cls, perm, AVTAB_ALLOWED, false);
}

static bool xperm_supported(void)
{
	struct selinux_policy *pol;
	bool ok;

	mutex_lock(&selinux_state.policy_mutex);
	pol = rcu_dereference_protected(selinux_state.policy,
					lockdep_is_held(&selinux_state.policy_mutex));
	ok = pol && pol->policydb.policyvers >= POLICYDB_VERSION_XPERMS_IOCTL;
	mutex_unlock(&selinux_state.policy_mutex);
	return ok;
}

int load_policy(void)
{
	if (!getenforce())
		pr_info("[selinux]: enforcing is false, applying rules anyway\n");

	/* our domain: unconfined, permissive in debug builds */
	sepolicy_add_domain(DOMAIN);
	sepolicy_set_permissive(DOMAIN);
	sepolicy_add_typeattribute(DOMAIN, "mlstrustedsubject");
	sepolicy_add_typeattribute(DOMAIN, "netdomain");
	sepolicy_add_typeattribute(DOMAIN, "bluetoothdomain");

	/* an unconstrained file type for the daemon's own files */
	sepolicy_add_type(DOMAIN_FILE);
	sepolicy_add_typeattribute(DOMAIN_FILE, "file_type");
	sepolicy_add_typeattribute(DOMAIN_FILE, "mlstrustedobject");
	apply("domain", DOMAIN_FILE, ALL, ALL);

	/* allow all */
	apply(DOMAIN, ALL, ALL, ALL);

	/* allow any ioctl */
	if (xperm_supported()) {
		sepolicy_add_xperm(DOMAIN, ALL, "blk_file", ALL, AVTAB_XPERMS_ALLOWED, false);
		sepolicy_add_xperm(DOMAIN, ALL, "fifo_file", ALL, AVTAB_XPERMS_ALLOWED, false);
		sepolicy_add_xperm(DOMAIN, ALL, "chr_file", ALL, AVTAB_XPERMS_ALLOWED, false);
		sepolicy_add_xperm(DOMAIN, ALL, "file", ALL, AVTAB_XPERMS_ALLOWED, false);
	}

	/* our daemon triggered by init */
	apply("init", DOMAIN, ALL, ALL);

	/* copied from Magisk/KernelSU rules */
	apply("servicemanager", DOMAIN, "dir", "search");
	apply("servicemanager", DOMAIN, "dir", "read");
	apply("servicemanager", DOMAIN, "file", "open");
	apply("servicemanager", DOMAIN, "file", "read");
	apply("servicemanager", DOMAIN, "process", "getattr");
	apply("domain", DOMAIN, "process", "sigchld");

	apply("logd", DOMAIN, "dir", "search");
	apply("logd", DOMAIN, "file", "read");
	apply("logd", DOMAIN, "file", "open");
	apply("logd", DOMAIN, "file", "getattr");

	apply("domain", DOMAIN, "fd", "use");
	apply("domain", DOMAIN, "fifo_file", "write");
	apply("domain", DOMAIN, "fifo_file", "read");
	apply("domain", DOMAIN, "fifo_file", "open");
	apply("domain", DOMAIN, "fifo_file", "getattr");
	apply("domain", DOMAIN, "unix_stream_socket", "read");
	apply("domain", DOMAIN, "unix_stream_socket", "write");
	apply("domain", DOMAIN, "unix_stream_socket", "connectto");
	apply("domain", DOMAIN, "unix_stream_socket", "getopt");
	apply("domain", DOMAIN, "unix_stream_socket", "getattr");

	apply("domain", DOMAIN, "memfd_file", "execute");
	apply("domain", DOMAIN, "memfd_file", "getattr");
	apply("domain", DOMAIN, "memfd_file", "map");
	apply("domain", DOMAIN, "memfd_file", "read");
	apply("domain", DOMAIN, "memfd_file", "write");

	apply("hwservicemanager", DOMAIN, "dir", "search");
	apply("hwservicemanager", DOMAIN, "file", "read");
	apply("hwservicemanager", DOMAIN, "file", "open");
	apply("hwservicemanager", DOMAIN, "process", "getattr");

	apply("domain", DOMAIN, "binder", ALL);

	apply("system_server", DOMAIN, "process", "getpgid");
	apply("system_server", DOMAIN, "process", "sigkill");

	pr_info("[selinux]: policy for domain '%s' loaded\n", DOMAIN);
	return 0;
}

int sepolicy_init(void)
{
	return load_policy();
}

void sepolicy_exit(void)
{
	pr_info("[selinux]: sepolicy exit\n");
}

#if defined(__clang__)
#pragma clang attribute pop
#endif
