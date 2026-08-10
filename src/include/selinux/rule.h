/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NKSU_SELINUX_RULE_H
#define _NKSU_SELINUX_RULE_H

#include <linux/types.h>

/*
 * avc_reset – invalidate all cached access vectors after a
 * policy modification.  Must be called after every successful
 * rule/type change on the live policy.
 */
void avc_reset(void);

/* ── Rule insertion ─────────────────────────────────────── */

int sepolicy_add_rule(const char *sname, const char *tname,
		      const char *cname, const char *pname,
		      int effect, bool invert);

int sepolicy_allow_any_any(const char *sname);
int sepolicy_allow_all_types(const char *sname, const char *cname);

/* ── Type-attribute association ──────────────────────────── */

int sepolicy_add_typeattribute(const char *type_name,
			       const char *attr_name);

/* ── Extended permissions (ioctl ranges etc.) ────────────── */

int sepolicy_add_xperm(const char *s, const char *t, const char *c,
		       const char *range, int effect, bool invert);

/* ── Debug audit (CONFIG_NKSU_DEBUG only) ────────────────── */

#ifdef CONFIG_NKSU_DEBUG
int sepolicy_make_audit(void);
#endif

#endif /* _NKSU_SELINUX_RULE_H */
