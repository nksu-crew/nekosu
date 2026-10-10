/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_SYMBOL_COMPAT_H
#define NKSU_SYMBOL_COMPAT_H

/*
 * Indirection layer for unexported kernel symbols.
 *
 * Problem: these symbols are no longer exported on GKI/KMI kernels. A direct
 * reference makes the module fail to load with "Unknown symbol ... (err -2)".
 *
 * Approach: for each symbol declare a pointer of the same type, fill it at
 * load time via nksu_ksym_lookup(), then use macros to redirect the original
 * name to the pointer. This removes the relocation so unexported symbols can
 * still be called.
 *
 *   extern typeof(alloc_uid) *nksu_alloc_uid;   // function pointer
 *   #define alloc_uid nksu_alloc_uid            // redirect call sites
 *
 *   extern typeof(selinux_state) *nksu_selinux_state; // data pointer
 *   #define selinux_state (*nksu_selinux_state)      // dereference
 *
 * Requirement: include this header *after* every kernel/SELinux header that
 * declares these symbols, otherwise the macros would rewrite the
 * declarations. Each .c file therefore puts it last in its include block.
 */

#include <linux/types.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/xattr.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/binfmts.h>
#include <linux/cred.h>
#include <linux/nsproxy.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>
#include <asm/fixmap.h>

#include "security.h"
#include "ss/policydb.h"
#include "ss/services.h"
#include "ss/avtab.h"
#include "ss/symtab.h"
#include "ss/hashtab.h"
#include "ss/ebitmap.h"
#include "ss/constraint.h"
#include "avc.h"
#include "avc_ss.h"
#include "xfrm.h"
#include "objsec.h"

/* function symbols (pointers) */
extern typeof(alloc_uid) *nksu_alloc_uid;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
extern typeof(set_cred_ucounts) *nksu_set_cred_ucounts;
#endif
extern typeof(switch_task_namespaces) *nksu_switch_task_namespaces;
extern typeof(avc_ss_reset) *nksu_avc_ss_reset;
extern typeof(selnl_notify_policyload) *nksu_selnl_notify_policyload;
extern typeof(selinux_status_update_policyload) *nksu_selinux_status_update_policyload;
extern typeof(symtab_search) *nksu_symtab_search;
extern typeof(symtab_insert) *nksu_symtab_insert;
extern typeof(avtab_search_node) *nksu_avtab_search_node;
extern typeof(avtab_search_node_next) *nksu_avtab_search_node_next;
extern typeof(avtab_insert_nonunique) *nksu_avtab_insert_nonunique;
extern typeof(avtab_alloc) *nksu_avtab_alloc;
extern typeof(avtab_destroy) *nksu_avtab_destroy;
extern typeof(avtab_alloc_dup) *nksu_avtab_alloc_dup;
extern typeof(policydb_filenametr_search) *nksu_policydb_filenametr_search;
extern typeof(policydb_write) *nksu_policydb_write;
extern typeof(policydb_read) *nksu_policydb_read;
extern typeof(policydb_destroy) *nksu_policydb_destroy;
extern typeof(ebitmap_set_bit) *nksu_ebitmap_set_bit;
extern typeof(ebitmap_get_bit) *nksu_ebitmap_get_bit;
extern typeof(ebitmap_destroy) *nksu_ebitmap_destroy;
extern typeof(ebitmap_cpy) *nksu_ebitmap_cpy;
extern typeof(hashtab_duplicate) *nksu_hashtab_duplicate;
extern typeof(hashtab_destroy) *nksu_hashtab_destroy;
extern typeof(hashtab_map) *nksu_hashtab_map;
extern typeof(__hashtab_insert) *nksu___hashtab_insert;
extern typeof(security_context_to_sid) *nksu_security_context_to_sid;

/* VFS helpers used to bootstrap /data/adb/nksu in the manager scan.  vfs_mkdir
 * and lookup_one_len are not part of every GKI export set, so reach them
 * through kallsyms like the rest. */
extern typeof(vfs_mkdir) *nksu_vfs_mkdir;
extern typeof(lookup_one_len) *nksu_lookup_one_len;
/* Persist the nksu_file label on the bootstrapped daemon (see selinux.c). */
extern typeof(__vfs_setxattr_noperm) *nksu___vfs_setxattr_noperm;

/*
 * spawn primitives (kernel/spawn/spawn.c). None of these is exported on GKI, and they
 * are exactly what a self-contained call_usermodehelper replacement needs:
 * spawn a task, exec a program in it, wait for it, reset its handlers.
 */
extern typeof(kernel_thread) *nksu_kernel_thread;
extern typeof(kernel_execve) *nksu_kernel_execve;
extern typeof(kernel_wait) *nksu_kernel_wait;
extern typeof(flush_signal_handlers) *nksu_flush_signal_handlers;

/* data symbol (pointer, dereferenced by the macro) */
extern typeof(selinux_state) *nksu_selinux_state;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
extern typeof(selinux_blob_sizes) *nksu_selinux_blob_sizes;
#endif
extern typeof(init_nsproxy) *nksu_init_nsproxy;

extern typeof(__set_fixmap) *nksu___set_fixmap;
extern typeof(copy_to_kernel_nofault) *nksu_copy_to_kernel_nofault;
/*
 * Macro redirection. symbol_compat.c needs the real types to define the
 * variables and cast, so it defines NKSU_SYMBOL_COMPAT_NO_MACROS before
 * including this header, skipping this block.
 */
#ifndef NKSU_SYMBOL_COMPAT_NO_MACROS
#define alloc_uid                        nksu_alloc_uid
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
#define set_cred_ucounts                 nksu_set_cred_ucounts
#endif
#define switch_task_namespaces           nksu_switch_task_namespaces
#define avc_ss_reset                     nksu_avc_ss_reset
#define selnl_notify_policyload          nksu_selnl_notify_policyload
#define selinux_status_update_policyload nksu_selinux_status_update_policyload
#define symtab_search                    nksu_symtab_search
#define symtab_insert                    nksu_symtab_insert
#define avtab_search_node                nksu_avtab_search_node
#define avtab_search_node_next           nksu_avtab_search_node_next
#define avtab_insert_nonunique           nksu_avtab_insert_nonunique
#define avtab_alloc                      nksu_avtab_alloc
#define avtab_destroy                    nksu_avtab_destroy
#define avtab_alloc_dup                  nksu_avtab_alloc_dup
#define policydb_filenametr_search       nksu_policydb_filenametr_search
#define policydb_write                   nksu_policydb_write
#define policydb_read                    nksu_policydb_read
#define policydb_destroy                 nksu_policydb_destroy
#define ebitmap_set_bit                  nksu_ebitmap_set_bit
#define ebitmap_get_bit                  nksu_ebitmap_get_bit
#define ebitmap_destroy                  nksu_ebitmap_destroy
#define ebitmap_cpy                      nksu_ebitmap_cpy
#define hashtab_duplicate                nksu_hashtab_duplicate
#define hashtab_destroy                  nksu_hashtab_destroy
#define hashtab_map                      nksu_hashtab_map
#define security_context_to_sid          nksu_security_context_to_sid
#define vfs_mkdir                        nksu_vfs_mkdir
#define lookup_one_len                   nksu_lookup_one_len
#define __vfs_setxattr_noperm            nksu___vfs_setxattr_noperm
#define kernel_thread                    nksu_kernel_thread
#define kernel_execve                    nksu_kernel_execve
#define kernel_wait                      nksu_kernel_wait
#define flush_signal_handlers            nksu_flush_signal_handlers
#define selinux_state                    (*nksu_selinux_state)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
#define selinux_blob_sizes               (*nksu_selinux_blob_sizes)
#endif
#define init_nsproxy                     (*nksu_init_nsproxy)
#define __set_fixmap nksu___set_fixmap
#define copy_to_kernel_nofault           nksu_copy_to_kernel_nofault
#endif /* NKSU_SYMBOL_COMPAT_NO_MACROS */

/* Resolve all symbols into the pointers; returns negative errno if any fail. */
int  nksu_symbol_compat_init(void);

/* Clear the pointers (module unload). */
void nksu_symbol_compat_exit(void);

#endif /* NKSU_SYMBOL_COMPAT_H */
