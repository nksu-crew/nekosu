// SPDX-License-Identifier: GPL-3.0-or-later
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include "fd.h"

static void *shared_buffer;

static int fmac_anon_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;

    if (!shared_buffer)
        return -ENODEV;
    if (size > PAGE_SIZE)
        return -EINVAL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
    vm_flags_set(vma, VM_READ | VM_WRITE | VM_SHARED | VM_DONTEXPAND | VM_DONTDUMP);
#else
    vma->vm_flags |= VM_READ | VM_WRITE | VM_SHARED | VM_DONTEXPAND | VM_DONTDUMP;
#endif

    return remap_vmalloc_range(vma, shared_buffer, 0);
}

static const struct file_operations fmac_anon_fops = {
    .owner = THIS_MODULE,
    .mmap  = fmac_anon_mmap,
};

void *fmac_shm_get(void)
{
    return shared_buffer;
}

size_t shm_size(void)
{
    return PAGE_SIZE;
}

int fmac_anonfd_get(void)
{
    pr_info("install fd to %d.\n", current->pid);
    if (!shared_buffer)
        return -ENODEV;

    return anon_inode_getfd("[fmac_shm]", &fmac_anon_fops, NULL, O_RDWR | O_CLOEXEC);
}

int fmac_anonfd_init(void)
{
    shared_buffer = vmalloc_user(PAGE_SIZE);
    if (!shared_buffer)
        return -ENOMEM;

    pr_info("anonfd shared buffer allocated: %p\n", shared_buffer);
    return 0;
}

void fmac_anonfd_exit(void)
{
    if (shared_buffer) {
        vfree(shared_buffer);
        shared_buffer = NULL;
    }
    pr_info("anonfd resources released\n");
}