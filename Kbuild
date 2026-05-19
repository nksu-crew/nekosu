nksu-y += src/anonfd.o src/nksu.o src/privilege.o src/ioctl.o src/manager.o

nksu-y += src/selinux/rule.o src/selinux/selinux.o src/selinux/policy.o src/selinux/domain.o src/selinux/dup.o 

nksu-y += src/profile/profile.o
nksu-y += src/ns.o

ifeq ($(CONFIG_NKSU_SYSCALL),y)
	ccflags-y += -DCONFIG_NKSU_SYSCALL=1
	nksu-y += src/syscall/syscall.o
	nksu-y += src/syscall/dispatch.o
	nksu-y += src/handle.o
	CFLAGS_src/syscall/syscall.o := -O3
	CFLAGS_src/syscall/dispatch.o := -O3
	CFLAGS_src/handle.o := -O3
else
	nksu-y += src/tracepoint.o
endif

obj-$(CONFIG_NKSU) += nksu.o

ifeq ($(CONFIG_NKSU_DEBUG),y)
	ccflags-y += -DCONFIG_NKSU_DEBUG=1
else
	ccflags-y += -O3
endif

ccflags-y += -I$(srctree)/security/selinux
ccflags-y += -I$(srctree)/security/selinux/include
ccflags-y += -I$(IDIR)
ccflags-y += -I$(objtree)/security/selinux
ccflags-y += -include $(srctree)/include/uapi/asm-generic/errno.h

ccflags-y += -std=gnu99
ccflags-y += -Wno-unused-variable
ccflags-y += -Wno-declaration-after-statement
ccflags-y += -Wno-unused-function
ccflags-y += -Werror=implicit-function-declaration
ccflags-y += -Werror=return-type

CFLAGS_src/manager.o     := -O3
CFLAGS_src/tracepoint.o  := -O3
