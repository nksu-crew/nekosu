package rd_init

import (
	"log"
	"os"
	"syscall"

	"nekosu/ncore/kmod"
)

func Main() {
    realInit, err := run()
    if err != nil {
        log.Println(err)
    }

    if err := syscall.Exec(realInit, os.Args, os.Environ()); err != nil {
        log.Printf("exec %s failed: %v", realInit, err)
        select {}
    }
}

func run() (realInit string, err error) {
    realInit = "/init.real"
    if _, err := os.Stat(realInit); os.IsNotExist(err) {
        realInit = "/system/bin/init"
    }

    if err := kmod.Load("/ncore.ko"); err != nil {
        log.Printf("cannot load ncore.ko: %v", err)
    }

    if err := os.Remove("/init"); err != nil {
        return realInit, err
    }

    return realInit, os.Symlink(realInit, "/init")
}