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
#include <linux/slab.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <linux/sched/signal.h>
#endif

#include <fmac.h>

#define PRIV_ROOT    (1 << 0)
#define PRIV_CAPS    (1 << 1)
#define PRIV_SELINUX (1 << 2)
#define PRIV_SECCOMP (1 << 3)
#define PRIV_ALL     (PRIV_ROOT | PRIV_CAPS | PRIV_SELINUX | PRIV_SECCOMP)

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

static void reset_groups(struct cred *cred)
{
	struct group_info *gi = groups_alloc(0);
	if (!gi)
		return;
	set_groups(cred, gi);
	put_group_info(gi);
}

void grant_privileges(unsigned int flags, kernel_cap_t caps_to_raise,
		      const char *target_domain)
{
	struct cred *new_cred;
	bool needs_commit = false;

	/* seccomp-only fast path: no cred changes needed */
	if ((flags & PRIV_SECCOMP) &&
	    !(flags & (PRIV_ROOT | PRIV_CAPS | PRIV_SELINUX))) {
		disable_seccomp();
		return;
	}

	new_cred = prepare_creds();
	if (!new_cred) {
		pr_err("prepare_creds failed! OOM?\n");
		return;
	}

	if (flags & PRIV_ROOT) {
		struct user_struct *new_user;

		new_cred->uid = GLOBAL_ROOT_UID;
		new_cred->euid = GLOBAL_ROOT_UID;
		new_cred->suid = GLOBAL_ROOT_UID;
		new_cred->fsuid = GLOBAL_ROOT_UID;
		new_cred->gid = GLOBAL_ROOT_GID;
		new_cred->egid = GLOBAL_ROOT_GID;
		new_cred->sgid = GLOBAL_ROOT_GID;
		new_cred->fsgid = GLOBAL_ROOT_GID;
		reset_groups(new_cred);
		new_cred->securebits = 0;

		new_user = alloc_uid(GLOBAL_ROOT_UID);
		if (!new_user) {
			pr_err("alloc_uid failed!\n");
			abort_creds(new_cred);
			return;
		}
		free_uid(new_cred->user);
		new_cred->user = new_user;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
		if (set_cred_ucounts(new_cred)) {
			pr_err("set_cred_ucounts failed!\n");
			abort_creds(new_cred);
			return;
		}
#endif

		needs_commit = true;
	}

	if (flags & PRIV_CAPS) {
		new_cred->cap_effective =
		    cap_combine(new_cred->cap_effective, caps_to_raise);
		new_cred->cap_permitted =
		    cap_combine(new_cred->cap_permitted, caps_to_raise);
		new_cred->cap_bset =
		    cap_combine(new_cred->cap_bset, caps_to_raise);
		needs_commit = true;
	}

	if ((flags & PRIV_SELINUX) && target_domain) {
		set_domain(target_domain, new_cred);
		needs_commit = true;
	}

	/* disable seccomp before commit_creds to avoid hook interference */
	if (flags & PRIV_SECCOMP)
		disable_seccomp();

	if (needs_commit) {
		commit_creds(new_cred);
		pr_info("privileges committed for PID %d\n", current->pid);
	} else {
		abort_creds(new_cred);
	}
}

void elevate_to_root(void)
{
	kernel_cap_t all_caps;
	struct profile p;
	uid_t uid_val = from_kuid(current_user_ns(), current_uid());

	if (nksu_profile_get_dup(uid_val, &p) < 0) {
		pr_err("failed to get profile!\n");
		return;
	}

	all_caps = p.caps;

	grant_privileges(PRIV_ALL, all_caps, p.selinux_domain);
	if (p.namespace == NKSU_NS_GLOBAL)
		switch_to_init_ns();
}
