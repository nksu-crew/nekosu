#ifndef CTL_H
#define CTL_H

#include <stdint.h>
#include "ioctl.h"   /* 提供 IOC_CMD、flag 枚举与 data 布局常量 */

enum Opcode {
    OP_AUTHENTICATE = 201,
    OP_GET_ROOT     = 202,
    OP_IOCTL        = 203
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