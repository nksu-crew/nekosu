#include <linux/kallsyms.h>
#include <asm/syscall.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>

#include <fmac.h>

static struct mm_struct *init_mm_ptr;
syscall_fn_t *syscall_table;

#define MAX_HOOKS 256

struct hook_entry {
    unsigned long addr;
    syscall_fn_t original;
};

static struct hook_entry hook_table[MAX_HOOKS];
static int hook_count = 0;
static DEFINE_SPINLOCK(hook_lock);

static unsigned long phys_from_virt(unsigned long addr, int *err)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    *err = 0;

    pgd = pgd_offset(init_mm_ptr, addr);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        goto fail;

    p4d = p4d_offset(pgd, addr);
    if (p4d_none(*p4d) || p4d_bad(*p4d))
        goto fail;

    pud = pud_offset(p4d, addr);
    if (pud_none(*pud) || pud_bad(*pud))
        goto fail;
#if defined(pud_leaf)
    if (pud_leaf(*pud))
        return __pud_to_phys(*pud) + (addr & ~PUD_MASK);
#endif

    pmd = pmd_offset(pud, addr);
#if defined(pmd_leaf)
    if (pmd_leaf(*pmd))
        return __pmd_to_phys(*pmd) + (addr & ~PMD_MASK);
#endif
    if (pmd_none(*pmd) || pmd_bad(*pmd))
        goto fail;

    pte = pte_offset_kernel(pmd, addr);
    if (!pte || !pte_present(*pte))
        goto fail;

    return __pte_to_phys(*pte) + (addr & ~PAGE_MASK);

fail:
    *err = -ENOENT;
    return 0;
}

struct patch_info {
    void *dst;
    syscall_fn_t newval;
    atomic_t cpu_count;
    int result;
};

static int do_patch_nosync(struct patch_info *p)
{
    unsigned long addr = (unsigned long)p->dst;
    unsigned long phy;
    void *map;
    int err;

    phy = phys_from_virt(addr, &err);
    if (err) {
        pr_err("nksu: phys_from_virt failed for 0x%lx\n", addr);
        return err;
    }

    map = (void *)set_fixmap_offset(FIX_TEXT_POKE0, phy);
    err = (int)copy_to_kernel_nofault(map, &p->newval, sizeof(syscall_fn_t));
    clear_fixmap(FIX_TEXT_POKE0);

    if (!err) {
        dsb(ish);
        isb();
    }
    return err;
}

static int patch_text_cb(void *arg)
{
    struct patch_info *p = arg;

    if (atomic_inc_return(&p->cpu_count) == num_online_cpus()) {
        p->result = do_patch_nosync(p);
        atomic_inc(&p->cpu_count);
    } else {
        while (atomic_read(&p->cpu_count) <= num_online_cpus())
            cpu_relax();
        isb();
    }
    return 0;
}

static int patch_syscall_slot(void *addr, syscall_fn_t newval)
{
    struct patch_info p = {
        .dst = addr,
        .newval = newval,
        .cpu_count = ATOMIC_INIT(0),
        .result = 0,
    };
    int ret = stop_machine(patch_text_cb, &p, cpu_online_mask);
    return ret ? ret : p.result;
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

int hook_one(int nr, syscall_fn_t fn, syscall_fn_t *orig, const char *name)
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

int syscalltable_init(void)
{
    init_mm_ptr = (struct mm_struct *)kallsyms_lookup_name("init_mm");
    if (!init_mm_ptr) {
        pr_err("nksu: failed to find init_mm\n");
        return -ENOENT;
    }
    syscall_table = (syscall_fn_t *)kallsyms_lookup_name("sys_call_table");
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