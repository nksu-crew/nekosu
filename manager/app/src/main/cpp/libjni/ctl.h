#ifndef CTL_H
#define CTL_H

#include <stdint.h>
#include "ioctl.h"   /* 提供 IOC_*、struct nksu_profile_data、struct fmac_uid_cap、struct fmac_sepolicy_rule */

enum Opcode {
    OP_AUTHENTICATE = 1,
    OP_GET_ROOT     = 2,
    OP_IOCTL        = 3
};

int    Ctl(enum Opcode code);

int    SetProfile(int fd, int uid, uint64_t caps, const char *domain, int namespace);
int    AddUid(int fd, int uid);
int    DelUid(int fd, int uid);
int    HasUid(int fd, int uid, int *has);               /* *has = 1 或 0 */
int    SetCap(int fd, int uid, uint64_t caps);
int    GetCap(int fd, int uid, uint64_t *caps);
int    DelCap(int fd, int uid);
int    AddSelinuxRule(int fd, const char *src, const char *tgt,
                      const char *cls, const char *perm,
                      int effect, int invert);

int    ScanDriverFd(void);
int    ScanCtlFd(void);

#endif /* CTL_H */