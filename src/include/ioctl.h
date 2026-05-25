#ifndef IOCTL_H
#define IOCTL_H
#include <linux/capability.h>
#include <linux/version.h>

struct fmac_rule {
    char path[1024];
    unsigned long status_bits;
};

struct fmac_uid_cap {
    unsigned int uid;
    uint64_t caps;
};

struct nksu_profile_data {
    unsigned int uid;
    uint64_t caps;
    char selinux_domain[64];
    int namespace;
};

struct fmac_sepolicy_rule {
    char src[64];
    char tgt[64];
    char cls[64];
    char perm[64];
    int effect;
    int invert;
};

#define IOC_MAGIC 'F'
#define IOC_GET_SHM _IO(IOC_MAGIC, 0)
#define IOC_BIND_EVT _IOW(IOC_MAGIC, 1, int)
#define IOC_CHK_WRITE _IOR(IOC_MAGIC, 2, int)
#define IOC_ADD_UID _IOW(IOC_MAGIC, 3, unsigned int)
#define IOC_DEL_UID _IOW(IOC_MAGIC, 4, unsigned int)
#define IOC_HAS_UID _IOWR(IOC_MAGIC, 5, unsigned int)
#define IOC_SET_CAP _IOW(IOC_MAGIC, 6, struct fmac_uid_cap)
#define IOC_GET_CAP _IOWR(IOC_MAGIC, 7, struct fmac_uid_cap)
#define IOC_DEL_CAP _IOW(IOC_MAGIC, 8, struct fmac_uid_cap)
#define IOC_SEL_ADD_RULE _IOW(IOC_MAGIC, 9, struct fmac_sepolicy_rule)
#define IOC_SET_PROFILE _IOW(IOC_MAGIC, 10, struct nksu_profile_data)

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