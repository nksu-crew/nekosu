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

#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/xattr.h>
#include <linux/magic.h>

#include <fmac.h>
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

static struct task_struct *nksu_init_thread;
static DECLARE_WAIT_QUEUE_HEAD(nksu_selinux_wq);

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
 * SELinux stores its structs at an LSM offset inside cred->security /
 * inode->i_security, not at the start.  The kernel's selinux_cred() /
 * selinux_inode() accessors add selinux_blob_sizes.lbs_*, but using them
 * would create a relocation against the unexported selinux_blob_sizes and
 * the module would fail to load.  Resolve the symbol through the compat
 * layer and add the offsets ourselves.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
static struct task_security_struct *nksu_cred_security(const struct cred *cred)
{
	return (struct task_security_struct *)((char *)cred->security +
					       selinux_blob_sizes.lbs_cred);
}

static struct inode_security_struct *nksu_inode_security(const struct inode *inode)
{
	return (struct inode_security_struct *)((char *)inode->i_security +
						selinux_blob_sizes.lbs_inode);
}
#else
static struct task_security_struct *nksu_cred_security(const struct cred *cred)
{
	return (struct task_security_struct *)cred->security;
}

static struct inode_security_struct *nksu_inode_security(const struct inode *inode)
{
	return (struct inode_security_struct *)inode->i_security;
}
#endif

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
		struct task_security_struct *tsec = nksu_cred_security(new_cred);

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

/* SID of DOMAIN_FILE (u:object_r:nksu_file:s0); 0 until policy is loaded. */
static u32 nksu_file_sid;

static void resolve_file_sid(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	int rc = security_context_to_sid(DOMAIN_FILE_CTX,
					  strlen(DOMAIN_FILE_CTX),
					  &nksu_file_sid, GFP_KERNEL);
#else
	int rc = security_context_to_sid(&selinux_state, DOMAIN_FILE_CTX,
					  strlen(DOMAIN_FILE_CTX),
					  &nksu_file_sid, GFP_KERNEL);
#endif

	if (rc) {
		pr_err("nksu: failed to get SID for %s: %d\n",
		       DOMAIN_FILE_CTX, rc);
		nksu_file_sid = 0;
	}
}

/*
 * Force DOMAIN_FILE onto an already-created path.  The manager bootstrap
 * (manager.c) runs as DOMAIN, which cannot use create_sid here: DOMAIN_FILE
 * has no `filesystem associate` permission, so creating the inode directly as
 * nksu_file would be denied.  Instead the file is created normally and the
 * cached inode is relabeled in place, exactly like nksu_relabel_tty_fds().
 * This is what lets init's injected init.rc exec /data/adb/nksu/ncore.
 *
 * The in-memory SID alone is not enough: init runs the injected init.rc exec
 * at post-fs-data, before the manager scan relabels the cached inode, and the
 * in-memory change is lost on reboot.  So also write the security.selinux
 * xattr, making the label persist so post-fs-data can exec the daemon on the
 * next boot (the first boot after install still cannot: the daemon does not
 * exist until the manager scan runs, after post-fs-data).
 */
void nksu_relabel_path(const char *path)
{
	struct file *file;
	struct inode *inode;
	struct inode_security_struct *sec;
	int rc;

	if (!nksu_file_sid)
		resolve_file_sid();
	if (!nksu_file_sid)
		return;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file))
		return;

	inode = file_inode(file);
	if (inode->i_security) {
		sec = nksu_inode_security(inode);
		if (sec)
			sec->sid = nksu_file_sid;
	}

	/*
	 * Persist the context.  Reached through a resolved pointer, so it may be
	 * NULL on a KMI where the symbol is missing; the in-memory SID above
	 * still keeps the current boot working in that case.
	 */
	if (__vfs_setxattr_noperm) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
		rc = __vfs_setxattr_noperm(mnt_idmap(file->f_path.mnt),
					   file->f_path.dentry,
					   XATTR_NAME_SELINUX, DOMAIN_FILE_CTX,
					   strlen(DOMAIN_FILE_CTX) + 1, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
		rc = __vfs_setxattr_noperm(mnt_user_ns(file->f_path.mnt),
					   file->f_path.dentry,
					   XATTR_NAME_SELINUX, DOMAIN_FILE_CTX,
					   strlen(DOMAIN_FILE_CTX) + 1, 0);
#else
		rc = __vfs_setxattr_noperm(file->f_path.dentry, XATTR_NAME_SELINUX,
					   DOMAIN_FILE_CTX,
					   strlen(DOMAIN_FILE_CTX) + 1, 0);
#endif
		if (rc)
			pr_warn("nksu: cannot persist label on %s: %d\n", path,
				rc);
	}

	filp_close(file, NULL);
}

/*
 * Relabel the process's tty (pts) fds to DOMAIN_FILE.  `cmd`/`pm` hand the
 * caller's terminal to system_server via the binder ShellCallback; without
 * this system_server's write to the pty is denied and the command fails with
 * "Failed transaction (2147483646)".  All domains may access DOMAIN_FILE
 * (see load_policy()), so this is safe and mirrors KernelSU's
 * ksu_handle_devpts().
 */
void nksu_relabel_tty_fds(void)
{
	int i;

	if (!nksu_file_sid)
		return;

	for (i = 0; i < 3; i++) {
		struct file *file = fget(i);
		struct inode *inode;

		if (!file)
			continue;

		inode = file_inode(file);
		if (inode && inode->i_sb &&
		    inode->i_sb->s_magic == DEVPTS_SUPER_MAGIC) {
			struct inode_security_struct *sec = NULL;

			if (inode->i_security)
				sec = nksu_inode_security(inode);

			if (sec) {
				sec->sid = nksu_file_sid;
				inode->i_uid.val = 0;
				inode->i_gid.val = 0;
			}
		}
		fput(file);
	}
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
    // Too noisy, disabled.
    /*
	rc = sepolicy_make_audit();
	if (rc) {
		pr_err("[selinux]: failed to make audit: %d\n", rc);
		return rc;
	}*/
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

	/* Cache the SID used to relabel the root shell's tty. */
	resolve_file_sid();

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
	int ret = -ETIMEDOUT;

	pr_info("[selinux]: waiting for SELinux policy...\n");

	while (timeout_ms > 0) {
		if (kthread_should_stop())
			return 0;

		if (READ_ONCE(selinux_state.policy))
			break;

		msleep(10);
		timeout_ms -= 10;
	}

	if (READ_ONCE(selinux_state.policy)) {
		pr_info("[selinux]: SELinux policy ready, continuing init\n");
		ret = load_hook();
	} else {
		pr_err("[selinux]: SELinux policy not ready after 30s, giving up\n");
	}

	/*
	 * Do not return on our own: if we exit while the module still holds
	 * our task_struct, the later kthread_stop() dereferences freed memory.
	 * Wait here until selinux_exit() asks us to stop.
	 */
	wait_event_interruptible(nksu_selinux_wq, kthread_should_stop());
	return ret;
}

int init_selinux_hook(void)
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

void selinux_exit(void)
{
	pr_info("[selinux]: sepolicy exit – restoring original policy\n");

	if (nksu_init_thread) {
		kthread_stop(nksu_init_thread);
		nksu_init_thread = NULL;
	}

	sepolicy_restore();
}

#if defined(__clang__)
#pragma clang attribute pop
#endif
