#pragma once

#include <asm/syscall.h>

/*
 * Kernel-side syscall-table edits.  Three things, nothing else:
 *
 *   syscall_table_resolve()      find sys_call_table
 *   syscall_slot_hook()          replace one slot, handing back the old handler
 *   syscall_slots_restore_all()  put every replaced slot back
 *
 * Slots are restored as a group, not per owner: whoever recorded a slot gets
 * it back only when syscall_slots_restore_all() runs.  That is how the
 * boot-time hooks (the temporary watcher and the init.rc read/fstat proxies)
 * are torn down together at the zygote stage.
 */
extern syscall_fn_t *syscall_table;

int syscall_table_resolve(void);

int syscall_slot_hook(int nr, syscall_fn_t fn, syscall_fn_t *previous,
                      const char *tag);

void syscall_slots_restore_all(void);
