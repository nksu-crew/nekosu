package log

/*
#cgo LDFLAGS: -llog
#include <android/log.h>
#include <stdlib.h>

static void bridge_log(int priority, const char *tag, const char *msg) {
    __android_log_print(priority, tag, "%s", msg);
}

*/
import "C"

import (
	"fmt"
	"unsafe"
)

const logTag = "ncore"

type logPriority = C.int

const (
	logDEBUG logPriority = C.ANDROID_LOG_DEBUG
	logINFO  logPriority = C.ANDROID_LOG_INFO
	logWARN  logPriority = C.ANDROID_LOG_WARN
	logERROR logPriority = C.ANDROID_LOG_ERROR
)

func Err(format string, args ...any) {
	log(logERROR, format, args...)
}

func Info(format string, args ...any) {
	log(logINFO, format, args...)
}

func Debug(format string, args ...any) {
	log(logDEBUG, format, args...)
}

func log(priority logPriority, format string, args ...any) {
	msg := fmt.Sprintf(format, args...)
	cs := C.CString(msg)
	tag := C.CString(logTag)
	defer C.free(unsafe.Pointer(cs))
	defer C.free(unsafe.Pointer(tag))
	C.bridge_log(priority, tag, cs)
}
