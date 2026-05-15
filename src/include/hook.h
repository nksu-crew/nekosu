#define SU_PATH             "/system/bin/su"
#define SU_PATH_LEN         (sizeof(SU_PATH))

#define REDIRECT_TARGET     "/data/adb/ksud"
#define REDIRECT_TARGET_LEN (sizeof(REDIRECT_TARGET))

#define SH_PATH             "/system/bin/sh"
#define SH_PATH_LEN         (sizeof(SH_PATH))

#define PUSH_STR(sp, str, len) ({                         \
    unsigned long _sp = (sp);                             \
    const char *_str = (str);                             \
    size_t _len = (len);                                  \
    unsigned long _addr = (_sp - _len) & ~15UL;     \
                                                          \
    copy_to_user((void __user *)_addr, _str, _len) ? 0 : _addr; \
})


static inline bool path_is_su(const char *p)
{
	return memcmp(p, SU_PATH, SU_PATH_LEN) == 0;
}

int init_syscall_hook(void);
void exit_syscall_hook(void);