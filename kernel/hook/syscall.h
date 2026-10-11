#pragma once

#include <asm/syscall.h>

int syscalltable_init(void);
void syscalltable_exit(void);

extern syscall_fn_t *syscall_table;

int hook_save(int nr, syscall_fn_t fn, syscall_fn_t *orig, const char *name);

int hook_nosave(int nr, syscall_fn_t fn, const char* name);
