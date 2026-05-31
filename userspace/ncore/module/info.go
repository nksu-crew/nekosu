package module

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"

	"golang.org/x/sys/unix"
)

func GetModuleInfo(mpath string) (map[string]string, error) {

	file, err := os.Open(
		filepath.Join(mod_path, mpath, "module.prop"),
	)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	info, err := file.Stat()
	if err != nil {
		return nil, err
	}

	size := int(info.Size())

	if size == 0 {
		return nil, fmt.Errorf("empty file")
	}

	data, err := unix.Mmap(
		int(file.Fd()),
		0,
		size,
		unix.PROT_READ,
		unix.MAP_PRIVATE,
	)
	if err != nil {
		return nil, err
	}
	defer unix.Munmap(data)

	props := make(map[string]string)

	start := 0

	parse := func(line []byte) {

		// 去除 \r
		line = bytes.TrimSuffix(line, []byte{'\r'})

		// 空行
		if len(line) == 0 {
			return
		}

		// 注释
		if line[0] == '#' {
			return
		}

		// 查找 =
		idx := bytes.IndexByte(line, '=')
		if idx == -1 {
			return
		}

		key := string(line[:idx])
		value := string(line[idx+1:])

		props[key] = value
	}

	for i, b := range data {

		if b != '\n' {
			continue
		}

		parse(data[start:i])

		start = i + 1
	}
	if start < len(data) {
		parse(data[start:])
	}

	return props, nil
}
