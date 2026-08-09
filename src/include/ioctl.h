#ifndef IOCTL_H
#define IOCTL_H
#include <linux/ioctl.h>
#include <linux/capability.h>
#include <linux/version.h>

/*
 * 统一的控制接口：所有操作通过同一个 IOC_CMD 下发，
 * 请求体为 { flag, size, data[] }，内核按 flag 从 data 中自由解析。
 * 不依赖任何业务结构体传递数据。
 */

#define IOC_MAGIC 'F'
#define IOC_CMD_NR 0
#define IOC_CMD _IOWR(IOC_MAGIC, IOC_CMD_NR, struct fmac_ioc)

/* 操作类型 */
enum {
    IOC_GET_SHM = 0,   /* data 为空，返回 shm fd */
    IOC_BIND_EVT,      /* data[4]   = int efd */
    IOC_CHK_WRITE,     /* data[4]   = int changed (out) */
    IOC_ADD_UID,       /* data[4]   = uint32_t uid */
    IOC_DEL_UID,       /* data[4]   = uint32_t uid */
    IOC_HAS_UID,       /* data[4]   = uint32_t uid (in/out: 0 或 1) */
    IOC_SET_CAP,       /* data[12]  = uint32_t uid | uint64_t caps */
    IOC_GET_CAP,       /* data[12]  = uint32_t uid (in), uint64_t caps (out) */
    IOC_DEL_CAP,       /* data[12]  = uint32_t uid */
    IOC_SEL_ADD_RULE,  /* data[264] = src[64] tgt[64] cls[64] perm[64] effect[4] invert[4] */
    IOC_SET_PROFILE,   /* data[80]  = uid[4] caps[8] domain[64] namespace[4] */
};

struct fmac_ioc {
    unsigned int flag;
    unsigned int size;
    unsigned char data[];
};

/* 各 flag 的 data 布局（与 userspace ioctl.h 保持一致） */
#define FMAC_DATA_UID       4
#define FMAC_DATA_EFD       4
#define FMAC_DATA_CHKWRITE  4
#define FMAC_DATA_CAP       12
#define FMAC_DATA_SELRULE   264
#define FMAC_DATA_PROFILE   80

#define FMAC_OFF_UID     0
#define FMAC_OFF_CAPS    4
#define FMAC_OFF_DOMAIN  12
#define FMAC_OFF_NS      76
#define FMAC_OFF_TGT     64
#define FMAC_OFF_CLS     128
#define FMAC_OFF_PERM    192
#define FMAC_OFF_EFFECT  256
#define FMAC_OFF_INVERT  260

static inline kernel_cap_t u64_to_cap(u64 v)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    kernel_cap_t res;
    res.val = v;
    return res;
#else
    kernel_cap_t cap;
    cap.cap[0] = (u32)v;
    cap.cap[1] = (u32)(v >> 32);
    return cap;
#endif
}

static inline u64 cap_to_u64(kernel_cap_t cap)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
    return cap.val;
#else
    return ((u64)cap.cap[1] << 32) | cap.cap[0];
#endif
}

int fmac_ctlfd_get(void);

#endif /* IOCTL_H */
