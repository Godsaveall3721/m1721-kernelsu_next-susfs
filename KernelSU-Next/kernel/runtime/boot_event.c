#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kmod.h>
#include <linux/namei.h>
#include <linux/printk.h>
#include <linux/workqueue.h>

#include "feature/selinux_hide.h"

#include "policy/allowlist.h"
#include "klog.h" // IWYU pragma: keep
#include "runtime/ksud_boot.h"
#include "runtime/ksud.h"
#include "selinux/selinux.h"
#include "manager/manager_observer.h"
#include "manager/throne_tracker.h"

bool ksu_module_mounted __read_mostly = false;
bool ksu_boot_completed __read_mostly = false;

extern void ksu_avc_spoof_late_init();

static int ksu_ksud_umh_init(struct subprocess_info *info, struct cred *new)
{
	return ksu_set_usermodehelper_cred(new);
}

static void ksu_run_ksud_post_fs_work(struct work_struct *work)
{
	static unsigned int attempts;
	char *argv[] = { (char *)KSUD_PATH, "post-fs-data", NULL };
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/system/sbin:/system/bin:/system/xbin",
		NULL
	};
	struct subprocess_info *info;
	int ret;

	info = call_usermodehelper_setup(argv[0], argv, envp, GFP_KERNEL,
					 ksu_ksud_umh_init, NULL, NULL);
	if (!info) {
		ret = -ENOMEM;
	} else {
		ret = call_usermodehelper_exec(info, UMH_WAIT_EXEC);
	}
	pr_info("ksud post-fs-data fallback: attempt=%u ret=%d\n",
		attempts, ret);

	if (attempts++ == 0)
		schedule_delayed_work(
			to_delayed_work(work), msecs_to_jiffies(20000));
}

static DECLARE_DELAYED_WORK(ksu_run_ksud_post_fs_work_item,
			    ksu_run_ksud_post_fs_work);

static void ksu_run_ksud_services_work(struct work_struct *work)
{
	char *argv[] = { (char *)KSUD_PATH, "services", NULL };
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/system/sbin:/system/bin:/system/xbin",
		NULL
	};
	struct subprocess_info *info;
	int ret;

	info = call_usermodehelper_setup(argv[0], argv, envp, GFP_KERNEL,
					 ksu_ksud_umh_init, NULL, NULL);
	if (!info) {
		ret = -ENOMEM;
	} else {
		ret = call_usermodehelper_exec(info, UMH_WAIT_PROC);
	}
	pr_info("ksud services fallback: ret=%d\n", ret);
}

static DECLARE_DELAYED_WORK(ksu_run_ksud_services_work_item,
			    ksu_run_ksud_services_work);

static void ksu_run_vectord_work(struct work_struct *work)
{
	char *argv[] = {
		"/system/bin/sh",
		"/data/adb/modules/zygisk_vector/run-vectord.sh",
		NULL
	};
	char *envp[] = {
		"HOME=/",
		"PATH=/sbin:/system/sbin:/system/bin:/system/xbin",
		NULL
	};
	struct subprocess_info *info;
	int ret;

	info = call_usermodehelper_setup(argv[0], argv, envp, GFP_KERNEL,
					 ksu_ksud_umh_init, NULL, NULL);
	if (!info) {
		ret = -ENOMEM;
	} else {
		ret = call_usermodehelper_exec(info, UMH_WAIT_EXEC);
	}
	pr_info("vectord fallback: ret=%d\n", ret);
}

static DECLARE_DELAYED_WORK(ksu_run_vectord_work_item,
			    ksu_run_vectord_work);

void on_post_fs_data(void)
{
	static bool done = false;
	if (done) {
		pr_info("on_post_fs_data already done\n");
		return;
	}
	done = true;
	pr_info("on_post_fs_data!\n");

	ksu_load_allow_list();
	ksu_observer_init();
	// sanity check, this may influence the performance
	ksu_stop_input_hook_runtime();
	ksu_selinux_hide_handle_post_fs_data();

	schedule_delayed_work(&ksu_run_ksud_post_fs_work_item, 0);
	// This vendor init consumes vold.decrypt before KernelSU's RC can observe it,
	// and refuses init-to-ksu transitions. Start module services through UMH instead.
	schedule_delayed_work(&ksu_run_ksud_services_work_item,
			      msecs_to_jiffies(25000));
	schedule_delayed_work(&ksu_run_vectord_work_item, 0);
}

extern void ext4_unregister_sysfs(struct super_block *sb);

int nuke_ext4_sysfs(const char *mnt)
{
	struct path path;
	int err = kern_path(mnt, 0, &path);
	if (err) {
		pr_err("nuke path err: %d\n", err);
		return err;
	}

	struct super_block *sb = path.dentry->d_inode->i_sb;
	const char *name = sb->s_type->name;
	if (strcmp(name, "ext4") != 0) {
		pr_info("nuke but module aren't mounted\n");
		path_put(&path);
		return -EINVAL;
	}

	ext4_unregister_sysfs(sb);
	path_put(&path);
	return 0;
}

void on_module_mounted(void)
{
	pr_info("on_module_mounted!\n");
	ksu_module_mounted = true;
}

void on_boot_completed(void)
{
    ksu_boot_completed = true;
    pr_info("on_boot_completed!\n");
    track_throne(true);
    ksu_selinux_hide_drop_backup_if_unused();
    ksu_avc_spoof_late_init();
}
