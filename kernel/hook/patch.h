#pragma once

#include <linux/types.h>

/*
 * Overwrite `size` bytes of kernel memory at `slot` with `newval`, using a
 * fixmap alias while the other CPUs are parked in stop_machine().
 *
 * For read-only data and dispatcher tables, e.g. the selinuxfs op tables the
 * SELinux hiding feature redirects.  This lives apart from hook/syscall.c on
 * purpose: that file belongs to the syscall-table hook path and features must
 * not have to reach into it.
 */
/*
 * Resolve the editing prerequisites (init_mm) once.  Idempotent; the
 * syscall-table hook calls this during its init so it keeps failing early when
 * the kernel cannot be patched at all.
 */
int nksu_patch_init(void);

int nksu_patch_text(void *slot, const void *newval, size_t size);
