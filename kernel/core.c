// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * FMAC - File Monitoring and Access Control Kernel Module
 * Copyright (C) 2025 Aqnya
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <fmac.h>
#include "symbol/symbol_compat.h"
#include "nksu.h"
#include "boot/init.h"
#include "privilege/profile_store.h"
#include "selinux/hide.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aqnya");
MODULE_DESCRIPTION("nekosu");
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);

bool late_load = false;

const char *nksu_version(void)
{
    return NKSU_GIT_COMMIT;
}

typedef struct {
    const char *name;
    int (*init)(void);
    void (*exit)(void);
} module_component_t;

/*
 * SELinux needs the policy, which is loaded during the "selinux_setup"
 * stage; everything else needs /data and the zygote.  Late load runs both
 * groups back to back, a first-stage (vendor_boot) load stages them.
 */
static const module_component_t selinux_components[] = {
    {
        .name = "SELinux Hook",
        .init = init_selinux_hook,
        .exit = selinux_exit,
    },
};

static const module_component_t feature_components[] = {
    {
        .name = "Anonymous FD",
        .init = fmac_anonfd_init,
        .exit = fmac_anonfd_exit,
    },
    {
        .name = "uid profile",
        .init = nksu_profile_init,
        .exit = nksu_profile_clear_all,
    },
    {
        .name = "profile store",
        .init = nksu_profile_store_init,
        .exit = nksu_profile_store_exit,
    },
    {
        .name = "SELinux hide",
        .init = nksu_selinux_hide_init,
        .exit = nksu_selinux_hide_exit,
    },
#ifndef CONFIG_NKSU_SYSCALL
    {
        .name = "tracepoint hook",
        .init = load_tracepoint_hook,
        .exit = unload_tracepoint_hook,
    },
#endif
    {
        .name = "manager scan",
        .init = appscan_init,
        .exit = appscan_exit,
    },
#ifdef CONFIG_NKSU_SYSCALL
    {
        .name = "syscall dispatch",
        .init = nksu_dispatch_init,
        .exit = nksu_dispatch_exit,
    },
    {
        .name = "syscall hook",
        .init = init_syscall_hook,
        .exit = NULL,
    },
#endif
};

static int nekosu_init_component(const module_component_t *comp, int index)
{
    int ret;

    if (!comp->init) {
        pr_debug("Skipping %s (no init function)\n", comp->name);
        return 0;
    }
#ifdef CONFIG_NKSU_DEBUG
    ktime_t t0 = ktime_get();
#endif

    pr_info("Initializing %s...\n", comp->name);
    ret = comp->init();

#ifdef CONFIG_NKSU_DEBUG
    {
        s64 us = ktime_to_us(ktime_sub(ktime_get(), t0));
        if (ret)
            pr_err("Failed to initialize %s: %d (took %lld us)\n", comp->name, ret, us);
        else
            pr_info("%s initialized in %lld us\n", comp->name, us);
    }
#else
    if (ret)
        pr_err("Failed to initialize %s: %d\n", comp->name, ret);
    else
        pr_debug("%s initialized successfully (index: %d)\n", comp->name, index);
#endif

    return ret;
}

static void nekosu_cleanup_components(const module_component_t *comps, int count)
{
    int i;
    for (i = count - 1; i >= 0; i--) {
        if (comps[i].exit) {
            pr_debug("Cleaning up %s...\n", comps[i].name);
            comps[i].exit();
        }
    }
}

static int init_component_list(const module_component_t *comps, int count)
{
    int ret, i;

    for (i = 0; i < count; i++) {
        ret = nekosu_init_component(&comps[i], i);
        if (ret) {
            nekosu_cleanup_components(comps, i);
            return ret;
        }
    }
    return 0;
}

int nksu_init_selinux_components(void)
{
    return init_component_list(selinux_components,
                               ARRAY_SIZE(selinux_components));
}

void nksu_exit_selinux_components(void)
{
    nekosu_cleanup_components(selinux_components,
                              ARRAY_SIZE(selinux_components));
}

int nksu_init_feature_components(void)
{
    return init_component_list(feature_components,
                               ARRAY_SIZE(feature_components));
}

void nksu_exit_feature_components(void)
{
    nekosu_cleanup_components(feature_components,
                              ARRAY_SIZE(feature_components));
}

static int nekosu_init_all_components(void)
{
    int ret = nksu_init_selinux_components();

    if (ret)
        return ret;

    ret = nksu_init_feature_components();
    if (ret)
        nksu_exit_selinux_components();

    return ret;
}

static void nekosu_cleanup_all_components(void)
{
    nksu_exit_feature_components();
    nksu_exit_selinux_components();
}

static int __init nekosu_init(void)
{
    int ret;

    late_load = ((current->pid != 1) || strcmp(current->comm, "init"));

    pr_info("Loading nekosu module...\n");

#ifdef CONFIG_NKSU_DEBUG
    pr_alert("The current build is in debug mode, and security may be compromised.\n");
    pr_info("nekosu build commit: %s\n", NKSU_GIT_COMMIT);
#endif

    /*
     * Resolve the GKI/KMI-unexported kernel symbols into function/data
     * pointers first. Components then reach them through this indirection
     * layer, so the module no longer carries relocations for those symbols
     * and will not fail to load with "Unknown symbol".
     */
    ret = nksu_symbol_compat_init();
    if (ret) {
        pr_err("Failed to resolve unexported symbols: %d\n", ret);
        return ret;
    }

    if (!late_load) {
        ret = hook_init();
    } else {
        ret = nekosu_init_all_components();
    }

    if (ret) {
        pr_err("Failed to initialize nekosu: %d\n", ret);
        nksu_symbol_compat_exit();
        return ret;
    }

    pr_info("nekosu module loaded successfully\n");
    return 0;
}

static void __exit nekosu_exit(void)
{
    pr_info("Unloading nekosu module...\n");
    if (!late_load)
        hook_exit();
    else
        nekosu_cleanup_all_components();

    nksu_symbol_compat_exit();
    nksu_ksym_cache_clear();
    pr_info("nekosu module unloaded\n");
}

module_init(nekosu_init);
module_exit(nekosu_exit);
