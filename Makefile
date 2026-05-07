MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
IDIR := $(MDIR)/src/include

$(info -- KDIR: $(KDIR))
$(info -- MDIR: $(MDIR))
$(info -- IDIR: $(IDIR))

include $(MDIR)/Kbuild

ifeq ($(KERNELRELEASE),)

.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(MDIR) IDIR=$(IDIR) modules


clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) clean

endif
