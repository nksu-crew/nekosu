// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Implementation of the unexported-symbol indirection layer.
 *
 * Defines all symbol pointers and fills them with real addresses at runtime
 * via nksu_ksym_lookup(). See symbol_compat.h.
 */

/* Use the real types to declare the pointers so macros cannot rewrite them */
#define NKSU_SYMBOL_COMPAT_NO_MACROS
#include <fmac.h>
#include "symbol/symbol_compat.h"

/* definitions */
typeof(alloc_uid) *nksu_alloc_uid;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
typeof(set_cred_ucounts) *nksu_set_cred_ucounts;
#endif
typeof(switch_task_namespaces) *nksu_switch_task_namespaces;
typeof(avc_ss_reset) *nksu_avc_ss_reset;
typeof(selnl_notify_policyload) *nksu_selnl_notify_policyload;
typeof(selinux_status_update_policyload) *nksu_selinux_status_update_policyload;
typeof(symtab_search) *nksu_symtab_search;
typeof(symtab_insert) *nksu_symtab_insert;
typeof(avtab_search_node) *nksu_avtab_search_node;
typeof(avtab_search_node_next) *nksu_avtab_search_node_next;
typeof(avtab_insert_nonunique) *nksu_avtab_insert_nonunique;
typeof(avtab_alloc) *nksu_avtab_alloc;
typeof(avtab_destroy) *nksu_avtab_destroy;
typeof(avtab_alloc_dup) *nksu_avtab_alloc_dup;
typeof(policydb_filenametr_search) *nksu_policydb_filenametr_search;
typeof(policydb_write) *nksu_policydb_write;
typeof(policydb_read) *nksu_policydb_read;
typeof(policydb_destroy) *nksu_policydb_destroy;
typeof(ebitmap_set_bit) *nksu_ebitmap_set_bit;
typeof(ebitmap_get_bit) *nksu_ebitmap_get_bit;
typeof(ebitmap_destroy) *nksu_ebitmap_destroy;
typeof(ebitmap_cpy) *nksu_ebitmap_cpy;
typeof(hashtab_duplicate) *nksu_hashtab_duplicate;
typeof(hashtab_destroy) *nksu_hashtab_destroy;
typeof(hashtab_map) *nksu_hashtab_map;
typeof(__hashtab_insert) *nksu___hashtab_insert;
typeof(security_context_to_sid) *nksu_security_context_to_sid;
typeof(vfs_mkdir) *nksu_vfs_mkdir;
typeof(vfs_unlink) *nksu_vfs_unlink;
typeof(lookup_one_len) *nksu_lookup_one_len;
typeof(__vfs_setxattr_noperm) *nksu___vfs_setxattr_noperm;

typeof(kernel_thread) *nksu_kernel_thread;
typeof(kernel_execve) *nksu_kernel_execve;
typeof(kernel_wait) *nksu_kernel_wait;
typeof(flush_signal_handlers) *nksu_flush_signal_handlers;

typeof(selinux_state) *nksu_selinux_state;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
typeof(selinux_blob_sizes) *nksu_selinux_blob_sizes;
#endif
typeof(init_nsproxy) *nksu_init_nsproxy;
typeof(__set_fixmap) *nksu___set_fixmap;
typeof(copy_to_kernel_nofault) *nksu_copy_to_kernel_nofault;

#define NKSU_RESOLVE(ptr, sym)                                     \
	do {                                                       \
		(ptr) = (typeof(ptr))nksu_ksym_lookup(sym);        \
		if (!(ptr)) {                                      \
			pr_err("[ksym] unresolved %s\n", (sym));   \
			missing++;                                 \
		}                                                  \
	} while (0)

int nksu_symbol_compat_init(void)
{
	int missing = 0;

	NKSU_RESOLVE(nksu_alloc_uid, "alloc_uid");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
	NKSU_RESOLVE(nksu_set_cred_ucounts, "set_cred_ucounts");
#endif
	NKSU_RESOLVE(nksu_switch_task_namespaces, "switch_task_namespaces");

	NKSU_RESOLVE(nksu_avc_ss_reset, "avc_ss_reset");
	NKSU_RESOLVE(nksu_selnl_notify_policyload, "selnl_notify_policyload");
	NKSU_RESOLVE(nksu_selinux_status_update_policyload,
		     "selinux_status_update_policyload");
	NKSU_RESOLVE(nksu_security_context_to_sid, "security_context_to_sid");
	NKSU_RESOLVE(nksu_vfs_mkdir, "vfs_mkdir");
	NKSU_RESOLVE(nksu_vfs_unlink, "vfs_unlink");
	NKSU_RESOLVE(nksu_lookup_one_len, "lookup_one_len");
	NKSU_RESOLVE(nksu___vfs_setxattr_noperm, "__vfs_setxattr_noperm");
	NKSU_RESOLVE(nksu_selinux_state, "selinux_state");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	NKSU_RESOLVE(nksu_selinux_blob_sizes, "selinux_blob_sizes");
#endif
	NKSU_RESOLVE(nksu_init_nsproxy, "init_nsproxy");

	NKSU_RESOLVE(nksu_symtab_search, "symtab_search");
	NKSU_RESOLVE(nksu_symtab_insert, "symtab_insert");
	NKSU_RESOLVE(nksu_avtab_search_node, "avtab_search_node");
	NKSU_RESOLVE(nksu_avtab_search_node_next, "avtab_search_node_next");
	NKSU_RESOLVE(nksu_avtab_insert_nonunique, "avtab_insert_nonunique");
	NKSU_RESOLVE(nksu_avtab_alloc, "avtab_alloc");
	NKSU_RESOLVE(nksu_avtab_destroy, "avtab_destroy");
	NKSU_RESOLVE(nksu_avtab_alloc_dup, "avtab_alloc_dup");
	NKSU_RESOLVE(nksu_policydb_filenametr_search, "policydb_filenametr_search");
	NKSU_RESOLVE(nksu_policydb_write, "policydb_write");
	NKSU_RESOLVE(nksu_policydb_read, "policydb_read");
	NKSU_RESOLVE(nksu_policydb_destroy, "policydb_destroy");
	NKSU_RESOLVE(nksu_ebitmap_set_bit, "ebitmap_set_bit");
	NKSU_RESOLVE(nksu_ebitmap_get_bit, "ebitmap_get_bit");
	NKSU_RESOLVE(nksu_ebitmap_destroy, "ebitmap_destroy");
	NKSU_RESOLVE(nksu_ebitmap_cpy, "ebitmap_cpy");
	NKSU_RESOLVE(nksu_hashtab_duplicate, "hashtab_duplicate");
	NKSU_RESOLVE(nksu_hashtab_destroy, "hashtab_destroy");
	NKSU_RESOLVE(nksu_hashtab_map, "hashtab_map");
	NKSU_RESOLVE(nksu___hashtab_insert, "__hashtab_insert");

	/* kernel/spawn/spawn.c primitives -- no direct relocation against these */
	NKSU_RESOLVE(nksu_kernel_thread, "kernel_thread");
	NKSU_RESOLVE(nksu_kernel_execve, "kernel_execve");
	NKSU_RESOLVE(nksu_kernel_wait, "kernel_wait");
	NKSU_RESOLVE(nksu_flush_signal_handlers, "flush_signal_handlers");
	NKSU_RESOLVE(nksu___set_fixmap, "__set_fixmap");
	NKSU_RESOLVE(nksu_copy_to_kernel_nofault, "copy_to_kernel_nofault");

	if (missing) {
		pr_err("[ksym] %d unexported symbol(s) unresolved, cache=%lu\n",
		       missing, nksu_ksym_count());
		return -ENOENT;
	}

	pr_info("[ksym] all unexported symbols resolved, cache=%lu\n",
		nksu_ksym_count());
	return 0;
}

void nksu_symbol_compat_exit(void)
{
	nksu_alloc_uid = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
	nksu_set_cred_ucounts = NULL;
#endif
	nksu_switch_task_namespaces = NULL;
	nksu_avc_ss_reset = NULL;
	nksu_selnl_notify_policyload = NULL;
	nksu_selinux_status_update_policyload = NULL;
	nksu_security_context_to_sid = NULL;
	nksu_vfs_mkdir = NULL;
	nksu_vfs_unlink = NULL;
	nksu_lookup_one_len = NULL;
	nksu___vfs_setxattr_noperm = NULL;
	nksu_selinux_state = NULL;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	nksu_selinux_blob_sizes = NULL;
#endif
	nksu_init_nsproxy = NULL;
	nksu_symtab_search = NULL;
	nksu_symtab_insert = NULL;
	nksu_avtab_search_node = NULL;
	nksu_avtab_search_node_next = NULL;
	nksu_avtab_insert_nonunique = NULL;
	nksu_avtab_alloc = NULL;
	nksu_avtab_destroy = NULL;
	nksu_avtab_alloc_dup = NULL;
	nksu_policydb_filenametr_search = NULL;
	nksu_policydb_write = NULL;
	nksu_policydb_read = NULL;
	nksu_policydb_destroy = NULL;
	nksu_ebitmap_set_bit = NULL;
	nksu_ebitmap_get_bit = NULL;
	nksu_ebitmap_destroy = NULL;
	nksu_ebitmap_cpy = NULL;
	nksu_hashtab_duplicate = NULL;
	nksu_hashtab_destroy = NULL;
	nksu_hashtab_map = NULL;
	nksu___hashtab_insert = NULL;
	nksu___set_fixmap = NULL;
	nksu_copy_to_kernel_nofault = NULL;
	nksu_kernel_thread = NULL;
	nksu_kernel_execve = NULL;
	nksu_kernel_wait = NULL;
	nksu_flush_signal_handlers = NULL;
}

