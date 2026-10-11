#ifndef NKSU_MANAGER_H
#define NKSU_MANAGER_H

#include <linux/types.h>

int appscan_init(void);
void appscan_exit(void);
bool is_manager(void);

/*
 * True once the manager scan has confirmed (or installed) /data/adb/nksu/ncore.
 * The su hook reads this instead of touching the filesystem: it runs inside a
 * syscall tracepoint, where sleeping (kern_path) is not allowed.
 */
bool is_ncore_ready(void);

/* True when uid is the currently verified manager's UID.  Used by the profile
 * store to keep the manager's own entry out of allow.profile: it is re-granted
 * each boot after the signature check, so a stale entry must never survive a
 * failed verification (or a reinstall that changes the UID). */
bool is_manager_uid(uid_t uid);

struct cred;

/*
 * Borrow a cred switched to the nksu domain (load_policy() grants it
 * allow-any-any) while touching /data/system or /data/adb from a context that
 * the kernel SELinux domain cannot read.  Pair every begin with an end.
 */
const struct cred *nksu_scan_creds_begin(void);
void nksu_scan_creds_end(const struct cred *old);

/* True when `path` exists, checked under the nksu scan creds. */
bool nksu_file_exists(const char *path);

#endif /* NKSU_MANAGER_H */
