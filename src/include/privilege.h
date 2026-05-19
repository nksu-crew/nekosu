#include <linux/capability.h>

#ifndef PRIVILEGE_H
#define PRIVILEGE_H

#define PRIV_ROOT (1 << 0)
#define PRIV_CAPS (1 << 1)
#define PRIV_SELINUX (1 << 2)
#define PRIV_SECCOMP (1 << 3)
#define PRIV_ALL (PRIV_ROOT | PRIV_CAPS | PRIV_SELINUX | PRIV_SECCOMP)

void grant_privileges(unsigned int flags, kernel_cap_t caps_to_raise,
		      const char *target_domain);
void elevate_to_root(void);

#endif /* PRIVILEGE_H */