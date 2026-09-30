# GKI LKM 兼容性

为什么 Android GKI 外部内核模块能在**同一 KMI** 的不同机型之间通用，以及本项目构建时的要求。

| | |
|---|---|
| 参考平台 | Android 16 / Linux 6.12 GKI |
| 核心结论 | 兼容性由 KMI（Kernel Module Interface）决定，而非设备代号；但模块仍必须针对某一个精确的 KMI 代次构建。 |

---

## 1. 两种不同的构建场景

把这两者混为一谈是矛盾说法的常见来源：

| 场景 | 需要设备代号吗？ | 决定兼容性的因素 |
|---|---|---|
| 完整 AOSP 系统镜像（`lunch <product>`） | 需要 | 产品配置、分区布局、厂商模块 |
| 厂商内核树 | 通常需要 | 厂商分支、设备树、配置碎片 |
| **GKI 外部 LKM（本项目）** | **不需要** | **KMI 代次、内核配置、签名策略** |

本项目构建的是 GKI 外部 LKM，因此不存在 `lunch <product>` 步骤。

---

## 2. KMI 才是兼容性的单位

Android GKI 将内核核心与厂商模块分离。外部模块必须与运行中内核的 KMI 精确匹配。

| Android 版本 | Linux 内核 | KMI |
|---|---|---|
| Android 12 | 5.10 | `android12-5.10` |
| Android 13 | 5.10 | `android13-5.10` |
| Android 13 | 5.15 | `android13-5.15` |
| Android 14 | 5.15 | `android14-5.15` |
| Android 14 | 6.1 | `android14-6.1` |
| Android 15 | 6.6 | `android15-6.6` |
| Android 16 | 6.12 | `android16-6.12` |

`kernel/common` 仓库的分支名就是 KMI 名本身；内核 **manifest** 仓库则使用带 `common-` 前缀的同一名称，因此 `repo init -b common-android16-6.12` 与直接克隆 `android16-6.12` 分支得到的是同一份源码。

因此：

* `android15-6.6` 的模块无法加载到 `android16-6.12` 内核；
* 一个 `android16-6.12` 构建可在共享该 KMI 的**不同机型**上使用。

这里的“通用”指*一套源码、每个 KMI 代次一份构建、同一代次内跨机型共享*，而不是“一个二进制通吃所有 Android 设备”。

### 2.1 只看大版本

Google 只在同一个 `<android>-<kernel>` 代次内保证 KMI 稳定性，且该保证覆盖内核**大版本**：`6.12.x` 全部属于 `android16-6.12`，针对 `6.12.30` 构建的模块可加载在同一 Android 代次的任意 `6.12.y` 内核上。这正是构建只需两个入参（Android 大版本与内核大版本）的原因，默认值分别为 `16` 与 `6.12`。

### 2.2 为什么 point release 不同也能加载

模块携带两个版本载体，在同一 KMI 代次内它们都容忍不同的 point release：

* **符号 CRC 表（`__versions`）** —— CRC 按 KMI 计算且在代次内唯一，因此同一 Android 代次的所有 `6.12.y` 内核都匹配。这就是 KMI 承诺的本质。
* **vermagic 字符串** —— 在 MODVERSIONS 生效时，内核只比较标志部分（`SMP preempt mod_unload modversions aarch64`），并跳过其前的 release 段。

因此用 DDK 的 `6.12.76-4k` 树构建的模块，在 `6.12.30-android16-...` 的设备上通过普通 `insmod` 即可加载 —— 不需要任何特殊的加载器。`mgz install` 也是首先尝试普通 `insmod`；`ksud insmod` 只是它被拦截时的回退手段，并非必需。

---

## 3. 选择或构建正确的 LKM

1. **读取设备事实**

   ```bash
   uname -r                       # 6.12.30-android16-5-g...-4k
   getprop ro.build.version.release   # 16
   ```

2. **推导 KMI** —— Android 16 + Linux 6.12 → `android16-6.12`。

3. **针对该 KMI 构建** —— `MGZ_ANDROID_VERSION=16 MGZ_KERNEL_VERSION=6.12`，对应分支 `android16-6.12`。

4. **检查模块依赖的内核配置**

   ```bash
   zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES'
   ```

5. **加载并验证**

   ```bash
   sudo insmod /data/local/tmp/gzvm_unlock.ko
   dmesg | tail -30
   ```

---

## 4. 通用性的边界

“同 KMI 即通用”仅在以下条件全部成立时有效：

* [ ] 运行中的内核确实是 `android16-6.12`；
* [ ] 所需内核选项已开启（`MODULES`、`KALLSYMS_ALL`、`KPROBES`）；
* [ ] 厂商没有破坏 GKI ABI；
* [ ] Root 权限可用；
* [ ] SELinux 策略允许加载模块；
* [ ] 签名强制策略没有拒绝该模块；
* [ ] Root 方案（KernelSU / Magisk）允许 `insmod`。

---

## 5. 常被重复的错误说法

| 说法 | 事实 |
|---|---|
| “模块签名让这事不可能” | 对有 Root 的人无意义：永久 Root 方案修补了内核，临时 Root 方案本身就在加载 LKM —— 未签名模块是可接受的 |
| “SELinux 会阻止” | root 上下文执行 `insmod` 几乎不会被拒绝，且 `setenforce 0` 始终可用 |
| “BL 锁定就不能加载 LKM” | BL 锁只决定你能*刷入*什么，不决定已获得 root 的运行时能否 `insmod` |
| “符号不在导出白名单里” | 这里用到的符号（`misc_register`、`register_kprobe`、`arm_smccc_hvc`）全部已导出 |
| “给 6.12 编的模块不能用于 6.12.x” | 同一 Android 代次内 `6.12.y` 的 KMI 稳定性有保证，已有项目可以证明 |

---

## 6. 结论

* 外部 GKI LKM 的“通用”指*每个 KMI 代次一份构建*，而非一个二进制通吃。
* Android 16 / Linux 6.12 → KMI `android16-6.12` → 分支 `android16-6.12`。
* 构建外部 LKM 不需要 `lunch <product>`；完整 AOSP 构建才需要。
* 通用性仍要求：KMI 精确匹配、内核选项齐备、加载策略允许。

---

## 附录：命令速查

```bash
# 设备与系统
uname -r
cat /proc/version
getprop ro.build.version.release
getprop ro.build.version.sdk
getprop ro.product.device

# 内核配置
zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG|CONFIG_KALLSYMS|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'

# 模块路径
ls /vendor/lib/modules/
ls /odm/lib/modules/
ls /system_dlkm/lib/modules/
lsmod | grep -E 'gzvm'

# 加载与验证
sudo insmod /data/local/tmp/gzvm_unlock.ko
dmesg | tail -30
lsmod | grep gzvm_unlock
sudo rmmod gzvm_unlock

# Root 与 SELinux
getenforce
setenforce 0
cat /proc/sys/kernel/modules_disabled
dmesg | grep -i avc

# Root 方案
sudo -V
command -v ksud        # 通常只有 `su -c` 能找到
ksud module list
```

> 如果在设备本地端（手机而非电脑）运行，去掉 `adb shell` 前缀即可。
