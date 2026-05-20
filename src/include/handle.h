#include <fmac.h>

long handle_prctl_hooks(struct pt_regs *regs);
long hook_path_at(struct pt_regs *regs);
long hook__NR_execveat(struct pt_regs *regs);
long hook__NR_execve(struct pt_regs *regs);