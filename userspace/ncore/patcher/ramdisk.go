package patcher

import (
	"bytes"
	"compress/gzip"
	"fmt"
	"io"
	"os"

	"github.com/klauspost/compress/zstd"
	"github.com/pierrec/lz4/v4"
)

var (
	magicGzip   = []byte{0x1f, 0x8b}
	magicBzip2  = []byte{0x42, 0x5a, 0x68}
	magicXZ     = []byte{0xfd, 0x37, 0x7a, 0x58, 0x5a, 0x00}
	magicLZMA   = []byte{0x5d, 0x00, 0x00, 0x00}
	magicLZO    = []byte{0x89, 0x4c, 0x5a, 0x4f, 0x00, 0x0d, 0x0a, 0x1a, 0x0a}
	magicLZ4Leg = []byte{0x02, 0x21, 0x4c, 0x18}
	magicLZ4Frm = []byte{0x04, 0x22, 0x4d, 0x18}
	magicZSTD   = []byte{0x28, 0xb5, 0x2f, 0xfd}

	magicCPIONewc    = []byte{0x30, 0x37, 0x30, 0x37, 0x30, 0x31} // "070701"
	magicCPIONewcCRC = []byte{0x30, 0x37, 0x30, 0x37, 0x30, 0x32} // "070702"
	magicCPIOBin     = []byte{0xc7, 0x71}
)

type FormatEntry struct {
	Name      string
	Desc      string
	Magic     []byte
	KernelCfg string
}

var formats = []FormatEntry{
	{Name: "gzip", Desc: "GZIP compressed", Magic: magicGzip, KernelCfg: "CONFIG_RD_GZIP"},
	{Name: "bzip2", Desc: "BZIP2 compressed", Magic: magicBzip2, KernelCfg: "CONFIG_RD_BZIP2"},
	{Name: "xz", Desc: "XZ compressed", Magic: magicXZ, KernelCfg: "CONFIG_RD_XZ"},
	{Name: "lzma", Desc: "LZMA compressed", Magic: magicLZMA, KernelCfg: "CONFIG_RD_LZMA"},
	{Name: "lzo", Desc: "LZO compressed", Magic: magicLZO, KernelCfg: "CONFIG_RD_LZO"},
	{Name: "lz4-legacy", Desc: "LZ4 compressed (legacy)", Magic: magicLZ4Leg, KernelCfg: "CONFIG_RD_LZ4"},
	{Name: "lz4-framed", Desc: "LZ4 compressed (framed)", Magic: magicLZ4Frm, KernelCfg: "CONFIG_RD_LZ4"},
	{Name: "zstd", Desc: "Zstandard compressed", Magic: magicZSTD, KernelCfg: "CONFIG_RD_ZSTD"},
	{Name: "cpio-newc", Desc: "CPIO archive (newc, uncompressed)", Magic: magicCPIONewc},
	{Name: "cpio-newc-crc", Desc: "CPIO archive (newc+crc)", Magic: magicCPIONewcCRC},
	{Name: "cpio-binary", Desc: "CPIO archive (binary)", Magic: magicCPIOBin},
}

func matchMagic(buf, magic []byte) bool {
	if len(magic) > len(buf) {
		return false
	}
	return bytes.Equal(buf[:len(magic)], magic)
}

func printHexDump(buf []byte, max int) {
	n := len(buf)
	if n > max {
		n = max
	}
	fmt.Printf("  Hex dump (first %d bytes): ", n)
	for i := 0; i < n; i++ {
		fmt.Printf("%02x ", buf[i])
	}
	fmt.Println()
}

func DetectFormat(buf []byte) *FormatEntry {
	if len(buf) > 32 {
		buf = buf[:32]
	}
	for i := range formats {
		if matchMagic(buf, formats[i].Magic) {
			return &formats[i]
		}
	}
	return nil
}

func Detect(buf []byte) int {
	if len(buf) == 0 {
		fmt.Fprintln(os.Stderr, "Error: buffer is empty")
		return 1
	}
	if len(buf) > 32 {
		buf = buf[:32]
	}

	fmt.Println("=== initramfs Format Detector ===")
	printHexDump(buf, 16)
	fmt.Println()

	matched := false
	for _, e := range formats {
		if matchMagic(buf, e.Magic) {
			fmt.Printf("[MATCH] %-14s  %s\n", e.Name, e.Desc)
			if e.KernelCfg != "" {
				fmt.Printf("        Kernel config : %s=y\n", e.KernelCfg)
			} else {
				fmt.Println("        Kernel config : none needed (raw CPIO)")
			}
			matched = true
		}
	}
	if !matched {
		fmt.Println("[UNKNOWN] Cannot identify format.")
		fmt.Println("  Possible: encrypted / custom / corrupted")
		return 2
	}
	return 0
}

func Decompress(buf []byte) ([]byte, *FormatEntry, error) {
	if len(buf) < 2 {
		return nil, nil, fmt.Errorf("buffer too small")
	}

	entry := DetectFormat(buf)
	if entry == nil {
		return nil, nil, fmt.Errorf("unknown ramdisk format")
	}

	r := bytes.NewReader(buf)

	switch entry.Name {
	case "gzip":
		gr, err := gzip.NewReader(r)
		if err != nil {
			return nil, entry, fmt.Errorf("gzip open: %w", err)
		}
		defer gr.Close()
		out, err := io.ReadAll(gr)
		if err != nil {
			return nil, entry, fmt.Errorf("gzip decompress: %w", err)
		}
		return out, entry, nil

	case "zstd":
		dec, err := zstd.NewReader(r)
		if err != nil {
			return nil, entry, fmt.Errorf("zstd open: %w", err)
		}
		defer dec.Close()
		out, err := io.ReadAll(dec)
		if err != nil {
			return nil, entry, fmt.Errorf("zstd decompress: %w", err)
		}
		return out, entry, nil

    case "lz4-framed", "lz4-legacy":
		lr := lz4.NewReader(r)
		out, err := io.ReadAll(lr)
		if err != nil {
			return nil, entry, fmt.Errorf("lz4 decompress: %w", err)
		}
		return out, entry, nil

	case "cpio-newc", "cpio-newc-crc", "cpio-binary":
		out := make([]byte, len(buf))
		copy(out, buf)
		return out, entry, nil

	default:
		return nil, entry, fmt.Errorf("decompress not implemented for format: %s", entry.Name)
	}
}

func Compress(cpio []byte, entry *FormatEntry) ([]byte, error) {
	var buf bytes.Buffer

	switch entry.Name {
	case "gzip":
		gw := gzip.NewWriter(&buf)
		if _, err := gw.Write(cpio); err != nil {
			return nil, fmt.Errorf("gzip write: %w", err)
		}
		if err := gw.Close(); err != nil {
			return nil, fmt.Errorf("gzip close: %w", err)
		}

	case "zstd":
		enc, err := zstd.NewWriter(&buf)
		if err != nil {
			return nil, fmt.Errorf("zstd open: %w", err)
		}
		if _, err := enc.Write(cpio); err != nil {
			return nil, fmt.Errorf("zstd write: %w", err)
		}
		if err := enc.Close(); err != nil {
			return nil, fmt.Errorf("zstd close: %w", err)
		}

	case "lz4-framed", "lz4-legacy":
		lw := lz4.NewWriter(&buf)
		if _, err := lw.Write(cpio); err != nil {
			return nil, fmt.Errorf("lz4 write: %w", err)
		}
		if err := lw.Close(); err != nil {
			return nil, fmt.Errorf("lz4 close: %w", err)
		}

	case "cpio-newc", "cpio-newc-crc", "cpio-binary":
		return cpio, nil

	default:
		return nil, fmt.Errorf("compress not implemented for format: %s", entry.Name)
	}

	return buf.Bytes(), nil
}
