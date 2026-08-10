nksu-y += src/nksu.o src/privilege.o src/ioctl.o src/manager.o

nksu-y += src/selinux/rule.o src/selinux/selinux.o src/selinux/policy.o src/selinux/domain.o

nksu-y += src/profile/profile.o
nksu-y += src/ns.o
nksu-y += src/handle.o

nksu-y += src/fd/anonfd.o
nksu-y += src/fd/eventfd.o
nksu-y += src/fd/shm_hash.o

ifeq ($(CONFIG_NKSU_SYSCALL),y)
	ccflags-y += -DCONFIG_NKSU_SYSCALL=1
	nksu-y += src/syscall/syscall.o
	nksu-y += src/syscall/dispatch.o
	CFLAGS_src/syscall/syscall.o := -O3
	CFLAGS_src/syscall/dispatch.o := -O3
endif

obj-$(CONFIG_NKSU) += nksu.o

ifeq ($(CONFIG_NKSU_DEBUG),y)
	ccflags-y += -DCONFIG_NKSU_DEBUG=1
else
	ccflags-y += -O3
endif

ifeq ($(CONFIG_LTO_CLANG),y)
    # Clang LTO
    ccflags-y += -flto=thin
    CFLAGS_nksu.o := -flto=thin
    CFLAGS_src/privilege.o := -flto=thin
    CFLAGS_src/ioctl.o := -flto=thin
    CFLAGS_src/manager.o := -flto=thin -O3
    CFLAGS_src/selinux/rule.o := -flto=thin
    CFLAGS_src/selinux/selinux.o := -flto=thin
    CFLAGS_src/selinux/policy.o := -flto=thin
    CFLAGS_src/selinux/domain.o := -flto=thin
    CFLAGS_src/profile/profile.o := -flto=thin
    CFLAGS_src/ns.o := -flto=thin
    CFLAGS_src/handle.o := -flto=thin -O3
    CFLAGS_src/fd/anonfd.o := -flto=thin
    CFLAGS_src/fd/eventfd.o := -flto=thin
    CFLAGS_src/fd/shm_hash.o := -flto=thin
    ifeq ($(CONFIG_NKSU_SYSCALL),y)
        CFLAGS_src/syscall/syscall.o := -flto=thin -O3
        CFLAGS_src/syscall/dispatch.o := -flto=thin -O3
    endif
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

# CFLAGS_src/manager.o  := -O3
# CFLAGS_src/handle.o   := -O3