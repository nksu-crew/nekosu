/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NKSU_SELINUX_POLICY_H
#define _NKSU_SELINUX_POLICY_H

int  sepolicy_dup_and_apply(void);
void sepolicy_restore(void);
int  sepolicy_init(void);
void sepolicy_exit(void);
int  load_policy(void);

/*
 * The untouched pre-NekoSU policy, kept aside by sepolicy_dup_and_apply().
 * The SELinux hiding feature answers userspace requests from this copy so the
 * injected domain/rules stay invisible.  NULL before the swap or after
 * sepolicy_restore().
 */
struct selinux_policy;
struct selinux_policy *nksu_orig_policy_get(void);

#endif /* _NKSU_SELINUX_POLICY_H */
