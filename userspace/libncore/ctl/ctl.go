package ctl

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"syscall"
	"unsafe"

	"golang.org/x/sys/unix"
)

/*
#include "ioctl.h"
*/
import "C"

type Opcode uint32

const (
	OpcodeAuthenticate Opcode = 1
	OpcodeGetRoot      Opcode = 2
	OpcodeIoctl        Opcode = 3
)

var (
	IOC_GET_SHM   = uint32(C.IOC_GET_SHM)
	IOC_BIND_EVT  = uint32(C.IOC_BIND_EVT)
	IOC_CHK_WRITE = uint32(C.IOC_CHK_WRITE)
	IOC_ADD_UID   = uint32(C.IOC_ADD_UID)
	IOC_DEL_UID   = uint32(C.IOC_DEL_UID)
	IOC_HAS_UID   = uint32(C.IOC_HAS_UID)

	IOC_SET_CAP = uint32(C.IOC_SET_CAP)
	IOC_GET_CAP = uint32(C.IOC_GET_CAP)
	IOC_DEL_CAP = uint32(C.IOC_DEL_CAP)

	IOC_SEL_ADD_RULE = uint32(C.IOC_SEL_ADD_RULE)
	IOC_SET_PROFILE  = uint32(C.IOC_SET_PROFILE)
)

func ioctl(
	fd int,
	cmd uint32,
	arg unsafe.Pointer,
) error {

	r := C.bioctl(
		C.int(fd),
		C.ulong(cmd),
		arg,
	)

	if r < 0 {
		return fmt.Errorf("ioctl failed")
	}

	return nil
}

func prctl1(op uint32) (int, error) {
	rop := uintptr(op + 200)
	r, _, errno := syscall.Syscall(syscall.SYS_PRCTL, rop, 0, 0)
	if errno != 0 {
		return 0, errno
	}
	return int(r), nil
}

func Ctl(code Opcode) error {
	switch code {
	case OpcodeAuthenticate, OpcodeGetRoot, OpcodeIoctl:
		_, err := prctl1(uint32(code))
		return err
	default:
		return fmt.Errorf("unknown opcode: %d", code)
	}
}

func copyToCChar64(dst *[64]C.char, s string) {
	for i := 0; i < 64; i++ {
		if i < len(s) {
			dst[i] = C.char(s[i])
		} else {
			dst[i] = 0
			break
		}
	}
	dst[63] = 0
}

func SetProfile(fd int, uid int, caps uint64, domain string, namespace int) error {
	var data C.struct_nksu_profile_data

	data.uid = C.uint(uint32(uid))
	data.caps = C.uint64_t(caps)
	copyToCChar64(&data.selinux_domain, domain)
	data.namespace = C.int(int32(namespace))

	return ioctl(fd, IOC_SET_PROFILE, unsafe.Pointer(&data))
}

func AddUid(fd int, uid int) error {
	if uid < 0 {
		return fmt.Errorf("invalid uid")
	}
	val := uint32(uid)
	return ioctl(fd, IOC_ADD_UID, unsafe.Pointer(&val))
}

func DelUid(fd int, uid int) error {
	if uid < 0 {
		return fmt.Errorf("invalid uid")
	}
	val := uint32(uid)
	return ioctl(fd, IOC_DEL_UID, unsafe.Pointer(&val))
}

func HasUid(fd int, uid int) (bool, error) {
	if uid < 0 {
		return false, fmt.Errorf("invalid uid")
	}
	val := uint32(uid)
	if err := ioctl(fd, IOC_HAS_UID, unsafe.Pointer(&val)); err != nil {
		return false, err
	}
	return val != 0, nil
}

func SetCap(fd int, uid int, caps uint64) error {
	var uc C.struct_fmac_uid_cap
	uc.uid = C.uint(uid)
	uc.caps = C.uint64_t(caps)
	return ioctl(fd, IOC_SET_CAP, unsafe.Pointer(&uc))
}

func GetCap(fd int, uid int) (uint64, error) {
	var uc C.struct_fmac_uid_cap
	uc.uid = C.uint(uid)
	if err := ioctl(fd, IOC_GET_CAP, unsafe.Pointer(&uc)); err != nil {
		return 0, err
	}
	return uint64(uc.caps), nil
}

func DelCap(fd int, uid int) error {
	var uc C.struct_fmac_uid_cap
	uc.uid = C.uint(uid)
	return ioctl(fd, IOC_DEL_CAP, unsafe.Pointer(&uc))
}

func AddSelinuxRule(fd int, src, tgt, cls, perm string, effect int, invert bool) error {
	var r C.struct_fmac_sepolicy_rule

	copyToCChar64(&r.src, src)
	copyToCChar64(&r.tgt, tgt)
	copyToCChar64(&r.cls, cls)
	copyToCChar64(&r.perm, perm)

	r.effect = C.int(effect)
	if invert {
		r.invert = 1
	} else {
		r.invert = 0
	}
	return ioctl(fd, IOC_SEL_ADD_RULE, unsafe.Pointer(&r))
}

func ScanDriverFd() (int, error) {
	return scanFdByLink("[fmac_shm]")
}

func ScanCtlFd() (int, error) {
	return scanFdByLink("[fmac_ctl]")
}

func scanFdByLink(target string) (int, error) {
	dir, err := os.Open("/proc/self/fd")
	if err != nil {
		return -1, err
	}
	defer dir.Close()

	entries, err := dir.Readdirnames(-1)
	if err != nil {
		return -1, err
	}

	for _, name := range entries {
		fdNum, err := strconv.Atoi(name)
		if err != nil {
			continue
		}
		link, err := os.Readlink(filepath.Join("/proc/self/fd", name))
		if err != nil {
			continue
		}
		if contains(link, target) {
			return fdNum, nil
		}
	}
	return -1, fmt.Errorf("fd not found: %s", target)
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && (s == sub || len(s) > 0 && containsStr(s, sub))
}

func containsStr(s, sub string) bool {
	for i := 0; i <= len(s)-len(sub); i++ {
		if s[i:i+len(sub)] == sub {
			return true
		}
	}
	return false
}

type Event struct {
	fd int
}

func NewEvent() (Event, error) {
	fd, _, errno := syscall.Syscall(syscall.SYS_EVENTFD2, 0, syscall.O_CLOEXEC, 0)
	if errno != 0 {
		return Event{}, errno
	}
	return Event{fd: int(fd)}, nil
}

func (e Event) Close() {
	syscall.Close(e.fd)
}

func (e Event) Wait() (uint64, error) {
	pfd := []unix.PollFd{
		{Fd: int32(e.fd), Events: unix.POLLIN},
	}
	for {
		n, err := unix.Poll(pfd, -1)
		if err != nil || n <= 0 {
			continue
		}
		var val uint64
		buf := (*[8]byte)(unsafe.Pointer(&val))
		nr, err := unix.Read(e.fd, buf[:])
		if err != nil || nr != 8 {
			continue
		}
		return val, nil
	}
}

func (e Event) WaitTimeout(timeoutMs int) (int64, error) {
	pfd := []unix.PollFd{
		{Fd: int32(e.fd), Events: unix.POLLIN},
	}

	n, err := unix.Poll(pfd, timeoutMs)
	if err != nil || n <= 0 {
		return -1, err
	}

	var val uint64
	buf := (*[8]byte)(unsafe.Pointer(&val))
	nr, err := unix.Read(e.fd, buf[:])
	if err != nil || nr != 8 {
		return -1, fmt.Errorf("read error")
	}
	return int64(val), nil
}
