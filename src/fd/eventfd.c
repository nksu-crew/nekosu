// SPDX-License-Identifier: GPL-3.0-or-later
#include <linux/eventfd.h>
#include <linux/version.h>
#include "fd.h"

static struct eventfd_ctx *event_ctx;

int nksu_bind_eventfd(int fd)
{
    struct eventfd_ctx *ctx = eventfd_ctx_fdget(fd);
    if (IS_ERR(ctx))
        return PTR_ERR(ctx);

    event_ctx = ctx;
    return 0;
}

void fmac_notify_user(void)
{
    if (!event_ctx)
        return;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
    eventfd_signal(event_ctx);
#else
    eventfd_signal(event_ctx, 1);
#endif
}

void fmac_eventfd_exit(void)
{
    if (event_ctx) {
        eventfd_ctx_put(event_ctx);
        event_ctx = NULL;
    }
}