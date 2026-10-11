#include <linux/kallsyms.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <asm/syscall.h>

#include <fmac.h>
#include "hook/patch.h"
#include "hook/syscall.h"
#include "symbol/symbol.h"
/* Last: redirects unexported symbols through resolved pointers. */
#include "symbol/symbol_compat.h"

syscall_fn_t *syscall_table;

#define MAX_HOOKS 256

struct hook_entry {
    unsigned long addr;
    syscall_fn_t original;
};

static struct hook_entry hook_table[MAX_HOOKS];
static int hook_count = 0;
static DEFINE_SPINLOCK(hook_lock);

/*
 * Replacing a slot goes through hook/patch.c, the single owner of the
 * text-patch primitive (and of init_mm).  Do not add a second patcher here.
 */
static int patch_syscall_slot(void *addr, syscall_fn_t newval)
{
    return nksu_patch_text(addr, &newval, sizeof(newval));
}

static int syscalltable_hook(unsigned long addr, syscall_fn_t hook_fn)
{
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&hook_lock, flags);
    if (hook_count >= MAX_HOOKS) {
        spin_unlock_irqrestore(&hook_lock, flags);
        return -ENOMEM;
    }
    hook_table[hook_count].addr = addr;
    hook_table[hook_count].original = *(syscall_fn_t *)addr;
    hook_count++;
    spin_unlock_irqrestore(&hook_lock, flags);

    ret = patch_syscall_slot((void *)addr, hook_fn);
    if (ret) {
        spin_lock_irqsave(&hook_lock, flags);
        hook_count--;
        spin_unlock_irqrestore(&hook_lock, flags);
        pr_err("nksu: patch failed: %d\n", ret);
    }
    return ret;
}

static int syscalltable_unhook(unsigned long addr)
{
    unsigned long flags;
    syscall_fn_t orig;
    int i, ret;

    spin_lock_irqsave(&hook_lock, flags);
    for (i = 0; i < hook_count; i++) {
        if (hook_table[i].addr == addr)
            break;
    }
    if (i == hook_count) {
        spin_unlock_irqrestore(&hook_lock, flags);
        pr_err("nksu: unhook addr not found\n");
        return -ENOENT;
    }
    orig = hook_table[i].original;
    hook_table[i] = hook_table[--hook_count];
    spin_unlock_irqrestore(&hook_lock, flags);

    ret = patch_syscall_slot((void *)addr, orig);
    if (ret)
        pr_err("nksu: unhook patch failed: %d\n", ret);
    return ret;
}

static syscall_fn_t syscalltable_get_original(unsigned long addr)
{
    unsigned long flags;
    syscall_fn_t orig = NULL;
    int i;

    spin_lock_irqsave(&hook_lock, flags);
    for (i = 0; i < hook_count; i++) {
        if (hook_table[i].addr == addr) {
            orig = hook_table[i].original;
            break;
        }
    }
    spin_unlock_irqrestore(&hook_lock, flags);
    return orig;
}

int hook_save(int nr, syscall_fn_t fn, syscall_fn_t *orig, const char *name)
{
    unsigned long addr = (unsigned long)&syscall_table[nr];
    int ret = syscalltable_hook(addr, fn);
    if (ret) {
        pr_err("nksu: failed to hook %s: %d\n", name, ret);
        return ret;
    }
    *orig = syscalltable_get_original(addr);
    pr_info("nksu: hooked %s\n", name);
    return 0;
}

int hook_nosave(int nr, syscall_fn_t fn, const char* name){
    unsigned long addr = (unsigned long)&syscall_table[nr];
    int ret = syscalltable_hook(addr, fn);
    if (ret) {
        pr_err("nksu: failed to hook %s: %d\n", name, ret);
        return ret;
    }
    pr_info("nksu: hooked %s\n", name);
    return 0;
}

int syscalltable_init(void)
{
    /*
     * init_mm belongs to the patcher (hook/patch.c); failing here keeps
     * nksu_dispatch_init() erroring out early when the kernel cannot be
     * patched at all.
     */
    int ret = nksu_patch_init();
    if (ret) {
        pr_err("nksu: failed to find init_mm\n");
        return ret;
    }

    syscall_table = (syscall_fn_t *)nksu_ksym_lookup("sys_call_table");
    if (!syscall_table) {
        pr_err("nksu: failed to find sys_call_table\n");
        return -ENOENT;
    }
    pr_info("nksu: syscall table at %px\n", syscall_table);
    return 0;
}

void syscalltable_exit(void)
{
    while (hook_count > 0)
        syscalltable_unhook(hook_table[hook_count - 1].addr);
}
