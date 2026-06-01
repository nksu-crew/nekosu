package kmod

/*
#include <stdlib.h>
#include "kmod.h"
*/
import "C"
import (
	"fmt"
	"syscall"
	"unsafe"
)

func Load(path string) error {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	rc := C.kmod_load(cpath)
	if rc != 0 {
		return fmt.Errorf("kmod_load(%s): %w", path, syscall.Errno(-rc))
	}
	return nil
}
