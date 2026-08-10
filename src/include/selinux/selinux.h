/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NKSU_SELINUX_SELINUX_H
#define _NKSU_SELINUX_SELINUX_H

#define DOMAIN     "nksu"
#define DOMAIN_CTX "u:r:" DOMAIN ":s0"

void setenforce(bool status);
bool getenforce(void);
int  set_domain(const char *domain, struct cred *new_cred);
int  init_selinux_hook(void);
void __exit selinux_exit(void);

#endif /* _NKSU_SELINUX_SELINUX_H */
