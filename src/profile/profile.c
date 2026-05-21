// SPDX-License-Identifier: GPL-3.0
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/hashtable.h>
#include <linux/atomic.h>
#include <linux/spinlock.h>
#include <linux/preempt.h>
#include <linux/bitmap.h>
#include <linux/string.h>

#include "klog.h"
#include "profile.h"
#include "ns.h"

#define NKSU_PROFILE_HASHBITS 10
#define PROFILE_BUCKETS (1 << NKSU_PROFILE_HASHBITS)

#define NKSU_BITMAP_MAX_UID 32768

struct nksu_profile {
    uid_t uid;
    kernel_cap_t caps;
    char selinux_domain[64];
    int namespace;
    struct hlist_node hnode;
    struct rcu_head rcu;
};

struct profile_cache_cpu {
    uid_t uid;
    struct nksu_profile *profile;
    u64 version;
};

static DECLARE_BITMAP(nksu_profile_bitmap, NKSU_BITMAP_MAX_UID);
static struct hlist_head g_profile_table[PROFILE_BUCKETS];
static spinlock_t g_bucket_locks[PROFILE_BUCKETS];
static u64 g_profile_version = 0;

static DEFINE_PER_CPU(struct profile_cache_cpu, profile_cpu_l0);

static inline void profile_commit_version(void)
{
    smp_store_release(&g_profile_version, g_profile_version + 1);
}

static struct nksu_profile *nksu_profile_lookup(uid_t uid)
{
    struct profile_cache_cpu *pc;
    struct nksu_profile *node = NULL;
    u64 ver;
    u32 bkt;

    if (uid < NKSU_BITMAP_MAX_UID && !test_bit(uid, nksu_profile_bitmap))
        return NULL;

    preempt_disable();
    pc = this_cpu_ptr(&profile_cpu_l0);
    ver = smp_load_acquire(&g_profile_version);

    if (likely(pc->version == ver && pc->uid == uid)) {
        struct nksu_profile *cached = pc->profile;
        preempt_enable();
        return cached;
    }
    preempt_enable();

    rcu_read_lock();
    bkt = hash_32(uid, NKSU_PROFILE_HASHBITS);
    hlist_for_each_entry_rcu (node, &g_profile_table[bkt], hnode) {
        if (node->uid == uid)
            goto found;
    }
    node = NULL;

found:
    preempt_disable();
    pc = this_cpu_ptr(&profile_cpu_l0);
    pc->uid = uid;
    pc->profile = node;
    pc->version = ver;
    preempt_enable();

    rcu_read_unlock();
    return node;
}

typedef int (*profile_apply_fn)(struct nksu_profile *node, void *ctx);

static int profile_update(uid_t uid, profile_apply_fn apply, void *ctx)
{
    struct nksu_profile *new_node, *old_node = NULL, *pos;
    u32 bkt = hash_32(uid, NKSU_PROFILE_HASHBITS);
    int ret;

    spin_lock(&g_bucket_locks[bkt]);

    hlist_for_each_entry (pos, &g_profile_table[bkt], hnode) {
        if (pos->uid == uid) {
            old_node = pos;
            break;
        }
    }

    new_node = old_node ? kmemdup(old_node, sizeof(*new_node), GFP_ATOMIC) : kzalloc(sizeof(*new_node), GFP_ATOMIC);

    if (!new_node) {
        ret = -ENOMEM;
        goto out_unlock;
    }

    new_node->uid = uid;
    INIT_HLIST_NODE(&new_node->hnode);

    ret = apply(new_node, ctx);
    if (ret) {
        kfree(new_node);
        goto out_unlock;
    }

    if (old_node) {
        hlist_replace_rcu(&old_node->hnode, &new_node->hnode);
        kfree_rcu(old_node, rcu);
    } else {
        if (uid < NKSU_BITMAP_MAX_UID)
            set_bit(uid, nksu_profile_bitmap);
        hlist_add_head_rcu(&new_node->hnode, &g_profile_table[bkt]);
    }

    profile_commit_version();

out_unlock:
    spin_unlock(&g_bucket_locks[bkt]);
    return ret;
}

static int apply_caps(struct nksu_profile *node, void *ctx)
{
    node->caps = *(kernel_cap_t *)ctx;
    return 0;
}

static int apply_domain(struct nksu_profile *node, void *ctx)
{
    const char *domain = ctx;
    if (domain)
        strscpy(node->selinux_domain, domain, sizeof(node->selinux_domain));
    else
        node->selinux_domain[0] = '\0';
    return 0;
}

struct set_all_ctx {
    kernel_cap_t caps;
    const char *domain;
    int namespace;
};

static int apply_ns(struct nksu_profile *node, void *ctx)
{
    int ns = *(int *)ctx;
    if (ns != NKSU_NS_INHERITED && ns != NKSU_NS_INDIVIDUAL && ns != NKSU_NS_GLOBAL)
        return -EINVAL;
    node->namespace = ns;
    return 0;
}

static int apply_default(struct nksu_profile *node, void *ctx)
{
    node->caps = (kernel_cap_t){ 0 };
    strscpy(node->selinux_domain, "u:r:nksu:s0", sizeof(node->selinux_domain));
    node->namespace = NKSU_NS_INHERITED;
    return 0;
}

static int apply_all(struct nksu_profile *node, void *ctx)
{
    struct set_all_ctx *s = ctx;
    node->caps = s->caps;
    if (s->domain)
        strscpy(node->selinux_domain, s->domain, sizeof(node->selinux_domain));
    else
        node->selinux_domain[0] = '\0';
    node->namespace = s->namespace;
    return 0;
}

int nksu_profile_set_ns(uid_t uid, int ns)
{
    return profile_update(uid, apply_ns, &ns);
}

int nksu_profile_set_caps(uid_t uid, kernel_cap_t caps)
{
    return profile_update(uid, apply_caps, &caps);
}

int nksu_profile_set_domain(uid_t uid, const char *domain)
{
    return profile_update(uid, apply_domain, (void *)domain);
}

int nksu_profile_set_default(uid_t uid)
{
    return profile_update(uid, apply_default, NULL);
}

int nksu_profile_set(uid_t uid, kernel_cap_t caps, const char *domain, int ns)
{
    struct set_all_ctx ctx = { .caps = caps, .domain = domain, .namespace = ns };
    return profile_update(uid, apply_all, &ctx);
}

int nksu_profile_get_dup(uid_t uid, struct profile *out_buf)
{
    struct nksu_profile *ptr;
    int ret = -ENOENT;

    rcu_read_lock();
    ptr = nksu_profile_lookup(uid);
    if (ptr) {
        out_buf->caps = ptr->caps;
        strscpy(out_buf->selinux_domain, ptr->selinux_domain, sizeof(out_buf->selinux_domain));
        out_buf->namespace = ptr->namespace;
        ret = 0;
    }
    rcu_read_unlock();
    return ret;
}

bool nksu_profile_has_uid(uid_t uid)
{
    return test_bit(uid, nksu_profile_bitmap);
}

bool nksu_profile_has_profile(uid_t uid)
{
    return !!nksu_profile_lookup(uid);
}

void nksu_profile_clear(uid_t uid)
{
    struct nksu_profile *node = NULL, *pos;
    u32 bkt = hash_32(uid, NKSU_PROFILE_HASHBITS);

    spin_lock(&g_bucket_locks[bkt]);

    hlist_for_each_entry (pos, &g_profile_table[bkt], hnode) {
        if (pos->uid == uid) {
            node = pos;
            break;
        }
    }

    if (node) {
        hash_del_rcu(&node->hnode);
        if (uid < NKSU_BITMAP_MAX_UID)
            clear_bit(uid, nksu_profile_bitmap);
        kfree_rcu(node, rcu);
        profile_commit_version();
    }

    spin_unlock(&g_bucket_locks[bkt]);
}

void nksu_profile_clear_all(void)
{
    struct nksu_profile *node;
    struct hlist_node *tmp;
    int bkt;

    profile_commit_version();
    bitmap_zero(nksu_profile_bitmap, NKSU_BITMAP_MAX_UID);

    for (bkt = 0; bkt < PROFILE_BUCKETS; bkt++) {
        spin_lock(&g_bucket_locks[bkt]);
        hlist_for_each_entry_safe (node, tmp, &g_profile_table[bkt], hnode) {
            hlist_del_rcu(&node->hnode);
            kfree_rcu(node, rcu);
        }
        spin_unlock(&g_bucket_locks[bkt]);
    }
}

int __init nksu_profile_init(void)
{
    int i;
    bitmap_zero(nksu_profile_bitmap, NKSU_BITMAP_MAX_UID);
    for (i = 0; i < PROFILE_BUCKETS; i++) {
        INIT_HLIST_HEAD(&g_profile_table[i]);
        spin_lock_init(&g_bucket_locks[i]);
    }
    return 0;
}
