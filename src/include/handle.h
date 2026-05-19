#include <fmac.h>

long handle_prctl_hooks(struct pt_regs *regs);

static inline unsigned long try_redirect_path(struct pt_regs *regs,
					      int arg_index)
{
	char buf[MAX_PATH_LEN];
	unsigned long sp;
	const char __user *upath;

	if (!current->mm)
		return 0;

	upath = (const char __user *)regs->regs[arg_index];
	if (!upath)
		return 0;

	if (strncpy_from_user(buf, upath, sizeof(buf)) < 0)
		return 0;

	buf[sizeof(buf) - 1] = '\0';
	if (!path_is_su(buf))
		return 0;

	sp = user_stack_pointer(regs);
	if (!sp)
		return 0;

	return PUSH_STR(sp, SH_PATH, SH_PATH_LEN);
}