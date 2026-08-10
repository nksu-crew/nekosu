/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Profile storage — a single contiguous RCU-protected blob containing
 * all managed UID profiles.  Entries are sorted by uid for binary search;
 * SELinux domain strings live in a packed arena at the tail of the blob.
 * Updates allocate a new blob and atomically swap the global pointer.
 */
#ifndef __NKSU_PROFILE_H
#define __NKSU_PROFILE_H

#include <linux/capability.h>
#include <linux/rcupdate.h>
#include <linux/string.h>
#include <linux/types.h>

#define NKSU_PROFILE_MAGIC	0x4b4e5355 /* "NKSU" */

#define NKSU_PROFILE_INIT_CAP	16	/* initial entry slots */
#define NKSU_PROFILE_INIT_ARENA	256	/* initial string arena size */
#define NKSU_BITMAP_MAX_UID	32768

/*
 * Blob header — immediately followed by the entry array and string arena.
 *
 * Layout:
 *   [header] [entries[capacity]] [arena[arena_cap]]
 *
 * The entry array is kept sorted by uid for O(log N) binary search.
 * The arena packs domain strings back-to-back; entries reference them
 * by offset+length.
 */
struct nksu_profile_blob {
	struct rcu_head rcu;
	u32 magic;
	u32 version;		/* monotonic generation counter */
	u32 count;		/* active entries */
	u32 capacity;		/* total entry slots */
	u32 arena_used;		/* bytes consumed in arena */
	u32 arena_cap;		/* total arena capacity */
	u32 bitmap[1024];	/* fast UID presence bitmap */
};

/* A single profile entry — 20 bytes, cache-line friendly */
struct nksu_profile_entry {
	u32 uid;
	u32 cap_lo, cap_hi;	/* kernel_cap_t split for fixed layout */
	u16 ns;			/* NKSU_NS_* */
	u16 domain_off;		/* offset into arena, 0xffff if none */
	u16 domain_len;
	u16 _pad;
};

/* Arena accessors — arena lives right after the entry array */
static inline struct nksu_profile_entry *blob_entries(struct nksu_profile_blob *b)
{
	return (struct nksu_profile_entry *)(b + 1);
}

static inline u8 *blob_arena(struct nksu_profile_blob *b)
{
	return (u8 *)(blob_entries(b) + b->capacity);
}

static inline size_t blob_total_size(const struct nksu_profile_blob *b)
{
	return sizeof(*b)
		+ (size_t)b->capacity * sizeof(struct nksu_profile_entry)
		+ b->arena_cap;
}

/*
 * Transfer kernel_cap_t to/from entry storage.  We use memcpy
 * because kernel_cap_t's internal layout varies across kernel
 * versions (struct with cap[], plain u32, etc.).
 */
static inline kernel_cap_t entry_caps(const struct nksu_profile_entry *e)
{
	kernel_cap_t c;

	memcpy(&c, &e->cap_lo, sizeof(c));
	return c;
}

static inline void entry_set_caps(struct nksu_profile_entry *e,
				  kernel_cap_t caps)
{
	memcpy(&e->cap_lo, &caps, sizeof(caps));
}

/* --- public API --- */

/* O(1) bitmap check.  Safe to call from any context. */
bool nksu_profile_has_uid(uid_t uid);

/*
 * Look up a profile for uid.  On success, caps and ns are written back
 * and the domain string is copied into domain_buf (at most domain_len
 * bytes, always null-terminated).  If no domain is stored, domain_buf[0]
 * is set to '\0'.
 *
 * Returns 0 on success, -ENOENT if no profile exists for uid.
 */
int nksu_profile_get(uid_t uid, kernel_cap_t *caps, int *ns,
		     char *domain_buf, size_t domain_len);

int nksu_profile_set(uid_t uid, kernel_cap_t caps, const char *domain, int ns);
int nksu_profile_set_caps(uid_t uid, kernel_cap_t caps);
int nksu_profile_set_domain(uid_t uid, const char *domain);
int nksu_profile_set_ns(uid_t uid, int ns);
int nksu_profile_set_default(uid_t uid);

void nksu_profile_clear(uid_t uid);
void nksu_profile_clear_all(void);

int nksu_profile_init(void);

#endif /* __NKSU_PROFILE_H */
