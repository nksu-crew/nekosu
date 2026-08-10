#ifndef HANDLE_H
#define HANDLE_H

#include <fmac.h>

long handle_prctl_hooks(struct pt_regs *regs);
long hook_path_at(struct pt_regs *regs);
long hook__NR_execveat(struct pt_regs *regs);
long hook__NR_execve(struct pt_regs *regs);

#ifndef CONFIG_NKSU_SYSCALL
void mark_threads_by_uid(uid_t uid);
void mark_threads_by_pid(pid_t pid);
int load_tracepoint_hook(void);
void unload_tracepoint_hook(void);
#endif

#endif /* HANDLE_H */