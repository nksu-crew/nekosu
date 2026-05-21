// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * FMAC - File Monitoring and Access Control Kernel Module
 * Copyright (C) 2025 Aqnya
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/eventfd.h>
#include <linux/jhash.h>
#include <fmac.h>

#define FMAC_SHM_SIZE PAGE_SIZE

static void *shared_buffer;
struct eventfd_ctx *event_ctx = NULL;
u32 last_hash;

static int fmac_anon_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;

    if (!shared_buffer)
        return -ENODEV;

    if (size > FMAC_SHM_SIZE)
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
    .mmap = fmac_anon_mmap,
};

int fmac_anonfd_get(void)
{
    pr_info("install fd to %d.\n", current->pid);
    if (!shared_buffer)
        return -ENODEV;

    return anon_inode_getfd("[fmac_shm]", &fmac_anon_fops, NULL, O_RDWR | O_CLOEXEC);
}

int bind_eventfd(int fd)
{
    struct eventfd_ctx *ctx = eventfd_ctx_fdget(fd);
    if (IS_ERR(ctx))
        return PTR_ERR(ctx);

    event_ctx = ctx;
    return 0;
}

void notify_user(void)
{
    if (event_ctx) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
        eventfd_signal(event_ctx);
#else
        eventfd_signal(event_ctx, 1);
#endif
    }
}

void eventfd_cleanup(void)
{
    if (event_ctx) {
        eventfd_ctx_put(event_ctx);
        event_ctx = NULL;
    }
}

static u32 get_mem_hash(void)
{
    return jhash(shared_buffer, FMAC_SHM_SIZE, 0);
}

bool check_mmap_write(void)
{
    u32 now_hash = get_mem_hash();
    if (now_hash != last_hash) {
        last_hash = now_hash;
        return true;
    } else {
        return false;
    }
}

int fmac_anonfd_init(void)
{
    shared_buffer = vmalloc_user(FMAC_SHM_SIZE);
    if (!shared_buffer)
        return -ENOMEM;

    pr_info("anonfd shared buffer allocated: %p\n", shared_buffer);
    last_hash = get_mem_hash();
    return 0;
}

void fmac_anonfd_exit(void)
{
    if (shared_buffer) {
        vfree(shared_buffer);
        shared_buffer = NULL;
    }

    eventfd_cleanup();

    pr_info("anonfd resources released\n");
}
