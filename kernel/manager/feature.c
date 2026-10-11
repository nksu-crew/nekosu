// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Kernel feature registry, driven by the manager over IOC_FEATURE_*.
 *
 * KernelSU keeps an equivalent table (policy/feature.c) and lets ksud flip the
 * switches; here the manager talks to the kernel directly through the control
 * fd, so there is no userspace daemon in the loop.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/string.h>

#include "manager/feature.h"

#define NKSU_MAX_FEATURES 16

static DEFINE_MUTEX(nksu_feature_mutex);
static const struct nksu_feature *nksu_features[NKSU_MAX_FEATURES];
static int nksu_feature_count;

static const struct nksu_feature *nksu_feature_find(u32 id)
{
    int i;

    for (i = 0; i < nksu_feature_count; i++) {
        if (nksu_features[i]->id == id)
            return nksu_features[i];
    }
    return NULL;
}

int nksu_feature_register(const struct nksu_feature *feature)
{
    int i, ret = 0;

    if (!feature || !feature->name)
        return -EINVAL;

    mutex_lock(&nksu_feature_mutex);
    for (i = 0; i < nksu_feature_count; i++) {
        if (nksu_features[i]->id == feature->id) {
            ret = -EEXIST;
            goto out;
        }
    }
    if (nksu_feature_count >= NKSU_MAX_FEATURES) {
        ret = -ENOSPC;
        goto out;
    }
    nksu_features[nksu_feature_count++] = feature;
out:
    mutex_unlock(&nksu_feature_mutex);
    return ret;
}

int nksu_feature_get(u32 id, u64 *value)
{
    const struct nksu_feature *feature;
    int ret = -ENOENT;

    if (!value)
        return -EINVAL;

    mutex_lock(&nksu_feature_mutex);
    feature = nksu_feature_find(id);
    if (feature && feature->get)
        ret = feature->get(value);
    mutex_unlock(&nksu_feature_mutex);
    return ret;
}

int nksu_feature_set(u32 id, u64 value)
{
    const struct nksu_feature *feature;
    int ret = -ENOENT;

    mutex_lock(&nksu_feature_mutex);
    feature = nksu_feature_find(id);
    if (feature && feature->set)
        ret = feature->set(value);
    mutex_unlock(&nksu_feature_mutex);
    return ret;
}

int nksu_feature_render(char *buf, size_t size)
{
    int i, len = 0;

    if (!buf || size == 0)
        return -EINVAL;

    mutex_lock(&nksu_feature_mutex);
    for (i = 0; i < nksu_feature_count; i++) {
        const struct nksu_feature *feature = nksu_features[i];
        u64 value = 0;

        if (feature->get)
            feature->get(&value);

        len += scnprintf(buf + len, size - len, "%u %s %llu\n", feature->id,
                         feature->name, (unsigned long long)value);
        if ((size_t)len >= size)
            break;
    }
    mutex_unlock(&nksu_feature_mutex);

    return len;
}
