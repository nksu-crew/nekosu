#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "ss/policydb.h"
#include "ss/services.h"
#include "ss/hashtab.h"
#include "security.h"

static int add_type_to_attr(struct policydb *p, const char *type_name, const char *attr_name)
{
    struct type_datum *type;
    struct type_datum *attr;

    type = symtab_search(&p->p_types, type_name);
    attr = symtab_search(&p->p_types, attr_name);

    if (!type || !attr) {
        pr_err("[selinux]: failed to find type(%s) or attr(%s)\n", type_name, attr_name);
        return -EINVAL;
    }

    if (!attr->attribute) {
        pr_err("[selinux]: %s is not an attribute\n", attr_name);
        return -EINVAL;
    }

    if (!p->type_attr_map_array) {
        pr_err("[selinux]: type_attr_map_array is NULL\n");
        return -EINVAL;
    }

    if (type->value > p->p_types.nprim || attr->value > p->p_types.nprim) {
        pr_err("[selinux]: type value out of range\n");
        return -EINVAL;
    }

    ebitmap_set_bit(&p->type_attr_map_array[type->value - 1], attr->value - 1, 1);

    return 0;
}

static int add_type_to_policy(struct policydb *p, const char *name)
{
    struct type_datum *type;
    char *name_copy;
    uint32_t new_value;
    void *tmp;
    int i, rc;

    type = symtab_search(&p->p_types, name);
    if (type)
        return 0;

    type = kzalloc(sizeof(*type), GFP_KERNEL);
    if (!type)
        return -ENOMEM;

    name_copy = kstrdup(name, GFP_KERNEL);
    if (!name_copy) {
        kfree(type);
        return -ENOMEM;
    }

    new_value = p->p_types.nprim + 1;
    type->primary = 1;
    type->value = new_value;

    tmp = krealloc(p->sym_val_to_name[SYM_TYPES], sizeof(char *) * new_value, GFP_KERNEL);
    if (!tmp) {
        rc = -ENOMEM;
        goto err_free;
    }
    p->sym_val_to_name[SYM_TYPES] = tmp;
    p->sym_val_to_name[SYM_TYPES][new_value - 1] = name_copy;

    tmp = krealloc(p->type_val_to_struct, sizeof(struct type_datum *) * new_value, GFP_KERNEL);
    if (!tmp) {
        p->sym_val_to_name[SYM_TYPES][new_value - 1] = NULL;
        rc = -ENOMEM;
        goto err_free;
    }
    p->type_val_to_struct = tmp;
    p->type_val_to_struct[new_value - 1] = type;

    tmp = krealloc(p->type_attr_map_array, sizeof(struct ebitmap) * new_value, GFP_KERNEL);
    if (!tmp) {
        p->sym_val_to_name[SYM_TYPES][new_value - 1] = NULL;
        p->type_val_to_struct[new_value - 1] = NULL;
        rc = -ENOMEM;
        goto err_free;
    }
    p->type_attr_map_array = tmp;
    ebitmap_init(&p->type_attr_map_array[new_value - 1]);

    rc = symtab_insert(&p->p_types, name_copy, type);
    if (rc) {
        ebitmap_destroy(&p->type_attr_map_array[new_value - 1]);
        p->type_attr_map_array[new_value - 1] = (struct ebitmap){};
        p->sym_val_to_name[SYM_TYPES][new_value - 1] = NULL;
        p->type_val_to_struct[new_value - 1] = NULL;
        goto err_free;
    }

    p->p_types.nprim++;

    ebitmap_set_bit(&p->type_attr_map_array[new_value - 1], new_value - 1, 1);

    for (i = 0; i < p->p_roles.nprim; ++i) {
        if (!p->role_val_to_struct[i])
            continue;
        rc = ebitmap_set_bit(&p->role_val_to_struct[i]->types, new_value - 1, 1);
        if (rc) {
            pr_err("[selinux]: failed to set role types bit for role %d\n", i);
        }
    }

    return 0;

err_free:
    kfree(name_copy);
    kfree(type);
    return rc;
}

int sepolicy_add_domain(const char *name)
{
    struct selinux_policy *policy;
    struct policydb *p;
    int rc;

    mutex_lock(&selinux_state.policy_mutex);

    policy = rcu_dereference_protected(selinux_state.policy, lockdep_is_held(&selinux_state.policy_mutex));
    if (!policy) {
        rc = -EINVAL;
        goto out;
    }

    p = &policy->policydb;

    rc = add_type_to_policy(p, name);
    if (rc)
        goto out;

    rc = add_type_to_attr(p, name, "domain");

out:
    mutex_unlock(&selinux_state.policy_mutex);
    return rc;
}
