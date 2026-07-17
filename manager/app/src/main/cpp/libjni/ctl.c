#include "ctl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/eventfd.h>
#include <sys/poll.h>
#include <stdint.h>

static void copy_to_char64(char dst[64], const char *s)
{
    if (s) {
        strncpy(dst, s, 63);
        dst[63] = '\0';
    } else {
        memset(dst, 0, 64);
    }
}

static int scan_fd_by_link(const char *target)
{
    DIR *dir;
    struct dirent *ent;
    char path[64];
    char link[256];
    int fdnum;

    dir = opendir("/proc/self/fd");
    if (!dir)
        return -1;

    errno = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        fdnum = atoi(ent->d_name);
        snprintf(path, sizeof(path), "/proc/self/fd/%s", ent->d_name);

        ssize_t len = readlink(path, link, sizeof(link) - 1);
        if (len < 0)
            continue;
        link[len] = '\0';

        if (strstr(link, target)) {
            closedir(dir);
            return fdnum;
        }
    }

    closedir(dir);
    return -1;
}

int ioctl_cmd(int fd, unsigned long cmd, void *arg)
{
    if (ioctl(fd, cmd, arg) < 0)
        return -1;
    return 0;
}

int ctl_prctl(unsigned int op)
{
    unsigned long rop = (unsigned long)op + 200;
    if (prctl(rop, 0, 0, 0, 0) < 0)
        return -1;
    return 0;
}

int Ctl(enum Opcode code)
{
    switch (code) {
    case OP_AUTHENTICATE:
    case OP_GET_ROOT:
    case OP_IOCTL:
        return ctl_prctl((unsigned int)code);
    default:
        errno = EINVAL;
        return -1;
    }
}

int SetProfile(int fd, int uid, uint64_t caps, const char *domain, int namespace)
{
    struct nksu_profile_data data;
    memset(&data, 0, sizeof(data));

    data.uid       = (unsigned int)uid;
    data.caps      = caps;
    copy_to_char64(data.selinux_domain, domain);
    data.namespace = (int)namespace;

    return ioctl_cmd(fd, IOC_SET_PROFILE, &data);
}

int AddUid(int fd, int uid)
{
    if (uid < 0) {
        errno = EINVAL;
        return -1;
    }
    unsigned int val = (unsigned int)uid;
    return ioctl_cmd(fd, IOC_ADD_UID, &val);
}

int DelUid(int fd, int uid)
{
    if (uid < 0) {
        errno = EINVAL;
        return -1;
    }
    unsigned int val = (unsigned int)uid;
    return ioctl_cmd(fd, IOC_DEL_UID, &val);
}

int HasUid(int fd, int uid, int *has)
{
    if (uid < 0 || !has) {
        errno = EINVAL;
        return -1;
    }
    unsigned int val = (unsigned int)uid;
    if (ioctl_cmd(fd, IOC_HAS_UID, &val) < 0)
        return -1;
    *has = (val != 0);
    return 0;
}

int SetCap(int fd, int uid, uint64_t caps)
{
    struct fmac_uid_cap uc;
    memset(&uc, 0, sizeof(uc));
    uc.uid  = (unsigned int)uid;
    uc.caps = caps;
    return ioctl_cmd(fd, IOC_SET_CAP, &uc);
}

int GetCap(int fd, int uid, uint64_t *caps)
{
    if (!caps) {
        errno = EINVAL;
        return -1;
    }
    struct fmac_uid_cap uc;
    memset(&uc, 0, sizeof(uc));
    uc.uid = (unsigned int)uid;

    if (ioctl_cmd(fd, IOC_GET_CAP, &uc) < 0)
        return -1;
    *caps = uc.caps;
    return 0;
}

int DelCap(int fd, int uid)
{
    struct fmac_uid_cap uc;
    memset(&uc, 0, sizeof(uc));
    uc.uid = (unsigned int)uid;
    return ioctl_cmd(fd, IOC_DEL_CAP, &uc);
}

int AddSelinuxRule(int fd, const char *src, const char *tgt,
                   const char *cls, const char *perm,
                   int effect, int invert)
{
    struct fmac_sepolicy_rule rule;
    memset(&rule, 0, sizeof(rule));

    copy_to_char64(rule.src,  src);
    copy_to_char64(rule.tgt,  tgt);
    copy_to_char64(rule.cls,  cls);
    copy_to_char64(rule.perm, perm);

    rule.effect = effect;
    rule.invert = invert ? 1 : 0;

    return ioctl_cmd(fd, IOC_SEL_ADD_RULE, &rule);
}

int ScanDriverFd(void)
{
    return scan_fd_by_link("[fmac_shm]");
}

int ScanCtlFd(void)
{
    return scan_fd_by_link("[fmac_ctl]");
}