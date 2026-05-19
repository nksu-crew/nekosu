#include <fmac.h>

long handle_prctl_hooks(struct pt_regs *regs);
unsigned long try_redirect_path(struct pt_regs *regs,
					      int arg_index);