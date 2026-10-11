#ifndef _HOOK_H
#define _HOOK_H

#include "boot/init_rc.h"

#define SU_PATH             "/system/bin/su"
#define SU_PATH_LEN         (sizeof(SU_PATH))

/*
 * `su` is redirected to our own userspace binary, which runs the `su` command
 * (ncore's su mode).  The kernel has already escalated the process by the time
 * it is exec'd, so ncore just parses the su arguments and execs the shell; it
 * also no longer depends on KernelSU's ksud.
 */
#define REDIRECT_TARGET     NKSU_NCORE_PATH
#define REDIRECT_TARGET_LEN (sizeof(REDIRECT_TARGET))

#define SH_PATH             "/system/bin/sh"
#define SH_PATH_LEN         (sizeof(SH_PATH))


static inline bool path_is_su(const char *p)
{
	return memcmp(p, SU_PATH, SU_PATH_LEN) == 0;
}

int init_syscall_hook(void);

#endif