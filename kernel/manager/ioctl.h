#ifndef IOCTL_H
#define IOCTL_H
#include <linux/ioctl.h>
#include <linux/capability.h>
#include <linux/version.h>
#include <linux/build_bug.h>

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
    /* 11 was IOC_LIST_MODULES; module enumeration now lives in ncore (userspace). */
    IOC_SET_SEPOLICY = 12, /* data = KernelSU-format sepolicy batch, size = 长度 */
    /*
     * data[<= NKSU_PROFILE_TEXT_MAX] (out) = the profile table as text, one
     * `<uid> <caps_hex> <ns> <domain>` line per entry.  Returns the number of
     * bytes written, or -ENOSPC when the buffer is too small.  Backs the
     * manager's JNI listProfiles() so it never has to persist the profiles.
     */
    IOC_GET_PROFILES = 13,
    /*
     * data[<= 64] (out) = the kernel module's build version string
     * (NKSU_GIT_COMMIT), NUL-terminated.  The manager compares it with its own
     * bound version so it never runs a userspace ncore that does not match the
     * flashed LKM.
     */
    IOC_GET_VERSION = 14,
    /*
     * Kernel feature toggles (see manager/feature.h).  LIST serialises the
     * registered features as "<id> <name> <value>\n" lines; GET/SET act on a
     * single feature id.  Backs the manager's JNI feature query/toggle.
     */
    IOC_FEATURE_LIST = 15, /* data[<= NKSU_FEATURE_TEXT_MAX] out = feature table */
    IOC_FEATURE_GET = 16,  /* data[12] = uint32_t id (in) | uint64_t value (out) */
    IOC_FEATURE_SET = 17,  /* data[12] = uint32_t id | uint64_t value */
};

#define NKSU_PROFILE_TEXT_MAX (64 * 1024)
#define NKSU_FEATURE_TEXT_MAX 1024

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
#define FMAC_DATA_FEATURE   12

/*
 * prctl opcodes.  201 and 203 are reserved for the manager (is_manager-gated);
 * 204 lets the boot-time daemon obtain the control fd (root-gated) so it can
 * send IOC_SET_SEPOLICY.
 */
#define NKSU_PRCTL_GET_DRIVER_FD 204

#define FMAC_OFF_UID     0
#define FMAC_OFF_CAPS    4
#define FMAC_OFF_DOMAIN  12
#define FMAC_OFF_NS      76
#define FMAC_OFF_TGT     64
#define FMAC_OFF_CLS     128
#define FMAC_OFF_PERM    192
#define FMAC_OFF_EFFECT  256
#define FMAC_OFF_INVERT  260
#define FMAC_OFF_FEATURE_VALUE 4

/*
 * The flag payloads are a wire contract shared with the manager, and the
 * offsets are hand-written, so pin them against the element sizes at compile
 * time (C11 static_assert).  Any drift here changes the on-wire ABI.
 */
static_assert(sizeof(struct fmac_ioc) == 2 * sizeof(unsigned int),
	      "fmac_ioc header must be two u32s");
static_assert(FMAC_DATA_UID == sizeof(unsigned int), "IOC uid is a u32");
static_assert(FMAC_DATA_EFD == sizeof(int), "IOC eventfd is an int");
static_assert(FMAC_DATA_CHKWRITE == sizeof(int), "IOC chkwrite is an int");
static_assert(FMAC_OFF_CAPS == FMAC_OFF_UID + FMAC_DATA_UID, "uid/caps layout");
static_assert(FMAC_DATA_CAP == FMAC_OFF_CAPS + sizeof(u64), "cap payload size");
static_assert(FMAC_OFF_DOMAIN == FMAC_OFF_CAPS + sizeof(u64), "caps/domain layout");
static_assert(FMAC_OFF_NS == FMAC_OFF_DOMAIN + 64, "domain/ns layout");
static_assert(FMAC_DATA_PROFILE == FMAC_OFF_NS + sizeof(int), "profile payload size");
static_assert(FMAC_OFF_TGT == FMAC_OFF_UID + 64, "rule src/tgt layout");
static_assert(FMAC_OFF_CLS == FMAC_OFF_TGT + 64, "rule tgt/cls layout");
static_assert(FMAC_OFF_PERM == FMAC_OFF_CLS + 64, "rule cls/perm layout");
static_assert(FMAC_OFF_EFFECT == FMAC_OFF_PERM + 64, "rule perm/effect layout");
static_assert(FMAC_OFF_INVERT == FMAC_OFF_EFFECT + sizeof(int), "rule effect/invert layout");
static_assert(FMAC_DATA_SELRULE == FMAC_OFF_INVERT + sizeof(int), "selrule payload size");
static_assert(FMAC_OFF_FEATURE_VALUE == FMAC_DATA_UID, "feature id/value layout");
static_assert(FMAC_DATA_FEATURE == FMAC_OFF_FEATURE_VALUE + sizeof(u64), "feature payload size");

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
