/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NKSU_SELINUX_SELINUX_H
#define _NKSU_SELINUX_SELINUX_H

#define DOMAIN           "nksu"
#define DOMAIN_FILE      "nksu_file"
#define DOMAIN_CTX       "u:r:" DOMAIN ":s0"
#define DOMAIN_FILE_CTX  "u:object_r:" DOMAIN_FILE ":s0"

void setenforce(bool status);
bool getenforce(void);
int  set_domain(const char *domain, struct cred *new_cred);

/*
 * Subjective SID of the current task.  The kernel's current_sid() inline pulls
 * in selinux_blob_sizes via selinux_cred(); this accessor avoids the
 * relocation (see selinux.c).  hide.c uses it for its avc_has_perm checks.
 */
u32  nksu_current_sid(void);

/*
 * Relabel the caller's terminal (pts) fds to DOMAIN_FILE.  The package
 * manager hands the caller's tty to system_server through the binder
 * ShellCallback; without this, system_server's write to the pty is denied and
 * `pm`/`cmd` fail with "Failure calling service package: Failed transaction".
 * Called right after a `su` escalation, mirroring KernelSU's ksu_handle_devpts.
 */
void nksu_relabel_tty_fds(void);

/*
 * Relabel an existing path's inode to DOMAIN_FILE.  Used by the manager
 * bootstrap to label /data/adb/nksu/{ncore,bin/busybox} so the init.rc exec
 * (which runs them as DOMAIN) is allowed -- the policy only grants init access
 * to files labelled nksu_file.
 */
void nksu_relabel_path(const char *path);

int  init_selinux_hook(void);
void selinux_exit(void);

#endif /* _NKSU_SELINUX_SELINUX_H */
