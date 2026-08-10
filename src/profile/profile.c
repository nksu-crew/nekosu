// SPDX-License-Identifier: GPL-2.0
/*
 * Profile storage backed by a single contiguous RCU-protected blob.
 * Entries are sorted by uid for binary search; domain strings are
 * packed in a tail arena.  Updates allocate a new blob, copy the old
 * contents, apply the change, and swap the global RCU pointer atomically.
 */

#include <linux/bitops.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "profile.h"
#include "ns.h"

static DEFINE_MUTEX(profile_lock);
static struct nksu_profile_blob __rcu *g_profile_blob;

/* ── blob allocation ── */

static struct nksu_profile_blob *blob_alloc(u32 capacity, u32 arena_cap)
{
	size_t size;
	struct nksu_profile_blob *b;

	size = sizeof(*b)
		+ (size_t)capacity * sizeof(struct nksu_profile_entry)
		+ arena_cap;
	b = kzalloc(size, GFP_KERNEL);
	if (!b)
		return NULL;

	b->magic = NKSU_PROFILE_MAGIC;
	b->version = 1;
	b->capacity = capacity;
	b->arena_cap = arena_cap;

	return b;
}

/* ── binary search for uid in sorted entries ── */

/*
 * Binary search for uid.  If found, *idx is the index of the matching
 * entry and returns true.  If not found, *idx is the insertion point
 * (where uid should go to maintain sorted order) and returns false.
 */
static bool blob_bsearch(struct nksu_profile_blob *b, uid_t uid, u32 *idx)
{
	struct nksu_profile_entry *entries = blob_entries(b);
	u32 lo = 0, hi = b->count;

	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;

		if (entries[mid].uid < uid)
			lo = mid + 1;
		else
			hi = mid;
	}
	*idx = lo;
	return lo < b->count && entries[lo].uid == uid;
}

/* ── blob growth ── */

/*
 * Allocate a new blob at least 2x the current capacity (or arena),
 * copy all entries and arena contents from src, and return the new blob.
 * Returns ERR_PTR on failure.
 */
static struct nksu_profile_blob *blob_grow(struct nksu_profile_blob *src,
					   u32 min_capacity, u32 min_arena)
{
	struct nksu_profile_blob *dst;
	u32 new_cap, new_arena;
	size_t entry_bytes, arena_bytes;

	new_cap = max(src->capacity * 2, min_capacity);
	new_arena = max(src->arena_cap * 2, min_arena);

	dst = blob_alloc(new_cap, new_arena);
	if (!dst)
		return ERR_PTR(-ENOMEM);

	/* copy metadata */
	dst->version = src->version + 1;
	dst->count = src->count;
	dst->arena_used = src->arena_used;

	/* copy entries */
	entry_bytes = (size_t)src->count * sizeof(struct nksu_profile_entry);
	memcpy(blob_entries(dst), blob_entries(src), entry_bytes);

	/* copy arena */
	arena_bytes = src->arena_used;
	if (arena_bytes)
		memcpy(blob_arena(dst), blob_arena(src), arena_bytes);

	/* copy bitmap */
	memcpy(dst->bitmap, src->bitmap, sizeof(dst->bitmap));

	return dst;
}

/* ── bitmap helpers ── */

static inline void blob_set_bitmap(struct nksu_profile_blob *b, uid_t uid)
{
	if (uid < NKSU_BITMAP_MAX_UID)
		set_bit(uid, (unsigned long *)b->bitmap);
}

static inline void blob_clear_bitmap(struct nksu_profile_blob *b, uid_t uid)
{
	if (uid < NKSU_BITMAP_MAX_UID)
		clear_bit(uid, (unsigned long *)b->bitmap);
}

/* ── arena: append a string, growing the blob if needed ── */

/*
 * Write a string into the arena of *bp.  If there is not enough room,
 * *bp is replaced with a grown copy.  Returns the offset into the
 * arena or -ENOMEM.
 */
static int arena_put(struct nksu_profile_blob **bp, const char *str, u16 len)
{
	struct nksu_profile_blob *b = *bp;
	struct nksu_profile_blob *new_blob;
	u16 off;

	if (b->arena_used + len > b->arena_cap) {
		new_blob = blob_grow(b, b->capacity + 1,
				    max(b->arena_used + len,
					b->arena_cap * 2));
		if (IS_ERR(new_blob))
			return PTR_ERR(new_blob);
		*bp = new_blob;
		b = new_blob;
	}

	off = b->arena_used;
	memcpy(blob_arena(b) + off, str, len);
	b->arena_used += len;
	return off;
}

/* ── update: allocate new blob, apply change, swap ── */

typedef int (*blob_modify_fn)(struct nksu_profile_blob **bp, void *ctx);

/*
 * Atomic update: create a copy of the current blob, apply the
 * modification, and swap the global pointer.  The old blob is freed
 * after the RCU grace period.
 */
static int blob_update(blob_modify_fn mod, void *ctx)
{
	struct nksu_profile_blob *old, *new_blob;
	int ret;

	mutex_lock(&profile_lock);

	old = rcu_dereference_protected(g_profile_blob,
					lockdep_is_held(&profile_lock));
	if (!old) {
		/* first update ever — create an initial blob */
		new_blob = blob_alloc(NKSU_PROFILE_INIT_CAP,
				      NKSU_PROFILE_INIT_ARENA);
		if (!new_blob) {
			mutex_unlock(&profile_lock);
			return -ENOMEM;
		}
	} else {
		/* copy-on-write */
		new_blob = blob_alloc(old->capacity, old->arena_cap);
		if (!new_blob) {
			mutex_unlock(&profile_lock);
			return -ENOMEM;
		}
		new_blob->version = old->version + 1;
		new_blob->count = old->count;
		new_blob->arena_used = old->arena_used;
		memcpy(blob_entries(new_blob), blob_entries(old),
		       (size_t)old->count * sizeof(struct nksu_profile_entry));
		if (old->arena_used)
			memcpy(blob_arena(new_blob), blob_arena(old),
			       old->arena_used);
		memcpy(new_blob->bitmap, old->bitmap, sizeof(old->bitmap));
	}

	ret = mod(&new_blob, ctx);
	if (ret) {
		kfree(new_blob);
		mutex_unlock(&profile_lock);
		return ret;
	}

	rcu_assign_pointer(g_profile_blob, new_blob);
	mutex_unlock(&profile_lock);

	if (old)
		kfree_rcu(old, rcu);
	return 0;
}

/* ── modify: set all fields ── */

struct set_all_ctx {
	uid_t uid;
	kernel_cap_t caps;
	const char *domain;
	int ns;
};

static int __set_all(struct nksu_profile_blob **bp, void *ctx)
{
	struct nksu_profile_blob *b = *bp;
	struct set_all_ctx *s = ctx;
	struct nksu_profile_entry *entries = blob_entries(b);
	u32 idx;
	bool found;
	int domain_off = -1;
	u16 dlen = 0;

	if (s->domain && s->domain[0]) {
		dlen = (u16)strnlen(s->domain, 65535);
		if (dlen == 0)
			return -EINVAL;
		domain_off = arena_put(bp, s->domain, dlen);
		if (domain_off < 0)
			return domain_off;
		b = *bp; /* may have changed after grow */
		entries = blob_entries(b);
	}

	found = blob_bsearch(b, s->uid, &idx);

	if (found) {
		/* update existing */
		entries[idx].cap_lo = s->caps.cap[0];
		entries[idx].cap_hi = s->caps.cap[1];
		entries[idx].ns = (u16)s->ns;
		if (domain_off >= 0) {
			entries[idx].domain_off = (u16)domain_off;
			entries[idx].domain_len = dlen;
		}
	} else {
		/* insert new entry */
		if (b->count >= b->capacity) {
			struct nksu_profile_blob *g;
			u32 min_arena;

			min_arena = (domain_off >= 0)
				? b->arena_used : b->arena_cap;
			g = blob_grow(b, b->count + 1, min_arena);
			if (IS_ERR(g))
				return PTR_ERR(g);
			/* re-apply arena write into grown blob */
			if (domain_off >= 0) {
				domain_off = arena_put(&g, s->domain, dlen);
				if (domain_off < 0)
					return domain_off;
			}
			*bp = g;
			b = g;
			entries = blob_entries(b);
			/* re-find insertion point */
			blob_bsearch(b, s->uid, &idx);
		}
		/* shift right to make room */
		if (idx < b->count)
			memmove(&entries[idx + 1], &entries[idx],
				(size_t)(b->count - idx) * sizeof(*entries));
		b->count++;

		entries[idx].uid = s->uid;
		entries[idx].cap_lo = s->caps.cap[0];
		entries[idx].cap_hi = s->caps.cap[1];
		entries[idx].ns = (u16)s->ns;
		entries[idx].domain_off = (domain_off >= 0) ? (u16)domain_off : 0xffff;
		entries[idx].domain_len = dlen;

		blob_set_bitmap(b, s->uid);
	}

	return 0;
}

/* ── modify: set caps only ── */

struct set_field_ctx {
	uid_t uid;
	kernel_cap_t caps;
	const char *domain;
	int ns;
};

static int __set_caps(struct nksu_profile_blob **bp, void *ctx)
{
	struct set_field_ctx *s = ctx;
	struct nksu_profile_entry *entries = blob_entries(*bp);
	u32 idx;

	if (!blob_bsearch(*bp, s->uid, &idx))
		return -ENOENT;

	entries[idx].cap_lo = s->caps.cap[0];
	entries[idx].cap_hi = s->caps.cap[1];
	return 0;
}

static int __set_domain(struct nksu_profile_blob **bp, void *ctx)
{
	struct nksu_profile_blob *b = *bp;
	struct set_field_ctx *s = ctx;
	struct nksu_profile_entry *entries = blob_entries(b);
	u32 idx;
	int off;
	u16 dlen;

	if (!blob_bsearch(b, s->uid, &idx))
		return -ENOENT;

	if (!s->domain || !s->domain[0]) {
		entries[idx].domain_off = 0xffff;
		entries[idx].domain_len = 0;
		return 0;
	}

	dlen = (u16)strnlen(s->domain, 65535);
	off = arena_put(bp, s->domain, dlen);
	if (off < 0)
		return off;

	b = *bp;
	entries = blob_entries(b);
	entries[idx].domain_off = (u16)off;
	entries[idx].domain_len = dlen;
	return 0;
}

static int __set_ns(struct nksu_profile_blob **bp, void *ctx)
{
	struct set_field_ctx *s = ctx;
	struct nksu_profile_entry *entries = blob_entries(*bp);
	u32 idx;

	if (!blob_bsearch(*bp, s->uid, &idx))
		return -ENOENT;

	entries[idx].ns = (u16)s->ns;
	return 0;
}

/* ── modify: remove an entry ── */

struct clear_ctx {
	uid_t uid;
	bool clear_all;
};

static int __clear_entry(struct nksu_profile_blob **bp, void *ctx)
{
	struct clear_ctx *c = ctx;
	struct nksu_profile_blob *b = *bp;
	u32 idx;

	if (c->clear_all) {
		b->count = 0;
		b->arena_used = 0;
		memset(b->bitmap, 0, sizeof(b->bitmap));
		return 0;
	}

	if (!blob_bsearch(b, c->uid, &idx))
		return -ENOENT;

	blob_clear_bitmap(b, c->uid);

	/* shift remaining entries left */
	if (idx < b->count - 1)
		memmove(blob_entries(b) + idx,
			blob_entries(b) + idx + 1,
			(size_t)(b->count - idx - 1)
				* sizeof(struct nksu_profile_entry));
	b->count--;
	return 0;
}

/* ── public API ── */

bool nksu_profile_has_uid(uid_t uid)
{
	struct nksu_profile_blob *b;
	bool result = false;

	if (uid >= NKSU_BITMAP_MAX_UID)
		return false;

	rcu_read_lock();
	b = rcu_dereference(g_profile_blob);
	if (b)
		result = test_bit(uid, (unsigned long *)b->bitmap);
	rcu_read_unlock();
	return result;
}

int nksu_profile_get(uid_t uid, kernel_cap_t *caps, int *ns,
		     char *domain_buf, size_t domain_len)
{
	struct nksu_profile_blob *b;
	struct nksu_profile_entry *entries;
	const u8 *arena;
	u32 idx;
	bool found;
	int ret = -ENOENT;

	rcu_read_lock();
	b = rcu_dereference(g_profile_blob);
	if (!b)
		goto out;

	entries = blob_entries(b);
	arena = blob_arena(b);
	found = blob_bsearch(b, uid, &idx);

	if (found) {
		struct nksu_profile_entry *e = &entries[idx];

		if (caps)
			*caps = entry_caps(e);
		if (ns)
			*ns = e->ns;
		if (domain_buf && domain_len > 0) {
			if (e->domain_len > 0 && e->domain_off != 0xffff) {
				size_t n = min_t(size_t, e->domain_len,
						 domain_len - 1);

				memcpy(domain_buf, arena + e->domain_off, n);
				domain_buf[n] = '\0';
			} else {
				domain_buf[0] = '\0';
			}
		}
		ret = 0;
	}

out:
	rcu_read_unlock();
	return ret;
}

int nksu_profile_set(uid_t uid, kernel_cap_t caps, const char *domain, int ns)
{
	struct set_all_ctx ctx = {
		.uid = uid,
		.caps = caps,
		.domain = domain,
		.ns = ns,
	};

	return blob_update(__set_all, &ctx);
}

int nksu_profile_set_caps(uid_t uid, kernel_cap_t caps)
{
	struct set_field_ctx ctx = {
		.uid = uid,
		.caps = caps,
	};

	return blob_update(__set_caps, &ctx);
}

int nksu_profile_set_domain(uid_t uid, const char *domain)
{
	struct set_field_ctx ctx = {
		.uid = uid,
		.domain = domain,
	};

	return blob_update(__set_domain, &ctx);
}

int nksu_profile_set_ns(uid_t uid, int ns)
{
	struct set_field_ctx ctx = {
		.uid = uid,
		.ns = ns,
	};

	return blob_update(__set_ns, &ctx);
}

int nksu_profile_set_default(uid_t uid)
{
	kernel_cap_t caps = { { 0, 0 } };

	return nksu_profile_set(uid, caps, "u:r:nksu:s0", NKSU_NS_INHERITED);
}

void nksu_profile_clear(uid_t uid)
{
	struct clear_ctx ctx = { .uid = uid };

	blob_update(__clear_entry, &ctx);
}

void nksu_profile_clear_all(void)
{
	struct clear_ctx ctx = { .clear_all = true };

	blob_update(__clear_entry, &ctx);
}

int nksu_profile_init(void)
{
	struct nksu_profile_blob *b;

	b = blob_alloc(NKSU_PROFILE_INIT_CAP, NKSU_PROFILE_INIT_ARENA);
	if (!b)
		return -ENOMEM;

	rcu_assign_pointer(g_profile_blob, b);
	return 0;
}
