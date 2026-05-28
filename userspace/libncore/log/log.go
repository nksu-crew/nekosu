package log

/*
#cgo LDFLAGS: -llog
#include <android/log.h>
#include <stdlib.h>
#define LOG_TAG "ncore"
static void bridge_log(int priority, const char *msg) {
    __android_log_print(priority, LOG_TAG, "%s", msg);
}

*/
import "C"

import (
	"fmt"
	"unsafe"
)

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
	defer C.free(unsafe.Pointer(cs))
	C.bridge_log(priority, cs)
}
