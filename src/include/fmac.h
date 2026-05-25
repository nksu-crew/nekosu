// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * FMAC - File Monitoring and Access Control Kernel Module
 * Copyright (C) 2025 Aqnya
 */

#ifndef _LINUX_FMAC_H
#define _LINUX_FMAC_H

#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/version.h>
#include <asm/syscall.h>

#include "selinux/selinux.h"
#include "selinux/rule.h"
#include "selinux/policy.h"
#include "selinux/domain.h"
#include "selinux/dup.h"

#include "klog.h"
#include "privilege.h"
#include "tracepoint.h"
#include "ioctl.h"
#include "manager.h"
#include "hook.h"
#include "ns.h"

#include "../profile/profile.h"
#include "../fd/fd.h"

#ifdef CONFIG_NKSU_SYSCALL
#include "../syscall/dispatch.h"
#include "../syscall/syscall.h"
#endif

extern struct proc_dir_entry *fmac_proc_dir;

#define MAX_PATH_LEN 1024

#endif /* _LINUX_FMAC_H */
