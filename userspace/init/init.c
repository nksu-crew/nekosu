#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "kmod.h"

/* 各 KMI 版本嵌入的 nksu.ko（由 CMake 通过 llvm-objcopy 生成符号） */
extern const unsigned char _binary_android12_5_10_nksu_ko_start[];
extern const unsigned char _binary_android12_5_10_nksu_ko_end[];
extern const unsigned char _binary_android13_5_10_nksu_ko_start[];
extern const unsigned char _binary_android13_5_10_nksu_ko_end[];
extern const unsigned char _binary_android13_5_15_nksu_ko_start[];
extern const unsigned char _binary_android13_5_15_nksu_ko_end[];
extern const unsigned char _binary_android14_5_15_nksu_ko_start[];
extern const unsigned char _binary_android14_5_15_nksu_ko_end[];
extern const unsigned char _binary_android14_6_1_nksu_ko_start[];
extern const unsigned char _binary_android14_6_1_nksu_ko_end[];
extern const unsigned char _binary_android15_6_6_nksu_ko_start[];
extern const unsigned char _binary_android15_6_6_nksu_ko_end[];
extern const unsigned char _binary_android16_6_12_nksu_ko_start[];
extern const unsigned char _binary_android16_6_12_nksu_ko_end[];

#define KO_ENTRY(sym, kmi, android, major, minor)                          \
    {                                                                      \
        (kmi), (android), (major), (minor), _binary_##sym##_nksu_ko_start, \
            _binary_##sym##_nksu_ko_end                                    \
    }

static const struct ko_image ko_table[] = {
    KO_ENTRY(android12_5_10, "android12-5.10", 12, 5, 10),
    KO_ENTRY(android13_5_10, "android13-5.10", 13, 5, 10),
    KO_ENTRY(android13_5_15, "android13-5.15", 13, 5, 15),
    KO_ENTRY(android14_5_15, "android14-5.15", 14, 5, 15),
    KO_ENTRY(android14_6_1, "android14-6.1", 14, 6, 1),
    KO_ENTRY(android15_6_6, "android15-6.6", 15, 6, 6),
    KO_ENTRY(android16_6_12, "android16-6.12", 16, 6, 12),
};

#define KO_COUNT (sizeof(ko_table) / sizeof(ko_table[0]))

/* 解析 uname release，如 "5.10.198-android12-9-g1234567" */
static void parse_kernel_version(const char *release, int *major, int *minor, int *android)
{
    *major = *minor = *android = 0;

    const char *p = release;
    *major = (int)strtol(p, (char **)&p, 10);
    if (*p == '.') {
        p++;
        *minor = (int)strtol(p, (char **)&p, 10);
    }

    const char *tag = strstr(release, "-android");
    if (tag) {
        tag += strlen("-android");
        *android = (int)strtol(tag, NULL, 10);
    }
}

/* 精确匹配失败返回 KO_COUNT，由 kmod_load_many 遍历兜底 */
static size_t find_preferred(int major, int minor, int android)
{
    for (size_t i = 0; i < KO_COUNT; i++) {
        if (ko_table[i].major == major && ko_table[i].minor == minor &&
            ko_table[i].android == android)
            return i;
    }
    return KO_COUNT;
}

int main(int argc, char *argv[], char *envp[]) {

  mount("proc", "/proc", "proc", MS_NODEV | MS_NOEXEC | MS_NOSUID, NULL);

  const char *init = "/init.real";
  if (access(init, F_OK) != 0) {
    init = "/system/bin/init";
    if (access(init, F_OK) != 0) {
      return -1; // can't find out real init, panic.
    }
  }
  unlink("/init");
  int result = link(init, "/init");
  if (result != 0) {
    perror("link");
  }

  struct utsname uts;
  int major = 0, minor = 0, android = 0;
  size_t preferred = KO_COUNT;
  if (uname(&uts) == 0) {
    parse_kernel_version(uts.release, &major, &minor, &android);
    preferred = find_preferred(major, minor, android);
    fprintf(stderr, "nksu: kernel release=%s parsed=%d.%d android=%d\n",
            uts.release, major, minor, android);
  } else {
    perror("uname");
  }

  if (preferred < KO_COUNT)
    fprintf(stderr, "nksu: matched KMI %s\n", ko_table[preferred].kmi);
  else
    fprintf(stderr, "nksu: no exact KMI match, trying all variants\n");

  kmod_load_many(ko_table, KO_COUNT, preferred);
  umount2("/proc", MNT_DETACH);
  execve("/init", argv, envp);
  return 0;
}
