// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <fmac.h>

#include "ss/avtab.h"
#include "security.h"

#define ALL NULL

struct sepolicy_rule {
    const char *src;
    const char *tgt;
    const char *cls;
    const char *perm;
    int effect;
    bool invert;
};

#define RULE(_src, _tgt, _cls, _perm, _effect, _invert)                                                                \
    { .src = (_src), .tgt = (_tgt), .cls = (_cls), .perm = (_perm), .effect = (_effect), .invert = (_invert) }

#define ALLOW(_src, _tgt, _cls, _perm) RULE(_src, _tgt, _cls, _perm, AVTAB_ALLOWED, false)

#define DENY(_src, _tgt, _cls, _perm) RULE(_src, _tgt, _cls, _perm, AVTAB_AUDITDENY, true)

struct sepolicy_group {
    const char *name;
    const struct sepolicy_rule *rules;
    size_t count;
    bool required;
};

static const struct sepolicy_rule ksu_rules[] = {
    ALLOW("init", DOMAIN, NULL, NULL),
    ALLOW("servicemanager", DOMAIN, "dir", "search"),
    ALLOW("servicemanager", DOMAIN, "dir", "read"),
    ALLOW("servicemanager", DOMAIN, "file", "open"),
    ALLOW("servicemanager", DOMAIN, "file", "read"),
    ALLOW("servicemanager", DOMAIN, "process", "getattr"),
    ALLOW("domain", DOMAIN, "process", "sigchld"),
    ALLOW("logd", DOMAIN, "dir", "search"),
    ALLOW("logd", DOMAIN, "file", "read"),
    ALLOW("logd", DOMAIN, "file", "open"),
    ALLOW("logd", DOMAIN, "file", "getattr"),
    ALLOW("domain", DOMAIN, "fd", "use"),
    ALLOW("domain", DOMAIN, "fifo_file", "write"),
    ALLOW("domain", DOMAIN, "fifo_file", "read"),
    ALLOW("domain", DOMAIN, "fifo_file", "open"),
    ALLOW("domain", DOMAIN, "fifo_file", "getattr"),
    ALLOW("hwservicemanager", DOMAIN, "dir", "search"),
    ALLOW("hwservicemanager", DOMAIN, "file", "read"),
    ALLOW("hwservicemanager", DOMAIN, "file", "open"),
    ALLOW("hwservicemanager", DOMAIN, "process", "getattr"),
    ALLOW("domain", DOMAIN, "binder", ALL),
    ALLOW("system_server", DOMAIN, "process", "getpgid"),
    ALLOW("system_server", DOMAIN, "process", "sigkill"),
    ALLOW("system_server", DOMAIN, "process", "setsched"),
    ALLOW(DOMAIN, DOMAIN, "process", "fork"),
    ALLOW(DOMAIN, DOMAIN, "process", "setpgid"),
    ALLOW(DOMAIN, "package_service", "service_manager", "find"),
    ALLOW(DOMAIN, "servicemanager_prop", "file", "read"),
    ALLOW(DOMAIN, "servicemanager_prop", "file", "open"),
    ALLOW(DOMAIN, "system_file", "file", "getattr"),
    ALLOW(DOMAIN, "system_file", "file", "execute"),
    ALLOW(DOMAIN, "toolbox_exec", "file", "getattr"),
    ALLOW(DOMAIN, "toolbox_exec", "file", "execute"),
    ALLOW(DOMAIN, "toolbox_exec", "file", "read"),
    ALLOW(DOMAIN, "toolbox_exec", "file", "open"),
    ALLOW(DOMAIN, "untrusted_app_all_devpts", "chr_file", "read"),
    ALLOW(DOMAIN, "untrusted_app_all_devpts", "chr_file", "write"),
    ALLOW(DOMAIN, "untrusted_app_all_devpts", "chr_file", "getattr"),
    ALLOW(DOMAIN, "untrusted_app_all_devpts", "chr_file", "ioctl"),
    ALLOW(DOMAIN, "devpts", "chr_file", "read"),
    ALLOW(DOMAIN, "devpts", "chr_file", "write"),
    ALLOW(DOMAIN, "devpts", "chr_file", "getattr"),
    ALLOW(DOMAIN, "devpts", "chr_file", "ioctl"),

};

#define GROUP(_name, _rules, _required)                                                                                \
    { .name = (_name), .rules = (_rules), .count = ARRAY_SIZE(_rules), .required = (_required) }

static const struct sepolicy_group policy_groups[] = {
    GROUP("ksu_rules", ksu_rules, true),
};

static int apply_group(const struct sepolicy_group *grp)
{
    size_t i;
    int ret;
    int failed = 0;

    for (i = 0; i < grp->count; i++) {
        const struct sepolicy_rule *r = &grp->rules[i];

        ret = sepolicy_add_rule(r->src, r->tgt, r->cls, r->perm, r->effect, r->invert);
        if (ret) {
            pr_warn("[selinux:%s]: %s %s:%s %s -> err %d (skipped)\n", grp->name, r->src ? r->src : "*",
                    r->tgt ? r->tgt : "*", r->cls ? r->cls : "*", r->perm ? r->perm : "*", ret);
            failed++;
        }
    }

    if (failed) {
        pr_warn("[selinux:%s]: %d/%zu rule(s) failed\n", grp->name, failed, grp->count);
        if (grp->required)
            return -ENOEXEC;
    }

    pr_info("[selinux:%s]: %zu/%zu rule(s) applied\n", grp->name, grp->count - failed, grp->count);
    return 0;
}

int load_policy(void)
{
    size_t i;
    int ret;
    int failed_groups = 0;

    pr_info("[selinux]: loading policy for domain '%s'\n", DOMAIN);

    sepolicy_add_typeattribute(DOMAIN, "mlstrustedsubject");
    sepolicy_add_typeattribute(DOMAIN, "unconfineddomain");
    sepolicy_add_typeattribute(DOMAIN, "netdomain");
    sepolicy_add_typeattribute(DOMAIN, "bluetoothdomain");

    ret = sepolicy_allow_any_any(DOMAIN);
    if (ret)
        pr_warn("[selinux]: allow-any-any for '%s' failed: %d\n", DOMAIN, ret);

    sepolicy_add_xperm(DOMAIN, ALL, "blk_file", NULL, AVTAB_XPERMS_ALLOWED, false);
    sepolicy_add_xperm(DOMAIN, ALL, "fifo_file", NULL, AVTAB_XPERMS_ALLOWED, false);
    sepolicy_add_xperm(DOMAIN, ALL, "chr_file", NULL, AVTAB_XPERMS_ALLOWED, false);
    sepolicy_add_xperm(DOMAIN, ALL, "file", NULL, AVTAB_XPERMS_ALLOWED, false);

    for (i = 0; i < ARRAY_SIZE(policy_groups); i++) {
        ret = apply_group(&policy_groups[i]);
        if (ret)
            failed_groups++;
    }

    if (failed_groups) {
        pr_err("[selinux]: %d group(s) had required failures\n", failed_groups);
        return -ENOEXEC;
    }

    pr_info("[selinux]: policy loaded successfully\n");
    return 0;
}

int __init sepolicy_init(void)
{
    int ret;

    pr_info("[selinux]: sepolicy init\n");
    ret = load_policy();
    if (ret)
        pr_err("[selinux]: load_policy failed: %d\n", ret);

    return 0;
}

void __exit sepolicy_exit(void)
{
    pr_info("[selinux]: sepolicy exit\n");
}
