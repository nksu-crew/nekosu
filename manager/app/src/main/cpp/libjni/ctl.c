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

#define FMAC_MAX_DATA FMAC_DATA_SELRULE

static int ioc_call(int fd, unsigned int flag, void *data, size_t size)
{
    union {
        struct fmac_ioc msg;
        uint8_t raw[sizeof(struct fmac_ioc) + FMAC_MAX_DATA];
    } u;
    int ret;

    if (size > FMAC_MAX_DATA) {
        errno = EINVAL;
        return -1;
    }

    u.msg.flag = flag;
    u.msg.size = (uint32_t)size;
    if (size)
        memcpy(u.msg.data, data, size);

    ret = ioctl(fd, IOC_CMD, &u);
    if (ret == 0 && size)
        memcpy(data, u.msg.data, size);

    return ret;
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

int Ctl(enum Opcode code)
{
    switch (code) {
    case OP_AUTHENTICATE:
    case OP_GET_ROOT:
    case OP_IOCTL:
        return prctl((unsigned int)code, 0, 0, 0, 0);
    default:
        errno = EINVAL;
        return -1;
    }
}

int SetProfile(int fd, int uid, uint64_t caps, const char *domain, int namespace)
{
    uint8_t data[FMAC_DATA_PROFILE];
    uint32_t u = (uint32_t)uid;

    memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
    memcpy(data + FMAC_OFF_CAPS, &caps, sizeof(caps));
    copy_to_char64((char *)data + FMAC_OFF_DOMAIN, domain);
    memcpy(data + FMAC_OFF_NS, &namespace, sizeof(namespace));

    return ioc_call(fd, IOC_SET_PROFILE, data, sizeof(data));
}

int AddUid(int fd, int uid)
{
    if (uid < 0) {
        errno = EINVAL;
        return -1;
    }
    uint32_t val = (uint32_t)uid;
    return ioc_call(fd, IOC_ADD_UID, &val, sizeof(val));
}

int DelUid(int fd, int uid)
{
    if (uid < 0) {
        errno = EINVAL;
        return -1;
    }
    uint32_t val = (uint32_t)uid;
    return ioc_call(fd, IOC_DEL_UID, &val, sizeof(val));
}

int HasUid(int fd, int uid, int *has)
{
    if (uid < 0 || !has) {
        errno = EINVAL;
        return -1;
    }
    uint32_t val = (uint32_t)uid;
    if (ioc_call(fd, IOC_HAS_UID, &val, sizeof(val)) < 0)
        return -1;
    *has = (val != 0);
    return 0;
}

int SetCap(int fd, int uid, uint64_t caps)
{
    uint8_t data[FMAC_DATA_CAP];
    uint32_t u = (uint32_t)uid;

    memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
    memcpy(data + FMAC_OFF_CAPS, &caps, sizeof(caps));

    return ioc_call(fd, IOC_SET_CAP, data, sizeof(data));
}

int GetCap(int fd, int uid, uint64_t *caps)
{
    uint8_t data[FMAC_DATA_CAP];
    uint32_t u = (uint32_t)uid;

    if (!caps) {
        errno = EINVAL;
        return -1;
    }

    memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
    if (ioc_call(fd, IOC_GET_CAP, data, sizeof(data)) < 0)
        return -1;
    memcpy(caps, data + FMAC_OFF_CAPS, sizeof(*caps));
    return 0;
}

int DelCap(int fd, int uid)
{
    uint8_t data[FMAC_DATA_CAP];
    uint32_t u = (uint32_t)uid;

    memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
    memset(data + FMAC_OFF_CAPS, 0, sizeof(uint64_t));

    return ioc_call(fd, IOC_DEL_CAP, data, sizeof(data));
}

int AddSelinuxRule(int fd, const char *src, const char *tgt,
                   const char *cls, const char *perm,
                   int effect, int invert)
{
    uint8_t data[FMAC_DATA_SELRULE];
    int inv = invert ? 1 : 0;

    memset(data, 0, sizeof(data));
    copy_to_char64((char *)data + FMAC_OFF_UID,  src);
    copy_to_char64((char *)data + FMAC_OFF_TGT,  tgt);
    copy_to_char64((char *)data + FMAC_OFF_CLS,  cls);
    copy_to_char64((char *)data + FMAC_OFF_PERM, perm);
    memcpy(data + FMAC_OFF_EFFECT, &effect, sizeof(effect));
    memcpy(data + FMAC_OFF_INVERT, &inv, sizeof(inv));

    return ioc_call(fd, IOC_SEL_ADD_RULE, data, sizeof(data));
}

int ScanDriverFd(void)
{
    return scan_fd_by_link("[fmac_shm]");
}

int ScanCtlFd(void)
{
    return scan_fd_by_link("[fmac_ctl]");
}