#pragma once

#include <linux/lsm_hooks.h>
#include <linux/stddef.h>
#include <linux/version.h>

/*
 * Runtime patching of an existing LSM hook slot.
 *
 * Ported from KernelSU's hook/lsm_hook.c.  The SELinux hiding feature needs to
 * intercept security_setprocattr()'s SELinux implementation, which the kernel
 * reaches through the LSM hook table rather than the syscall table, so the
 * syscall dispatcher cannot see it.  There is no exported API to register an
 * out-of-tree LSM, so we find the security_hook_list entry whose handler is
 * `target_name` and overwrite just that slot, keeping the original aside.
 *
 * 6.12 turned the hook table into static calls; both the security_hook_list
 * slot and the static call trampoline have to be updated there (see the #if).
 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define NKSU_LSM_HOOK_HEADS_TYPE struct lsm_static_calls_table
#else
#define NKSU_LSM_HOOK_HEADS_TYPE struct security_hook_heads
#endif

struct nksu_lsm_hook {
	const char *head_name;
	const char *target_name;
	size_t head_offset;
	size_t hook_offset;
	void *replacement;
	void *original;
	struct security_hook_list *entry;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	struct lsm_static_call *scall;
#else
	struct security_hook_list list;
#endif
	/* Extra head entries to skip when no LSM in `head_name` registers the
	 * hook.  Unused for setprocattr (offset 0). */
	int offset;
};

#define NKSU_LSM_HOOK_INIT(member, target_symbol, replacement_fn, off)      \
	{                                                                   \
		.head_name = #member,                                       \
		.target_name = target_symbol,                               \
		.head_offset = offsetof(NKSU_LSM_HOOK_HEADS_TYPE, member),  \
		.hook_offset = offsetof(struct security_hook_list,          \
					hook.member),                       \
		.replacement = (void *)(replacement_fn),                    \
		.offset = off,                                              \
	}

/*
 * Replace the LSM hook handler that currently points to @target_name with
 * @replacement, saving the original into hook->original.  The slot is found by
 * scanning the hook tables and matching the function pointer (layout
 * independent), not by a compile-time field offset.
 * Returns 0, -ENOENT if the target is not registered, or a negative errno on
 * failure.  Call nksu_lsm_unhook() to restore it.
 */
int nksu_lsm_hook(struct nksu_lsm_hook *hook);

/* Restore the slot patched by nksu_lsm_hook() and clear hook->entry. */
void nksu_lsm_unhook(struct nksu_lsm_hook *hook);
