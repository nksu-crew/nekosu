#include <linux/nsproxy.h>
#include <linux/fs_struct.h>
#include <linux/sched/task.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/path.h>
#include <linux/dcache.h>
#include <fmac.h>

static int sync_fs_root(struct task_struct *target)
{
	struct path new_root;
	struct fs_struct *fs;
	int ret;

	task_lock(target);
	if (!target->fs) {
		task_unlock(target);
		return -EINVAL;
	}
	get_fs_root(target->fs, &new_root);
	task_unlock(target);

	ret = unshare_fs_struct();
	if (ret) {
		path_put(&new_root);
		return ret;
	}

	fs = current->fs;
	spin_lock(&fs->lock);
	path_get(&new_root);
	path_put(&fs->root);
	fs->root = new_root;

	path_get(&new_root);
	path_put(&fs->pwd);
	fs->pwd = new_root;
	spin_unlock(&fs->lock);

	path_put(&new_root);
	return 0;
}

int switch_to_init_ns(void)
{
	int ret;

	if (unlikely(!init_task.fs)) {
		pr_err("[ns]: init_task.fs is NULL\n");
		return -EINVAL;
	}

	get_nsproxy(&init_nsproxy);
	switch_task_namespaces(current, &init_nsproxy);

	ret = sync_fs_root(&init_task);
	if (ret)
		pr_warn("[ns]: mnt_ns root sync failed: %d\n", ret);
	else
		pr_info("[ns]: PID %d switched to init_ns\n",
			task_pid_nr(current));

	return ret;
}
