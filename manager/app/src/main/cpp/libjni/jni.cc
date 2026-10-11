#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/prctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string>
#include <vector>

#include "ioctl.h"
#include "log.h"

enum Opcode { OP_AUTHENTICATE = 201, OP_IOCTL = 203 };

class JniUtfString {
public:
  JniUtfString(JNIEnv *env, jstring jstr)
      : env_(env), jstr_(jstr), cstr_(nullptr) {
    if (jstr_ != nullptr) {
      cstr_ = env_->GetStringUTFChars(jstr_, nullptr);
    }
  }
  ~JniUtfString() {
    if (cstr_ != nullptr) {
      env_->ReleaseStringUTFChars(jstr_, cstr_);
    }
  }
  JniUtfString(const JniUtfString &) = delete;
  JniUtfString &operator=(const JniUtfString &) = delete;

  const char *c_str() const { return cstr_; }
  const char *c_str_or_empty() const { return cstr_ ? cstr_ : ""; }
  explicit operator bool() const { return cstr_ != nullptr; }

private:
  JNIEnv *env_;
  jstring jstr_;
  const char *cstr_;
};

#define FMAC_MAX_DATA 4096

namespace jni {
namespace {

inline void copy_field(void *dst, size_t cap, const char *s) {
  snprintf(static_cast<char *>(dst), cap, "%s", s ? s : "");
}

static int ioc_call(int fd, unsigned int flag, void *data, size_t size) {
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
  /*
   * Copy the payload back on any non-negative return.  Most flags return 0 on
   * success, but IOC_FEATURE_LIST / IOC_GET_PROFILES return the number of bytes
   * they wrote, so testing for 0 dropped the whole answer and looked like "the
   * kernel reported no features".
   */
  if (ret >= 0 && size)
    memcpy(data, u.msg.data, size);

  return ret;
}

static int scan_fd_by_link(const char *target) {
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

int Ctl(enum Opcode code) {
  switch (code) {
  case OP_AUTHENTICATE:
  case OP_IOCTL:
    return prctl((unsigned int)code, 0, 0, 0, 0);
  default:
    errno = EINVAL;
    return -1;
  }
}

int SetProfile(int fd, int uid, uint64_t caps, const char *domain, int ns) {
  uint8_t data[FMAC_DATA_PROFILE];
  uint32_t u = (uint32_t)uid;

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
  memcpy(data + FMAC_OFF_CAPS, &caps, sizeof(caps));
  copy_field(data + FMAC_OFF_DOMAIN, FMAC_OFF_NS - FMAC_OFF_DOMAIN, domain);
  memcpy(data + FMAC_OFF_NS, &ns, sizeof(ns));

  return ioc_call(fd, IOC_SET_PROFILE, data, sizeof(data));
}

int AddUid(int fd, int uid) {
  if (uid < 0) {
    errno = EINVAL;
    return -1;
  }
  uint32_t val = (uint32_t)uid;
  return ioc_call(fd, IOC_ADD_UID, &val, sizeof(val));
}

int DelUid(int fd, int uid) {
  if (uid < 0) {
    errno = EINVAL;
    return -1;
  }
  uint32_t val = (uint32_t)uid;
  return ioc_call(fd, IOC_DEL_UID, &val, sizeof(val));
}

int HasUid(int fd, int uid, int *has) {
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

int SetCap(int fd, int uid, uint64_t caps) {
  uint8_t data[FMAC_DATA_CAP];
  uint32_t u = (uint32_t)uid;

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
  memcpy(data + FMAC_OFF_CAPS, &caps, sizeof(caps));

  return ioc_call(fd, IOC_SET_CAP, data, sizeof(data));
}

int GetCap(int fd, int uid, uint64_t *caps) {
  uint8_t data[FMAC_DATA_CAP];
  uint32_t u = (uint32_t)uid;

  if (!caps) {
    errno = EINVAL;
    return -1;
  }

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));
  if (ioc_call(fd, IOC_GET_CAP, data, sizeof(data)) < 0)
    return -1;
  memcpy(caps, data + FMAC_OFF_CAPS, sizeof(*caps));
  return 0;
}

int DelCap(int fd, int uid) {
  uint8_t data[FMAC_DATA_CAP];
  uint32_t u = (uint32_t)uid;

  memset(data, 0, sizeof(data));
  memcpy(data + FMAC_OFF_UID, &u, sizeof(u));

  return ioc_call(fd, IOC_DEL_CAP, data, sizeof(data));
}

int AddSelinuxRule(int fd, const char *src, const char *tgt, const char *cls,
                   const char *perm, int effect, int invert) {
  uint8_t data[FMAC_DATA_SELRULE];
  int inv = invert ? 1 : 0;

  memset(data, 0, sizeof(data));
  copy_field(data + FMAC_OFF_UID, 64, src);
  copy_field(data + FMAC_OFF_TGT, 64, tgt);
  copy_field(data + FMAC_OFF_CLS, 64, cls);
  copy_field(data + FMAC_OFF_PERM, 64, perm);
  memcpy(data + FMAC_OFF_EFFECT, &effect, sizeof(effect));
  memcpy(data + FMAC_OFF_INVERT, &inv, sizeof(inv));

  return ioc_call(fd, IOC_SEL_ADD_RULE, data, sizeof(data));
}

int ScanDriverFd(void) { return scan_fd_by_link("[fmac_shm]"); }

int ScanCtlFd(void) { return scan_fd_by_link("[fmac_ctl]"); }

static int parse_gki_info(char *out_version, size_t out_size) {
  struct utsname uts;
  const char *release;
  char *endp;
  const char *p;
  const char *tag;
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
  major = (int)strtol(p, &endp, 10);
  p = endp;
  if (*p == '.') {
    p++;
    minor = (int)strtol(p, &endp, 10);
    p = endp;
  }

  if (major < 0 || minor < 0) {
    LOG_ERR("failed to parse kernel version: %s", release);
    return -1;
  }

  tag = strstr(release, "-android");
  if (tag) {
    const char *q = tag + strlen("-android");
    if (isdigit((unsigned char)*q)) {
      while (isdigit((unsigned char)*q))
        q++;
      if (*q == '-') {
        q++;
        if (isdigit((unsigned char)*q))
          is_gki = 1;
      }
    }
  }

  if (out_version && out_size > 0)
    snprintf(out_version, out_size, "%d.%02d", major, minor);

  LOG_INFO("kernel release=%s parsed_version=%d.%02d is_gki=%d", release, major,
           minor, is_gki);

  return is_gki;
}
} // namespace

static int fd = -1;
static int ctlfd = -1;
static JavaVM *g_vm = NULL;

namespace ncore {
static jint ctl(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  enum Opcode op;
  switch (value) {
  case 1:
    op = OP_AUTHENTICATE;
    break;
  case 3:
    op = OP_IOCTL;
    break;
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

static jint setProfile(JNIEnv *env, jobject thiz, jint uid, jlong caps,
                       jstring domainStr, jint ns) {
  (void)thiz;

  JniUtfString domain(env, domainStr);
  if (domainStr != nullptr && !domain) {
    return -1;
  }

  int ret = SetProfile(ctlfd, (int)uid, (uint64_t)caps, domain.c_str_or_empty(),
                       (int)ns);
  if (ret < 0) {
    LOG_ERR("setProfile failed");
    return -1;
  }
  return 0;
}

static jint adduid(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  if (AddUid(ctlfd, (int)value) < 0) {
    LOG_ERR("adduid failed");
    return -1;
  }
  return 0;
}

static jint deluid(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  if (DelUid(ctlfd, (int)value) < 0) {
    LOG_ERR("deluid failed");
    return -1;
  }
  return 0;
}

static jint hasuid(JNIEnv *env, jobject thiz, jint value) {
  (void)env;
  (void)thiz;

  int has = 0;
  if (HasUid(ctlfd, (int)value, &has) < 0) {
    return -1;
  }
  return has ? 1 : 0;
}

static jint setCap(JNIEnv *env, jobject thiz, jint uid, jlong caps) {
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

static jlong getCap(JNIEnv *env, jobject thiz, jint uid) {
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

static jint delCap(JNIEnv *env, jobject thiz, jint uid) {
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

static jint addSelinuxRule(JNIEnv *env, jobject thiz, jstring src, jstring tgt,
                           jstring cls, jstring permStr, jint effect,
                           jboolean invert) {
  (void)thiz;

  JniUtfString srcStr(env, src);
  JniUtfString tgtStr(env, tgt);
  JniUtfString clsStr(env, cls);
  JniUtfString permStrObj(env, permStr);

  if ((src != nullptr && !srcStr) || (tgt != nullptr && !tgtStr) ||
      (cls != nullptr && !clsStr) || (permStr != nullptr && !permStrObj)) {
    return -1;
  }

  int ret =
      AddSelinuxRule(ctlfd, srcStr.c_str_or_empty(), tgtStr.c_str_or_empty(),
                     clsStr.c_str_or_empty(), permStrObj.c_str_or_empty(),
                     (int)effect, invert ? 1 : 0);

  if (ret < 0) {
    LOG_ERR("addSelinuxRule failed");
    return -1;
  }
  return 0;
}

static jint addRule(JNIEnv *env, jobject thiz, jstring pathStr,
                    jlong statusBits) {
  (void)env;
  (void)thiz;
  (void)pathStr;
  (void)statusBits;
  return 0;
}

static jint delRule(JNIEnv *env, jobject thiz, jstring pathStr) {
  (void)env;
  (void)thiz;
  (void)pathStr;
  return 0;
}

/*
 * Ask the kernel for the current profile table (IOC_GET_PROFILES) and return
 * it as the same text the kernel persists to allow.profile.  The manager parses
 * this instead of keeping its own copy, so the kernel is the single source of
 * truth and no manager-side sync is needed.
 */
static jstring listProfiles(JNIEnv *env, jobject thiz) {
  (void)thiz;

  if (ctlfd < 0) {
    // Best effort: acquire the manager-gated control fd if nobody did yet.
    if (Ctl(OP_IOCTL) >= 0) {
      const int f = ScanCtlFd();
      if (f >= 0)
        ctlfd = f;
    }
  }

  if (ctlfd < 0) {
    LOG_ERR("listProfiles: control fd unavailable");
    return nullptr;
  }

  const uint32_t cap = 64 * 1024;
  // Keep the fmac_ioc header naturally aligned for the reinterpret_cast below.
  std::vector<uint32_t> buffer(
      (sizeof(struct fmac_ioc) + cap + sizeof(uint32_t) - 1) / sizeof(uint32_t),
      0);
  auto *raw = reinterpret_cast<uint8_t *>(buffer.data());
  auto *msg = reinterpret_cast<struct fmac_ioc *>(raw);
  msg->flag = IOC_GET_PROFILES;
  msg->size = cap;

  if (ioctl(ctlfd, IOC_CMD, raw) < 0) {
    LOG_ERR("listProfiles failed: %s", strerror(errno));
    return nullptr;
  }

  // The kernel only writes the text; the zero-filled tail keeps it NUL-ended.
  return env->NewStringUTF(reinterpret_cast<const char *>(msg->data));
}

/*
 * Read a file with root privileges, purely in userspace: fork `su -c cat` and
 * return its stdout as a byte array.  Used by the module WebUI to serve
 * root-only files without a shell round-trip in Kotlin.  Returns null on
 * failure (missing file, no root, ...).
 */
static jbyteArray readFile(JNIEnv *env, jobject thiz, jstring pathStr) {
  (void)thiz;
  if (pathStr == nullptr)
    return nullptr;

  const char *path = env->GetStringUTFChars(pathStr, nullptr);
  if (path == nullptr)
    return nullptr;

  std::string escaped;
  for (const char *p = path; *p != '\0'; ++p) {
    if (*p == '\'')
      escaped += "'\\''";
    else
      escaped += *p;
  }
  env->ReleaseStringUTFChars(pathStr, path);

  const std::string command =
      "PATH=/sbin:/system/sbin:/system/bin:/system/xbin; export PATH; cat '" +
      escaped + "'";

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return nullptr;

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return nullptr;
  }

  if (pid == 0) {
    // Only async-signal-safe calls here, before exec.
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[0]);
    close(pipefd[1]);
    const int devnull = open("/dev/null", O_WRONLY | O_CLOEXEC);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }
    execl("/system/bin/su", "su", "-c", command.c_str(),
          static_cast<char *>(nullptr));
    _exit(127);
  }

  close(pipefd[1]);

  std::vector<uint8_t> data;
  uint8_t buffer[65536];
  for (;;) {
    const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
    if (n > 0) {
      data.insert(data.end(), buffer, buffer + n);
    } else if (n == 0) {
      break;
    } else if (errno == EINTR) {
      continue;
    } else {
      break;
    }
  }
  close(pipefd[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || data.empty())
    return nullptr;

  jbyteArray result = env->NewByteArray(static_cast<jsize>(data.size()));
  if (result != nullptr) {
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(data.size()),
                            reinterpret_cast<const jbyte *>(data.data()));
  }
  return result;
}

static void helloLog(JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  LOG_DEBUG("Hello, this is a log from C!");
  LOG_INFO("ncore build-as lib (C version)");
}

static jboolean isGki(JNIEnv *env, jobject thiz) {
  (void)env;
  (void)thiz;
  int ret = parse_gki_info(NULL, 0);
  return (ret == 1) ? JNI_TRUE : JNI_FALSE;
}

static jstring kernelVersion(JNIEnv *env, jobject thiz) {
  (void)thiz;
  char ver[16];
  int ret = parse_gki_info(ver, sizeof(ver));
  if (ret < 0) {
    return NULL;
  }
  return env->NewStringUTF(ver);
}

/*
 * The running kernel module's build version (IOC_GET_VERSION).  The manager
 * binds its own version to it so it can tell that the flashed LKM is stale and
 * never install a userspace ncore that does not match it.
 */
static jstring moduleVersion(JNIEnv *env, jobject thiz) {
  (void)thiz;

  if (ctlfd < 0) {
    // Best effort: acquire the manager-gated control fd if nobody did yet.
    if (Ctl(OP_IOCTL) >= 0) {
      const int f = ScanCtlFd();
      if (f >= 0)
        ctlfd = f;
    }
  }

  if (ctlfd < 0) {
    LOG_ERR("moduleVersion: control fd unavailable");
    return nullptr;
  }

  char buf[64];
  memset(buf, 0, sizeof(buf));
  if (ioc_call(ctlfd, IOC_GET_VERSION, buf, sizeof(buf)) < 0) {
    LOG_ERR("moduleVersion failed: %s", strerror(errno));
    return nullptr;
  }
  buf[sizeof(buf) - 1] = '\0';
  return env->NewStringUTF(buf);
}
/*
 * Kernel feature toggles (IOC_FEATURE_*).  featureList() returns the
 * "<id> <name> <value>" text the kernel renders, so the manager can show which
 * features exist and whether they are on; featureGet/featureSet act on one.
 * The wire layout is a uint32 id followed by a uint64 value, matching
 * FMAC_OFF_FEATURE_VALUE.
 */
static jstring featureList(JNIEnv *env, jobject thiz) {
  (void)thiz;

  if (ctlfd < 0) {
    if (Ctl(OP_IOCTL) >= 0) {
      const int f = ScanCtlFd();
      if (f >= 0)
        ctlfd = f;
    }
  }
  if (ctlfd < 0) {
    LOG_ERR("featureList: control fd unavailable");
    return nullptr;
  }

  char buf[1024];
  memset(buf, 0, sizeof(buf));
  if (ioc_call(ctlfd, IOC_FEATURE_LIST, buf, sizeof(buf)) < 0) {
    LOG_ERR("featureList failed: %s", strerror(errno));
    return nullptr;
  }
  buf[sizeof(buf) - 1] = '\0';
  return env->NewStringUTF(buf);
}

static jlong featureGet(JNIEnv *env, jobject thiz, jint id) {
  (void)env;
  (void)thiz;

  if (ctlfd < 0) {
    if (Ctl(OP_IOCTL) >= 0) {
      const int f = ScanCtlFd();
      if (f >= 0)
        ctlfd = f;
    }
  }
  if (ctlfd < 0)
    return -1;

  uint8_t payload[FMAC_DATA_FEATURE];
  uint32_t fid = (uint32_t)id;
  uint64_t value = 0;
  memcpy(payload, &fid, sizeof(fid));
  memcpy(payload + FMAC_OFF_FEATURE_VALUE, &value, sizeof(value));

  if (ioc_call(ctlfd, IOC_FEATURE_GET, payload, sizeof(payload)) < 0) {
    LOG_ERR("featureGet(%d) failed: %s", id, strerror(errno));
    return -1;
  }
  memcpy(&value, payload + FMAC_OFF_FEATURE_VALUE, sizeof(value));
  return (jlong)value;
}

static jint featureSet(JNIEnv *env, jobject thiz, jint id, jlong value) {
  (void)env;
  (void)thiz;

  if (ctlfd < 0) {
    if (Ctl(OP_IOCTL) >= 0) {
      const int f = ScanCtlFd();
      if (f >= 0)
        ctlfd = f;
    }
  }
  if (ctlfd < 0)
    return -1;

  uint8_t payload[FMAC_DATA_FEATURE];
  uint32_t fid = (uint32_t)id;
  uint64_t fvalue = (uint64_t)value;
  memcpy(payload, &fid, sizeof(fid));
  memcpy(payload + FMAC_OFF_FEATURE_VALUE, &fvalue, sizeof(fvalue));

  if (ioc_call(ctlfd, IOC_FEATURE_SET, payload, sizeof(payload)) < 0) {
    LOG_ERR("featureSet(%d, %lld) failed: %s", id, (long long)value,
            strerror(errno));
    return -1;
  }
  return 0;
}
} // namespace ncore

const JNINativeMethod gMethods[] = {
    {"ctl", "(I)I", (void *)ncore::ctl},
    {"setProfile", "(IJLjava/lang/String;I)I", (void *)ncore::setProfile},
    {"adduid", "(I)I", (void *)ncore::adduid},
    {"deluid", "(I)I", (void *)ncore::deluid},
    {"hasuid", "(I)I", (void *)ncore::hasuid},
    {"setCap", "(IJ)I", (void *)ncore::setCap},
    {"getCap", "(I)J", (void *)ncore::getCap},
    {"delCap", "(I)I", (void *)ncore::delCap},
    {"addSelinuxRule",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
     "String;IZ)I",
     (void *)ncore::addSelinuxRule},
    {"addRule", "(Ljava/lang/String;J)I", (void *)ncore::addRule},
    {"delRule", "(Ljava/lang/String;)I", (void *)ncore::delRule},
    {"listProfiles", "()Ljava/lang/String;", (void *)ncore::listProfiles},
    {"readFile", "(Ljava/lang/String;)[B", (void *)ncore::readFile},
    {"helloLog", "()V", (void *)ncore::helloLog},
    {"isGki", "()Z", (void *)ncore::isGki},
    {"kernelVersion", "()Ljava/lang/String;", (void *)ncore::kernelVersion},
    {"moduleVersion", "()Ljava/lang/String;", (void *)ncore::moduleVersion},
    {"featureList", "()Ljava/lang/String;", (void *)ncore::featureList},
    {"featureGet", "(I)J", (void *)ncore::featureGet},
    {"featureSet", "(IJ)I", (void *)ncore::featureSet},
};

static int registerNativeMethods(JNIEnv *env) {
  jclass clazz = env->FindClass("me/nekosu/aqnya/ncore");
  if (clazz == NULL) {
    LOG_ERR("FindClass failed");
    return -1;
  }

  if (env->RegisterNatives(clazz, gMethods,
                           sizeof(gMethods) / sizeof(gMethods[0])) < 0) {
    LOG_ERR("RegisterNatives failed");
    return -1;
  }

  return 0;
}

bool authenticate(void) {
  if (Ctl(OP_AUTHENTICATE) < 0) {
    return false;
  }
  return true;
}

inline JNIEnv *GetJNIEnv(JavaVM *vm) {
  JNIEnv *env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
    LOG_ERR("GetEnv failed");
    return nullptr;
  }
  return env;
}

} // namespace jni

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
  (void)reserved;

  jni::g_vm = vm;

  JNIEnv *env = jni::GetJNIEnv(vm);
  if (!env)
    return -1;

  if (!jni::authenticate()) {
    LOG_ERR("ctl error: authenticate failed");
  }

  if (jni::registerNativeMethods(env) < 0) {
    LOG_ERR("registerNativeMethods failed");
    return -1;
  }

  return JNI_VERSION_1_6;
}