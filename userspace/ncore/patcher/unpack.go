package patcher

import (
	"fmt"
	"os"

	"nekosu/ncore/boot"
)

type BootPatcher struct {
	OrigBuf  []byte
	Unpacked *boot.UnpackedBoot
	Format   *FormatEntry
	CPIO     []byte
}

func OpenBootImage(origPath string) (*BootPatcher, error) {
	origBuf, err := os.ReadFile(origPath)
	if err != nil {
		return nil, fmt.Errorf("read image: %w", err)
	}

	unpacked, err := boot.UnpackBootImage(origBuf)
	if err != nil {
		return nil, fmt.Errorf("unpack boot image: %w", err)
	}
	fmt.Printf("ramdisk size    : %d bytes\n", len(unpacked.Ramdisk))

	if len(unpacked.Ramdisk) < 4 {
		return nil, fmt.Errorf("ramdisk too small (%d bytes)", len(unpacked.Ramdisk))
	}

	cpio, fmtEntry, err := Decompress(unpacked.Ramdisk)
	if err != nil {
		return nil, fmt.Errorf("decompress ramdisk: %w", err)
	}
	fmt.Printf("format          : %s (%s)\n", fmtEntry.Name, fmtEntry.Desc)
	fmt.Printf("cpio size       : %d bytes\n", len(cpio))

	return &BootPatcher{
		OrigBuf:  origBuf,
		Unpacked: unpacked,
		Format:   fmtEntry,
		CPIO:     cpio,
	}, nil
}

func (bp *BootPatcher) Save(outPath string) error {
	fmt.Printf("patched cpio    : %d bytes\n", len(bp.CPIO))

	newRamdisk, err := Compress(bp.CPIO, bp.Format)
	if err != nil {
		return fmt.Errorf("compress ramdisk: %w", err)
	}
	fmt.Printf("new ramdisk size: %d bytes\n", len(newRamdisk))

	bp.Unpacked.Ramdisk = newRamdisk

	f, err := os.OpenFile(outPath, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("create output: %w", err)
	}
	defer f.Close()

	if err := boot.RepackBootImage(f, bp.OrigBuf, bp.Unpacked); err != nil {
		return fmt.Errorf("repack: %w", err)
	}

	fmt.Printf("wrote           : %s\n", outPath)
	return nil
}
