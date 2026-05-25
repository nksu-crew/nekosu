package main

/*
#include <jni.h>
#include <fcntl.h>
#include <time.h>
#include <sys/uio.h>
#include <string.h>
#include <stdlib.h>

static const char* bridge_GetStringUTFChars(JNIEnv *env, jstring s) {
    if (s == NULL) return NULL;
    return (*env)->GetStringUTFChars(env, s, NULL);
}

static void bridge_ReleaseStringUTFChars(JNIEnv *env, jstring s, const char *p) {
    if (s == NULL || p == NULL) return;
    (*env)->ReleaseStringUTFChars(env, s, p);
}
*/
import "C"
import (
	"unsafe"

	"nekosu/libncore/ctl"
	"nekosu/libncore/log"
)

var (
	fd    C.int = -1
	ctlfd C.int = -1
)

//export JNI_OnLoad
func JNI_OnLoad(vm *C.JavaVM, reserved unsafe.Pointer) C.jint {
	_ = reserved
	_ = vm

	if err := ctl.Ctl(ctl.OpcodeAuthenticate); err != nil {
		log.Err("ctl error: %s", err.Error())
	}

	return C.JNI_VERSION_1_6
}

//export Java_me_nekosu_aqnya_ncore_ctl
func Java_me_nekosu_aqnya_ncore_ctl(env *C.JNIEnv, thiz C.jobject, value C.jint) C.jint {
	_ = thiz
	_ = env

	var op ctl.Opcode
	switch value {
	case 1:
		op = ctl.OpcodeAuthenticate
	case 2:
		op = ctl.OpcodeGetRoot
	case 3:
		op = ctl.OpcodeIoctl
	default:
		return -1
	}

	if err := ctl.Ctl(op); err != nil {
		log.Err("ctl error: %s", err.Error())
	}

	if value == 1 {
		f, err := ctl.ScanDriverFd()
		if err != nil {
			log.Err("fail to scan fd: %s", err.Error())
		} else {
			fd = C.int(f)
		}
	}
	if value == 3 {
		f, err := ctl.ScanCtlFd()
		if err != nil {
			log.Err("fail to scan ctlfd: %s", err.Error())
		} else {
			ctlfd = C.int(f)
		}
		log.Info("ctlfd after scan: %d", ctlfd)
	}

	log.Info("ctl fd: %d", fd)
	if fd < 0 {
		return -1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_setProfile
func Java_me_nekosu_aqnya_ncore_setProfile(
	env *C.JNIEnv, thiz C.jobject,
	uid C.jint, caps C.jlong, domainStr C.jstring, namespace C.jint,
) C.jint {
	_ = thiz

	var domain string
	if domainStr != C.jstring(0) {
		p := C.bridge_GetStringUTFChars(env, domainStr)
		if p != nil {
			domain = C.GoString(p)
			C.bridge_ReleaseStringUTFChars(env, domainStr, p)
		}
	}

	err := ctl.SetProfile(
		int(ctlfd),
		int(uid),
		uint64(caps),
		domain,
		int(namespace),
	)

	if err != nil {
		log.Err("setProfile failed: %s", err.Error())
		return -1
	}

	return 0
}

//export Java_me_nekosu_aqnya_ncore_adduid
func Java_me_nekosu_aqnya_ncore_adduid(env *C.JNIEnv, thiz C.jobject, value C.jint) C.jint {
	_ = thiz
	_ = env
	if err := ctl.AddUid(int(ctlfd), int(value)); err != nil {
		log.Err("adduid failed: %s", err.Error())
		return -1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_deluid
func Java_me_nekosu_aqnya_ncore_deluid(env *C.JNIEnv, thiz C.jobject, value C.jint) C.jint {
	_ = thiz
	_ = env
	if err := ctl.DelUid(int(ctlfd), int(value)); err != nil {
		log.Err("deluid failed: %s", err.Error())
		return -1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_hasuid
func Java_me_nekosu_aqnya_ncore_hasuid(env *C.JNIEnv, thiz C.jobject, value C.jint) C.jint {
	_ = thiz
	_ = env
	has, err := ctl.HasUid(int(ctlfd), int(value))
	if err != nil {
		return -1
	}
	if has {
		return 1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_setCap
func Java_me_nekosu_aqnya_ncore_setCap(env *C.JNIEnv, thiz C.jobject, uid C.jint, caps C.jlong) C.jint {
	_ = thiz
	_ = env
	if uid < 0 {
		return -1
	}
	if err := ctl.SetCap(int(ctlfd), int(uid), uint64(caps)); err != nil {
		log.Err("setCap failed: %s", err.Error())
		return -1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_getCap
func Java_me_nekosu_aqnya_ncore_getCap(env *C.JNIEnv, thiz C.jobject, uid C.jint) C.jlong {
	_ = thiz
	_ = env
	if uid < 0 {
		return -1
	}
	caps, err := ctl.GetCap(int(ctlfd), int(uid))
	if err != nil {
		log.Err("getCap failed: %s", err.Error())
		return -1
	}
	return C.jlong(caps)
}

//export Java_me_nekosu_aqnya_ncore_delCap
func Java_me_nekosu_aqnya_ncore_delCap(env *C.JNIEnv, thiz C.jobject, uid C.jint) C.jint {
	_ = thiz
	_ = env
	if uid < 0 {
		return -1
	}
	if err := ctl.DelCap(int(ctlfd), int(uid)); err != nil {
		log.Err("delCap failed: %s", err.Error())
		return -1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_addSelinuxRule
func Java_me_nekosu_aqnya_ncore_addSelinuxRule(
	env *C.JNIEnv, thiz C.jobject,
	src, tgt, cls, permStr C.jstring,
	effect C.jint, invert C.jboolean,
) C.jint {
	_ = thiz

	getStr := func(s C.jstring) (*C.char, func()) {
		if s == C.jstring(0) {
			return nil, func() {}
		}
		p := C.bridge_GetStringUTFChars(env, s)
		return p, func() { C.bridge_ReleaseStringUTFChars(env, s, p) }
	}
	goStr := func(p *C.char) string {
		if p == nil {
			return ""
		}
		return C.GoString(p)
	}

	srcP, relSrc := getStr(src)
	defer relSrc()
	tgtP, relTgt := getStr(tgt)
	defer relTgt()
	clsP, relCls := getStr(cls)
	defer relCls()
	permP, relPerm := getStr(permStr)
	defer relPerm()

	if err := ctl.AddSelinuxRule(
		int(ctlfd),
		goStr(srcP), goStr(tgtP), goStr(clsP), goStr(permP),
		int(effect), invert != 0,
	); err != nil {
		log.Err("addSelinuxRule failed: %s", err.Error())
		return -1
	}
	return 0
}

//export Java_me_nekosu_aqnya_ncore_addRule
func Java_me_nekosu_aqnya_ncore_addRule(env *C.JNIEnv, thiz C.jobject, pathStr C.jstring, statusBits C.jlong) C.jint {
	_ = thiz
	_ = env
	_ = pathStr
	_ = statusBits
	return 0
}

//export Java_me_nekosu_aqnya_ncore_delRule
func Java_me_nekosu_aqnya_ncore_delRule(env *C.JNIEnv, thiz C.jobject, pathStr C.jstring) C.jint {
	_ = thiz
	_ = env
	_ = pathStr
	return 0
}

//export Java_me_nekosu_aqnya_ncore_helloLog
func Java_me_nekosu_aqnya_ncore_helloLog(env *C.JNIEnv, thiz C.jobject) {
	_ = thiz
	_ = env
	log.Debug("Hello, this is a log from Go!")
	log.Info("ncore build-as lib")
}

func main() {
}
