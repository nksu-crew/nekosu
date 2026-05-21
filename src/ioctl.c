#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include <fmac.h>
#include <linux/version.h>
#include <linux/capability.h>

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

static long ioc_add_uid(unsigned long arg)
{
    unsigned int id;
    if (copy_from_user(&id, (unsigned int __user *)arg, sizeof(id)))
        return -EFAULT;
    return nksu_profile_set_default((uid_t)id) ? -ENOMEM : 0;
}

static long ioc_del_uid(unsigned long arg)
{
    unsigned int id;
    if (copy_from_user(&id, (unsigned int __user *)arg, sizeof(id)))
        return -EFAULT;
    nksu_profile_clear((uid_t)id);
    return 0;
}

static long ioc_has_uid(unsigned long arg)
{
    unsigned int id;
    if (copy_from_user(&id, (void __user *)arg, sizeof(id)))
        return -EFAULT;
    id = nksu_profile_has_uid((uid_t)id) ? 1 : 0;
    if (copy_to_user((void __user *)arg, &id, sizeof(id)))
        return -EFAULT;
    return 0;
}

static long ioc_set_cap(unsigned long arg)
{
    struct fmac_uid_cap uc;
    kernel_cap_t caps;

    if (copy_from_user(&uc, (void __user *)arg, sizeof(uc)))
        return -EFAULT;

    caps = u64_to_cap(uc.caps);

    return nksu_profile_set_caps((uid_t)uc.uid, caps);
}

static long ioc_get_cap(unsigned long arg)
{
    struct fmac_uid_cap uc;
    struct profile p;

    if (copy_from_user(&uc, (void __user *)arg, sizeof(uc)))
        return -EFAULT;

    if (nksu_profile_get_dup((uid_t)uc.uid, &p))
        return -ENOENT;

    uc.caps = cap_to_u64(p.caps);

    return copy_to_user((void __user *)arg, &uc, sizeof(uc)) ? -EFAULT : 0;
}

static long ioc_del_cap(unsigned long arg)
{
    struct fmac_uid_cap uc;
    kernel_cap_t empty = CAP_EMPTY_SET;

    if (copy_from_user(&uc, (void __user *)arg, sizeof(uc)))
        return -EFAULT;

    return nksu_profile_set_caps((uid_t)uc.uid, empty);
}

static long ioc_sel_add_rule(unsigned long arg)
{
    struct fmac_sepolicy_rule r;
    if (copy_from_user(&r, (void __user *)arg, sizeof(r)))
        return -EFAULT;
    r.src[sizeof(r.src) - 1] = '\0';
    r.tgt[sizeof(r.tgt) - 1] = '\0';
    r.cls[sizeof(r.cls) - 1] = '\0';
    r.perm[sizeof(r.perm) - 1] = '\0';
    return sepolicy_add_rule(r.src[0] ? r.src : NULL, r.tgt[0] ? r.tgt : NULL, r.cls[0] ? r.cls : NULL,
                             r.perm[0] ? r.perm : NULL, r.effect, (bool)r.invert);
}

static long ioc_set_profile(unsigned long arg)
{
    struct nksu_profile_data pd;
    kernel_cap_t caps;

    if (copy_from_user(&pd, (void __user *)arg, sizeof(pd)))
        return -EFAULT;

    caps = u64_to_cap(pd.caps);

    return nksu_profile_set((uid_t)pd.uid, caps, pd.selinux_domain, pd.namespace);
}

static long fmac_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    switch (cmd) {
    case IOC_GET_SHM:
        ret = fmac_anonfd_get();
        break;

    case IOC_BIND_EVT: {
        int efd;
        if (copy_from_user(&efd, (int __user *)arg, sizeof(efd)))
            return -EFAULT;
        ret = bind_eventfd(efd);
        break;
    }

    case IOC_CHK_WRITE: {
        int changed = check_mmap_write() ? 1 : 0;
        if (copy_to_user((int __user *)arg, &changed, sizeof(changed)))
            return -EFAULT;
        break;
    }

    case IOC_ADD_UID:
        return ioc_add_uid(arg);
    case IOC_DEL_UID:
        return ioc_del_uid(arg);
    case IOC_HAS_UID:
        return ioc_has_uid(arg);
    case IOC_SET_CAP:
        return ioc_set_cap(arg);
    case IOC_GET_CAP:
        return ioc_get_cap(arg);
    case IOC_DEL_CAP:
        return ioc_del_cap(arg);
    case IOC_SEL_ADD_RULE:
        return ioc_sel_add_rule(arg);
    case IOC_SET_PROFILE:
        return ioc_set_profile(arg);
    default:
        return -ENOTTY;
    }

    return ret;
}

static const struct file_operations fmac_ctl_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = fmac_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = fmac_ioctl,
#endif
};

int fmac_ctlfd_get(void)
{
    return anon_inode_getfd("[fmac_ctl]", &fmac_ctl_fops, NULL, O_RDWR | O_CLOEXEC);
}
