// SPDX-License-Identifier: GPL-2.0

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/lockdep.h>
#include <fmac.h>

#include "ss/policydb.h"
#include "ss/services.h"
#include "ss/avtab.h"
#include "ss/hashtab.h"
#include "ss/ebitmap.h"
#include "ss/symtab.h"
#include "ss/constraint.h"
#include "security.h"
#include "avc.h"
#include "avc_ss.h"
#include "xfrm.h"

static struct selinux_policy *nksu_orig_policy __read_mostly = NULL;
static struct selinux_policy *nksu_work_policy __read_mostly = NULL;

static void nksu_avc_reset(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    avc_ss_reset(0);
    selnl_notify_policyload(0);
    selinux_status_update_policyload(0);
#else
    avc_ss_reset(selinux_state.avc, 0);
    selnl_notify_policyload(0);
    selinux_status_update_policyload(&selinux_state, 0);
#endif
    selinux_xfrm_notify_policyload();
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define _nksu_kvrealloc(p, new_sz, _old_sz) kvrealloc(p, new_sz, GFP_KERNEL)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#define _nksu_kvrealloc(p, new_sz, old_sz) kvrealloc(p, old_sz, new_sz, GFP_KERNEL)
#else
static void *_nksu_kvrealloc_compat(const void *p, size_t oldsz, size_t newsz, gfp_t f)
{
    void *n;
    if (oldsz >= newsz)
        return (void *)p;
    n = kvmalloc(newsz, f);
    if (!n)
        return NULL;
    memcpy(n, p, oldsz);
    kvfree(p);
    return n;
}
#define _nksu_kvrealloc(p, new_sz, old_sz) _nksu_kvrealloc_compat(p, old_sz, new_sz, GFP_KERNEL)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
#define _CONST_NODE const
#else
#define _CONST_NODE
#endif

static int _copy_ht_node(struct hashtab_node *dst, _CONST_NODE struct hashtab_node *src, void *data)
{
    dst->key = src->key;
    dst->datum = src->datum;
    return 0;
}

static int _destroy_ht_node_noop(void *key, void *datum, void *data)
{
    return 0;
}

static int _shallow_copy_hashtab(struct hashtab *dst, struct hashtab *src)
{
    return hashtab_duplicate(dst, src, _copy_ht_node, _destroy_ht_node_noop, NULL);
}

static int _copy_class_cb(struct hashtab_node *dst, _CONST_NODE struct hashtab_node *src, void *data)
{
    struct policydb *db = data;
    struct class_datum *ocls = src->datum, *ncls;
    struct constraint_node *on, *nn, *nprev = NULL;
    struct constraint_expr *oe, *ne, *eprev;

    dst->key = src->key;
    ncls = kmemdup(ocls, sizeof(*ocls), GFP_KERNEL);
    if (!ncls)
        return -ENOMEM;
    dst->datum = ncls;
    ncls->constraints = NULL;

    for (on = ocls->constraints; on; on = on->next) {
        nn = kmemdup(on, sizeof(*on), GFP_KERNEL);
        if (!nn)
            goto oom;
        nn->expr = NULL;
        if (nprev)
            nprev->next = nn;
        else
            ncls->constraints = nn;

        eprev = NULL;
        for (oe = on->expr; oe; oe = oe->next) {
            ne = kmemdup(oe, sizeof(*oe), GFP_KERNEL);
            if (!ne)
                goto oom;
            if (eprev)
                eprev->next = ne;
            else
                nn->expr = ne;
            if (oe->expr_type == CEXPR_NAMES) {
                if (ebitmap_cpy(&ne->names, &oe->names) < 0)
                    goto oom;
            }
            eprev = ne;
        }
        nprev = nn;
    }

    db->class_val_to_struct[ncls->value - 1] = ncls;
    return 0;
oom:
    return -ENOMEM;
}

static int _destroy_class_cb(void *key, void *datum, void *data)
{
    struct class_datum *cls = datum;
    struct constraint_node *n, *np;
    struct constraint_expr *e, *ep;

    if (!cls)
        return 0;
    for (n = cls->constraints; n;) {
        for (e = n->expr; e;) {
            if (e->expr_type == CEXPR_NAMES)
                ebitmap_destroy(&e->names);
            ep = e;
            e = e->next;
            kfree(ep);
        }
        np = n;
        n = n->next;
        kfree(np);
    }
    kfree(cls);
    return 0;
}

static void _free_classes(struct policydb *db)
{
    kfree(db->class_val_to_struct);
    if (db->p_classes.table.htable) {
        hashtab_map(&db->p_classes.table, _destroy_class_cb, NULL);
        hashtab_destroy(&db->p_classes.table);
    }
}

static int _copy_classes(struct policydb *nd, struct policydb *od)
{
    u32 n = nd->symtab[SYM_CLASSES].nprim;

    nd->class_val_to_struct = kcalloc(n, sizeof(*nd->class_val_to_struct), GFP_KERNEL);
    if (!nd->class_val_to_struct)
        return -ENOMEM;

    memset(&nd->p_classes.table, 0, sizeof(nd->p_classes.table));
    return hashtab_duplicate(&nd->p_classes.table, &od->p_classes.table, _copy_class_cb, _destroy_class_cb, nd);
}

static int _copy_avtab(struct avtab *dst, struct avtab *src)
{
    int ret, i;
    struct avtab_node *n;

    ret = avtab_alloc_dup(dst, src);
    if (ret < 0)
        return ret;
    dst->nel = 0;

    for (i = 0; i < src->nslot; i++) {
        for (n = src->htable[i]; n; n = n->next) {
            if (!avtab_insert_nonunique(dst, &n->key, &n->datum)) {
                avtab_destroy(dst);
                return -ENOMEM;
            }
        }
    }
    return 0;
}

static int _copy_role_cb(struct hashtab_node *dst, _CONST_NODE struct hashtab_node *src, void *data)
{
    struct policydb *db = data;
    struct role_datum *or = src->datum, *nr;
    int ret;

    nr = kmemdup(or, sizeof(*or), GFP_KERNEL);
    if (!nr)
        return -ENOMEM;
    dst->key = src->key;
    dst->datum = nr;

    ret = ebitmap_cpy(&nr->types, &or->types);
    if (ret)
        return ret;

    db->role_val_to_struct[or->value - 1] = nr;
    return 0;
}

static int _destroy_role_cb(void *key, void *datum, void *data)
{
    struct role_datum *r = datum;
    if (r) {
        ebitmap_destroy(&r->types);
        kfree(r);
    }
    return 0;
}

static void _free_roles(struct policydb *db)
{
    kfree(db->role_val_to_struct);
    if (db->p_roles.table.htable) {
        hashtab_map(&db->p_roles.table, _destroy_role_cb, NULL);
        hashtab_destroy(&db->p_roles.table);
    }
}

static int _copy_roles(struct policydb *nd, struct policydb *od)
{
    u32 n = od->p_roles.nprim;

    nd->role_val_to_struct = kcalloc(n, sizeof(*nd->role_val_to_struct), GFP_KERNEL);
    if (!nd->role_val_to_struct)
        return -ENOMEM;

    memset(&nd->p_roles.table, 0, sizeof(nd->p_roles.table));
    return hashtab_duplicate(&nd->p_roles.table, &od->p_roles.table, _copy_role_cb, _destroy_role_cb, nd);
}

static void _free_types(struct policydb *db)
{
    u32 i;

    if (db->type_attr_map_array) {
        for (i = 0; i < db->p_types.nprim; i++)
            ebitmap_destroy(&db->type_attr_map_array[i]);
        kvfree(db->type_attr_map_array);
    }
    kvfree(db->type_val_to_struct);
    kvfree(db->sym_val_to_name[SYM_TYPES]);
    hashtab_destroy(&db->p_types.table);
}

static int _copy_types(struct policydb *nd, struct policydb *od)
{
    u32 sz = nd->p_types.nprim, i;
    int ret = -ENOMEM;

    nd->type_attr_map_array = NULL;
    nd->type_val_to_struct = NULL;
    nd->sym_val_to_name[SYM_TYPES] = NULL;
    memset(&nd->p_types.table, 0, sizeof(nd->p_types.table));

    nd->type_attr_map_array = kvcalloc(sz, sizeof(struct ebitmap), GFP_KERNEL);
    if (!nd->type_attr_map_array)
        goto out;

    for (i = 0; i < sz; i++) {
        ret = ebitmap_cpy(&nd->type_attr_map_array[i], &od->type_attr_map_array[i]);
        if (ret < 0)
            goto out;
    }

    ret = -ENOMEM;
    nd->type_val_to_struct = kvcalloc(sz, sizeof(*nd->type_val_to_struct), GFP_KERNEL);
    if (!nd->type_val_to_struct)
        goto out;
    memcpy(nd->type_val_to_struct, od->type_val_to_struct, sz * sizeof(*nd->type_val_to_struct));

    nd->sym_val_to_name[SYM_TYPES] = kvcalloc(sz, sizeof(*nd->sym_val_to_name[SYM_TYPES]), GFP_KERNEL);
    if (!nd->sym_val_to_name[SYM_TYPES])
        goto out;
    memcpy(nd->sym_val_to_name[SYM_TYPES], od->sym_val_to_name[SYM_TYPES],
           sz * sizeof(*nd->sym_val_to_name[SYM_TYPES]));

    ret = _shallow_copy_hashtab(&nd->p_types.table, &od->p_types.table);
    if (ret < 0)
        goto out;

    return 0;
out:
    _free_types(nd);
    return ret;
}

static int _copy_permissive_map(struct policydb *nd, struct policydb *od)
{
    return ebitmap_cpy(&nd->permissive_map, &od->permissive_map);
}

static int _copy_filename_trans(struct policydb *nd, struct policydb *od)
{
    return _shallow_copy_hashtab(&nd->filename_trans, &od->filename_trans);
}

static struct selinux_policy *nksu_dup_policy(struct selinux_policy *src)
{
    struct selinux_policy *dst;
    struct policydb *nd, *od;
    int ret;

    dst = kmemdup(src, sizeof(*src), GFP_KERNEL);
    if (!dst)
        return NULL;

    nd = &dst->policydb;
    od = &src->policydb;

    ret = _copy_classes(nd, od);
    if (ret)
        goto err_free;

    ret = _copy_avtab(&nd->te_avtab, &od->te_avtab);
    if (ret)
        goto err_classes;

    ret = _copy_roles(nd, od);
    if (ret)
        goto err_avtab;

    ret = _copy_types(nd, od);
    if (ret)
        goto err_roles;

    ret = _copy_permissive_map(nd, od);
    if (ret)
        goto err_types;

    ret = _copy_filename_trans(nd, od);
    if (ret)
        goto err_pmap;

    return dst;

err_pmap:
    ebitmap_destroy(&nd->permissive_map);
err_types:
    _free_types(nd);
err_roles:
    _free_roles(nd);
err_avtab:
    avtab_destroy(&nd->te_avtab);
err_classes:
    _free_classes(nd);
err_free:
    kfree(dst);
    return NULL;
}

static void nksu_destroy_policy(struct selinux_policy *pol)
{
    struct policydb *db;

    if (!pol)
        return;

    db = &pol->policydb;
    _free_classes(db);
    avtab_destroy(&db->te_avtab);
    _free_roles(db);
    _free_types(db);
    ebitmap_destroy(&db->permissive_map);
    hashtab_destroy(&db->filename_trans);
    kfree(pol);
}

int sepolicy_dup_and_apply(void)
{
    struct selinux_policy *orig, *work;

    if (nksu_orig_policy) {
        pr_warn("[selinux] sepolicy_dup_and_apply: already active\n");
        return -EBUSY;
    }

    mutex_lock(&selinux_state.policy_mutex);

    orig = rcu_dereference_protected(selinux_state.policy, lockdep_is_held(&selinux_state.policy_mutex));

    if (!orig) {
        mutex_unlock(&selinux_state.policy_mutex);
        pr_err("[selinux] sepolicy_dup_and_apply: no live policy\n");
        return -ENOENT;
    }

    work = nksu_dup_policy(orig);
    if (!work) {
        mutex_unlock(&selinux_state.policy_mutex);
        pr_err("[selinux] sepolicy_dup_and_apply: dup failed\n");
        return -ENOMEM;
    }

    nksu_orig_policy = orig;
    nksu_work_policy = work;

    rcu_assign_pointer(selinux_state.policy, work);

    mutex_unlock(&selinux_state.policy_mutex);
    synchronize_rcu();

    pr_info("[selinux] policy duplicated, working copy installed\n");
    return 0;
}

void sepolicy_restore(void)
{
    struct selinux_policy *work;

    if (!nksu_orig_policy) {
        pr_warn("[selinux] sepolicy_restore: nothing to restore\n");
        return;
    }

    mutex_lock(&selinux_state.policy_mutex);

    work = rcu_dereference_protected(selinux_state.policy, lockdep_is_held(&selinux_state.policy_mutex));

    rcu_assign_pointer(selinux_state.policy, nksu_orig_policy);

    mutex_unlock(&selinux_state.policy_mutex);
    synchronize_rcu();

    nksu_destroy_policy(work);

    nksu_orig_policy = NULL;
    nksu_work_policy = NULL;

    nksu_avc_reset();

    pr_info("[selinux] original policy restored\n");
}
