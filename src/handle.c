#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>
#include <linux/compiler.h>

#include "hook.h"
#include "manager.h"
#include "fd/fd.h"
#include "privilege.h"
#include "ioctl.h"
#include <fmac.h>

#ifndef CONFIG_NKSU_SYSCALL
#include <linux/pid.h>
#include <linux/sched/signal.h>
#include <linux/trace_events.h>
#include <linux/tracepoint.h>
#endif

long handle_prctl_hooks(struct pt_regs *regs)
{
#if defined(__aarch64__)
    unsigned long option = regs->regs[0];
#elif defined(__x86_64__)
    unsigned long option = regs->di;
#endif

    if (likely(!is_manager())) {
        return 0;
    }

    switch (option) {
    case 201:
        fmac_anonfd_get();
        return 1;

    case 202:
        privilege_escalate_from_profile();
        return 1;

    case 203:
        fmac_ctlfd_get();
        return 1;

    default:
        return 0;
    }
}

static unsigned long push_str(unsigned long sp, const char *str, size_t len)
{
    unsigned long addr;

    if (unlikely(!len || len > PAGE_SIZE || sp < 128 + len))
        return 0;

    addr = (sp - 128UL - len) & ~15UL;

    if (unlikely(!access_ok((void __user *)addr, len)))
        return 0;

    if (unlikely(copy_to_user((void __user *)addr, str, len)))
        return 0;

    return addr;
}

static unsigned long try_redirect_path(struct pt_regs *regs, unsigned int arg_index, const char *target,
                                       size_t target_len)
{
    char buf[128];
    const char __user *upath;
    unsigned long sp;
    ssize_t ulen;

    if (unlikely(!current->mm))
        return 0;

#if defined(__aarch64__)
    if (unlikely(arg_index > 5))
        return 0;
#else
    if (unlikely(arg_index > 7))
        return 0;
#endif

    if (unlikely(!target || !target_len || target_len > PAGE_SIZE))
        return 0;

    upath = (const char __user *)regs->regs[arg_index];
    if (unlikely(!upath))
        return 0;

    ulen = strncpy_from_user(buf, upath, sizeof(buf));
    if (unlikely(ulen <= 0))
        return 0;

    /* only an exact match matters — bail early if the length differs */
    if (unlikely(ulen != SU_PATH_LEN) || !path_is_su(buf))
        return 0;

    sp = user_stack_pointer(regs);
    if (unlikely(!sp))
        return 0;

    return push_str(sp, target, target_len);
}

long hook_path_at(struct pt_regs *regs)
{
    unsigned long new_uaddr = try_redirect_path(regs, 1, SH_PATH, SH_PATH_LEN);
    if (new_uaddr > 0) {
        regs->regs[1] = new_uaddr;
    }
    return 0;
}

long hook__NR_execve(struct pt_regs *regs)
{
    unsigned long new_uaddr = try_redirect_path(regs, 0, REDIRECT_TARGET, REDIRECT_TARGET_LEN);
    if (new_uaddr > 0) {
        regs->regs[0] = new_uaddr;
        privilege_escalate_from_profile();
    }
    return 0;
}

long hook__NR_execveat(struct pt_regs *regs)
{
    unsigned long new_uaddr = try_redirect_path(regs, 1, REDIRECT_TARGET, REDIRECT_TARGET_LEN);
    if (new_uaddr > 0) {
        regs->regs[1] = new_uaddr;
        privilege_escalate_from_profile();
    }
    return 0;
}

/*
 * syscall → handler mapping used by both the direct-syscall-hook
 * path (iterated in init_syscall_hook) and the tracepoint path
 * (used by probe_sys_enter as a dispatch table).
 */
struct syscall_hook {
	int nr;
	long (*handler)(struct pt_regs *);
};

static const struct syscall_hook syscall_hooks[] = {
	{ __NR_prctl,      handle_prctl_hooks },
	{ __NR_execve,     hook__NR_execve    },
	{ __NR_execveat,   hook__NR_execveat  },
	{ __NR_faccessat,  hook_path_at       },
	{ __NR_newfstatat, hook_path_at       },
};

#ifdef CONFIG_NKSU_SYSCALL

int init_syscall_hook(void)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(syscall_hooks); i++) {
		ret = nksu_redirect_syscall(syscall_hooks[i].nr);
		if (ret) {
			pr_err("[hook]: can't redirect syscall %d: %d\n",
			       syscall_hooks[i].nr, ret);
			return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(syscall_hooks); i++) {
		ret = nksu_register_handler(syscall_hooks[i].nr,
					    syscall_hooks[i].handler);
		if (ret) {
			pr_err("[hook]: can't register handler for syscall %d: %d\n",
			       syscall_hooks[i].nr, ret);
			return ret;
		}
	}

	pr_info("[hook]: loaded syscall hook\n");
	return 0;
}
#endif /* CONFIG_NKSU_SYSCALL */

#ifndef CONFIG_NKSU_SYSCALL

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

/* sys_enter callback: dispatch to the matching handler via a flat switch.
 * The compiler turns this into a balanced decision tree (or jump table),
 * with no loop overhead and no array loads — important because this
 * probe fires on every single syscall from managed UIDs. */
static void probe_sys_enter(void *data, struct pt_regs *regs, long id)
{
	if (!nksu_profile_has_uid(__kuid_val(task_uid(current))))
		return;

	switch (id) {
	case __NR_faccessat:
	case __NR_newfstatat:
		hook_path_at(regs);
		return;
	case __NR_prctl:
		handle_prctl_hooks(regs);
		return;
	case __NR_execve:
		hook__NR_execve(regs);
		return;
	case __NR_execveat:
		hook__NR_execveat(regs);
		return;
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

#endif /* !CONFIG_NKSU_SYSCALL */
