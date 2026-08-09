// kmod.h
#pragma once
#include <stddef.h>
#include <stdint.h>

struct ko_image {
    const char *kmi; /* KMI 标识，如 "android12-5.10" */
    int android;     /* Android 版本，如 15 */
    int major;       /* 内核主版本，如 6 */
    int minor;       /* 内核次版本，如 6 */
    const unsigned char *start;
    const unsigned char *end;
};

/*
 * 依次尝试加载 images 中的模块。
 * preferred < count 时先尝试该下标，失败后按序遍历其余；否则全部按序尝试。
 * 全部失败返回最后一个错误码（负 errno）。
 */
int kmod_load_many(const struct ko_image *images, size_t count, size_t preferred);
