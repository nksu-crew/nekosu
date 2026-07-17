#ifndef CTL_H
#define CTL_H

#include <stdint.h>
#include "ioctl.h"   /* 提供 IOC_*、struct nksu_profile_data、struct fmac_uid_cap、struct fmac_sepolicy_rule */

/* 操作码，与原 Go 常量对应 */
enum Opcode {
    OP_AUTHENTICATE = 1,
    OP_GET_ROOT     = 2,
    OP_IOCTL        = 3
};

/* 事件封装 */
typedef struct {
    int fd;
} Event;

/* 初始化与清理 */
Event  Event_new(void);
void   Event_close(Event *e);

/* 等待事件 (无限等待) : 成功返回0，val 得到值；失败返回 -1 */
int    Event_wait(Event *e, uint64_t *val);

/* 等待事件 (超时，毫秒) : 成功返回0，val 得到值；超时返回1；失败返回 -1 */
int    Event_wait_timeout(Event *e, int timeout_ms, int64_t *val);

/* prctl 控制接口 */
int    ctl_prctl(unsigned int op);
int    Ctl(enum Opcode code);

/* ioctl 控制接口 */
int    ioctl_cmd(int fd, unsigned long cmd, void *arg);

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

/* 扫描内核驱动文件描述符 */
int    ScanDriverFd(void);   /* 寻找包含 "[fmac_shm]" 的 fd */
int    ScanCtlFd(void);      /* 寻找包含 "[fmac_ctl]" 的 fd */

#endif /* CTL_H */