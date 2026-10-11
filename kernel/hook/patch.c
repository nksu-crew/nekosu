// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Write kernel memory through a fixmap alias with the other CPUs stopped.
 *
 * Why this is not hook/syscall.c's patcher:
 *  - That one belongs to the syscall-table hook path.  Its init_mm_ptr and
 *    sys_call_table come from syscalltable_init(), which is owned by
 *    nksu_dispatch_init() (dispatch).  In the default tracepoint build that
 *    only runs on a first-stage load, so on a late load the pointer is NULL;
 *    and at the zygote stage nksu_dispatch_exit() -> syscalltable_exit()
 *    unhooks every entry recorded in hook_table, so anything patched through
 *    it would be reverted right before the feature components run.
 *  - A feature must therefore own its own patch path and its own init_mm, and
 *    must not depend on the dispatch/syscalltable lifetime.  Both copies share
 *    FIX_TEXT_POKE0, but stop_machine() serialises them.
 *
 * The names are prefixed so a thin-LTO build never sees two conflicting
 * `struct patch_info` definitions.
 */
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>
#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/printk.h>
#include <linux/spinlock.h> /* cpu_relax() */
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>

#include "hook/patch.h"
#include "symbol/symbol.h"
/* Last: redirects __set_fixmap / copy_to_kernel_nofault to resolved pointers. */
#include "symbol/symbol_compat.h"

static struct mm_struct *nksu_patch_init_mm;

static unsigned long nksu_patch_phys(unsigned long addr, int *err)
{
    pgd_t *pgd;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;

    *err = 0;

    pgd = pgd_offset(nksu_patch_init_mm, addr);
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

struct nksu_patch_info {
    void *dst;
    const void *newval;
    size_t size;
    atomic_t cpu_count;
    int result;
};

static __nocfi int nksu_do_patch(struct nksu_patch_info *p)
{
    unsigned long addr = (unsigned long)p->dst;
    unsigned long phy;
    void *map;
    int err;

    phy = nksu_patch_phys(addr, &err);
    if (err) {
        pr_err("nksu: patch: no mapping for 0x%lx\n", addr);
        return err;
    }

    map = (void *)set_fixmap_offset(FIX_TEXT_POKE0, phy);
    err = (int)copy_to_kernel_nofault(map, p->newval, p->size);
    clear_fixmap(FIX_TEXT_POKE0);

    if (!err) {
        dsb(ish);
        isb();
    }
    return err;
}

static int nksu_patch_cb(void *arg)
{
    struct nksu_patch_info *p = arg;

    if (atomic_inc_return(&p->cpu_count) == num_online_cpus()) {
        p->result = nksu_do_patch(p);
        atomic_inc(&p->cpu_count);
    } else {
        while (atomic_read(&p->cpu_count) <= num_online_cpus())
            cpu_relax();
        isb();
    }
    return 0;
}

int nksu_patch_text(void *slot, const void *newval, size_t size)
{
    struct nksu_patch_info p = {
        .dst = slot,
        .newval = newval,
        .size = size,
        .cpu_count = ATOMIC_INIT(0),
        .result = 0,
    };
    int ret;

    if (!slot || !newval || !size)
        return -EINVAL;

    if (!nksu_patch_init_mm)
        nksu_patch_init_mm = (struct mm_struct *)nksu_ksym_lookup("init_mm");
    if (!nksu_patch_init_mm) {
        pr_err("nksu: patch: init_mm unavailable\n");
        return -ENOENT;
    }

    ret = stop_machine(nksu_patch_cb, &p, cpu_online_mask);
    return ret ? ret : p.result;
}
