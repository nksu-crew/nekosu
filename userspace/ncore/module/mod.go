package module

import (
	"fmt"
	"os"
)

const mod_path = "/data/adb/ncore"

func ListModules() ([]string, error) {
	entries, err := os.ReadDir(mod_path)
	if err != nil {
		return nil, err
	}

	var modules []string

	for _, entry := range entries {
		if entry.IsDir() {
			modules = append(modules, entry.Name())
		}
	}

	return modules, nil
}

func RunModules() error {
	mod, err := ListModules()
	if err != nil {
		return err
	}
	Run(mod)
	return nil
}

func ShowModules() error {
	mod, err := ListModules()
	if err != nil {
		return err
	}
	for _, mod := range mod {
		info, err := GetModuleInfo(mod)
		if err != nil {
			return err
		}
		fmt.Println("{")
		fmt.Println("id: " + info["id"])
		fmt.Println("name: " + info["name"])
		fmt.Println("version: " + info["version"])
		fmt.Println("description: " + info["description"])
		fmt.Println("}")
	}
	return nil
}
