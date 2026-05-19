#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>
#include <linux/compiler.h>

#include "hook.h"
#include "manager.h"
#include "anonfd.h"
#include "privilege.h"
#include "ioctl.h"

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
		elevate_to_root();
		return 1;

	case 203:
		fmac_ctlfd_get();
		return 1;

	default:
		return 0;
	}
}

#ifdef CONFIG_NKSU_SYSCALL

static long hook_path_at(struct pt_regs *regs)
{
	if (!nksu_profile_has_uid(current_uid().val))
		return 0;
	unsigned long new_uaddr = try_redirect_path(regs, 1);
	if (new_uaddr > 0) {
		regs->regs[1] = new_uaddr;
	}
	return 0;
}

static long hook__NR_execve(struct pt_regs *regs)
{
	if (!nksu_profile_has_uid(current_uid().val))
		return 0;

	unsigned long new_uaddr = try_redirect_path(regs, 0);
	if (new_uaddr > 0) {
		regs->regs[0] = new_uaddr;
		elevate_to_root();
	}
	return 0;
}

static long hook__NR_execveat(struct pt_regs *regs)
{
	if (!nksu_profile_has_uid(current_uid().val))
		return 0;

	unsigned long new_uaddr = try_redirect_path(regs, 1);
	if (new_uaddr > 0) {
		regs->regs[1] = new_uaddr;
		elevate_to_root();
	}
	return 0;
}

int init_syscall_hook(void)
{
	int ret;
	ret = nksu_redirect_syscall(__NR_faccessat);
	if (ret) {
		pr_err("[hook]: can't redirect faccessat ret %d\n", ret);
		return ret;
	}

	ret = nksu_redirect_syscall(__NR_newfstatat);
	if (ret) {
		pr_err("[hook]: can't redirect newfstatat ret %d\n", ret);
		return ret;
	}

	ret = nksu_redirect_syscall(__NR_prctl);
	if (ret) {
		pr_err("[hook]: can't redirect prctl ret %d\n", ret);
		return ret;
	}

	ret = nksu_redirect_syscall(__NR_execve);
	if (ret) {
		pr_err("[hook]: can't redirect __NR_execve ret %d\n", ret);
		return ret;
	}

	ret = nksu_redirect_syscall(__NR_execveat);
	if (ret) {
		pr_err("[hook]: can't redirect __NR_execveat ret %d\n", ret);
		return ret;
	}

	ret = nksu_register_handler(__NR_faccessat, hook_path_at);
	if (ret) {
		pr_err("[hook]: can't register faccessat,ret %d\n", ret);
		return ret;
	}

	ret = nksu_register_handler(__NR_newfstatat, hook_path_at);
	if (ret) {
		pr_err("[hook]: can't register newfstatat,ret %d\n", ret);
		return ret;
	}

	ret = nksu_register_handler(__NR_prctl, handle_prctl_hooks);
	if (ret) {
		pr_err("[hook]: can't register prctl,ret %d\n", ret);
		return ret;
	}

	ret = nksu_register_handler(__NR_execve, hook__NR_execve);
	if (ret) {
		pr_err("[hook]: can't register __NR_execve,ret %d\n", ret);
		return ret;
	}

	ret = nksu_register_handler(__NR_execveat, hook__NR_execveat);
	if (ret) {
		pr_err("[hook]: can't register __NR_execveat,ret %d\n", ret);
		return ret;
	}

	pr_info("[hook]: loaded syscall hook\n");
	return 0;
}
#endif