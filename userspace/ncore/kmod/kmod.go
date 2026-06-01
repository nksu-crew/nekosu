package kmod

/*
#include <stdlib.h>
#include "kmod.h"
*/
import "C"
import (
	"fmt"
	"unsafe"
)

func Load(path string) error {
	cpath := C.CString(path)
	defer C.free(unsafe.Pointer(cpath))

	rc := C.kmod_load(cpath)
	if rc != 0 {
		return fmt.Errorf("kmod_load(%s) = %d", path, int(rc))
	}
	return nil
}
