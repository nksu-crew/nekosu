// SPDX-License-Identifier: GPL-3.0-or-later
#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/spinlock.h>
#include <linux/thread_info.h>
#include <linux/uidgid.h>
#include <linux/version.h>
#include <linux/nsproxy.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/sched/signal.h>
#endif

#include <fmac.h>

int privilege_validate(const struct privilege_desc *desc)
{
	if (!desc)
		return -EINVAL;

	if (desc->selinux_domain) {
		size_t len = strnlen(desc->selinux_domain, 64);
		if (len == 0 || len >= 64)
			return -EINVAL;
	}

	return 0;
}

static void disable_seccomp(void)
{
#if defined(CONFIG_SECCOMP) && defined(CONFIG_SECCOMP_FILTER)
	struct task_struct *task = current;

	if (task->seccomp.mode == SECCOMP_MODE_DISABLED)
		return;

	spin_lock_irq(&task->sighand->siglock);
	task->seccomp.mode = SECCOMP_MODE_DISABLED;
	clear_thread_flag(TIF_SECCOMP);
	spin_unlock_irq(&task->sighand->siglock);

	pr_info("seccomp disabled for PID %d\n", task->pid);
#endif
}

static int cred_set_root_uidgid(struct cred *new_cred)
{
	struct user_struct *new_user;

	new_cred->uid   = GLOBAL_ROOT_UID;
	new_cred->euid  = GLOBAL_ROOT_UID;
	new_cred->suid  = GLOBAL_ROOT_UID;
	new_cred->fsuid = GLOBAL_ROOT_UID;
	new_cred->gid   = GLOBAL_ROOT_GID;
	new_cred->egid  = GLOBAL_ROOT_GID;
	new_cred->sgid  = GLOBAL_ROOT_GID;
	new_cred->fsgid = GLOBAL_ROOT_GID;
	new_cred->securebits = 0;

	/* Clear supplementary groups */
	{
		struct group_info *gi = groups_alloc(0);
		if (!gi)
			return -ENOMEM;
		set_groups(new_cred, gi);
		put_group_info(gi);
	}

	/* Allocate root user_struct */
	new_user = alloc_uid(GLOBAL_ROOT_UID);
	if (!new_user)
		return -ENOMEM;

	free_uid(new_cred->user);
	new_cred->user = new_user;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
	if (set_cred_ucounts(new_cred)) {
		pr_err("set_cred_ucounts failed!\n");
		return -ENOMEM;
	}
#endif

	return 0;
}

static void cred_raise_caps(struct cred *new_cred, kernel_cap_t caps)
{
	new_cred->cap_effective = cap_combine(new_cred->cap_effective, caps);
	new_cred->cap_permitted = cap_combine(new_cred->cap_permitted, caps);
	new_cred->cap_bset      = cap_combine(new_cred->cap_bset, caps);
}

/*
 * Determine whether we need to touch credentials at all.
 */
static bool privilege_needs_cred(const struct privilege_desc *desc)
{
	return desc->set_root_uidgid ||
	       !cap_isclear(desc->caps_to_raise) ||
	       (desc->selinux_domain != NULL);
}

/*
 * Apply credential-level changes (UID/GID, caps, SELinux domain).
 * Returns the prepared creds on success (caller must commit/abort),
 * or ERR_PTR on failure.
 */
static struct cred *privilege_prepare_creds(const struct privilege_desc *desc)
{
	struct cred *new_cred;

	new_cred = prepare_creds();
	if (!new_cred) {
		pr_err("prepare_creds failed! OOM?\n");
		return ERR_PTR(-ENOMEM);
	}

	if (desc->set_root_uidgid) {
		int ret = cred_set_root_uidgid(new_cred);
		if (ret) {
			abort_creds(new_cred);
			return ERR_PTR(ret);
		}
	}

	if (!cap_isclear(desc->caps_to_raise))
		cred_raise_caps(new_cred, desc->caps_to_raise);

	if (desc->selinux_domain)
		set_domain(desc->selinux_domain, new_cred);

	return new_cred;
}

int privilege_escalate(const struct privilege_desc *desc)
{
	int ret;

	ret = privilege_validate(desc);
	if (ret)
		return ret;

	/* ── Credential phase ── */
	if (privilege_needs_cred(desc)) {
		struct cred *new_cred = privilege_prepare_creds(desc);
		if (IS_ERR(new_cred))
			return PTR_ERR(new_cred);

		commit_creds(new_cred);
		pr_info("privileges committed for PID %d\n", current->pid);
	}

	/* ── Post-cred phase (thread-local, no rollback needed) ── */
	if (desc->disable_seccomp)
		disable_seccomp();

	if (desc->switch_to_init_ns)
		switch_to_init_ns();

	return 0;
}

int privilege_escalate_from_profile(void)
{
	struct privilege_desc desc = { 0 };
	struct profile p;
	uid_t caller_uid;

	caller_uid = from_kuid(current_user_ns(), current_uid());
	if (nksu_profile_get_dup(caller_uid, &p) < 0) {
		pr_err("no profile found for UID %u\n", caller_uid);
		return -ENOENT;
	}

	desc.set_root_uidgid = true;
	desc.caps_to_raise   = p.caps;
	desc.selinux_domain  = p.selinux_domain[0] ? p.selinux_domain : NULL;
	desc.disable_seccomp = true;
	desc.switch_to_init_ns = (p.namespace == NKSU_NS_GLOBAL);

	return privilege_escalate(&desc);
}
