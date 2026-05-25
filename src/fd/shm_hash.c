// SPDX-License-Identifier: GPL-3.0-or-later
#include <linux/jhash.h>

#include "fd.h"

static u32 last_hash;

static u32 get_mem_hash(void)
{
    return jhash(fmac_shm_get(), shm_size(), 0);
}

void fmac_hash_init(void)
{
    last_hash = get_mem_hash();
}

bool fmac_check_mmap_write(void)
{
    u32 h = get_mem_hash();
    if (h != last_hash) {
        last_hash = h;
        return true;
    }
    return false;
}