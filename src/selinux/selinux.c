// SPDX-License-Identifier: GPL-2.0
/*
 * nksu SELinux entry point — wait for policy, dup it, inject our domain,
 * load rules, go.
 *
 * On most Android kernels SELinux policy loads after kernel modules probe,
 * so we spawn a kthread to poll selinux_state.policy until it's ready.
 */

#include "security.h"
#include "ss/symtab.h"
#include "ss/policydb.h"
#include "ss/ebitmap.h"
#include "ss/services.h"
#include "objsec.h"

#include <fmac.h>

static struct task_struct *nksu_init_thread;

/* toggle enforcing / permissive */

void setenforce(bool status)
{
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
	WRITE_ONCE(selinux_state.enforcing, status);
#endif
}

bool getenforce(void)
{
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
	return READ_ONCE(selinux_state.enforcing);
#else
	return true;
#endif
}

/*
 * Switch a cred's security context to the given domain.
 * Old SID is saved in ->osid so we can restore it later.
 * See selinux_bprm_committing_creds() for the canonical pattern.
 */
int set_domain(const char *domain, struct cred *new_cred)
{
	u32 newsid;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	int rc = security_context_to_sid(domain, strlen(domain),
					  &newsid, GFP_KERNEL);
#else
	int rc = security_context_to_sid(&selinux_state, domain,
					  strlen(domain), &newsid,
					  GFP_KERNEL);
#endif

	if (rc) {
		pr_err("nksu: failed to get SID for %s: %d\n", domain, rc);
		return rc;
	}

	if (new_cred->security) {
		struct task_security_struct *tsec = new_cred->security;

		tsec->osid          = tsec->sid;
		tsec->sid           = newsid;
		tsec->exec_sid      = 0;
		tsec->create_sid    = 0;
		tsec->keycreate_sid = 0;
		tsec->sockcreate_sid = 0;
		return 0;
	}

	return -EPERM;
}

/*
 * Mark a type as permissive — denials get logged but not enforced.
 * Only used in debug builds so the nksu domain never gets blocked.
 */
#ifdef CONFIG_NKSU_DEBUG
static bool do_allow(struct policydb *db, const char *type_name)
{
	struct type_datum *type;

	type = (struct type_datum *)symtab_search(&db->p_types, type_name);
	if (!type) {
		pr_err("[selinux]: type '%s' not found, cannot set permissive\n",
		       type_name);
		return false;
	}

	if (ebitmap_set_bit(&db->permissive_map, type->value, true)) {
		pr_err("[selinux]: failed to set permissive bit for '%s'\n",
		       type_name);
		return false;
	}

	return true;
}
#endif

/*
 * Core init — runs once policy is available:
 *   1. Dup the live policy (so we can mutate it safely)
 *   2. [debug] turn off dontaudit so every denial is visible
 *   3. Inject the nksu domain type
 *   4. Load our static allow rules
 *   5. [debug] make the domain permissive
 */
int load_hook(void)
{
	int rc;
#ifdef CONFIG_NKSU_DEBUG
	struct policydb *db;
#endif

	if (!getenforce()) {
		pr_info("[selinux]: enforcing is false, enabling\n");
		setenforce(true);
	}

	rc = sepolicy_dup_and_apply();
	if (rc) {
		pr_err("[selinux]: failed to dup policy (%d), aborting\n", rc);
		return rc;
	}

#ifdef CONFIG_NKSU_DEBUG
	rc = sepolicy_make_audit();
	if (rc) {
		pr_err("[selinux]: failed to make audit: %d\n", rc);
		return rc;
	}
#endif

	rc = sepolicy_add_domain(DOMAIN);
	if (rc) {
		pr_err("[selinux]: Failed to add domain '%s': %d\n",
		       DOMAIN, rc);
		return rc;
	}

	rc = sepolicy_init();
	if (rc) {
		pr_err("[selinux]: Failed to apply rules for '%s': %d\n",
		       DOMAIN, rc);
		return rc;
	}

#ifdef CONFIG_NKSU_DEBUG
	pr_info("[selinux]: debug mode, setting permissive for '%s'\n",
		DOMAIN);
	mutex_lock(&selinux_state.policy_mutex);
	db = &rcu_dereference_protected(selinux_state.policy,
		lockdep_is_held(&selinux_state.policy_mutex))->policydb;
	do_allow(db, DOMAIN);
	mutex_unlock(&selinux_state.policy_mutex);
	avc_reset();
#endif

	return 0;
}

/* poll for policy, give up after 30 seconds */
static int nksu_selinux_init_thread(void *data)
{
	int timeout_ms = 30 * 1000;

	pr_info("[selinux]: waiting for SELinux policy...\n");

	while (timeout_ms > 0) {
		if (kthread_should_stop())
			return -EINTR;

		if (READ_ONCE(selinux_state.policy))
			break;

		msleep(10);
		timeout_ms -= 10;
	}

	if (!READ_ONCE(selinux_state.policy)) {
		pr_err("[selinux]: SELinux policy not ready after 30s, giving up\n");
		return -ETIMEDOUT;
	}

	pr_info("[selinux]: SELinux policy ready, continuing init\n");
	return load_hook();
}

int __init init_selinux_hook(void)
{
	if (!READ_ONCE(selinux_state.policy)) {
		nksu_init_thread = kthread_run(nksu_selinux_init_thread,
					       NULL, "nksu-selinux-init");
		if (IS_ERR(nksu_init_thread)) {
			pr_err("[selinux]: failed to start init thread: %ld\n",
			       PTR_ERR(nksu_init_thread));
			return PTR_ERR(nksu_init_thread);
		}
		return 0;
	}

	return load_hook();
}

void __exit selinux_exit(void)
{
	pr_info("[selinux]: sepolicy exit – restoring original policy\n");

	if (nksu_init_thread) {
		kthread_stop(nksu_init_thread);
		nksu_init_thread = NULL;
	}

	sepolicy_restore();
}
