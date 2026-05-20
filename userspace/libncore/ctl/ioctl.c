#include <linux/ioctl.h>
#include <sys/ioctl.h>

int bioctl(int fd, unsigned long cmd, void *arg) { return ioctl(fd, cmd, arg); }