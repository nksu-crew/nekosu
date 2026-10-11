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
    syscall_fn_t previous;
};

static struct hook_entry hook_table[MAX_HOOKS];
static int hook_count = 0;
static DEFINE_SPINLOCK(hook_lock);

/*
 * Replacing a slot goes through hook/patch.c, the single owner of the
 * text-patch primitive (and of init_mm).  Do not add a second patcher here.
 */
static int patch_slot(void *addr, syscall_fn_t newval)
{
    return nksu_patch_text(addr, &newval, sizeof(newval));
}

static int record_slot(unsigned long addr, syscall_fn_t hook_fn)
{
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&hook_lock, flags);
    if (hook_count >= MAX_HOOKS) {
        spin_unlock_irqrestore(&hook_lock, flags);
        return -ENOMEM;
    }
    hook_table[hook_count].addr = addr;
    hook_table[hook_count].previous = *(syscall_fn_t *)addr;
    hook_count++;
    spin_unlock_irqrestore(&hook_lock, flags);

    ret = patch_slot((void *)addr, hook_fn);
    if (ret) {
        spin_lock_irqsave(&hook_lock, flags);
        hook_count--;
        spin_unlock_irqrestore(&hook_lock, flags);
        pr_err("nksu: patch failed: %d\n", ret);
    }
    return ret;
}

static int restore_slot(unsigned long addr)
{
    unsigned long flags;
    syscall_fn_t previous;
    int i, ret;

    spin_lock_irqsave(&hook_lock, flags);
    for (i = 0; i < hook_count; i++) {
        if (hook_table[i].addr == addr)
            break;
    }
    if (i == hook_count) {
        spin_unlock_irqrestore(&hook_lock, flags);
        pr_err("nksu: restore: slot not recorded\n");
        return -ENOENT;
    }
    previous = hook_table[i].previous;
    hook_table[i] = hook_table[--hook_count];
    spin_unlock_irqrestore(&hook_lock, flags);

    ret = patch_slot((void *)addr, previous);
    if (ret)
        pr_err("nksu: restore patch failed: %d\n", ret);
    return ret;
}

static syscall_fn_t recorded_previous(unsigned long addr)
{
    unsigned long flags;
    syscall_fn_t previous = NULL;
    int i;

    spin_lock_irqsave(&hook_lock, flags);
    for (i = 0; i < hook_count; i++) {
        if (hook_table[i].addr == addr) {
            previous = hook_table[i].previous;
            break;
        }
    }
    spin_unlock_irqrestore(&hook_lock, flags);
    return previous;
}

int syscall_slot_hook(int nr, syscall_fn_t fn, syscall_fn_t *previous,
                      const char *tag)
{
    unsigned long addr = (unsigned long)&syscall_table[nr];
    int ret = record_slot(addr, fn);

    if (ret) {
        pr_err("nksu: failed to hook %s: %d\n", tag, ret);
        return ret;
    }
    if (previous)
        *previous = recorded_previous(addr);
    pr_info("nksu: hooked %s\n", tag);
    return 0;
}

int syscall_table_resolve(void)
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

void syscall_slots_restore_all(void)
{
    while (hook_count > 0)
        restore_slot(hook_table[hook_count - 1].addr);
}
