
Meizu m1721（Linux 4.9.330）上集成 KernelSU-Next SUSFS 的方案。
适配 gitee.com/curtis-ly/LineageOS-M6Note

后续实机经验见[第 12 节：ROM 属性与应用启动兼容](#rom-metadata-compat)。
该节记录用户空间属性调整，与第 1～11 节的内核集成和框架接入分开验证，
无需为这个问题重新编译或刷入内核。


## 1. 测试环境

| 项目 | 版本 |
| --- | --- |
| Android 内核 | `lineage-20` / `38d4f18dec05` |
| Linux 内核 | `4.9.330` |
| KernelSU-Next | `194a4d0531bba5810a7a0d1014acbbf34d9ae284` |
| KernelSU-Next 分支 | `origin/legacy` |
| KSU Hook 模式 | Manual |
| Manager | `KernelSU_Next_v3.1.0_33024-release.apk` |

Manager APK 位于：

```text
apk/KernelSU_Next_v3.1.0_33024-release.apk
SHA-256: 5bf844d1431127dbb76042e9341de19216fdd7d394c01b3c96cc21216c8c69a5
```

模块安装/卸载验证使用的上游 SUSFS 模块：

```text
susfs4ksu-v1.5.5.zip
SHA-256: 405ccca7e6b4bd22f0a6be43c63212ec2beacfe04215a68f01dea9432330ce43
```

固定 KernelSU-Next：

```bash
cd KernelSU-Next
git fetch origin legacy
git switch -C m1721-legacy 194a4d0531bba5810a7a0d1014acbbf34d9ae284
```

`194a4d05` 是兼容修补的基线；本文第 4、5 节列出的本地修改仍要应用。

Manager APK 的 v2 证书必须匹配：

```text
证书大小: 0x3e6 (998)
SHA-256: 79e590113c4c4c0c222978e413a5faa801666957b1212a328e46c00c69821bf7
```

该值与 KernelSU-Next `kernel/Kbuild` 默认值相同，不需要改签名。

## 2. Android 内核接入

以下命令均在内核根目录执行：

```bash
cd android_kernel_meizu_m1721
```

### 2.1 目录链接

```bash
ln -s ../../KernelSU-Next/kernel drivers/kernelsu
readlink -f drivers/kernelsu
```

输出必须是：

```text
KernelSU-Next/kernel
```

目录名统一为 `drivers/kernelsu`。不要写成 `drivers/kernel`。

### 2.2 defconfig

在 `arch/arm64/configs/m1721_defconfig` 中：

```diff
 CONFIG_MODULES=y
+CONFIG_KSU=y
```

本方案使用不带 stack protector 的实测组合：

```diff
-CONFIG_CC_STACKPROTECTOR_STRONG=y
+# CONFIG_CC_STACKPROTECTOR_STRONG is not set
+CONFIG_CC_STACKPROTECTOR_NONE=y
```

生成配置后确认：

```text
CONFIG_KSU=y
CONFIG_KSU_MANUAL_HOOK=y
# CONFIG_KPROBES is not set
CONFIG_CRYPTO_SHA256=y
```

干净 `origin/legacy` 没有 `CONFIG_KSU_SUSFS` 符号，所以 `.config`
中不会出现这一行；不要为了“确认关闭”而手工添加一个不存在的配置。

### 2.3 Kconfig 和 Makefile

`drivers/Kconfig`：

```diff
+source "drivers/kernelsu/Kconfig"
```

`drivers/Makefile`：

```diff
+obj-$(CONFIG_KSU) += kernelsu/
```

## 3. 手动 Hook

### 3.1 reboot

`kernel/reboot.c`：

```diff
 static DEFINE_MUTEX(reboot_mutex);

+#ifdef CONFIG_KSU
+extern int ksu_handle_sys_reboot(int magic1, int magic2, unsigned int cmd,
+				 void __user **arg);
+#endif

 SYSCALL_DEFINE4(reboot, int, magic1, int, magic2, unsigned int, cmd,
 		void __user *, arg)
 {
 	...
 	int ret = 0;

+#ifdef CONFIG_KSU
+	ksu_handle_sys_reboot(magic1, magic2, cmd, &arg);
+#endif
+
 	/* We only trust the superuser with rebooting the system. */
```

调用必须在普通权限检查之前。

### 3.2 exec

`fs/exec.c`：

```diff
+#ifdef CONFIG_KSU
+extern bool ksu_execveat_hook __read_mostly;
+extern int ksu_handle_execveat(int *fd, struct filename **filename_ptr,
+			       void *argv, void *envp, int *flags);
+extern int ksu_handle_execveat_sucompat(int *fd,
+				       struct filename **filename_ptr,
+				       void *argv, void *envp, int *flags);
+#endif

 static int do_execveat_common(int fd, struct filename *filename,
 			      struct user_arg_ptr argv,
 			      struct user_arg_ptr envp,
 			      int flags)
 {
 	...
 	int retval;

+#ifdef CONFIG_KSU
+	if (unlikely(ksu_execveat_hook))
+		ksu_handle_execveat(&fd, &filename, &argv, &envp, &flags);
+	else
+		ksu_handle_execveat_sucompat(&fd, &filename, &argv, &envp,
+					     &flags);
+#endif
+
 	if (IS_ERR(filename))
 		return PTR_ERR(filename);
```

带 `__read_mostly` 的声明必须位于文件作用域。

### 3.3 faccessat

`fs/open.c`：

```diff
 SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode)
 {
 	...
 	unsigned int lookup_flags = LOOKUP_FOLLOW;

+#ifdef CONFIG_KSU
+	extern int ksu_handle_faccessat(int *dfd,
+					const char __user **filename_user,
+					int *mode, int *flags);
+
+	ksu_handle_faccessat(&dfd, &filename, &mode, NULL);
+#endif
+
 	if (mode & ~S_IRWXO)
 		return -EINVAL;
```

### 3.4 init.rc 读取

`fs/read_write.c`：

```diff
+#ifdef CONFIG_KSU
+extern bool ksu_vfs_read_hook __read_mostly;
+extern int ksu_handle_vfs_read(struct file **file_ptr, char __user **buf_ptr,
+			       size_t *count_ptr, loff_t **pos);
+#endif

 ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
 {
 	ssize_t ret;

+#ifdef CONFIG_KSU
+	if (unlikely(ksu_vfs_read_hook))
+		ksu_handle_vfs_read(&file, &buf, &count, &pos);
+#endif
+
 	if (!(file->f_mode & FMODE_READ))
 		return -EBADF;
```

### 3.5 stat、sucompat 和 init.rc 大小修正

`fs/stat.c` 的 `vfs_fstatat()`：

```diff
+#ifdef CONFIG_KSU
+	extern int ksu_handle_stat(int *dfd,
+				   const char __user **filename_user,
+				   int *flags);
+
+	ksu_handle_stat(&dfd, &filename, &flag);
+#endif
```

Android init 使用 `fstat()` 按文件大小分配缓冲区，因此
`SYSCALL_DEFINE2(newfstat, ...)` 返回后还必须修正 `st_size`：

```diff
 SYSCALL_DEFINE2(newfstat, unsigned int, fd, struct stat __user *, statbuf)
 {
 	...
 	if (!error)
 		error = cp_new_stat(&stat, statbuf);

+#ifdef CONFIG_KSU
+	if (!error) {
+		extern void ksu_handle_newfstat_ret(
+			unsigned int *fd, struct stat __user **statbuf_ptr);
+
+		ksu_handle_newfstat_ret(&fd, &statbuf);
+	}
+#endif
+
 	return error;
 }
```

没有这一步时，init.rc 注入内容会被截断，
`/data/adb/ksud post-fs-data` 就不会执行，模块卸载会一直等待重启。

### 3.6 音量键安全模式

`drivers/input/input.c`：

```diff
+#ifdef CONFIG_KSU
+extern bool ksu_input_hook __read_mostly;
+extern int ksu_handle_input_handle_event(unsigned int *type,
+					 unsigned int *code, int *value);
+#endif

 static void input_handle_event(struct input_dev *dev,
 			       unsigned int type, unsigned int code, int value)
 {
 	int disposition = input_get_disposition(dev, type, code, &value);

+#ifdef CONFIG_KSU
+	if (unlikely(ksu_input_hook))
+		ksu_handle_input_handle_event(&type, &code, &value);
+#endif
+
 	if (disposition != INPUT_IGNORE_EVENT && type != EV_SYN)
```

## 4. KernelSU-Next 的 Linux 4.9 兼容

### 4.1 头文件兼容

`kernel/hook/setuid_hook.c`：

```diff
 #include <linux/version.h>
-#include <linux/sched/signal.h>
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
+#include <linux/sched/signal.h>
+#endif
```

`kernel/infra/su_mount_ns.c`：

```diff
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
 #include <linux/sched/task.h>
+#else
+#include <linux/sched.h>
+#endif
```

`kernel/policy/app_profile.c`：

```diff
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
 #include <linux/sched/user.h>
 #include <linux/sched/signal.h>
+#endif
```

### 4.2 4.9 的 kernel_read/write

Linux 4.9 使用：

```c
kernel_read(file, offset, buffer, count)
kernel_write(file, buffer, count, offset)
```

不要直接调用新版参数顺序。改用
`ksu_kernel_read_compat()` 和 `ksu_kernel_write_compat()`。

`kernel/policy/allowlist.c`：

```diff
-#include <linux/compiler_types.h>
+#include <linux/version.h>
+#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
+#include <linux/compiler_types.h>
+#endif
```

所有 allowlist 文件的 `kernel_read/write` 都替换为兼容函数。

`kernel/runtime/ksud_integration.c`：

```diff
-	r = kernel_read(f, module_rc_buf, fsize, &pos);
+	r = ksu_kernel_read_compat(f, module_rc_buf, fsize, &pos);
```

`kernel/manager/apk_sign.c`：

```diff
+#include "compat/kernel_compat.h"
...
-	return kernel_read(fp, buffer, size, pos);
+	return ksu_kernel_read_compat(fp, buffer, size, pos);
...
-	kernel_read(fp, eocd_buffer, search_size, &pos);
+	if (ksu_kernel_read_compat(fp, eocd_buffer, search_size, &pos) !=
+	    search_size) {
+		kvfree(eocd_buffer);
+		goto clean;
+	}
```

`kernel/manager/throne_tracker.c`：

```diff
+#include "compat/kernel_compat.h"
...
-	ssize_t count = kernel_read(fp, &chr, sizeof(chr), &pos);
+	ssize_t count =
+		ksu_kernel_read_compat(fp, &chr, sizeof(chr), &pos);
...
-	count = kernel_read(fp, buf, sizeof(buf) - 1, &line_start);
+	count = ksu_kernel_read_compat(fp, buf, sizeof(buf) - 1,
+				       &line_start);
```

`apk_sign.c` 和 `throne_tracker.c` 必须同时修好，否则 Manager
可能显示“无法找到 v2 签名”或无法被内核认证。

## 5. 实机发现的额外关键修复

这些不是可选项，否则会出现“App 配置更新失败”或
“模块卸载后一直提示重启”。

### 5.1 Manager 的 libksud root shell

Manager 的 root shell 执行的是自身 `libksud.so debug su`，不是
`/system/bin/su`。`kernel/feature/sucompat.c` 需要：

```diff
+#include "manager/manager_identity.h"
+
+static bool is_manager_libksud(const char *path)
+{
+	static const char suffix[] = "/libksud.so";
+	size_t path_len;
+	size_t suffix_len = sizeof(suffix) - 1;
+
+	if (!path)
+		return false;
+
+	path_len = strlen(path);
+	return path_len >= suffix_len &&
+	       !strcmp(path + path_len - suffix_len, suffix);
+}
...
 	if (IS_ERR(filename))
 		return 0;

+	if (unlikely(is_manager()) && is_manager_libksud(filename->name)) {
+		pr_info("manager libksud root shell\n");
+		return escape_with_root_profile();
+	}
```

该入口只对 Manager 自身 UID 生效。

### 5.2 App Profile v2/v3 与内核 v4 兼容

当前内核内部使用 App Profile v4，但 3.1.0 Manager 发送 v2。
直接在 ioctl 中复制完整 v4 结构会越界，版本检查也会拒绝配置。

已从该 APK 的 `libkernelsu.so` 核对：`setAppProfile` 将结构清零
`0x308` 字节（776 字节），并在结构起始处写入版本号 `2`。v3
使用相同前缀，v4 在末尾增加 `rp_config.profile.flags`。

`kernel/policy/allowlist.c` 中把已有的 `migrate_profile()` 去掉
`static`，以便 `dispatch.c` 调用：

```diff
-static void migrate_profile(u32 version, struct app_profile *profile)
+void ksu_migrate_app_profile(u32 version, struct app_profile *profile)
```

同时在 `kernel/policy/allowlist.h` 的 `ksu_set_app_profile()` 声明后加入：

```diff
 int ksu_set_app_profile(struct app_profile *);
+void ksu_migrate_app_profile(u32 version, struct app_profile *profile);
```

在 `kernel/supercall/dispatch.c` 的 `#include` 区之后、
`do_grant_root()` 之前添加：

```c
static size_t ksu_app_profile_user_size(u32 version)
{
	if (version < KSU_APP_PROFILE_VER)
		return offsetof(struct app_profile, rp_config.profile.flags);
	return sizeof(struct app_profile);
}
```

v2/v3 的结构在 `rp_config.profile.flags` 之前结束，不能按 v4
的完整大小复制；v4 使用完整结构大小。

将文件中的 `do_get_app_profile()` 完整替换为：

```c
static int do_get_app_profile(void __user *arg)
{
#ifdef CONFIG_KSU_DISABLE_POLICY
	return -EOPNOTSUPP;
#endif
	struct app_profile request = {};
	struct app_profile output;
	struct app_profile *profile;
	u32 version;
	size_t profile_size;
	int ret = 0;

	if (copy_from_user(&version, arg, sizeof(version))) {
		pr_err("get_app_profile: copy version failed\n");
		return -EFAULT;
	}
	if (version < 2 || version > KSU_APP_PROFILE_VER)
		return -EINVAL;

	profile_size = ksu_app_profile_user_size(version);
	if (copy_from_user(&request, arg, profile_size)) {
		pr_err("get_app_profile: copy_from_user failed\n");
		return -EFAULT;
	}

	rcu_read_lock();
	profile = ksu_get_app_profile(request.curr_uid);
	rcu_read_unlock();
	if (!profile) {
		ret = -ENOENT;
	} else {
		output = *profile;
		output.version = version;
		if (version < 4 && output.allow_su &&
		    !strcmp(output.rp_config.profile.selinux_domain,
			    KERNEL_SU_CONTEXT)) {
			strscpy_pad(output.rp_config.profile.selinux_domain,
				    "u:r:su:s0",
				    sizeof(output.rp_config.profile.selinux_domain));
		}
		if (copy_to_user(arg, &output, profile_size)) {
			pr_err("get_app_profile: copy_to_user failed\n");
			ret = -EFAULT;
		}
		ksu_put_app_profile(profile);
	}

	return ret;
}
```

将文件中的 `do_set_app_profile()` 完整替换为：

```c
static int do_set_app_profile(void __user *arg)
{
#ifdef CONFIG_KSU_DISABLE_POLICY
	return -EOPNOTSUPP;
#endif
	struct app_profile profile = {};
	u32 version;
	size_t profile_size;
	int ret;

	if (copy_from_user(&version, arg, sizeof(version))) {
		pr_err("set_app_profile: copy version failed\n");
		return -EFAULT;
	}
	if (version < 2 || version > KSU_APP_PROFILE_VER)
		return -EINVAL;

	profile_size = ksu_app_profile_user_size(version);
	if (copy_from_user(&profile, arg, profile_size)) {
		pr_err("set_app_profile: copy_from_user failed\n");
		return -EFAULT;
	}

	ksu_migrate_app_profile(version, &profile);

	ret = ksu_set_app_profile(&profile);
	if (!ret) {
		ksu_persistent_allow_list();
#ifdef KSU_KPROBES_HOOK
		ksu_mark_running_process();
#endif
	}

	return ret;
}
```

`request` 和 `profile` 使用 `{}` 清零后，再把旧版本内容复制到
v4 结构的前部。写入时由 `ksu_migrate_app_profile()` 补齐 v4
字段；读取时只复制旧结构大小，并把默认 KSU domain 转回
`u:r:su:s0`，避免越界和旧 Manager 解析失败。

### 5.3 ksud post-fs-data 自动执行兜底

即使 init.rc 已注入，某些 4.9 厂商 init 仍不会可靠执行
`/data/adb/ksud post-fs-data`。在 zygote 阶段增加兜底：

`kernel/runtime/boot_event.c`：

```c
#include <linux/kmod.h>
#include <linux/workqueue.h>

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
		ret = call_usermodehelper_exec(info, UMH_WAIT_PROC);
	}
	pr_info("ksud post-fs-data fallback: attempt=%u ret=%d\n",
		attempts, ret);

	if (attempts++ == 0)
		schedule_delayed_work(
			to_delayed_work(work), msecs_to_jiffies(20000));
}

static DECLARE_DELAYED_WORK(ksu_run_ksud_post_fs_work_item,
			    ksu_run_ksud_post_fs_work);
```

`kernel/selinux/selinux.c` 需要把 usermodehelper 的 cred 切换到
KernelSU 域：

```c
int ksu_set_usermodehelper_cred(struct cred *cred)
{
	if (!cred)
		return -EINVAL;
	return transive_to_domain(KERNEL_SU_CONTEXT, cred, false);
}
```

这一步是必需的。如果 `ksud` 继承 `u:r:kernel:s0`，Zygisk Next 的
`zygiskd` 子进程也会继承该域，随后因读取 vendor/system_data 被
SELinux 拒绝，并把 `module.prop` 改写成
`❌ Last injection failed!`。使用上面的 init 回调后，模块脚本在
`u:r:ksu:s0` 执行。

在 `on_post_fs_data()` 末尾调用：

```c
schedule_delayed_work(&ksu_run_ksud_post_fs_work_item, 0);
```

第一次立即执行，20 秒后直接执行第二次。不要在这里通过
`/system/bin/sh` 重试；m1721 的 kernel usermodehelper 环境会返回
`-EACCES`。

验证日志必须出现两次成功调用：

```text
ksud post-fs-data fallback: attempt=0 ret=0
ksud post-fs-data fallback: attempt=1 ret=0
```

### 5.4 modules_update 的 SELinux 兼容

KernelSU 3.1 安装模块时会把 `modules_update` 标记为
`system_file`。开机由内核启动的 `ksud` 必须有权读取、重命名和
删除该类型，否则模块会一直留在 `modules_update`，或卸载时出现
`Permission denied`。

在 `kernel/selinux/rules.c` 的 `apply_kernelsu_rules_fn()` 中，
放在 `ksu_allow(db, "init", ...)` 规则之后：

```c
// Kernel usermodehelper must process system_file-labelled modules_update.
ksu_allow(db, "kernel", "system_file", "dir", ALL);
ksu_allow(db, "kernel", "system_file", "file", ALL);
ksu_allow(db, "kernel", "system_file", "lnk_file", ALL);
```

这是内核策略修复，不需要 `chcon`，也不需要在开机后手工执行
脚本。

## 6. 源码修改清单

### 6.1 手动维护的修改

| 文件 | 用途 | 本文位置 |
| --- | --- | --- |
| `arch/arm64/configs/m1721_defconfig` | 启用 KSU、关闭 KPROBES 和 stack protector | 2.2 |
| `drivers/Kconfig` | 引入 KernelSU Kconfig | 2.3 |
| `drivers/Makefile` | 编译 `drivers/kernelsu` | 2.3 |
| `kernel/reboot.c` | reboot hook | 3.1 |
| `fs/exec.c` | execve hook | 3.2 |
| `fs/open.c` | faccessat hook | 3.3 |
| `fs/read_write.c` | init.rc 读取 hook | 3.4 |
| `fs/stat.c` | stat hook 和 newfstat 大小修正 | 3.5 |
| `drivers/input/input.c` | 音量键安全模式 hook | 3.6 |
| `kernel/feature/sucompat.c` | Manager 的 `libksud.so` root shell | 5.1 |
| `kernel/policy/allowlist.c` | 旧版 App Profile 迁移 | 4.2、5.2 |
| `kernel/policy/allowlist.h` | 迁移函数声明 | 5.2 |
| `kernel/supercall/dispatch.c` | App Profile v2/v3 兼容 | 5.2 |
| `kernel/runtime/boot_event.c` | `ksud post-fs-data` 两次直接调用 | 5.3 |
| `kernel/selinux/rules.c` | 允许内核进程处理 `system_file` 模块 | 5.4 |

KernelSU-Next 中另外的 4.9 兼容文件：

```text
kernel/hook/setuid_hook.c
kernel/infra/su_mount_ns.c
kernel/manager/apk_sign.c
kernel/manager/throne_tracker.c
kernel/policy/app_profile.c
kernel/runtime/ksud_integration.c
```

具体修改见第 4 节。

### 6.2 编译时自动修改

KSU Kbuild 首次编译会自动修改：

```text
fs/namespace.c
fs/internal.h
include/linux/seccomp.h
security/selinux/hooks.c
security/selinux/selinuxfs.c
security/selinux/xfrm.c
security/selinux/include/objsec.h
```

这些是 `path_umount`、seccomp cache 和 SELinux 兼容补丁，
不需要手工维护。重复编译不会重复插入。

## 7. 构建

```bash
cd android_kernel_meizu_m1721

if [[ -f out/.config ]]; then
  make O=out clean
fi

make O=out ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  m1721_defconfig

make -j"$(nproc)" O=out ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- \
  CROSS_COMPILE_ARM32=arm-linux-gnueabihf-
```

检查：

```bash
grep -E '^CONFIG_(KSU|KSU_MANUAL_HOOK|CRYPTO_SHA256)=' out/.config
grep -E ' ksu_handle_(sys_reboot|execveat|execveat_sucompat|faccessat|stat|newfstat_ret|vfs_read|input_handle_event)$' \
  out/System.map
grep 'ksu_run_ksud_post_fs_work' out/System.map
```

产物：

```text
out/arch/arm64/boot/Image
out/arch/arm64/boot/Image.gz
out/arch/arm64/boot/Image.gz-dtb
out/System.map
```

## 8. 打包 boot


### 从源码重建

```bash
./magiskboot unpack boot.img
rm kernel_dtb
cp android_kernel_meizu_m1721/out/arch/arm64/boot/Image.gz-dtb kernel
./magiskboot repack boot.img new_boot.img
cp new_boot.img new_boot.img
adb reboot bootloader
fastboot flash boot new_boot.img
fastboot reboot
```


## 9. 验证

### 9.1 Manager 和 root

```bash
adb shell su -c id
```

应返回：

```text
uid=0(root) ... context=u:r:ksu:s0
```

安装上面的 3.1.0 APK 后，Manager 中内核版本应显示：

```text
33004
```

### 9.2 App Profile

在 Manager 中给任意测试应用授权一次。必须能正常保存，


## 10. 可选：SUSFS 1.5.5

本节完全可选。只需要 KernelSU-Next 时，在前 10 节完成后停止，
不要应用本节补丁，也不要安装 `susfs4ksu` 模块。

### 10.1 固定版本

```text
SUSFS 分支: kernel-4.9
SUSFS 提交: 41ba0b533b8524a0ba95a5952506a75f17355450
SUSFS 版本: v1.5.5
变体: NON-GKI
模块: susfs4ksu-v1.5.5.zip
模块 SHA-256:
405ccca7e6b4bd22f0a6be43c63212ec2beacfe04215a68f01dea9432330ce43
```

SUSFS 源码位于：

```text
susfs4ksu
```

### 10.2 固定 SUSFS 源码

本机 `susfs4ksu` 仓库切到与 1.5.5 模块匹配的 4.9 分支：

```bash
cd susfs4ksu
git fetch origin kernel-4.9
git switch -C m1721-kernel-4.9 \
  41ba0b533b8524a0ba95a5952506a75f17355450
```

不要使用 `master`。`master` 的 1.3.8 补丁面向旧 KernelSU
目录结构，与当前模块化 KernelSU-Next 不匹配。

### 10.3 导入 SUSFS 内核源码

基于第 1～10 节已经完成的 KernelSU-Next 内核，复制新增文件：

```bash
cp "susfs4ksu/kernel_patches/fs/susfs.c" "android_kernel_meizu_m1721/fs/"
cp "susfs4ksu/kernel_patches/include/linux/susfs.h" "android_kernel_meizu_m1721/include/linux/"
cp "susfs4ksu/kernel_patches/include/linux/susfs_def.h" "android_kernel_meizu_m1721/include/linux/"
```

`susfs.h` 还需要包含 `linux/stat.h`，否则
`susfs_sus_ino_for_generic_fillattr()` 会使用不完整的
`struct kstat` 声明：

```diff
 #include <linux/hashtable.h>
 #include <linux/path.h>
+#include <linux/stat.h>
 #include <linux/susfs_def.h>
```

### 10.4 应用 4.9 内核 Hook

```bash
cd android_kernel_meizu_m1721
git apply --reject \
  susfs4ksu/kernel_patches/50_add_susfs_in_kernel-4.9.patch
find . -name '*.rej'
```

本厂商树有 3 个文件需要人工合并：

```text
fs/notify/fdinfo.c
fs/proc/cmdline.c
fs/proc/task_mmu.c
```

其余文件由原补丁直接应用，包括 dcache、namei、namespace、overlayfs、
proc namespace、readdir、stat、statfs、kallsyms 和 uname Hook。

#### fdinfo 差异

本机树已有 `file` 参数和 `inotify_mark_user_mask()`。保留这些接口，
在 `inotify_fdinfo()` 的 `if (inode)` 内加入：

```c
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
	if (likely(current->susfs_task_state &
		   TASK_STRUCT_NON_ROOT_USER_APP_PROC) &&
	    unlikely(inode->i_state & INODE_STATE_SUS_KSTAT)) {
		struct path path;
		char *pathname = kmalloc(PAGE_SIZE, GFP_KERNEL);
		char *dpath;

		if (!pathname)
			goto out_seq_printf;
		dpath = d_path(&file->f_path, pathname, PAGE_SIZE);
		if (IS_ERR(dpath))
			goto out_free_pathname;
		if (kern_path(dpath, 0, &path))
			goto out_free_pathname;

		seq_printf(m,
			   "inotify wd:%x ino:%lx sdev:%x mask:%x ignored_mask:0 ",
			   inode_mark->wd,
			   path.dentry->d_inode->i_ino,
			   path.dentry->d_inode->i_sb->s_dev,
			   inotify_mark_user_mask(mark));
		show_mark_fhandle(m, path.dentry->d_inode);
		seq_putc(m, '\n');
		iput(inode);
		path_put(&path);
		kfree(pathname);
		return;
out_free_pathname:
		kfree(pathname);
	}
out_seq_printf:
#endif
```

#### cmdline 差异

本机使用 `new_command_line`，不是 `saved_command_line`：

```diff
+#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
+extern int susfs_spoof_cmdline_or_bootconfig(struct seq_file *m);
+#endif

 static int cmdline_proc_show(struct seq_file *m, void *v)
 {
+#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
+	if (!susfs_spoof_cmdline_or_bootconfig(m)) {
+		seq_putc(m, '\n');
+		return 0;
+	}
+#endif
 	seq_printf(m, "%s\n", new_command_line);
```

#### task_mmu 差异

补 include 和 extern：

```diff
 #include <linux/mm_inline.h>
+#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
+#include <linux/susfs_def.h>
+#endif

+#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
+extern void susfs_sus_ino_for_show_map_vma(unsigned long ino,
+					   dev_t *out_dev,
+					   unsigned long *out_ino);
+#endif
```

`show_map_vma()` 内对 `INODE_STATE_SUS_KSTAT` 的调用和
`bypass_orig_flow` 标签由原补丁的第三个 hunk 直接写入。

### 10.5 KernelSU-Next 接口

SUSFS 通过 `prctl(0xDEADBEEF, cmd, ...)` 操作内核。当前
KernelSU-Next 使用模块化目录，不能应用旧 `core_hook.c` 补丁。

#### Kconfig

在 `KernelSU-Next/kernel/Kconfig` 增加：

```kconfig
menu "KernelSU - SUSFS"

config KSU_SUSFS
	bool "KernelSU addon - SUSFS"
	depends on KSU
	default n

config KSU_SUSFS_HAS_MAGIC_MOUNT
	bool "KernelSU uses Magic Mount"
	depends on KSU_SUSFS
	default n

config KSU_SUSFS_SUS_PATH
	bool "Hide suspicious paths"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_SUS_MOUNT
	bool "Hide suspicious mounts"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
	bool "Automatically hide KernelSU default mounts"
	depends on KSU_SUSFS_SUS_MOUNT && KSU_SUSFS_HAS_MAGIC_MOUNT
	default n

config KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
	bool "Automatically hide bind mounts"
	depends on KSU_SUSFS_SUS_MOUNT && KSU_SUSFS_HAS_MAGIC_MOUNT
	default n

config KSU_SUSFS_SUS_KSTAT
	bool "Spoof suspicious kstat"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_SUS_OVERLAYFS
	bool "Spoof overlayfs kstat and statfs"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_TRY_UMOUNT
	bool "Use SUSFS try_umount"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
	bool "Automatically add bind mounts to try_umount"
	depends on KSU_SUSFS_TRY_UMOUNT && KSU_SUSFS_HAS_MAGIC_MOUNT
	default n

config KSU_SUSFS_SPOOF_UNAME
	bool "Spoof uname"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_ENABLE_LOG
	bool "Enable SUSFS kernel logging"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
	bool "Hide KernelSU and SUSFS symbols"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
	bool "Spoof proc cmdline"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_OPEN_REDIRECT
	bool "Enable open redirect"
	depends on KSU_SUSFS
	default y

config KSU_SUSFS_SUS_SU
	bool "Enable deprecated SUS_SU"
	depends on KSU_SUSFS && KPROBES
	default n

endmenu
```

#### 编译新增文件

在 `KernelSU-Next/kernel/Kbuild` 的 `sulog/fd.o` 后加入：

```make
ifeq ($(CONFIG_KSU_SUSFS),y)
kernelsu-objs += susfs/susfs_cmd.o
endif
```

新增 `KernelSU-Next/kernel/susfs/susfs_cmd.c`。该文件负责：

- 实现 `susfs_is_current_ksu_domain()`。
- 实现 `susfs_is_current_zygote_domain()`。
- 实现 `susfs_try_umount_all()`。
- 把 PRCTL 命令转交给 `fs/susfs.c`。
- 返回 SUSFS 版本、变体和功能位。

命令处理必须只允许 UID 0，并在每次 `copy_from_user` 前用
`ksu_access_ok()` 校验参数。完整实现位于当前工作树：

```text
KernelSU-Next/kernel/susfs/susfs_cmd.c
```

#### 初始化

`kernel/core/init.c`：

```c
#ifdef CONFIG_KSU_SUSFS
#include <linux/susfs.h>
#endif
```

在 `ksu_feature_init()` 后：

```c
#ifdef CONFIG_KSU_SUSFS
	susfs_init();
#endif
```

#### setuid 和 umount

`kernel/hook/setuid_hook.c` 在非 root 应用切换时设置任务状态：

```c
#ifdef CONFIG_KSU_SUSFS
	if (ksu_is_allow_uid_for_current(new_uid))
		current->susfs_task_state &=
			~TASK_STRUCT_NON_ROOT_USER_APP_PROC;
	else
		current->susfs_task_state |=
			TASK_STRUCT_NON_ROOT_USER_APP_PROC;
#endif
```

在 KernelSU 默认 umount 前执行 SUSFS 自定义列表：

```c
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
	susfs_try_umount(new_uid);
#endif
	ksu_handle_umount(old_uid, new_uid);
```

`kernel/feature/kernel_umount.c` 增加包装：

```c
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
void ksu_try_umount(const char *mnt, bool check_mnt,
		    int flags, uid_t uid)
{
	try_umount(mnt, flags);
}
#endif
```

#### PRCTL 入口

在 Android 内核 `kernel/sys.c` 的 `SYSCALL_DEFINE5(prctl, ...)` 中，
调用安全 Hook 前加入：

```c
#ifdef CONFIG_KSU_SUSFS
	if (option == 0xDEADBEEF) {
		error = ksu_handle_susfs_prctl(option, arg2, arg3, arg4, arg5);
		if (error != -ENOSYS)
			return error;
	}
#endif
```

#### 执行域修复

开机兜底不能直接让 `ksud` 继承 `u:r:kernel:s0`，否则 Zygisk Next
的 `zygiskd` 会因 SELinux 拒绝而注入失败。在
`kernel/selinux/selinux.c` 增加：

```c
int ksu_set_usermodehelper_cred(struct cred *cred)
{
	if (!cred)
		return -EINVAL;
	return transive_to_domain(KERNEL_SU_CONTEXT, cred, false);
}
```

`kernel/runtime/boot_event.c` 使用 usermodehelper 的 init 回调：

```c
static int ksu_ksud_umh_init(struct subprocess_info *info, struct cred *new)
{
	return ksu_set_usermodehelper_cred(new);
}
```

启动时使用 `call_usermodehelper_setup(... ksu_ksud_umh_init ...)`，
再调用 `call_usermodehelper_exec(info, UMH_WAIT_EXEC)`，不能直接
使用不带 cred 回调的 `call_usermodehelper()`。

### 10.6 核心配置

内核补丁和 defconfig 已启用：

```text
CONFIG_KSU_SUSFS=y
CONFIG_KSU_SUSFS_SUS_PATH=y
CONFIG_KSU_SUSFS_SUS_MOUNT=y
CONFIG_KSU_SUSFS_SUS_KSTAT=y
CONFIG_KSU_SUSFS_SUS_OVERLAYFS=y
CONFIG_KSU_SUSFS_TRY_UMOUNT=y
CONFIG_KSU_SUSFS_SPOOF_UNAME=y
CONFIG_KSU_SUSFS_ENABLE_LOG=y
CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS=y
CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y
CONFIG_KSU_SUSFS_OPEN_REDIRECT=y
```

当前 KSU-Next 不是 Magic Mount，因此关闭：

```text
# CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT is not set
# CONFIG_KSU_SUSFS_SUS_SU is not set
```

### 10.7 构建和安装模块

按第 7、8 节构建和打包 boot。安装模块：

```bash
adb push susfs4ksu-v1.5.5.zip /data/local/tmp/
adb shell "su -c '
/data/adb/ksud module install /data/local/tmp/susfs4ksu-v1.5.5.zip
/data/adb/ksud post-fs-data
'"
```

### 10.8 激活验证

```bash
adb shell "su -c '
/data/adb/ksu/bin/ksu_susfs show version
/data/adb/ksu/bin/ksu_susfs show variant
/data/adb/ksu/bin/ksu_susfs show enabled_features
'"
```

必须分别输出：

```text
v1.5.5
NON-GKI
CONFIG_KSU_SUSFS_SUS_PATH
CONFIG_KSU_SUSFS_SUS_MOUNT
CONFIG_KSU_SUSFS_SUS_KSTAT
CONFIG_KSU_SUSFS_SUS_OVERLAYFS
CONFIG_KSU_SUSFS_TRY_UMOUNT
CONFIG_KSU_SUSFS_SPOOF_UNAME
CONFIG_KSU_SUSFS_ENABLE_LOG
CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
CONFIG_KSU_SUSFS_OPEN_REDIRECT
```

已验证的 SUSFS boot：

```text
/run/host/mnt/DataDisk/phone/x/fastboot/new_boot_susfs_v155_umhfix.img
SHA-256: 5354fd9106fc65e46e70fdf0ba5acfc678dfdd7b5feccba13584708e98299164
```

`susfs4ksu` 模块默认的隐藏命令都放在 `post-fs-data.sh` 中未执行
的 heredoc 示例里。内核支持已激活，但具体隐藏哪些路径/挂载，
仍需按实际需求启用对应 `ksu_susfs` 命令。

## 11. 可选：Zygisk Next 与 Vector

本节只适用于使用 Zygisk/Xposed 的情况，与 SUSFS 相互独立。只使用
KernelSU-Next 或只使用 SUSFS 时可以跳过。

本机实测的运行顺序是：

1. 安装 Zygisk Next。
2. 安装 Vector。
3. 确认 `zygiskd` 已经加载 Vector 的 32/64 位库。
4. 启动 `vectord`，由它向 `system_server` 注入 Vector 框架。
5. 从 KernelSU Manager 点击 Vector 的“执行”打开寄生 Manager。

### 11.1 Android 11 的 unshare 兼容

Vector 2.2 的 `service.sh` 使用：

```sh
unshare --propagation slave -m ...
```

本机 Android 11 的 toybox `unshare` 不支持 `--propagation`，会直接报：

```text
unshare: Unknown option 'propagation'
```

因此 `vectord` 不会启动。使用 KernelSU 自带 busybox，并用 `setsid`
让 daemon 脱离模块启动脚本的父进程。完整文件位于：

```text
fixes/zygisk-vector/action.sh
fixes/zygisk-vector/post-fs-data.sh
fixes/zygisk-vector/run-vectord.sh
fixes/zygisk-vector/service.sh
```

安装或更新 Vector 后执行：

```bash
adb push fixes/zygisk-vector/action.sh /data/local/tmp/vector-action.sh
adb push fixes/zygisk-vector/post-fs-data.sh /data/local/tmp/vector-post-fs-data.sh
adb push fixes/zygisk-vector/run-vectord.sh /data/local/tmp/run-vectord.sh
adb push fixes/zygisk-vector/service.sh /data/local/tmp/vector-service.sh
adb shell "su -c '
MOD=/data/adb/modules/zygisk_vector
cp -a \"\$MOD/action.sh\" \"\$MOD/action.sh.orig-m1721\" 2>/dev/null || true
cp -a \"\$MOD/post-fs-data.sh\" \"\$MOD/post-fs-data.sh.orig-m1721\" 2>/dev/null || true
cp -a \"\$MOD/service.sh\" \"\$MOD/service.sh.orig-m1721\"
cp /data/local/tmp/vector-action.sh \"\$MOD/action.sh\"
cp /data/local/tmp/vector-post-fs-data.sh \"\$MOD/post-fs-data.sh\"
cp /data/local/tmp/run-vectord.sh \"\$MOD/run-vectord.sh\"
cp /data/local/tmp/vector-service.sh \"\$MOD/service.sh\"
chmod 755 \"\$MOD/action.sh\" \"\$MOD/post-fs-data.sh\" \
  \"\$MOD/run-vectord.sh\" \"\$MOD/service.sh\"
'"
```

重新安装 Vector 会覆盖这个文件，需要再次执行上述命令。

本机 `ksud` 的临时子进程会在 service 阶段结束时被回收，因此不能只
依赖 `post-fs-data.sh` 或 `service.sh` 的后台进程。最终可靠入口是
`action.sh`：

1. 点击 Vector 的“执行”。
2. 如果 `vectord` 不存在，脚本启动 `run-vectord.sh`。
3. 等待约 30 秒完成 system_server 补注入。
4. 打开寄生 Manager。
5. action 进程保持运行并托管 daemon，不能立即退出。

重启后第一次点击“执行”会等待约 30 秒，这是正常现象。`post-fs-data.sh`
和 `service.sh` 仍保留为提前启动尝试；检测到已有 `vectord` 时直接退出。

### 11.2 KernelSU service 阶段兜底

本机 init 在解析 KernelSU 追加的 init.rc 前已经将
`vold.decrypt` 设为 `trigger_restart_framework`，导致原有的
`services` 触发分支没有执行。表现是模块 `service.sh` 全部不运行。
同时把 `sys.boot_completed` 分支加入 init.rc 也不够：厂商策略拒绝了
`init -> ksu` 的 bounded transition。

因此在 `KernelSU-Next/kernel/runtime/boot_event.c` 中，用已经验证可用的
usermodehelper 在 post-fs-data 后延迟 25 秒执行模块 service 阶段：

```c
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

schedule_delayed_work(&ksu_run_ksud_services_work_item,
		      msecs_to_jiffies(25000));
```

本机还可能遇到 Zygisk daemon 已 Hook init，但第一次 Zygote `execve`
已经进入内核，导致 `zygiskd status` 显示 `zygote_states:0`。此时 daemon
没有失效，只需在 Hook 已安装后再重启一次 Zygote。

Zygisk Next 1.5.2 的许可证禁止修改和再分发模块文件，因此不能直接改
`modules/zygisksu/service.sh`；这样做还会触发 `Module files corrupted`。
本方案保持 Zygisk Next 官方 ZIP 原样，只增加独立辅助脚本：

```text
fixes/service.d/m1721-zygisk-zygote-check.sh
```

关键逻辑：

```sh
if "$MOD/bin/zygiskd" status 2>/dev/null |
    grep -q '^zygote_states:0$'; then
  setprop ctl.restart zygote
  exit 0
fi
```

安装官方 Zygisk Next 后执行：

```bash
adb push fixes/service.d/m1721-zygisk-zygote-check.sh \
  /data/local/tmp/m1721-zygisk-zygote-check.sh
adb shell "su -c '
mkdir -p /data/adb/service.d
cp /data/local/tmp/m1721-zygisk-zygote-check.sh \
  /data/adb/service.d/m1721-zygisk-zygote-check.sh
chmod 755 /data/adb/service.d/m1721-zygisk-zygote-check.sh
'"
```

该内核上 `persist.sys.usb.config` 可能被厂商设为 `none`，SystemServer
重启时会导致 ADB 消失。需要 ADB 调试时设为：

```bash
adb shell "su -c '
settings put global development_settings_enabled 1
settings put global adb_enabled 1
setprop persist.sys.usb.config mtp,adb
'"
```

之后 Zygote 重启会重新枚举 USB，但不会再关闭 USB 调试。

临时验证模块 service 阶段可执行：

```bash
adb shell "su -c '/data/adb/ksud services'"
```

### 11.3 Vector 和 system_server 的 SELinux 权限

本机 `system_server` 的 `execmem` 请求曾被拒绝：

```text
avc: denied { execmem } for comm="system_server"
scontext=u:r:system_server:s0 tcontext=u:r:system_server:s0
tclass=process permissive=0
```

随后 Vector 注入触发系统服务崩溃。模块自己的 `sepolicy.rule` 在早期
启动阶段未能生效，因此在
`KernelSU-Next/kernel/selinux/rules.c` 的内置规则中增加：

```c
ksu_allow(db, "zygote", "zygote", "process", "execmem");
ksu_allow(db, "system_server", "system_server", "process", "execmem");

ksu_allow(db, "zygote", "unlabeled", "file", ALL);
ksu_allow(db, "zygote", "nsfs", "file", "open");
ksu_allow(db, "zygote", "nsfs", "file", "read");
ksu_allow(db, "zygote", "zygote", "capability", "sys_chroot");
ksu_allow(db, "zygote", "fs_type", "filesystem", "unmount");

ksu_allow(db, "shell", "adb_data_file", "dir", "search");
ksu_allow(db, "shell", "adb_data_file", "dir", "open");
ksu_allow(db, "shell", "adb_data_file", "dir", "read");
ksu_allow(db, "shell", "adb_data_file", "dir", "getattr");
ksu_allow(db, "shell", "system_file", "file", "open");
ksu_allow(db, "shell", "system_file", "file", "read");
ksu_allow(db, "shell", "system_file", "file", "getattr");
ksu_allow(db, "shell", "system_file", "file", "map");
ksu_allow(db, "shell", "shell", "dir", "write");
ksu_allow(db, "system_server", "adb_data_file", "dir", "search");
ksu_allow(db, "system_server", "system_file", "file", "open");
ksu_allow(db, "system_server", "system_file", "file", "read");
ksu_allow(db, "system_server", "system_file", "file", "getattr");
ksu_allow(db, "system_server", "system_file", "file", "map");

ksu_allow(db, "dex2oat", "dex2oat_exec", "file", "execute");
ksu_allow(db, "dex2oat", "dex2oat_exec", "file", "execute_no_trans");
ksu_allow(db, "dex2oat", "system_linker_exec", "file", "execute_no_trans");
```

修改后必须按第 7、8 节重新编译并刷入 boot。

### 11.4 验证

先确认 Zygisk Next 已加载 Vector 的两种 ABI：

```bash
adb shell "su -c '
/data/adb/modules/zygisksu/bin/zygiskd status
ps -A -o PID,USER,NAME,ARGS | grep -E \"zn-daemon|vectord\"
'"
```

关键结果：

```text
inject_state:1
modules64:3,...,zygisk_vector
modules32:3,...,zygisk_vector
modules_with_issue:0
```

Vector 注入成功时，logcat 必须出现：

```text
Injected Vector framework into system_server.
Successfully injected Vector IPC binder for applications.
```

并确认用户存储已经解锁，否则 Manager 会因
`SharedPreferences in credential encrypted storage` 退出：

```bash
adb shell 'dumpsys user | grep -m1 "State:"'
```

输出必须是：

```text
State: RUNNING_UNLOCKED
```

最后执行 Manager：

```bash
adb shell "su -c '/data/adb/ksud module action zygisk_vector'"
adb shell "dumpsys activity activities | grep -E 'mResumedActivity|MainActivity'"
```

应看到：

```text
com.android.shell/org.matrix.vector.manager.ui.MainActivity
```

若仍失败，先检查 `dmesg` 中是否还有新的 `execmem` 或
`manager.apk` SELinux 拒绝，再检查 `vectord` 是否运行。

<a id="rom-metadata-compat"></a>

## 12. 实机经验：ROM 属性与应用启动兼容

### 12.1 先明确：这次修复了什么

2026-10-27 在以下环境中，步道乐跑启动后输出
`UniverseSecNo2: [hit] No2`，随后自行结束进程：

| 项目 | 本次实测环境 |
| --- | --- |
| 手机 | Meizu M6 Note / m1721 |
| 系统 | Android 11 / LineageOS 18.1 |
| 原安装内核 | `4.9.330-g38d4f18dec05-dirty #25` |
| root / SUSFS | KernelSU Next 33004 / SUSFS 1.5.5 |
| 应用 | 官方步道乐跑 `com.lptiyu.tanke`，4.3.1 / versionCode 231，arm64-v8a |
| 最终验证范围 | 普通启动进入隐私说明页，普通重启后仍能启动 |

最终有效改动是清除四项 ROM 属性中的 `UNOFFICIAL` 标记。
这次成功没有更换最终运行的内核，原 boot 分区的校验值也保持一致。
诊断期间曾临时启动诊断内核，但最终修复与重启验证使用原安装内核。

`No2` 是保护组件的汇总结果，同一个编号可能由不同条件触发。
本节只记录本机已经验证的条件，不把它定义为所有设备上的固定错误，
也不承诺这一调整能通过所有 root、ROM 或完整性检查。

### 12.2 为什么四项需要一起核对

本次观察到保护组件读取 ROM 配置并进行字符串匹配。
只去掉第一项属性里的标记后，该项检查通过，但后面的属性仍然命中；
同时调整四项后，这部分配置检查才通过。

| 属性 | 本机调整前 | 本机调整后 |
| --- | --- | --- |
| `ro.lineage.display.version` | `18.1-20230910-UNOFFICIAL-m1721` | `18.1-20230910-m1721` |
| `ro.lineage.releasetype` | `UNOFFICIAL` | 空值 |
| `ro.lineage.version` | `18.1-20230910-UNOFFICIAL-m1721` | `18.1-20230910-m1721` |
| `ro.modversion` | `18.1-20230910-UNOFFICIAL-m1721` | `18.1-20230910-m1721` |

表中的版本号用于解释本机结果，其他 ROM 不要照抄这些值。
配套脚本读取设备当前值，仅在包含 `UNOFFICIAL` 时移除该标记，
保留版本和机型部分，不替换成 `OFFICIAL`，也不新增原本不存在的属性。

这改变的是系统全局可见的元数据，其他读取这些属性的程序也会看到调整值；
它不会把非官方 ROM 变成官方 ROM，也不会改变应用签名或内核实现。

### 12.3 第一步：确认症状并保存基线

电脑安装 Android platform-tools，在本仓库根目录打开终端。
以下命令逐行执行，适用于 PowerShell 或常见 Linux/macOS shell。
如果同时连接多个设备，每条 `adb` 命令都加上对应的 `-s` 参数。

```bash
adb devices
adb shell "su -c id"
adb shell getprop ro.lineage.display.version
adb shell getprop ro.lineage.releasetype
adb shell getprop ro.lineage.version
adb shell getprop ro.modversion
adb shell "su -c 'getprop ro.lineage.display.version; getprop ro.lineage.releasetype; getprop ro.lineage.version; getprop ro.modversion'" > rom-metadata-before.txt
```

先确认 root 返回 `uid=0`，再看四项是否包含 `UNOFFICIAL`。
如果都没有这个标记，本节没有对应的已验证触发条件，先继续定位原因。

重新打开应用后只查看相关日志：

```bash
adb shell am force-stop com.lptiyu.tanke
adb shell am start -n com.lptiyu.tanke/com.lptiyu.tanke.activities.splash.SplashActivity
adb logcat -d -v threadtime -s UniverseSecNo2:V
```

核对刚刚启动的时间和 PID，避免把旧日志当作新失败。
仅看到相同编号不足以证明原因相同；接下来的临时对照才用于判断是否适用。

### 12.4 第二步：只做临时对照

使用本次实机验证过的脚本：

```text
fixes/service.d/98-lepao-rom-metadata.sh
```

脚本依赖 KernelSU 自带的 `/data/adb/ksu/bin/resetprop`。
先核对文件可执行，再推送并运行一次：

```bash
adb shell "su -c 'test -x /data/adb/ksu/bin/resetprop && echo RESET_PROP_READY'"
adb push fixes/service.d/98-lepao-rom-metadata.sh /data/local/tmp/98-lepao-rom-metadata.sh
adb shell "su -c 'sh /data/local/tmp/98-lepao-rom-metadata.sh'"
adb shell getprop ro.lineage.display.version
adb shell getprop ro.lineage.releasetype
adb shell getprop ro.lineage.version
adb shell getprop ro.modversion
```

应看到原有版本号保留、相关标记消失；`ro.lineage.releasetype`
原值如果只有 `UNOFFICIAL`，输出一行空白是预期结果。

然后重新启动应用：

```bash
adb shell am force-stop com.lptiyu.tanke
adb shell am start -n com.lptiyu.tanke/com.lptiyu.tanke.activities.splash.SplashActivity
```

在手机上解锁并观察至少 30 秒，再检查：

```bash
adb shell pidof com.lptiyu.tanke
adb logcat -d -v threadtime -s UniverseSecNo2:V
```

进程应仍存在，没有对应本次启动的新 No2，且实际界面能显示。
只看到 PID 不能证明应用界面正常；息屏截图也不能用来判断应用黑屏。
隐私协议、登录和权限由使用者自行选择，本次验证停在隐私说明页。

本步骤使用 `resetprop -n`，只调整运行时属性，不写入 ROM 分区。
如果没有改善，先执行 `adb reboot`，重启并解锁后核对基线；
不要继续安装开机脚本。本次实测不需要关闭 root、框架或 USB 调试。

### 12.5 第三步：临时验证成功后再设置开机执行

只有上一节实际有效时再安装。以下命令拒绝覆盖同名文件；
若设备已有该脚本，先检查其内容与来源。

```bash
adb shell "su -c 'mkdir -p /data/adb/service.d'"
adb shell "su -c 'test ! -e /data/adb/service.d/98-lepao-rom-metadata.sh && cp /data/local/tmp/98-lepao-rom-metadata.sh /data/adb/service.d/98-lepao-rom-metadata.sh && chmod 700 /data/adb/service.d/98-lepao-rom-metadata.sh'"
adb shell "su -c 'ls -l /data/adb/service.d/98-lepao-rom-metadata.sh'"
adb reboot
```

等待 Android 启动完成并解锁，再核对：

```bash
adb shell getprop sys.boot_completed
adb shell "su -c id"
adb shell uname -a
adb shell getprop ro.lineage.display.version
adb shell getprop ro.lineage.releasetype
adb shell getprop ro.lineage.version
adb shell getprop ro.modversion
adb shell settings get global adb_enabled
adb shell settings get global development_settings_enabled
```

`sys.boot_completed` 应为 `1`，root 仍可用，属性调整应自动生效。
如果原先启用了 USB 调试和开发者选项，最后两项也应仍为 `1`。
再次按 12.4 节普通启动应用，检查真实界面和本次日志。

本机重启后这些检查均通过；应用同一进程继续存活超过 15 分钟，
没有新增 No2。原内核、root、Vector/Zygisk、原有模块和应用数据保留。
这里证明的是启动问题已改善，没有验证登录或运动功能全部可用。

### 12.6 回滚：移出开机目录，再普通重启

不要把别人的原始属性值写到自己的 ROM。
本方案未修改系统分区属性文件，移除开机入口后普通重启即可恢复 ROM 提供的值。

```bash
adb shell "su -c 'mkdir -p /data/adb/rom-metadata-rollback && test ! -e /data/adb/rom-metadata-rollback/98-lepao-rom-metadata.sh && mv /data/adb/service.d/98-lepao-rom-metadata.sh /data/adb/rom-metadata-rollback/98-lepao-rom-metadata.sh'"
adb reboot
```

重启后再次读取四项，与 `rom-metadata-before.txt` 对照。
把脚本移到 `service.d` 目录之外，避免仅改扩展名后仍被启动器执行。
如果回滚目录已有同名文件，上述命令会停止覆盖，先核对已有备份。
只做过临时对照、没有安装开机入口时，直接普通重启即可。

### 12.7 排查经验：把不同问题分开验证

| 现象 | 本次得到的经验 |
| --- | --- |
| No2 后退出 | 保护组件可能汇总多个条件；本机确证的一项是 ROM 配置包含 `UNOFFICIAL`，不能只凭编号判断 root 或内核有错。 |
| 只改一个属性仍失败 | 多个配置可能分别触发同一退出结果；观察到第一项通过，不代表后面也通过。 |
| Vector 模块启用但 daemon 没运行 | 另行检查工作目录、`ANDROID_ROOT`/`ANDROID_DATA`、`BOOTCLASSPATH`、`DEX2OATBOOTCLASSPATH` 和 disable/remove 标记。本机另修过这些启动问题，但修完当时仍有 No2。 |
| USB 调试在重启后消失 | 核对模块是否主动写入 USB 模式、`adb_enabled` 或 `development_settings_enabled`。本机另修过相关脚本，不能用关闭调试来代替定位 No2。 |
| 最后能进入隐私页 | 这是启动验证边界，不等于已经同意协议、登录、跑步成功或所有环境检查都已排除。 |

如果其他设备不符合本节条件，保留其基线，围绕实际失败点重新排查。
应用或 ROM 更新后也应重新做临时对照，不把这次经验当作跨版本的固定结论。
