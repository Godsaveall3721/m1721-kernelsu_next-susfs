#include <linux/cred.h>
#include <linux/errno.h>
#include <linux/susfs.h>
#include <linux/susfs_def.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "compat/kernel_compat.h"
#include "selinux/selinux.h"

#define KERNEL_SU_OPTION 0xDEADBEEF

bool susfs_is_current_ksu_domain(void)
{
	return is_ksu_domain();
}

bool susfs_is_current_zygote_domain(void)
{
	return is_zygote(current_cred());
}

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
void susfs_try_umount_all(uid_t uid)
{
	susfs_try_umount(uid);
}
#endif

static int susfs_reply(unsigned long arg5, int error)
{
	if (copy_to_user((int __user *)arg5, &error, sizeof(error)))
		pr_info("susfs: copy_to_user failed\n");
	return 0;
}

static bool susfs_user_accessible(unsigned long arg, size_t size)
{
	return arg && ksu_access_ok((void __user *)arg, size);
}

int ksu_handle_susfs_prctl(int option, unsigned long arg2, unsigned long arg3,
			   unsigned long arg4, unsigned long arg5)
{
	int error = 0;

	if (option != KERNEL_SU_OPTION)
		return -ENOSYS;
	if (current_uid().val != 0)
		return -EPERM;

#ifdef CONFIG_KSU_SUSFS_SUS_PATH
	if (arg2 == CMD_SUSFS_ADD_SUS_PATH) {
		if (!susfs_user_accessible(arg3, sizeof(struct st_susfs_sus_path)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_add_sus_path(
			(struct st_susfs_sus_path __user *)arg3);
		return susfs_reply(arg5, error);
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
	if (arg2 == CMD_SUSFS_ADD_SUS_MOUNT) {
		if (!susfs_user_accessible(arg3, sizeof(struct st_susfs_sus_mount)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_add_sus_mount(
			(struct st_susfs_sus_mount __user *)arg3);
		return susfs_reply(arg5, error);
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
	if (arg2 == CMD_SUSFS_ADD_SUS_KSTAT) {
		if (!susfs_user_accessible(arg3,
					   sizeof(struct st_susfs_sus_kstat)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_add_sus_kstat(
			(struct st_susfs_sus_kstat __user *)arg3);
		return susfs_reply(arg5, error);
	}
	if (arg2 == CMD_SUSFS_UPDATE_SUS_KSTAT) {
		if (!susfs_user_accessible(arg3,
					   sizeof(struct st_susfs_sus_kstat)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_update_sus_kstat(
			(struct st_susfs_sus_kstat __user *)arg3);
		return susfs_reply(arg5, error);
	}
	if (arg2 == CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY) {
		if (!susfs_user_accessible(arg3,
					   sizeof(struct st_susfs_sus_kstat)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_add_sus_kstat(
			(struct st_susfs_sus_kstat __user *)arg3);
		return susfs_reply(arg5, error);
	}
#endif

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
	if (arg2 == CMD_SUSFS_ADD_TRY_UMOUNT) {
		if (!susfs_user_accessible(arg3,
					   sizeof(struct st_susfs_try_umount)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_add_try_umount(
			(struct st_susfs_try_umount __user *)arg3);
		return susfs_reply(arg5, error);
	}
	if (arg2 == CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS) {
		susfs_run_try_umount_for_current_mnt_ns();
		return arg5 ? susfs_reply(arg5, 0) : 0;
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
	if (arg2 == CMD_SUSFS_SET_UNAME) {
		if (!susfs_user_accessible(arg3,
					   sizeof(struct st_susfs_uname)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_set_uname((struct st_susfs_uname __user *)arg3);
		return susfs_reply(arg5, error);
	}
#endif

#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
	if (arg2 == CMD_SUSFS_ENABLE_LOG) {
		if (!susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		if (arg3 > 1)
			return -EINVAL;
		susfs_set_log(arg3);
		return susfs_reply(arg5, 0);
	}
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
	if (arg2 == CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG) {
		if (!susfs_user_accessible(
			    arg3, SUSFS_FAKE_CMDLINE_OR_BOOTCONFIG_SIZE) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_set_cmdline_or_bootconfig((char __user *)arg3);
		return susfs_reply(arg5, error);
	}
#endif

#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
	if (arg2 == CMD_SUSFS_ADD_OPEN_REDIRECT) {
		if (!susfs_user_accessible(
			    arg3, sizeof(struct st_susfs_open_redirect)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		error = susfs_add_open_redirect(
			(struct st_susfs_open_redirect __user *)arg3);
		return susfs_reply(arg5, error);
	}
#endif

	if (arg2 == CMD_SUSFS_SHOW_VERSION) {
		const char version[] = SUSFS_VERSION;

		if (!susfs_user_accessible(arg3, sizeof(version)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		if (copy_to_user((void __user *)arg3, version, sizeof(version)))
			error = -EFAULT;
		return susfs_reply(arg5, error);
	}

	if (arg2 == CMD_SUSFS_SHOW_VARIANT) {
		const char variant[] = SUSFS_VARIANT;

		if (!susfs_user_accessible(arg3, sizeof(variant)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
		if (copy_to_user((void __user *)arg3, variant, sizeof(variant)))
			error = -EFAULT;
		return susfs_reply(arg5, error);
	}

	if (arg2 == CMD_SUSFS_SHOW_ENABLED_FEATURES) {
		u64 features = 0;

		if (!susfs_user_accessible(arg3, sizeof(features)) ||
		    !susfs_user_accessible(arg5, sizeof(error)))
			return -EFAULT;
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
		features |= BIT(0);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
		features |= BIT(1);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
		features |= BIT(2);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
		features |= BIT(3);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
		features |= BIT(4);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_OVERLAYFS
		features |= BIT(5);
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
		features |= BIT(6);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
		features |= BIT(7);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
		features |= BIT(8);
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
		features |= BIT(9);
#endif
#ifdef CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
		features |= BIT(10);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
		features |= BIT(11);
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
		features |= BIT(12);
#endif
#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
		features |= BIT(14);
#endif
		if (copy_to_user((void __user *)arg3, &features,
				 sizeof(features)))
			error = -EFAULT;
		return susfs_reply(arg5, error);
	}

	return -ENOSYS;
}
