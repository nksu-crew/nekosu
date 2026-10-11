#ifndef FMAC_IOCTL_H
#define FMAC_IOCTL_H

#include <linux/ioctl.h>
#include <stdint.h>

#define FMAC_MAGIC 'F'
#define IOC_CMD _IOWR(FMAC_MAGIC, 0, struct fmac_ioc)

enum fmac_flag {
  IOC_GET_SHM = 0,  /* data 为空，返回 shm fd */
  IOC_BIND_EVT,     /* data[4]   = int efd */
  IOC_CHK_WRITE,    /* data[4]   = int changed (out) */
  IOC_ADD_UID,      /* data[4]   = uint32_t uid */
  IOC_DEL_UID,      /* data[4]   = uint32_t uid */
  IOC_HAS_UID,      /* data[4]   = uint32_t uid (in/out: 0 或 1) */
  IOC_SET_CAP,      /* data[12]  = uint32_t uid | uint64_t caps */
  IOC_GET_CAP,      /* data[12]  = uint32_t uid (in), uint64_t caps (out) */
  IOC_DEL_CAP,      /* data[12]  = uint32_t uid */
  IOC_SEL_ADD_RULE, /* data[264] = src[64] tgt[64] cls[64] perm[64] effect[4]
                       invert[4] */
  IOC_SET_PROFILE,  /* data[80]  = uid[4] caps[8] domain[64] namespace[4] */
  /* 11 was IOC_LIST_MODULES; module enumeration now lives in ncore. */
  IOC_SET_SEPOLICY = 12, /* data = KernelSU-format sepolicy batch, size = length */
  IOC_GET_PROFILES = 13, /* data[<=64KiB] out = profile table as text */
  IOC_GET_VERSION = 14,  /* data[<=64] out = kernel module build version */
  IOC_FEATURE_LIST = 15, /* data[<=1024] out = "<id> <name> <value>" lines */
  IOC_FEATURE_GET = 16,  /* data[12] = uint32_t id (in) | uint64_t value (out) */
  IOC_FEATURE_SET = 17,  /* data[12] = uint32_t id | uint64_t value */
};

struct fmac_ioc {
  uint32_t flag;
  uint32_t size;
  uint8_t data[];
};

#define FMAC_DATA_UID 4
#define FMAC_DATA_EFD 4
#define FMAC_DATA_CHKWRITE 4
#define FMAC_DATA_CAP 12
#define FMAC_DATA_SELRULE 264
#define FMAC_DATA_PROFILE 80
#define FMAC_DATA_FEATURE 12

#define FMAC_OFF_UID 0
#define FMAC_OFF_CAPS 4
#define FMAC_OFF_DOMAIN 12
#define FMAC_OFF_NS 76
#define FMAC_OFF_TGT 64
#define FMAC_OFF_CLS 128
#define FMAC_OFF_PERM 192
#define FMAC_OFF_EFFECT 256
#define FMAC_OFF_INVERT 260
#define FMAC_OFF_FEATURE_VALUE 4

#endif /* FMAC_IOCTL_H */
