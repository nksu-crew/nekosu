#include <stdint.h>
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

struct fmac_uid_cap {
  unsigned int uid;
  uint64_t caps;
};

#include <linux/ioctl.h>

#define FMAC_MAGIC 'F'

#define IOC_GET_SHM _IO(FMAC_MAGIC, 0)
#define IOC_BIND_EVT _IOW(FMAC_MAGIC, 1, int)
#define IOC_CHK_WRITE _IOR(FMAC_MAGIC, 2, int)
#define IOC_ADD_UID _IOW(FMAC_MAGIC, 3, int)
#define IOC_DEL_UID _IOW(FMAC_MAGIC, 4, int)
#define IOC_HAS_UID _IOWR(FMAC_MAGIC, 5, int)

#define IOC_SET_CAP _IOW(FMAC_MAGIC, 6, struct fmac_uid_cap)
#define IOC_GET_CAP _IOWR(FMAC_MAGIC, 7, struct fmac_uid_cap)
#define IOC_DEL_CAP _IOW(FMAC_MAGIC, 8, struct fmac_uid_cap)

#define IOC_SEL_ADD_RULE _IOW(FMAC_MAGIC, 9, struct fmac_sepolicy_rule)

#define IOC_SET_PROFILE _IOW(FMAC_MAGIC, 10, struct nksu_profile_data)
