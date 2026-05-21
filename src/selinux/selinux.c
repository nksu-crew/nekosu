#include "security.h"
#include "ss/symtab.h"
#include "ss/policydb.h"
#include "ss/ebitmap.h"
#include "ss/services.h"
#include "objsec.h"

#include <fmac.h>

void setenforce(bool status)
{
// true or false
#ifdef CONFIG_SECURITY_SELINUX_DEVELOP
    //selinux_state.enforcing = status;
    WRITE_ONCE(selinux_state.enforcing, status);
#endif
}

bool getenforce(void)
{
    return READ_ONCE(selinux_state.enforcing);
}

int set_domain(const char *domain, struct cred *new_cred)
{
    u32 newsid;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    int rc = security_context_to_sid(domain, strlen(domain), &newsid, GFP_KERNEL);
#else
    int rc = security_context_to_sid(&selinux_state, domain, strlen(domain), &newsid, GFP_KERNEL);
#endif

    if (rc) {
        pr_err("Failed to get SID for %s: %d\n", domain, rc);
        return rc;
    }

    if (new_cred->security) {
        struct task_security_struct *tsec = new_cred->security;

        tsec->osid = tsec->sid;
        tsec->sid = newsid;
        tsec->exec_sid = 0;
        tsec->create_sid = 0;
        tsec->keycreate_sid = 0;
        tsec->sockcreate_sid = 0;
        return 0;
    }

    return -EPERM;
}

bool do_allow(struct policydb *db, const char *type_name)
{
    struct type_datum *type;
    type = (struct type_datum *)symtab_search(&db->p_types, type_name);
    if (type == NULL) {
        pr_err("type null,do_allow false\n");
        return false;
    }
    if (ebitmap_set_bit(&db->permissive_map, type->value, true)) {
        pr_err("can't set bitmap\n");
        return false;
    }
    return true;
}

int __init init_selinux_hook(void)
{
    struct policydb *db;
    int rc;
    if (!selinux_state.policy)
        return -1;

    if (!getenforce()) {
        pr_info("[selinux]: enforcing is false,set 1\n");
        setenforce(true);
    }

    int ret = sepolicy_dup_and_apply();
    if (ret) {
        pr_err("[selinux]: failed to dup policy (%d), aborting\n", ret);
        return ret;
    }

#ifdef CONFIG_NKSU_DEBUG
    rc = sepolicy_make_audit();
    if (rc) {
        pr_err("[selinux]: failed to make audit: %d\n", rc);
        return rc;
    }
#endif

    rc = sepolicy_add_domain(DOMAIN);
    if (rc) {
        pr_err("Failed to add domain '%s': %d\n", DOMAIN, rc);
        return rc;
    }

    rc = sepolicy_init();
    if (rc) {
        pr_err("Failed to apply rules 'nksu': %d\n", rc);
        return rc;
    }

#ifdef CONFIG_NKSU_DEBUG
    pr_info("[selinux]: debug mode, setting permissive\n");
    mutex_lock(&selinux_state.policy_mutex);
    db = &rcu_dereference_protected(selinux_state.policy, lockdep_is_held(&selinux_state.policy_mutex))->policydb;
    do_allow(db, DOMAIN);
    mutex_unlock(&selinux_state.policy_mutex);
    avc_reset();
#endif

    return 0;
}

void __exit selinux_exit(void)
{
    pr_info("[selinux]: sepolicy exit – restoring original policy\n");
    sepolicy_restore();
}
