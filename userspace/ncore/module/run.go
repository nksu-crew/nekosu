package module

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
)

func Run(paths []string) {
	var wg sync.WaitGroup
	for _, path := range paths {
		wg.Add(1)
		go func(p string) {
			defer wg.Done()
			script := filepath.Join(mod_path, p, "service.sh")
			cmd := exec.Command("/system/bin/sh", script)
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				fmt.Fprintf(os.Stderr, "[%s] error: %v\n", script, err)
			}
		}(path)
	}
	wg.Wait()
}
