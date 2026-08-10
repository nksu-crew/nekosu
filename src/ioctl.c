#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/anon_inodes.h>
#include <linux/version.h>
#include <linux/capability.h>

#include "fmac.h"
#include "ioctl.h"

static long ioc_get_shm(void)
{
    return fmac_anonfd_get();
}

static long ioc_bind_evt(const void __user *data, unsigned int size)
{
    int efd;

    if (size < sizeof(efd))
        return -EINVAL;
    if (copy_from_user(&efd, data, sizeof(efd)))
        return -EFAULT;

    return nksu_bind_eventfd(efd);
}

static long ioc_chk_write(void __user *data, unsigned int size)
{
    int changed = check_mmap_write() ? 1 : 0;

    if (size < sizeof(changed))
        return -EINVAL;
    return copy_to_user(data, &changed, sizeof(changed)) ? -EFAULT : 0;
}

static long ioc_add_uid(const void __user *data, unsigned int size)
{
    unsigned int id;

    if (size < sizeof(id))
        return -EINVAL;
    if (copy_from_user(&id, data, sizeof(id)))
        return -EFAULT;

    return nksu_profile_set_default((uid_t)id) ? -ENOMEM : 0;
}

static long ioc_del_uid(const void __user *data, unsigned int size)
{
    unsigned int id;

    if (size < sizeof(id))
        return -EINVAL;
    if (copy_from_user(&id, data, sizeof(id)))
        return -EFAULT;

    nksu_profile_clear((uid_t)id);
    return 0;
}

static long ioc_has_uid(void __user *data, unsigned int size)
{
    unsigned int id;

    if (size < sizeof(id))
        return -EINVAL;
    if (copy_from_user(&id, data, sizeof(id)))
        return -EFAULT;

    id = nksu_profile_has_uid((uid_t)id) ? 1 : 0;
    return copy_to_user(data, &id, sizeof(id)) ? -EFAULT : 0;
}

static long ioc_set_cap(const void __user *data, unsigned int size)
{
    unsigned int uid;
    u64 caps;

    if (size < FMAC_DATA_CAP)
        return -EINVAL;
    if (copy_from_user(&uid, data + FMAC_OFF_UID, sizeof(uid)) ||
        copy_from_user(&caps, data + FMAC_OFF_CAPS, sizeof(caps)))
        return -EFAULT;

    return nksu_profile_set_caps((uid_t)uid, u64_to_cap(caps));
}

static long ioc_get_cap(void __user *data, unsigned int size)
{
    kernel_cap_t caps;
    unsigned int uid;
    u64 caps_u64;

    if (size < FMAC_DATA_CAP)
        return -EINVAL;
    if (copy_from_user(&uid, data + FMAC_OFF_UID, sizeof(uid)))
        return -EFAULT;

    if (nksu_profile_get((uid_t)uid, &caps, NULL, NULL, 0))
        return -ENOENT;

    caps_u64 = cap_to_u64(caps);
    return copy_to_user(data + FMAC_OFF_CAPS, &caps_u64, sizeof(caps_u64)) ? -EFAULT : 0;
}

static long ioc_del_cap(const void __user *data, unsigned int size)
{
    kernel_cap_t empty = CAP_EMPTY_SET;
    unsigned int uid;

    if (size < FMAC_DATA_CAP)
        return -EINVAL;
    if (copy_from_user(&uid, data + FMAC_OFF_UID, sizeof(uid)))
        return -EFAULT;

    return nksu_profile_set_caps((uid_t)uid, empty);
}

static long ioc_sel_add_rule(const void __user *data, unsigned int size)
{
    char src[64], tgt[64], cls[64], perm[64];
    int effect, invert;

    if (size < FMAC_DATA_SELRULE)
        return -EINVAL;

    if (copy_from_user(src, data + FMAC_OFF_UID, sizeof(src)) ||
        copy_from_user(tgt, data + FMAC_OFF_TGT, sizeof(tgt)) ||
        copy_from_user(cls, data + FMAC_OFF_CLS, sizeof(cls)) ||
        copy_from_user(perm, data + FMAC_OFF_PERM, sizeof(perm)) ||
        copy_from_user(&effect, data + FMAC_OFF_EFFECT, sizeof(effect)) ||
        copy_from_user(&invert, data + FMAC_OFF_INVERT, sizeof(invert)))
        return -EFAULT;

    src[63] = '\0';
    tgt[63] = '\0';
    cls[63] = '\0';
    perm[63] = '\0';

    return sepolicy_add_rule(src[0] ? src : NULL, tgt[0] ? tgt : NULL, cls[0] ? cls : NULL,
                             perm[0] ? perm : NULL, effect, (bool)invert);
}

static long ioc_set_profile(const void __user *data, unsigned int size)
{
    unsigned int uid;
    u64 caps;
    char domain[64];
    int namespace;

    if (size < FMAC_DATA_PROFILE)
        return -EINVAL;

    if (copy_from_user(&uid, data + FMAC_OFF_UID, sizeof(uid)) ||
        copy_from_user(&caps, data + FMAC_OFF_CAPS, sizeof(caps)) ||
        copy_from_user(domain, data + FMAC_OFF_DOMAIN, sizeof(domain)) ||
        copy_from_user(&namespace, data + FMAC_OFF_NS, sizeof(namespace)))
        return -EFAULT;

    domain[63] = '\0';

    return nksu_profile_set((uid_t)uid, u64_to_cap(caps), domain, namespace);
}

static long fmac_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct fmac_ioc ioc;
    const void __user *data;

    if (_IOC_TYPE(cmd) != IOC_MAGIC || _IOC_NR(cmd) != IOC_CMD_NR)
        return -ENOTTY;

    if (copy_from_user(&ioc, (void __user *)arg, sizeof(ioc)))
        return -EFAULT;

    data = (const char __user *)arg + sizeof(ioc);

    switch (ioc.flag) {
    case IOC_GET_SHM:
        return ioc_get_shm();
    case IOC_BIND_EVT:
        return ioc_bind_evt(data, ioc.size);
    case IOC_CHK_WRITE:
        return ioc_chk_write((void __user *)data, ioc.size);
    case IOC_ADD_UID:
        return ioc_add_uid(data, ioc.size);
    case IOC_DEL_UID:
        return ioc_del_uid(data, ioc.size);
    case IOC_HAS_UID:
        return ioc_has_uid((void __user *)data, ioc.size);
    case IOC_SET_CAP:
        return ioc_set_cap(data, ioc.size);
    case IOC_GET_CAP:
        return ioc_get_cap((void __user *)data, ioc.size);
    case IOC_DEL_CAP:
        return ioc_del_cap(data, ioc.size);
    case IOC_SEL_ADD_RULE:
        return ioc_sel_add_rule(data, ioc.size);
    case IOC_SET_PROFILE:
        return ioc_set_profile(data, ioc.size);
    default:
        return -ENOTTY;
    }
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
