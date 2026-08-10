// SPDX-License-Identifier: GPL-2.0
/*
 * Policy lifecycle: deep-copy the live policy, mutate it, restore it.
 *
 * Three jobs:
 *   1. sepolicy_dup_and_apply() — clone the current policy, RCU-swap it in
 *   2. sepolicy_restore()       — swap the original back, destroy the clone
 *   3. sepolicy_init() / load_policy() — pump our static allow rules in
 *
 * The copy is a full deep clone: classes (with constraint ebitmaps),
 * avtab, roles (with types ebitmap), types (with type_attr_map_array),
 * permissive_map, and filename_trans.  That way the working copy is
 * completely independent and safe to mangle.
 *
 * Locking: policy_mutex protects selinux_state.policy;
 * synchronize_rcu() after every pointer swap.
 */

#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/version.h>
#include <linux/printk.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/lockdep.h>
#include <linux/mm.h>
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

/* saved pointers, alive for the lifetime of the hook */
static struct selinux_policy *nksu_orig_policy __read_mostly;
static struct selinux_policy *nksu_work_policy __read_mostly;

/* kvrealloc signature changed across kernel versions */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
#define _nksu_kvrealloc(p, new_sz, _old_sz) kvrealloc(p, new_sz, GFP_KERNEL)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
#define _nksu_kvrealloc(p, new_sz, old_sz) \
	kvrealloc(p, old_sz, new_sz, GFP_KERNEL)
#else
static void *_nksu_kvrealloc_compat(const void *p, size_t oldsz,
				    size_t newsz, gfp_t f)
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
#define _nksu_kvrealloc(p, new_sz, old_sz) \
	_nksu_kvrealloc_compat(p, old_sz, new_sz, GFP_KERNEL)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
#define _CONST_NODE const
#else
#define _CONST_NODE
#endif

/* thin wrapper so we can hook extra work later */
static inline void nksu_commit_avc_reset(void)
{
	avc_reset();
}

/* shallow hashtab copy — only the node pointers, not the key/datum contents */
static int _copy_ht_node(struct hashtab_node *dst,
			 _CONST_NODE struct hashtab_node *src, void *data)
{
	dst->key   = src->key;
	dst->datum = src->datum;
	return 0;
}

static int _destroy_ht_node_noop(void *key, void *datum, void *data)
{
	return 0;
}

static int _shallow_copy_hashtab(struct hashtab *dst, struct hashtab *src)
{
	return hashtab_duplicate(dst, src,
				 _copy_ht_node, _destroy_ht_node_noop, NULL);
}

/* class deep-copy — constraint linked lists have ebitmaps to clone */

static int _copy_class_cb(struct hashtab_node *dst,
			  _CONST_NODE struct hashtab_node *src, void *data)
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
	ncls->constraints   = NULL;
	ncls->validatetrans = NULL;

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
	for (n = cls->constraints; n; ) {
		for (e = n->expr; e; ) {
			if (e->expr_type == CEXPR_NAMES)
				ebitmap_destroy(&e->names);
			ep = e;
			e = e->next;
			kfree(ep);
		}
		np = n;
		n  = n->next;
		kfree(np);
	}
	kfree(cls);
	return 0;
}

static void _free_classes(struct policydb *db)
{
	kfree(db->class_val_to_struct);
	db->class_val_to_struct = NULL;
	if (db->p_classes.table.htable) {
		hashtab_map(&db->p_classes.table, _destroy_class_cb, NULL);
		hashtab_destroy(&db->p_classes.table);
	}
}

static int _copy_classes(struct policydb *nd, struct policydb *od)
{
	u32 n = od->symtab[SYM_CLASSES].nprim;

	nd->class_val_to_struct = kcalloc(n, sizeof(*nd->class_val_to_struct),
					  GFP_KERNEL);
	if (!nd->class_val_to_struct)
		return -ENOMEM;

	memset(&nd->p_classes.table, 0, sizeof(nd->p_classes.table));
	return hashtab_duplicate(&nd->p_classes.table, &od->p_classes.table,
				 _copy_class_cb, _destroy_class_cb, nd);
}

/* avtab deep-copy */

static int _copy_avtab(struct avtab *dst, struct avtab *src)
{
	struct avtab_node *n;
	int i, ret;

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

/* role deep-copy — types ebitmap must be independently allocated */

static int _copy_role_cb(struct hashtab_node *dst,
			 _CONST_NODE struct hashtab_node *src, void *data)
{
	struct policydb *db = data;
	struct role_datum *or = src->datum, *nr;
	int ret;

	nr = kmemdup(or, sizeof(*or), GFP_KERNEL);
	if (!nr)
		return -ENOMEM;
	dst->key   = src->key;
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
	db->role_val_to_struct = NULL;
	if (db->p_roles.table.htable) {
		hashtab_map(&db->p_roles.table, _destroy_role_cb, NULL);
		hashtab_destroy(&db->p_roles.table);
	}
}

static int _copy_roles(struct policydb *nd, struct policydb *od)
{
	u32 n = od->p_roles.nprim;

	nd->role_val_to_struct = kcalloc(n, sizeof(*nd->role_val_to_struct),
					 GFP_KERNEL);
	if (!nd->role_val_to_struct)
		return -ENOMEM;

	memset(&nd->p_roles.table, 0, sizeof(nd->p_roles.table));
	return hashtab_duplicate(&nd->p_roles.table, &od->p_roles.table,
				 _copy_role_cb, _destroy_role_cb, nd);
}

/*
 * type / attribute map deep-copy
 *
 * type_datum has no embedded pointers — plain memcpy is fine.
 * type_attr_map_array contains ebitmaps that each need cloning.
 */

static void _free_types(struct policydb *db)
{
	u32 i;

	if (db->type_attr_map_array) {
		for (i = 0; i < db->p_types.nprim; i++)
			ebitmap_destroy(&db->type_attr_map_array[i]);
		kvfree(db->type_attr_map_array);
		db->type_attr_map_array = NULL;
	}
	kvfree(db->type_val_to_struct);
	db->type_val_to_struct = NULL;
	kvfree(db->sym_val_to_name[SYM_TYPES]);
	db->sym_val_to_name[SYM_TYPES] = NULL;
	hashtab_destroy(&db->p_types.table);
}

static int _copy_types(struct policydb *nd, struct policydb *od)
{
	u32 sz = nd->p_types.nprim, i;
	int ret = -ENOMEM;

	nd->type_attr_map_array       = NULL;
	nd->type_val_to_struct        = NULL;
	nd->sym_val_to_name[SYM_TYPES] = NULL;
	memset(&nd->p_types.table, 0, sizeof(nd->p_types.table));

	nd->type_attr_map_array = kvcalloc(sz, sizeof(struct ebitmap),
					   GFP_KERNEL);
	if (!nd->type_attr_map_array)
		goto out;

	for (i = 0; i < sz; i++) {
		ret = ebitmap_cpy(&nd->type_attr_map_array[i],
				  &od->type_attr_map_array[i]);
		if (ret < 0)
			goto out;
	}

	ret = -ENOMEM;
	nd->type_val_to_struct = kvcalloc(sz, sizeof(*nd->type_val_to_struct),
					  GFP_KERNEL);
	if (!nd->type_val_to_struct)
		goto out;
	memcpy(nd->type_val_to_struct, od->type_val_to_struct,
	       sz * sizeof(*nd->type_val_to_struct));

	nd->sym_val_to_name[SYM_TYPES] =
		kvcalloc(sz, sizeof(*nd->sym_val_to_name[SYM_TYPES]),
			 GFP_KERNEL);
	if (!nd->sym_val_to_name[SYM_TYPES])
		goto out;
	memcpy(nd->sym_val_to_name[SYM_TYPES],
	       od->sym_val_to_name[SYM_TYPES],
	       sz * sizeof(*nd->sym_val_to_name[SYM_TYPES]));

	ret = _shallow_copy_hashtab(&nd->p_types.table, &od->p_types.table);
	if (ret < 0)
		goto out;

	return 0;
out:
	_free_types(nd);
	return ret;
}

/* permissive_map and filename_trans are straightforward copies */
static int _copy_permissive_map(struct policydb *nd, struct policydb *od)
{
	return ebitmap_cpy(&nd->permissive_map, &od->permissive_map);
}

static int _copy_filename_trans(struct policydb *nd, struct policydb *od)
{
	return _shallow_copy_hashtab(&nd->filename_trans,
				     &od->filename_trans);
}

/*
 * Full deep-clone of a selinux_policy.
 * Copies layers in order: classes → avtab → roles → types →
 * permissive_map → filename_trans.  If any layer fails we unwind
 * everything already allocated.
 */
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

/* Public API — policy lifecycle */

int sepolicy_dup_and_apply(void)
{
	struct selinux_policy *orig, *work;

	if (nksu_orig_policy) {
		pr_warn("[selinux] sepolicy_dup_and_apply: already active\n");
		return -EBUSY;
	}

	mutex_lock(&selinux_state.policy_mutex);

	orig = rcu_dereference_protected(selinux_state.policy,
					 lockdep_is_held(&selinux_state.policy_mutex));
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

	work = rcu_dereference_protected(selinux_state.policy,
					 lockdep_is_held(&selinux_state.policy_mutex));

	rcu_assign_pointer(selinux_state.policy, nksu_orig_policy);

	mutex_unlock(&selinux_state.policy_mutex);
	synchronize_rcu();

	nksu_destroy_policy(work);

	nksu_orig_policy = NULL;
	nksu_work_policy = NULL;

	nksu_commit_avc_reset();

	pr_info("[selinux] original policy restored\n");
}

/* Static rule definitions — loaded once after the copy is in place */

#define ALL NULL

struct sepolicy_rule {
	const char *src;
	const char *tgt;
	const char *cls;
	const char *perm;
	int         effect;
	bool        invert;
};

#define RULE(_src, _tgt, _cls, _perm, _effect, _invert)        \
	{ .src = (_src), .tgt = (_tgt), .cls = (_cls),        \
	  .perm = (_perm), .effect = (_effect),                \
	  .invert = (_invert) }

#define ALLOW(_src, _tgt, _cls, _perm) \
	RULE(_src, _tgt, _cls, _perm, AVTAB_ALLOWED, false)

#define DENY(_src, _tgt, _cls, _perm) \
	RULE(_src, _tgt, _cls, _perm, AVTAB_AUDITDENY, true)

struct sepolicy_group {
	const char                  *name;
	const struct sepolicy_rule  *rules;
	size_t                       count;
	bool                         required;
};

/* allow rules the nksu domain needs at minimum */
static const struct sepolicy_rule ksu_rules[] = {
	ALLOW("init",              DOMAIN, NULL,         NULL),
	ALLOW("servicemanager",    DOMAIN, "dir",        "search"),
	ALLOW("servicemanager",    DOMAIN, "dir",        "read"),
	ALLOW("servicemanager",    DOMAIN, "file",       "open"),
	ALLOW("servicemanager",    DOMAIN, "file",       "read"),
	ALLOW("servicemanager",    DOMAIN, "process",    "getattr"),
	ALLOW("domain",            DOMAIN, "process",    "sigchld"),
	ALLOW("logd",              DOMAIN, "dir",        "search"),
	ALLOW("logd",              DOMAIN, "file",       "read"),
	ALLOW("logd",              DOMAIN, "file",       "open"),
	ALLOW("logd",              DOMAIN, "file",       "getattr"),
	ALLOW("domain",            DOMAIN, "fd",         "use"),
	ALLOW("domain",            DOMAIN, "fifo_file",  "write"),
	ALLOW("domain",            DOMAIN, "fifo_file",  "read"),
	ALLOW("domain",            DOMAIN, "fifo_file",  "open"),
	ALLOW("domain",            DOMAIN, "fifo_file",  "getattr"),
	ALLOW("hwservicemanager",  DOMAIN, "dir",        "search"),
	ALLOW("hwservicemanager",  DOMAIN, "file",       "read"),
	ALLOW("hwservicemanager",  DOMAIN, "file",       "open"),
	ALLOW("hwservicemanager",  DOMAIN, "process",    "getattr"),
	ALLOW("domain",            DOMAIN, "binder",     ALL),
	ALLOW("system_server",     DOMAIN, "process",    "getpgid"),
	ALLOW("system_server",     DOMAIN, "process",    "sigkill"),
	ALLOW("system_server",     DOMAIN, "process",    "setsched"),
	ALLOW(DOMAIN,              DOMAIN, "process",    "fork"),
	ALLOW(DOMAIN,              DOMAIN, "process",    "setpgid"),
	ALLOW(DOMAIN,              "package_service",
				 "service_manager",   "find"),
	ALLOW(DOMAIN,              "servicemanager_prop",
				 "file",             "read"),
	ALLOW(DOMAIN,              "servicemanager_prop",
				 "file",             "open"),
	ALLOW(DOMAIN,              "system_file",
				 "file",             "getattr"),
	ALLOW(DOMAIN,              "system_file",
				 "file",             "execute"),
	ALLOW(DOMAIN,              "toolbox_exec",
				 "file",             "getattr"),
	ALLOW(DOMAIN,              "toolbox_exec",
				 "file",             "execute"),
	ALLOW(DOMAIN,              "toolbox_exec",
				 "file",             "read"),
	ALLOW(DOMAIN,              "toolbox_exec",
				 "file",             "open"),
	ALLOW(DOMAIN,              "untrusted_app_all_devpts",
				 "chr_file",         "read"),
	ALLOW(DOMAIN,              "untrusted_app_all_devpts",
				 "chr_file",         "write"),
	ALLOW(DOMAIN,              "untrusted_app_all_devpts",
				 "chr_file",         "getattr"),
	ALLOW(DOMAIN,              "untrusted_app_all_devpts",
				 "chr_file",         "ioctl"),
	ALLOW(DOMAIN,              "devpts",
				 "chr_file",         "read"),
	ALLOW(DOMAIN,              "devpts",
				 "chr_file",         "write"),
	ALLOW(DOMAIN,              "devpts",
				 "chr_file",         "getattr"),
	ALLOW(DOMAIN,              "devpts",
				 "chr_file",         "ioctl"),
};

#define GROUP(_name, _rules, _required)                                \
	{ .name = (_name), .rules = (_rules),                          \
	  .count = ARRAY_SIZE(_rules), .required = (_required) }

static const struct sepolicy_group policy_groups[] = {
	GROUP("ksu_rules", ksu_rules, true),
};

/* apply one group rule-by-rule; failures are warnings unless required */
static int apply_group(const struct sepolicy_group *grp)
{
	size_t i;
	int ret, failed = 0;

	for (i = 0; i < grp->count; i++) {
		const struct sepolicy_rule *r = &grp->rules[i];

		ret = sepolicy_add_rule(r->src, r->tgt, r->cls, r->perm,
					r->effect, r->invert);
		if (ret) {
			pr_warn("[selinux:%s]: %s %s:%s %s -> err %d (skipped)\n",
				grp->name,
				r->src  ? r->src  : "*",
				r->tgt  ? r->tgt  : "*",
				r->cls  ? r->cls  : "*",
				r->perm ? r->perm : "*",
				ret);
			failed++;
		}
	}

	if (failed) {
		pr_warn("[selinux:%s]: %d/%zu rule(s) failed\n",
			grp->name, failed, grp->count);
		if (grp->required)
			return -ENOEXEC;
	}

	pr_info("[selinux:%s]: %zu/%zu rule(s) applied\n",
		grp->name, grp->count - failed, grp->count);
	return 0;
}

/* pump all static rules and attributes into the current policy */
int load_policy(void)
{
	size_t i;
	int ret, failed_groups = 0;

	pr_info("[selinux]: loading policy for domain '%s'\n", DOMAIN);

	sepolicy_add_typeattribute(DOMAIN, "mlstrustedsubject");
	sepolicy_add_typeattribute(DOMAIN, "unconfineddomain");
	sepolicy_add_typeattribute(DOMAIN, "netdomain");
	sepolicy_add_typeattribute(DOMAIN, "bluetoothdomain");

	ret = sepolicy_allow_any_any(DOMAIN);
	if (ret)
		pr_warn("[selinux]: allow-any-any for '%s' failed: %d\n",
			DOMAIN, ret);

	sepolicy_add_xperm(DOMAIN, ALL, "blk_file",  NULL,
			   AVTAB_XPERMS_ALLOWED, false);
	sepolicy_add_xperm(DOMAIN, ALL, "fifo_file", NULL,
			   AVTAB_XPERMS_ALLOWED, false);
	sepolicy_add_xperm(DOMAIN, ALL, "chr_file",  NULL,
			   AVTAB_XPERMS_ALLOWED, false);
	sepolicy_add_xperm(DOMAIN, ALL, "file",      NULL,
			   AVTAB_XPERMS_ALLOWED, false);

	for (i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		ret = apply_group(&policy_groups[i]);
		if (ret)
			failed_groups++;
	}

	if (failed_groups) {
		pr_err("[selinux]: %d group(s) had required failures\n",
		       failed_groups);
		return -ENOEXEC;
	}

	pr_info("[selinux]: policy loaded successfully\n");
	return 0;
}

int sepolicy_init(void)
{
	int ret;

	pr_info("[selinux]: sepolicy init\n");
	ret = load_policy();
	if (ret)
		pr_err("[selinux]: load_policy failed: %d\n", ret);

	return 0;
}

void sepolicy_exit(void)
{
	pr_info("[selinux]: sepolicy exit\n");
}
