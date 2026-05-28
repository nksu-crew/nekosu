package boot

import (
	"encoding/binary"
	"errors"
	"os"
	"unsafe"
)

var (
	ErrBufferTooSmall    = errors.New("buffer too small")
	ErrBadMagic          = errors.New("bad magic")
	ErrUnexpectedVersion = errors.New("unexpected version")
)

type BootVersion int

const (
	BootV0 BootVersion = 0
	BootV1 BootVersion = 1
	BootV2 BootVersion = 2
	BootV3 BootVersion = 3
	BootV4 BootVersion = 4
)

type BootImage struct {
	Version BootVersion
	buf     []byte // original buffer, headers parsed from it
}

func (b *BootImage) V0() *BootImgHdrV0 {
	return (*BootImgHdrV0)(unsafe.Pointer(&b.buf[0]))
}
func (b *BootImage) V1() *BootImgHdrV1 {
	return (*BootImgHdrV1)(unsafe.Pointer(&b.buf[0]))
}
func (b *BootImage) V2() *BootImgHdrV2 {
	return (*BootImgHdrV2)(unsafe.Pointer(&b.buf[0]))
}
func (b *BootImage) V3() *BootImgHdrV3 {
	return (*BootImgHdrV3)(unsafe.Pointer(&b.buf[0]))
}
func (b *BootImage) V4() *BootImgHdrV4 {
	return (*BootImgHdrV4)(unsafe.Pointer(&b.buf[0]))
}

type UnpackedBoot struct {
	Kernel       []byte
	Ramdisk      []byte
	Second       []byte
	RecoveryDtbo []byte
	Dtb          []byte
	Signature    []byte
}

func alignSize(size, pageSize uintptr) uintptr {
	if pageSize == 0 {
		return size
	}
	return (size + pageSize - 1) &^ (pageSize - 1)
}

func getSection(buf []byte, offset *uintptr, size uint64, pageSize uintptr) ([]byte, error) {
	if size == 0 {
		return nil, nil
	}
	start := *offset
	end := start + uintptr(size)
	if end > uintptr(len(buf)) {
		return nil, ErrBufferTooSmall
	}
	section := buf[start:end]
	*offset += alignSize(uintptr(size), pageSize)
	return section, nil
}

func writeSection(dst, src []byte, pageSize uintptr) uintptr {
	if len(src) == 0 {
		return 0
	}
	copy(dst, src)
	return alignSize(uintptr(len(src)), pageSize)
}

func checkSize(buf []byte, need uintptr) error {
	if uintptr(len(buf)) < need {
		return ErrBufferTooSmall
	}
	return nil
}

func le64(b [8]byte) uint64 {
	return binary.LittleEndian.Uint64(b[:])
}

func ParseBootImage(buf []byte) (*BootImage, error) {
	if uintptr(len(buf)) < unsafe.Sizeof(BootImgHdrV3{}) {
		return nil, ErrBufferTooSmall
	}

	hdr := (*BootImgHdrV3)(unsafe.Pointer(&buf[0]))
	if string(hdr.Magic[:BootMagicSize]) != BootMagic {
		return nil, ErrBadMagic
	}

	ver := BootVersion(hdr.HeaderVersion)
	var need uintptr
	switch ver {
	case BootV0:
		need = unsafe.Sizeof(BootImgHdrV0{})
	case BootV1:
		need = unsafe.Sizeof(BootImgHdrV1{})
	case BootV2:
		need = unsafe.Sizeof(BootImgHdrV2{})
	case BootV3:
		need = unsafe.Sizeof(BootImgHdrV3{})
	case BootV4:
		need = unsafe.Sizeof(BootImgHdrV4{})
	default:
		return nil, ErrUnexpectedVersion
	}

	if err := checkSize(buf, need); err != nil {
		return nil, err
	}

	return &BootImage{Version: ver, buf: buf}, nil
}

func UnpackBootImage(buf []byte) (*UnpackedBoot, error) {
	img, err := ParseBootImage(buf)
	if err != nil {
		return nil, err
	}

	unpacked := &UnpackedBoot{}
	var offset uintptr

	switch img.Version {
	case BootV0, BootV1, BootV2:
		v0 := img.V0()
		pageSize := uintptr(v0.PageSize)
		offset = pageSize

		if unpacked.Kernel, err = getSection(buf, &offset, uint64(v0.KernelSize), pageSize); err != nil {
			return nil, err
		}
		if unpacked.Ramdisk, err = getSection(buf, &offset, uint64(v0.RamdiskSize), pageSize); err != nil {
			return nil, err
		}
		if unpacked.Second, err = getSection(buf, &offset, uint64(v0.SecondSize), pageSize); err != nil {
			return nil, err
		}

		if img.Version >= BootV1 {
			v1 := img.V1()
			if unpacked.RecoveryDtbo, err = getSection(buf, &offset, uint64(v1.RecoveryDtboSize), pageSize); err != nil {
				return nil, err
			}
		}
		if img.Version == BootV2 {
			v2 := img.V2()
			if unpacked.Dtb, err = getSection(buf, &offset, uint64(v2.DtbSize), pageSize); err != nil {
				return nil, err
			}
		}

	case BootV3, BootV4:
		v3 := img.V3()
		const pageSize uintptr = 4096
		offset = alignSize(uintptr(v3.HeaderSize), pageSize)

		if unpacked.Kernel, err = getSection(buf, &offset, uint64(v3.KernelSize), pageSize); err != nil {
			return nil, err
		}
		if unpacked.Ramdisk, err = getSection(buf, &offset, uint64(v3.RamdiskSize), pageSize); err != nil {
			return nil, err
		}

		if img.Version == BootV4 {
			v4 := img.V4()
			if unpacked.Signature, err = getSection(buf, &offset, uint64(v4.SignatureSize), pageSize); err != nil {
				return nil, err
			}
		}
	}

	return unpacked, nil
}

func RepackBootImage(f *os.File, originalBuf []byte, unpacked *UnpackedBoot) error {
	img, err := ParseBootImage(originalBuf)
	if err != nil {
		return err
	}

	var totalSize uintptr
	var pageSize uintptr

	switch img.Version {
	case BootV0, BootV1, BootV2:
		v0 := img.V0()
		pageSize = uintptr(v0.PageSize)
		totalSize += pageSize
		totalSize += alignSize(uintptr(len(unpacked.Kernel)), pageSize)
		totalSize += alignSize(uintptr(len(unpacked.Ramdisk)), pageSize)
		totalSize += alignSize(uintptr(len(unpacked.Second)), pageSize)
		if img.Version >= BootV1 {
			totalSize += alignSize(uintptr(len(unpacked.RecoveryDtbo)), pageSize)
		}
		if img.Version == BootV2 {
			totalSize += alignSize(uintptr(len(unpacked.Dtb)), pageSize)
		}

	case BootV3, BootV4:
		v3 := img.V3()
		pageSize = 4096
		totalSize += alignSize(uintptr(v3.HeaderSize), pageSize)
		totalSize += alignSize(uintptr(len(unpacked.Kernel)), pageSize)
		totalSize += alignSize(uintptr(len(unpacked.Ramdisk)), pageSize)
		if img.Version == BootV4 {
			totalSize += alignSize(uintptr(len(unpacked.Signature)), pageSize)
		}
	}

	if err := f.Truncate(int64(totalSize)); err != nil {
		return err
	}

	out, err := mmapFile(f, int(totalSize))
	if err != nil {
		return err
	}
	defer munmapFile(out)

	if totalSize > 1024*1024 {
		madviseSequential(out)
	}

	var offset uintptr

	switch img.Version {
	case BootV0, BootV1, BootV2:
		var hdrLen uintptr
		switch img.Version {
		case BootV0:
			hdrLen = unsafe.Sizeof(BootImgHdrV0{})
		case BootV1:
			hdrLen = unsafe.Sizeof(BootImgHdrV1{})
		case BootV2:
			hdrLen = unsafe.Sizeof(BootImgHdrV2{})
		}

		copy(out[:hdrLen], originalBuf[:hdrLen])

		outV0 := (*BootImgHdrV0)(unsafe.Pointer(&out[0]))
		outV0.KernelSize = uint32(len(unpacked.Kernel))
		outV0.RamdiskSize = uint32(len(unpacked.Ramdisk))
		outV0.SecondSize = uint32(len(unpacked.Second))

		if img.Version >= BootV1 {
			outV1 := (*BootImgHdrV1)(unsafe.Pointer(&out[0]))
			outV1.RecoveryDtboSize = uint32(len(unpacked.RecoveryDtbo))
		}
		if img.Version == BootV2 {
			outV2 := (*BootImgHdrV2)(unsafe.Pointer(&out[0]))
			outV2.DtbSize = uint32(len(unpacked.Dtb))
		}

		offset = pageSize
		offset += writeSection(out[offset:], unpacked.Kernel, pageSize)
		offset += writeSection(out[offset:], unpacked.Ramdisk, pageSize)
		offset += writeSection(out[offset:], unpacked.Second, pageSize)
		if img.Version >= BootV1 {
			offset += writeSection(out[offset:], unpacked.RecoveryDtbo, pageSize)
		}
		if img.Version == BootV2 {
			offset += writeSection(out[offset:], unpacked.Dtb, pageSize)
		}

	case BootV3, BootV4:
		var hdrLen uintptr
		if img.Version == BootV3 {
			hdrLen = unsafe.Sizeof(BootImgHdrV3{})
		} else {
			hdrLen = unsafe.Sizeof(BootImgHdrV4{})
		}

		copy(out[:hdrLen], originalBuf[:hdrLen])

		outV3 := (*BootImgHdrV3)(unsafe.Pointer(&out[0]))
		outV3.KernelSize = uint32(len(unpacked.Kernel))
		outV3.RamdiskSize = uint32(len(unpacked.Ramdisk))

		if img.Version == BootV4 {
			outV4 := (*BootImgHdrV4)(unsafe.Pointer(&out[0]))
			outV4.SignatureSize = uint32(len(unpacked.Signature))
		}

		v3 := img.V3()
		offset = alignSize(uintptr(v3.HeaderSize), pageSize)

		offset += writeSection(out[offset:], unpacked.Kernel, pageSize)
		offset += writeSection(out[offset:], unpacked.Ramdisk, pageSize)
		if img.Version == BootV4 {
			offset += writeSection(out[offset:], unpacked.Signature, pageSize)
		}
	}

	_ = offset
	return nil
}
