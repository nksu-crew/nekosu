#pragma once

#include <asm/syscall.h>

int syscalltable_init(void);
void syscalltable_exit(void);

extern syscall_fn_t *syscall_table;

int hook_save(int nr, syscall_fn_t fn, syscall_fn_t *orig, const char *name);

int hook_nosave(int nr, syscall_fn_t fn, const char* name);

/*
 * Overwrite `size` bytes at `slot` with `newval`, using the same stop_machine +
 * fixmap path as the syscall table hook.  Used to redirect the selinuxfs op
 * tables (function-pointer slots) for the SELinux hiding feature.
 */
int nksu_patch_text(void *slot, const void *newval, size_t size);
