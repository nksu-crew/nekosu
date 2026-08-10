/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PRIVILEGE_H
#define PRIVILEGE_H

#include <linux/capability.h>

/*
 * privilege_desc — describes the full set of privilege changes to apply.
 *
 * All fields are explicit; the caller decides exactly what to change.
 * For the common case of "give me everything", use
 * privilege_escalate_from_profile() which reads the stored profile and
 * builds a descriptor from it.
 */
struct privilege_desc {
	bool set_root_uidgid;       /* set all UIDs/GIDs to root (0) */
	kernel_cap_t caps_to_raise; /* capabilities to add to effective/permitted/bset */
	const char *selinux_domain; /* target SELinux domain (NULL = no change) */
	bool disable_seccomp;       /* reset seccomp to DISABLED */
	bool switch_to_init_ns;     /* switch to init namespace */
};

/*
 * Validate a privilege descriptor. Returns 0 if valid, -EINVAL otherwise.
 * Must be called before privilege_escalate() if you want early validation.
 */
int privilege_validate(const struct privilege_desc *desc);

/*
 * Apply all privilege changes described by `desc` to the current task.
 *
 * Operations are applied in this order:
 *   1. Prepare new credentials
 *   2. Set root UID/GID (if requested)
 *   3. Raise capabilities
 *   4. Set SELinux domain
 *   5. Commit credentials
 *   6. Disable seccomp
 *   7. Switch to init namespace
 *
 * Returns 0 on success, negative errno on failure.
 * On failure, any uncommitted credential changes are discarded.
 */
int privilege_escalate(const struct privilege_desc *desc);

/*
 * Convenience: lookup the profile for the current task's UID, build a
 * descriptor that requests everything the profile allows, and escalate.
 *
 * This is the main entry point — replaces the old elevate_to_root().
 */
int privilege_escalate_from_profile(void);

#endif /* PRIVILEGE_H */
