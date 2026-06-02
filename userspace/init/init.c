#include <stdio.h>
#include <unistd.h>

#include "kmod.h"

extern const unsigned char _binary_nksu_ko_start[];
extern const unsigned char _binary_nksu_ko_end[];

int main(int argc, char *argv[], char *envp[]) {
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

  int fd = memfd_create("nksu", 0);

  write(fd, _binary_nksu_ko_start, _binary_nksu_ko_end - _binary_nksu_ko_start);

  char path[64];
  snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);

  kmod_load(path);
  execve("/init", argv, envp);
  return 0;
}