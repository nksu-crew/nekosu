// SPDX-License-Identifier: GPL-2.0
/*
 * Runtime patching of an existing LSM hook slot, ported from KernelSU's
 * hook/lsm_hook.c (see hook/lsm_hook.h for why).
 *
 * The slot is located by walking the hook tables and matching the function
 * pointer, NOT by a compile-time field offset: security_hook_heads may be
 * __randomize_layout (CONFIG_RANDSTRUCT), so its member offsets in the module's
 * build headers can differ from the running kernel's.  The table is bounded
 * with kallsyms_lookup_size_offset().
 *
 * The whole file calls resolved-but-unexported functions through pointers and
 * patches RO data through a fixmap, so it drops the CFI check at its own call
 * sites the same way the rest of the selinux subsystem does.
 */
#include <asm/barrier.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lsm_hooks.h>
#include <linux/mutex.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/string.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#include <linux/jump_label.h>
#include <linux/static_call.h>
#endif

#include "hook/lsm_hook.h"
#include "hook/patch.h"
#include "symbol/symbol.h"

#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), \
			     apply_to = function)
#endif

static DEFINE_MUTEX(nksu_lsm_hook_lock);

static int nksu_lsm_patch_slot(void **slot, void *value)
{
	void *patched = value;
	int ret;

	ret = nksu_patch_text(slot, &patched, sizeof(patched));
	if (!ret)
		smp_wmb();
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
static int nksu_lsm_update_scall(struct lsm_static_call *scall, void *value)
{
	__static_call_update(scall->key, scall->trampoline, value);
	smp_wmb();
	return 0;
}
#endif

int nksu_lsm_hook(struct nksu_lsm_hook *hook)
{
	int ret = 0;
	struct security_hook_list *entry = NULL;
	void *target;
	const char *target_name;
	int (*lookup_size)(unsigned long, unsigned long *, unsigned long *) =
		NULL;

	if (!hook || !hook->replacement)
		return -EINVAL;

	mutex_lock(&nksu_lsm_hook_lock);

	if (hook->entry) {
		ret = -EALREADY;
		goto out_unlock;
	}

	target_name = hook->target_name;
	if (!target_name) {
		ret = -EINVAL;
		goto out_unlock;
	}

	target = hook->original;
	if (!target)
		target = (void *)nksu_ksym_lookup(target_name);
	if (!target) {
		pr_err("[lsm_hook] cannot resolve %s\n", target_name);
		ret = -ENOENT;
		goto out_unlock;
	}

	lookup_size = (void *)nksu_ksym_lookup("kallsyms_lookup_size_offset");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	{
		unsigned long scalls_addr, active_addr, sym_size;
		u32 lsm_active_cnt = 5;
		size_t scalls_count, i;
		struct lsm_static_call *scalls;
		struct security_hook_list *selected_entry = NULL;
		struct lsm_static_call *selected_scall = NULL;
		void **selected_slot = NULL;
		void *selected_origin = NULL;

		scalls_addr = nksu_ksym_lookup("static_calls_table");
		if (!scalls_addr) {
			pr_err("[lsm_hook] no static_calls_table\n");
			ret = -ENOSYS;
			goto out_unlock;
		}

		sym_size = sizeof(struct lsm_static_calls_table);
		if (lookup_size)
			lookup_size(scalls_addr, &sym_size, NULL);
		else
			pr_warn("[lsm_hook] no kallsyms_lookup_size_offset\n");

		active_addr = nksu_ksym_lookup("lsm_active_cnt");
		if (active_addr)
			lsm_active_cnt = *(u32 *)active_addr;
		if (lsm_active_cnt == 0 || lsm_active_cnt > 20) {
			pr_err("[lsm_hook] bad lsm_active_cnt %u\n",
			       lsm_active_cnt);
			ret = -EINVAL;
			goto out_unlock;
		}
		if (sym_size %
		    (lsm_active_cnt * sizeof(struct lsm_static_call)))
			pr_warn("[lsm_hook] odd static_calls_table size\n");
		scalls_count = sym_size / sizeof(struct lsm_static_call);
		if (!scalls_count) {
			ret = -ENOSYS;
			goto out_unlock;
		}

		scalls = (struct lsm_static_call *)scalls_addr;
		for (i = 0; i < scalls_count; i++) {
			struct lsm_static_call *scall = &scalls[i];
			void **slot;
			void *current;

			entry = READ_ONCE(scall->hl);
			if (!entry)
				continue;
			slot = (void **)((char *)entry + hook->hook_offset);
			current = READ_ONCE(*slot);
			if (current == hook->replacement) {
				ret = -EALREADY;
				goto out_unlock;
			}
			if (current != target)
				continue;
			selected_entry = entry;
			selected_scall = scall;
			selected_slot = slot;
			selected_origin = current;
			break;
		}

		if (!selected_scall) {
			pr_err("[lsm_hook] %s not found in %s\n", target_name,
			       hook->head_name);
			ret = -ENOENT;
			goto out_unlock;
		}

		hook->original = selected_origin;

		ret = nksu_lsm_patch_slot(selected_slot, hook->replacement);
		if (ret) {
			pr_err("[lsm_hook] patch %s failed: %d\n", target_name,
			       ret);
			goto out_unlock;
		}
		if (nksu_lsm_update_scall(selected_scall, hook->replacement)) {
			nksu_lsm_patch_slot(selected_slot, selected_origin);
			ret = -EFAULT;
			goto out_unlock;
		}
		if (!selected_origin)
			static_branch_enable(selected_scall->active);

		hook->entry = selected_entry;
		hook->scall = selected_scall;
		pr_info("[lsm_hook] hooked %s\n", target_name);
	}
#else
	{
		unsigned long heads_addr, heads_size;
		struct hlist_head *head, *head_end;
		struct security_hook_list *selected_entry = NULL;
		void **selected_slot = NULL;
		void *selected_origin = NULL;

		heads_addr = nksu_ksym_lookup("security_hook_heads");
		if (!heads_addr) {
			pr_err("[lsm_hook] no security_hook_heads\n");
			ret = -ENOENT;
			goto out_unlock;
		}

		heads_size = sizeof(struct security_hook_heads);
		if (lookup_size)
			lookup_size(heads_addr, &heads_size, NULL);

		head = (struct hlist_head *)heads_addr;
		head_end = (struct hlist_head *)(heads_addr + heads_size);
		for (; head < head_end; head++) {
			hlist_for_each_entry(entry, head, list) {
				void **slot = (void **)((char *)entry +
							hook->hook_offset);
				void *current = READ_ONCE(*slot);

				if (current == hook->replacement) {
					ret = -EALREADY;
					goto out_unlock;
				}
				if (current == target) {
					selected_entry = entry;
					selected_slot = slot;
					selected_origin = current;
					break;
				}
			}
			if (selected_entry)
				break;
		}

		if (!selected_entry) {
			pr_err("[lsm_hook] %s not found in %s\n", target_name,
			       hook->head_name);
			ret = -ENOENT;
			goto out_unlock;
		}

		hook->original = selected_origin;

		ret = nksu_lsm_patch_slot(selected_slot, hook->replacement);
		if (ret) {
			pr_err("[lsm_hook] patch %s failed: %d\n", target_name,
			       ret);
			goto out_unlock;
		}

		hook->entry = selected_entry;
		pr_info("[lsm_hook] hooked %s\n", target_name);
	}
#endif

out_unlock:
	mutex_unlock(&nksu_lsm_hook_lock);
	return ret;
}

void nksu_lsm_unhook(struct nksu_lsm_hook *hook)
{
	void **slot;

	if (!hook)
		return;

	mutex_lock(&nksu_lsm_hook_lock);

	if (!hook->entry) {
		mutex_unlock(&nksu_lsm_hook_lock);
		return;
	}

	slot = (void **)((char *)hook->entry + hook->hook_offset);
	if (nksu_lsm_patch_slot(slot, hook->original)) {
		pr_err("[lsm_hook] restore %s failed\n", hook->target_name);
		mutex_unlock(&nksu_lsm_hook_lock);
		return;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	if (hook->scall && nksu_lsm_update_scall(hook->scall, hook->original)) {
		nksu_lsm_patch_slot(slot, hook->replacement);
		mutex_unlock(&nksu_lsm_hook_lock);
		return;
	}
	hook->scall = NULL;
#endif

	synchronize_rcu();
	hook->entry = NULL;
	pr_info("[lsm_hook] restored %s\n", hook->target_name);
	mutex_unlock(&nksu_lsm_hook_lock);
}

#if defined(__clang__)
#pragma clang attribute pop
#endif
