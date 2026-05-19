#ifndef TRACEPOINT_H
#define TRACEPOINT_H

void mark_threads_by_uid(uid_t uid);
void mark_threads_by_pid(pid_t pid);
int load_tracepoint_hook(void);
void unload_tracepoint_hook(void);

static __always_inline void log_upath(const char *tag, unsigned long uaddr)
{
#ifdef CONFIG_NKSU_DEBUG
	char buf[256];
	long n = strncpy_from_user(buf, (const char __user *)uaddr, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
		pr_info("[tracepoint]: %s -> %s\n", tag, buf);
	}
#endif
}

#endif /* TRACEPOINT_H */