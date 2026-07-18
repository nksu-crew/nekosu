#include <jni.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <android/log.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/utsname.h>
#include "ctl.h"

#define LOG_TAG "ncore"
#define LOG_ERR(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOG_INFO(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOG_DEBUG(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

static int fd    = -1;
static int ctlfd = -1;
static JavaVM *g_vm = NULL;

static jint ncore_ctl(JNIEnv *env, jobject thiz, jint value)
{
    (void)env;
    (void)thiz;

    enum Opcode op;
    switch (value) {
    case 1: op = OP_AUTHENTICATE; break;
    case 2: op = OP_GET_ROOT;     break;
    case 3: op = OP_IOCTL;        break;
    default:
        return -1;
    }

    if (Ctl(op) < 0) {
        LOG_ERR("ctl error: operation failed");
    }

    if (value == 1) {
        int f = ScanDriverFd();
        if (f < 0) {
            LOG_ERR("fail to scan fd");
        } else {
            fd = f;
        }
    }

    if (value == 3) {
        int f = ScanCtlFd();
        if (f < 0) {
            LOG_ERR("fail to scan ctlfd");
        } else {
            ctlfd = f;
        }
        LOG_INFO("ctlfd after scan: %d", ctlfd);
    }

    LOG_INFO("ctl fd: %d", fd);
    return (fd < 0) ? -1 : 0;
}

static jint ncore_setProfile(JNIEnv *env, jobject thiz,
                             jint uid, jlong caps,
                             jstring domainStr, jint namespace)
{
    (void)thiz;

    const char *domain = NULL;
    if (domainStr != NULL) {
        domain = (*env)->GetStringUTFChars(env, domainStr, NULL);
        if (domain == NULL) {
            return -1;
        }
    }

    int ret = SetProfile(ctlfd, (int)uid, (uint64_t)caps,
                         domain ? domain : "", (int)namespace);

    if (domainStr != NULL) {
        (*env)->ReleaseStringUTFChars(env, domainStr, domain);
    }

    if (ret < 0) {
        LOG_ERR("setProfile failed");
        return -1;
    }
    return 0;
}

static jint ncore_adduid(JNIEnv *env, jobject thiz, jint value)
{
    (void)env;
    (void)thiz;

    if (AddUid(ctlfd, (int)value) < 0) {
        LOG_ERR("adduid failed");
        return -1;
    }
    return 0;
}

static jint ncore_deluid(JNIEnv *env, jobject thiz, jint value)
{
    (void)env;
    (void)thiz;

    if (DelUid(ctlfd, (int)value) < 0) {
        LOG_ERR("deluid failed");
        return -1;
    }
    return 0;
}

static jint ncore_hasuid(JNIEnv *env, jobject thiz, jint value)
{
    (void)env;
    (void)thiz;

    int has = 0;
    if (HasUid(ctlfd, (int)value, &has) < 0) {
        return -1;
    }
    return has ? 1 : 0;
}

static jint ncore_setCap(JNIEnv *env, jobject thiz,
                         jint uid, jlong caps)
{
    (void)env;
    (void)thiz;

    if (uid < 0) {
        return -1;
    }
    if (SetCap(ctlfd, (int)uid, (uint64_t)caps) < 0) {
        LOG_ERR("setCap failed");
        return -1;
    }
    return 0;
}

static jlong ncore_getCap(JNIEnv *env, jobject thiz, jint uid)
{
    (void)env;
    (void)thiz;

    if (uid < 0) {
        return -1;
    }
    uint64_t caps = 0;
    if (GetCap(ctlfd, (int)uid, &caps) < 0) {
        LOG_ERR("getCap failed");
        return -1;
    }
    return (jlong)caps;
}

static jint ncore_delCap(JNIEnv *env, jobject thiz, jint uid)
{
    (void)env;
    (void)thiz;

    if (uid < 0) {
        return -1;
    }
    if (DelCap(ctlfd, (int)uid) < 0) {
        LOG_ERR("delCap failed");
        return -1;
    }
    return 0;
}

static jint ncore_addSelinuxRule(JNIEnv *env, jobject thiz,
                                 jstring src, jstring tgt,
                                 jstring cls, jstring permStr,
                                 jint effect, jboolean invert)
{
    (void)thiz;

    #define GET_STR(jstr, cstr)                                          \
        const char *cstr = NULL;                                         \
        if (jstr != NULL) {                                              \
            cstr = (*env)->GetStringUTFChars(env, jstr, NULL);           \
            if (cstr == NULL) goto fail;                                 \
        }

    #define RELEASE_STR(jstr, cstr)                                      \
        if (jstr != NULL) (*env)->ReleaseStringUTFChars(env, jstr, cstr);

    GET_STR(src,     srcStr);
    GET_STR(tgt,     tgtStr);
    GET_STR(cls,     clsStr);
    GET_STR(permStr, permStr_);

    int ret = AddSelinuxRule(ctlfd,
                             srcStr   ? srcStr   : "",
                             tgtStr   ? tgtStr   : "",
                             clsStr   ? clsStr   : "",
                             permStr_ ? permStr_ : "",
                             (int)effect,
                             invert ? 1 : 0);

    RELEASE_STR(src,     srcStr);
    RELEASE_STR(tgt,     tgtStr);
    RELEASE_STR(cls,     clsStr);
    RELEASE_STR(permStr, permStr_);

    if (ret < 0) {
        LOG_ERR("addSelinuxRule failed");
        return -1;
    }
    return 0;

fail:
    RELEASE_STR(src,     srcStr);
    RELEASE_STR(tgt,     tgtStr);
    RELEASE_STR(cls,     clsStr);
    RELEASE_STR(permStr, permStr_);
    return -1;

    #undef GET_STR
    #undef RELEASE_STR
}

static jint ncore_addRule(JNIEnv *env, jobject thiz,
                          jstring pathStr, jlong statusBits)
{
    (void)env; (void)thiz; (void)pathStr; (void)statusBits;
    return 0;
}

static jint ncore_delRule(JNIEnv *env, jobject thiz, jstring pathStr)
{
    (void)env; (void)thiz; (void)pathStr;
    return 0;
}

static void ncore_helloLog(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    LOG_DEBUG("Hello, this is a log from C!");
    LOG_INFO("ncore build-as lib (C version)");
}

static int parse_gki_info(char *out_version, size_t out_size)
{
    struct utsname uts;
    const char *release, *p, *tag;
    int major = -1, minor = -1;
    int is_gki = 0;

    if (out_version && out_size > 0)
        out_version[0] = '\0';

    if (uname(&uts) != 0) {
        LOG_ERR("uname failed");
        return -1;
    }

    release = uts.release; /* e.g. "5.10.198-android12-9-g1234567" */

    p = release;
    major = (int)strtol(p, (char **)&p, 10);
    if (*p == '.') {
        p++;
        minor = (int)strtol(p, (char **)&p, 10);
    }

    if (major < 0 || minor < 0) {
        LOG_ERR("failed to parse kernel version: %s", release);
        return -1;
    }

    tag = strstr(release, "-android");
    if (tag) {
        const char *q = tag + strlen("-android");
        if (isdigit((unsigned char)*q)) {
            while (isdigit((unsigned char)*q)) q++;
            if (*q == '-') {
                q++;
                if (isdigit((unsigned char)*q))
                    is_gki = 1;
            }
        }
    }

    if (out_version && out_size > 0)
        snprintf(out_version, out_size, "%d.%02d", major, minor);

    LOG_INFO("kernel release=%s parsed_version=%d.%02d is_gki=%d",
             release, major, minor, is_gki);

    return is_gki;
}

static jboolean ncore_isGki(JNIEnv *env, jobject thiz)
{
    (void)env; (void)thiz;
    int ret = parse_gki_info(NULL, 0);
    return (ret == 1) ? JNI_TRUE : JNI_FALSE;
}

static jstring ncore_kernelVersion(JNIEnv *env, jobject thiz)
{
    (void)thiz;
    char ver[16];
    int ret = parse_gki_info(ver, sizeof(ver));
    if (ret < 0) {
        return NULL;
    }
    return (*env)->NewStringUTF(env, ver);
}

static const JNINativeMethod gMethods[] = {
    { "ctl",              "(I)I",                    (void *)ncore_ctl },
    { "setProfile",       "(IJLjava/lang/String;I)I",(void *)ncore_setProfile },
    { "adduid",           "(I)I",                    (void *)ncore_adduid },
    { "deluid",           "(I)I",                    (void *)ncore_deluid },
    { "hasuid",           "(I)I",                    (void *)ncore_hasuid },
    { "setCap",           "(IJ)I",                   (void *)ncore_setCap },
    { "getCap",           "(I)J",                    (void *)ncore_getCap },
    { "delCap",           "(I)I",                    (void *)ncore_delCap },
    { "addSelinuxRule",   "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IZ)I", (void *)ncore_addSelinuxRule },
    { "addRule",          "(Ljava/lang/String;J)I",  (void *)ncore_addRule },
    { "delRule",          "(Ljava/lang/String;)I",   (void *)ncore_delRule },
    { "helloLog",         "()V",                     (void *)ncore_helloLog },
    { "isGki",             "()Z",                      (void *)ncore_isGki },
    { "kernelVersion",      "()Ljava/lang/String;",     (void *)ncore_kernelVersion },
};

static int registerNativeMethods(JNIEnv *env)
{
    jclass clazz = (*env)->FindClass(env, "me/nekosu/aqnya/ncore");
    if (clazz == NULL) {
        LOG_ERR("FindClass failed");
        return -1;
    }

    if ((*env)->RegisterNatives(env, clazz, gMethods,
                                sizeof(gMethods) / sizeof(gMethods[0])) < 0) {
        LOG_ERR("RegisterNatives failed");
        return -1;
    }

    return 0;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;

    g_vm = vm;

    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        LOG_ERR("GetEnv failed");
        return -1;
    }

    if (Ctl(OP_AUTHENTICATE) < 0) {
        LOG_ERR("ctl error: authenticate failed");
    }

    if (registerNativeMethods(env) < 0) {
        LOG_ERR("registerNativeMethods failed");
        return -1;
    }

    return JNI_VERSION_1_6;
}