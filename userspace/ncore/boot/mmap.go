//go:build linux

package boot

import (
	"golang.org/x/sys/unix"
	"os"
)

func mmapFile(f *os.File, size int) ([]byte, error) {
	return unix.Mmap(int(f.Fd()), 0, size,
		unix.PROT_READ|unix.PROT_WRITE, unix.MAP_SHARED)
}

func munmapFile(b []byte) {
	unix.Munmap(b)
}

func madviseSequential(b []byte) {
	unix.Madvise(b, unix.MADV_SEQUENTIAL)
}
