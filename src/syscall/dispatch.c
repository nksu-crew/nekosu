// SPDX-License-Identifier: GPL-3.0
#include <linux/cred.h>
#include <linux/kallsyms.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/nospec.h>

#include "type.h"
#include <fmac.h>

syscall_fn_t nksu_orig_table[__NR_syscalls] ____cacheline_aligned;
nksu_handler_t virt_table[__NR_syscalls] ____cacheline_aligned;

static int nksu_syscall_nr = -1;

static int hook_and_save(int nr, syscall_fn_t new_fn, const char *tag)
{
	syscall_fn_t orig = NULL;
	int ret;

	if ((unsigned int)nr >= (unsigned int)__NR_syscalls)
		return -EINVAL;

	ret = hook_one(nr, new_fn, &orig, tag);
	if (ret)
		return ret;

	WRITE_ONCE(nksu_orig_table[nr], orig);

	pr_info("[syscall]: slot %d hooked: orig=%ps new=%ps\n", nr, orig,
		new_fn);
	return 0;
}

int nksu_register_handler(u32 nr, nksu_handler_t fn)
{
	if (nr >= __NR_syscalls || !fn)
		return -EINVAL;

	if (READ_ONCE(virt_table[nr]) != NULL)
		return -EEXIST;

	smp_store_release(&virt_table[nr], fn);
	return 0;
}

void nksu_unregister_handler(u32 nr)
{
	if (nr < __NR_syscalls)
		WRITE_ONCE(virt_table[nr], NULL);
}

asmlinkage long nksu_dispatch_fast(const struct pt_regs *regs)
{
	unsigned int nr = (unsigned int)regs->regs[8];
	syscall_fn_t orig;

	if (unlikely(nr >= (unsigned int)__NR_syscalls))
		return -ENOSYS;

	nr = array_index_nospec(nr, __NR_syscalls);
	orig = READ_ONCE(nksu_orig_table[nr]);

	if (likely(!nksu_profile_has_uid(current_uid().val)))
		return orig ? orig(regs) : -ENOSYS;

	{
		nksu_handler_t handler = READ_ONCE(virt_table[nr]);
		if (unlikely(handler)) {
			long ret = handler((struct pt_regs *)regs);
			if (ret)
				return ret;
		}
	}

	return orig ? orig(regs) : -ENOSYS;
}

int nksu_redirect_syscall(int real_nr)
{
	return hook_and_save(real_nr, nksu_dispatch_fast, "nksu_redirect");
}

int nksu_get_syscall_nr(void)
{
	return nksu_syscall_nr;
}

static unsigned long resolve_ni_syscall(void)
{
	static const char *const names[] = {
		"__arm64_sys_ni_syscall.cfi_jt",
		"__arm64_sys_ni_syscall",
		"sys_ni_syscall",
		"__sys_ni_syscall",
		NULL,
	};
	int i;

	for (i = 0; names[i]; i++) {
		unsigned long addr = kallsyms_lookup_name(names[i]);
		if (addr)
			return addr;
	}
	return 0;
}

static int find_random_ni_slot(void)
{
	unsigned long ni_addr = resolve_ni_syscall();
	int selected = -1, count = 0, i;

	if (!ni_addr)
		return -ENOENT;

	for (i = 0; i < __NR_syscalls; i++) {
		unsigned long slot = (unsigned long)READ_ONCE(syscall_table[i]);
		if (slot != ni_addr)
			continue;
		count++;
		if ((get_random_u32() % count) == 0)
			selected = i;
	}
	return selected;
}

int nksu_dispatch_init(void)
{
	int rc, ret;

	rc = syscalltable_init();
	if (rc < 0)
		return rc;

	memset(nksu_orig_table, 0, sizeof(nksu_orig_table));
	memset(virt_table, 0, sizeof(virt_table));

	nksu_syscall_nr = find_random_ni_slot();
	if (nksu_syscall_nr < 0) {
		syscalltable_exit();
		return -ENOENT;
	}

	ret =
	    hook_and_save(nksu_syscall_nr, nksu_dispatch_fast,
			  "nksu_dispatch_fast");
	if (ret) {
		nksu_syscall_nr = -1;
		syscalltable_exit();
	}
	return ret;
}

void nksu_dispatch_exit(void)
{
	if (nksu_syscall_nr < 0)
		return;

	syscalltable_exit();

	memset(nksu_orig_table, 0, sizeof(nksu_orig_table));
	memset(virt_table, 0, sizeof(virt_table));

	nksu_syscall_nr = -1;
}
