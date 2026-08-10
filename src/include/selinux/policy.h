/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NKSU_SELINUX_POLICY_H
#define _NKSU_SELINUX_POLICY_H

int  sepolicy_dup_and_apply(void);
void sepolicy_restore(void);
int  sepolicy_init(void);
void sepolicy_exit(void);
int  load_policy(void);

#endif /* _NKSU_SELINUX_POLICY_H */
