// SPDX-License-Identifier: GPL-2.0
/*
 * Syscall interception via kernel tracepoints.
 * We attach probes to sys_enter (to intercept syscalls) and
 * sched_process_fork (to propagate tracepoint flags to children
 * of managed processes).  The sys_enter probe dispatches through
 * a static hook table keyed by syscall number.
 */

#include <asm/syscall.h>
#include <linux/kernel.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/trace_events.h>
#include <linux/tracepoint.h>

#include "handle.h"
#include "tracepoint.h"
#include <fmac.h>

struct syscall_hook {
	int nr;
	long (*handler)(struct pt_regs *);
};

static const struct syscall_hook syscall_hooks[] = {
	{ __NR_prctl,	   handle_prctl_hooks },
	{ __NR_execve,	   hook__NR_execve	  },
	{ __NR_execveat,   hook__NR_execveat   },
	{ __NR_faccessat,  hook_path_at	  },
	{ __NR_newfstatat, hook_path_at	  },
};

static struct tracepoint *tp_sys_enter;
static struct tracepoint *tp_sched_fork;

/* mark every thread belonging to uid so its syscalls hit the tracepoint */
void mark_threads_by_uid(uid_t uid)
{
	struct task_struct *g, *p;

	rcu_read_lock();
	for_each_process_thread(g, p) {
		if (__kuid_val(task_uid(p)) == uid)
			set_tsk_thread_flag(p, TIF_SYSCALL_TRACEPOINT);
	}
	rcu_read_unlock();
}

/* mark every thread in the process identified by pid */
void mark_threads_by_pid(pid_t pid)
{
	struct task_struct *task, *t;

	rcu_read_lock();

	task = find_task_by_vpid(pid);
	if (!task)
		goto out;

	for_each_thread(task, t)
		set_tsk_thread_flag(t, TIF_SYSCALL_TRACEPOINT);

out:
	rcu_read_unlock();
}

/* sys_enter callback: look up the syscall number in the hook table */
static void probe_sys_enter(void *data, struct pt_regs *regs, long id)
{
	int i;

	if (!nksu_profile_has_uid(__kuid_val(task_uid(current))))
		return;

	for (i = 0; i < ARRAY_SIZE(syscall_hooks); i++) {
		if (syscall_hooks[i].nr == id) {
			syscall_hooks[i].handler(regs);
			return;
		}
	}
}

/* fork callback: ensure the child of a managed UID also gets traced */
static void probe_sched_fork(void *data, struct task_struct *parent,
			     struct task_struct *child)
{
	if (!nksu_profile_has_uid(__kuid_val(task_uid(child))))
		return;
	mark_threads_by_uid(__kuid_val(task_uid(child)));
}

struct tp_find_ctx {
	const char *name;
	struct tracepoint **out;
};

static void tp_find_cb(struct tracepoint *tp, void *priv)
{
	struct tp_find_ctx *ctx = priv;

	if (*ctx->out)
		return;
	if (strcmp(tp->name, ctx->name) == 0)
		*ctx->out = tp;
}

/* walk the kernel tracepoint table to find one by name */
static struct tracepoint *find_tracepoint(const char *name)
{
	struct tracepoint *result = NULL;
	struct tp_find_ctx ctx = { .name = name, .out = &result };

	for_each_kernel_tracepoint(tp_find_cb, &ctx);
	return result;
}

int load_tracepoint_hook(void)
{
	int ret;

	tp_sys_enter = find_tracepoint("sys_enter");
	if (!tp_sys_enter) {
		pr_err("cannot find sys_enter tracepoint\n");
		return -ENOENT;
	}

	tp_sched_fork = find_tracepoint("sched_process_fork");
	if (!tp_sched_fork) {
		pr_err("cannot find sched_process_fork tracepoint\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_sys_enter, probe_sys_enter, NULL);
	if (ret) {
		pr_err("register sys_enter probe failed: %d\n", ret);
		return ret;
	}

	ret = tracepoint_probe_register(tp_sched_fork, probe_sched_fork, NULL);
	if (ret) {
		pr_err("register sched_process_fork probe failed: %d\n", ret);
		tracepoint_probe_unregister(tp_sys_enter, probe_sys_enter, NULL);
		return ret;
	}

	pr_info("tracepoint hooks loaded\n");
	return 0;
}

void unload_tracepoint_hook(void)
{
	if (tp_sys_enter)
		tracepoint_probe_unregister(tp_sys_enter, probe_sys_enter, NULL);
	if (tp_sched_fork)
		tracepoint_probe_unregister(tp_sched_fork, probe_sched_fork, NULL);

	tracepoint_synchronize_unregister();

	pr_info("tracepoint hooks unloaded\n");
}
