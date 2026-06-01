package patcher

import (
	"bytes"
	"fmt"
	"io"

	"github.com/cavaliergopher/cpio"
)

func (bp *BootPatcher) ReplaceInitAndBackupOld(newInitBytes []byte) error {
	inReader := bytes.NewReader(bp.CPIO)
	cpioReader := cpio.NewReader(inReader)

	var outBuf bytes.Buffer
	cpioWriter := cpio.NewWriter(&outBuf)

	found := false

	for {
		hdr, err := cpioReader.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			return fmt.Errorf("read cpio header: %w", err)
		}

		if hdr.Name == "init" || hdr.Name == "./init" {
			found = true
			origName := hdr.Name

			realName := "init.real"
			if origName == "./init" {
				realName = "./init.real"
			}

			fmt.Printf("Found %s: renaming to %s and injecting new init (size: %d)\n", origName, realName, len(newInitBytes))

			hdrReal := *hdr
			hdrReal.Name = realName

			if err := cpioWriter.WriteHeader(&hdrReal); err != nil {
				return fmt.Errorf("write init.real header: %w", err)
			}
			if hdr.Size > 0 {
				if _, err := io.Copy(cpioWriter, cpioReader); err != nil {
					return fmt.Errorf("copy original init data to init.real: %w", err)
				}
			}

			hdrNew := *hdr
			hdrNew.Name = origName
			hdrNew.Size = int64(len(newInitBytes))

			if err := cpioWriter.WriteHeader(&hdrNew); err != nil {
				return fmt.Errorf("write new init header: %w", err)
			}
			if _, err := cpioWriter.Write(newInitBytes); err != nil {
				return fmt.Errorf("write new init data: %w", err)
			}

		} else {
			if err := cpioWriter.WriteHeader(hdr); err != nil {
				return fmt.Errorf("write header %s: %w", hdr.Name, err)
			}
			if hdr.Size > 0 {
				if _, err := io.Copy(cpioWriter, cpioReader); err != nil {
					return fmt.Errorf("copy data %s: %w", hdr.Name, err)
				}
			}
		}
	}

	if !found {
		return fmt.Errorf("init file not found in ramdisk")
	}

	if err := cpioWriter.Close(); err != nil {
		return fmt.Errorf("close cpio writer: %w", err)
	}

	bp.CPIO = outBuf.Bytes()
	return nil
}
