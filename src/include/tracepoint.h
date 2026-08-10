#ifndef TRACEPOINT_H
#define TRACEPOINT_H

void mark_threads_by_uid(uid_t uid);
void mark_threads_by_pid(pid_t pid);
int load_tracepoint_hook(void);
void unload_tracepoint_hook(void);

#endif /* TRACEPOINT_H */
