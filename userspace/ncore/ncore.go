package main

import (
	"context"
	"fmt"
	"log"
	"os"

	"github.com/urfave/cli/v3"
	"nekosu/ncore/boot"
	"nekosu/ncore/kmod"
	"nekosu/ncore/module"
	"nekosu/ncore/patcher"
)

func main() {
	cmd := &cli.Command{
		Name:  "ncore",
		Usage: "nekosu userspace tools.",
		Commands: []*cli.Command{
			{
				Name:      "load",
				Usage:     "load kernel module",
				ArgsUsage: "<path>",
				Action: func(ctx context.Context, cmd *cli.Command) error {
					if cmd.Args().Len() == 0 {
						return fmt.Errorf("path required")
					}
					return kmod.Load(cmd.Args().First())
				},
			},
			{
				Name:  "boot",
				Usage: "boot image tools",
				Commands: []*cli.Command{
					{
						Name:      "unpack",
						Usage:     "unpack boot image",
						ArgsUsage: "<image> <outdir>",
						Action: func(ctx context.Context, cmd *cli.Command) error {
							if cmd.Args().Len() < 2 {
								return fmt.Errorf("usage: unpack <image> <outdir>")
							}
							return bootUnpack(cmd.Args().Get(0), cmd.Args().Get(1))
						},
					},
					{
						Name:      "repack",
						Usage:     "repack boot image from unpacked dir",
						ArgsUsage: "<original> <indir> <output>",
						Action: func(ctx context.Context, cmd *cli.Command) error {
							if cmd.Args().Len() < 3 {
								return fmt.Errorf("usage: repack <original> <indir> <output>")
							}
							return bootRepack(cmd.Args().Get(0), cmd.Args().Get(1), cmd.Args().Get(2))
						},
					},
					{
						Name:      "patch",
						Usage:     "patch boot image: backup old init to init.real and inject new init",
						ArgsUsage: "<image> <new_init_path> <output>",
						Action: func(ctx context.Context, cmd *cli.Command) error {
							if cmd.Args().Len() < 3 {
								return fmt.Errorf("usage: patch <image> <new_init_path> <output>")
							}
							return bootPatch(cmd.Args().Get(0), cmd.Args().Get(1), cmd.Args().Get(2))
						},
					},
					{
						Name:      "info",
						Usage:     "print boot image header info",
						ArgsUsage: "<image>",
						Action: func(ctx context.Context, cmd *cli.Command) error {
							if cmd.Args().Len() == 0 {
								return fmt.Errorf("image path required")
							}
							return bootInfo(cmd.Args().First())
						},
					},
				},
			},
			{
				Name:  "mod",
				Usage: "load userspace module",
				Commands: []*cli.Command{
					{
						Name:  "show",
						Usage: "show all installed module.",
						Action: func(ctx context.Context, cmd *cli.Command) error {
							return module.ShowModules()
						},
					},
					{
						Name:  "run",
						Usage: "run modules",
						Action: func(ctx context.Context, cmd *cli.Command) error {
							return module.RunModules()
						},
					},
				},
			},
		},
	}

	if err := cmd.Run(context.Background(), os.Args); err != nil {
		log.Fatal(err)
	}
}

func bootUnpack(imgPath, outDir string) error {
	buf, err := os.ReadFile(imgPath)
	if err != nil {
		return fmt.Errorf("read image: %w", err)
	}

	unpacked, err := boot.UnpackBootImage(buf)
	if err != nil {
		return fmt.Errorf("unpack: %w", err)
	}

	if err := os.MkdirAll(outDir, 0755); err != nil {
		return err
	}

	files := map[string][]byte{
		"kernel":        unpacked.Kernel,
		"ramdisk":       unpacked.Ramdisk,
		"second":        unpacked.Second,
		"recovery_dtbo": unpacked.RecoveryDtbo,
		"dtb":           unpacked.Dtb,
		"signature":     unpacked.Signature,
	}
	for name, data := range files {
		if len(data) == 0 {
			continue
		}
		path := outDir + "/" + name
		if err := os.WriteFile(path, data, 0644); err != nil {
			return fmt.Errorf("write %s: %w", name, err)
		}
		fmt.Printf("wrote %s (%d bytes)\n", path, len(data))
	}
	return nil
}

func bootRepack(origPath, inDir, outPath string) error {
	origBuf, err := os.ReadFile(origPath)
	if err != nil {
		return fmt.Errorf("read original: %w", err)
	}

	readOptional := func(name string) []byte {
		data, _ := os.ReadFile(inDir + "/" + name)
		return data
	}

	unpacked := &boot.UnpackedBoot{
		Kernel:       readOptional("kernel"),
		Ramdisk:      readOptional("ramdisk"),
		Second:       readOptional("second"),
		RecoveryDtbo: readOptional("recovery_dtbo"),
		Dtb:          readOptional("dtb"),
		Signature:    readOptional("signature"),
	}

	f, err := os.OpenFile(outPath, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		return fmt.Errorf("create output: %w", err)
	}
	defer f.Close()

	if err := boot.RepackBootImage(f, origBuf, unpacked); err != nil {
		return fmt.Errorf("repack: %w", err)
	}

	fmt.Printf("wrote %s\n", outPath)
	return nil
}

func bootPatch(origPath, newInitPath, outPath string) error {
	newInitBytes, err := os.ReadFile(newInitPath)
	if err != nil {
		return fmt.Errorf("read new init file: %w", err)
	}

	bp, err := patcher.OpenBootImage(origPath)
	if err != nil {
		return fmt.Errorf("open boot image: %w", err)
	}

	if err := bp.ReplaceInitAndBackupOld(newInitBytes); err != nil {
		return fmt.Errorf("replace and backup init: %w", err)
	}

	if err := bp.Save(outPath); err != nil {
		return fmt.Errorf("save patched image: %w", err)
	}

	return nil
}

func bootInfo(imgPath string) error {
	buf, err := os.ReadFile(imgPath)
	if err != nil {
		return fmt.Errorf("read image: %w", err)
	}

	img, err := boot.ParseBootImage(buf)
	if err != nil {
		return fmt.Errorf("parse: %w", err)
	}

	fmt.Printf("header_version: %d\n", img.Version)

	switch img.Version {
	case boot.BootV0, boot.BootV1, boot.BootV2:
		h := img.V0()
		fmt.Printf("page_size:      %d\n", h.PageSize)
		fmt.Printf("kernel_size:    %d\n", h.KernelSize)
		fmt.Printf("ramdisk_size:   %d\n", h.RamdiskSize)
		fmt.Printf("second_size:    %d\n", h.SecondSize)
		fmt.Printf("os_version:     0x%08x\n", h.OsVersion)
		fmt.Printf("name:           %s\n", nullStr(h.Name[:]))
		fmt.Printf("cmdline:        %s\n", nullStr(h.Cmdline[:]))
		if img.Version >= boot.BootV1 {
			fmt.Printf("recovery_dtbo_size: %d\n", img.V1().RecoveryDtboSize)
		}
		if img.Version == boot.BootV2 {
			fmt.Printf("dtb_size:       %d\n", img.V2().DtbSize)
		}
	case boot.BootV3, boot.BootV4:
		h := img.V3()
		fmt.Printf("header_size:    %d\n", h.HeaderSize)
		fmt.Printf("kernel_size:    %d\n", h.KernelSize)
		fmt.Printf("ramdisk_size:   %d\n", h.RamdiskSize)
		fmt.Printf("os_version:     0x%08x\n", h.OsVersion)
		fmt.Printf("cmdline:        %s\n", nullStr(h.Cmdline[:]))
		if img.Version == boot.BootV4 {
			fmt.Printf("signature_size: %d\n", img.V4().SignatureSize)
		}
	}
	return nil
}

func nullStr(b []byte) string {
	for i, c := range b {
		if c == 0 {
			return string(b[:i])
		}
	}
	return string(b)
}
