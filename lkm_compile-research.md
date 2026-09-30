# 报告：Android GKI LKM 跨机型通用性分析

## 元数据

| 项目 | 内容 |
|---|---|
| 报告日期 | 2026-09-27 |
| 主题 | Android GKI 外部内核模块的跨机型通用性 |
| 涉及项目 | [`cyanmint/lkm4ctr`](https://github.com/cyanmint/lkm4ctr) |
| 目标系统 | Android 16 / Linux 6.12 |
| 核心结论 | LKM 兼容性由 **KMI** 决定，而不是具体机型；相同 KMI 下可跨机型通用 |

---

## 摘要

对于 Android GKI 外部内核模块，例如 `lkm4ctr`，其兼容单位是 **KMI（Kernel Module Interface）**，而不是设备代号（Product Name）。

在 **Android 16 + Linux 6.12** 系统中，对应的 KMI 为：

```text
android16-6.12
```

只要目标设备的内核确实属于该 KMI，并且满足所需内核配置与加载策略，那么为 `android16-6.12` 构建的 `.ko` 模块可以在多个不同机型之间通用。

因此：

- **编译 AOSP 整机系统镜像**：通常需要 `lunch <product>`，需要具体设备代号。
- **编译 GKI 外部 LKM**：不需要具体设备代号，关键是匹配内核分支、KMI、内核配置和签名策略。

此前将“AOSP 整机编译需要设备代号”错误地套用到“GKI 外部模块编译”上，导致了表述矛盾。本报告对此进行修正。

---

## 1. 背景与问题

在讨论 Android 内核模块编译时，容易混淆两个不同场景：

1. **编译完整 Android 系统镜像**
   - 需要选择具体产品配置。
   - 通常使用：
     ```bash
     source build/envsetup.sh
     lunch <product>-userdebug
     ```
   - `<product>` 对应具体设备或虚拟设备。

2. **编译 GKI 外部内核模块（LKM）**
   - 目标是生成可加载的 `.ko` 文件。
   - 兼容性由 KMI 决定。
   - 不需要为每个机型单独选择 `lunch` 产品。

`lkm4ctr` 属于第二种场景。因此，它的“通用”不是“一个二进制通吃所有 Android”，而是“一套代码库按 KMI 构建多个版本，同一 KMI 下跨机型通用”。

---

## 2. 关键发现

### 2.1 兼容性单位是 KMI，不是机型

Android GKI 方案将内核核心与供应商模块分离。外部模块必须与设备内核的 KMI 完全匹配。

`lkm4ctr` 的用户手册明确指出：

> 模块必须与设备 KMI 完全匹配。

这意味着：

- `android15-6.6` 的模块不能用于 `android16-6.12`。
- `android16-6.12` 的模块可以在多个使用相同 KMI 的机型上使用。
- 机型差异不是决定因素，KMI 才是。

### 2.2 `lkm4ctr` 的通用性来源

`lkm4ctr` 通过构建矩阵覆盖多个 Android GKI KMI 家族，例如：

| Android 版本 | Linux 内核版本 | KMI |
|---|---|---|
| Android 12 | 5.10 | `android12-5.10` |
| Android 13 | 5.10 | `android13-5.10` |
| Android 13 | 5.15 | `android13-5.15` |
| Android 14 | 5.15 | `android14-5.15` |
| Android 14 | 6.1 | `android14-6.1` |
| Android 15 | 6.6 | `android15-6.6` |
| Android 16 | 6.12 | `android16-6.12` |

因此，对于 Android 16 / Linux 6.12，应选择：

```text
android16-6.12
```

该版本可在相同 KMI 的不同机型间通用。

### 2.3 设备代号的作用范围

| 场景 | 是否需要具体机型代号 | 决定兼容性的因素 |
|---|---|---|
| AOSP 整机编译 | 需要 `lunch <product>` | 产品配置、分区布局、厂商模块 |
| 厂商内核源码编译 | 通常需要设备或平台配置 | 厂商内核分支、设备树、配置碎片 |
| GKI 外部 LKM 编译 | 不需要具体机型 | KMI、内核分支、内核配置、签名策略 |

---

## 3. 对 Android 16 / Linux 6.12 的映射

对于目标系统：

- Android 版本：`16`
- Linux 内核版本：`6.12`

对应关系如下：

| 项目 | 值 |
|---|---|
| Android 版本 | 16 |
| Linux 内核版本 | 6.12 |
| KMI | `android16-6.12` |
| 内核源码分支 | `common-android16-6.12` |
| 推荐模块版本 | `lkm4ctr` 的 `android16-6.12` 构建 |
| 是否需要具体机型 | 否 |

同步内核源码时，应使用：

```bash
repo init -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12
repo sync -c -j$(nproc) -q
```

不需要执行：

```bash
lunch <your_product>
```

---

## 4. 正确选择或编译 LKM 的流程

### 4.1 确认设备信息

```bash
adb shell uname -r
adb shell getprop ro.build.version.release
```

示例输出：

```text
6.12.5-android16-0-g...
16
```

### 4.2 确认 KMI

根据 Android 版本和 Linux 内核版本判断：

```text
Android 16 + Linux 6.12 -> android16-6.12
```

### 4.3 选择对应构建

对于 `lkm4ctr`：

- 选择 `android16-6.12` 版本。
- 不要选择 `android15-6.6` 或其他 KMI 版本。

### 4.4 检查内核配置

设备内核必须启用模块所需配置，例如：

```bash
adb shell "zcat /proc/config.gz | grep -E 'CONFIG_NAMESPACES|CONFIG_NET_NS|CONFIG_TIME_NS'"
```

常见要求包括：

- `CONFIG_NAMESPACES=y`
- `CONFIG_NET_NS=y`
- `CONFIG_TIME_NS=y`

如果 `/proc/config.gz` 不存在，可能内核未启用 `CONFIG_IKCONFIG_PROC`，或厂商移除了该文件。

### 4.5 加载模块

```bash
adb push lkm4ctr.ko /data/local/tmp/
adb shell su -c "insmod /data/local/tmp/lkm4ctr.ko"
```

或根据 Root 方案使用 KernelSU / Magisk 提供的模块加载方式。

### 4.6 使用诊断工具

`lkm4ctr` 提供用户空间诊断工具 `lkm4ctr_checker`，用于检测设备内核是否真正支持容器隔离功能。

检测结果可能为：

- `PASS`：真正隔离。
- `STUB`：仅记账，无实际隔离。
- `FAIL`：调用失败。

---

## 5. 通用性的边界

“相同 KMI 下跨机型通用”不是无条件的。必须满足以下条件：

- [ ] 设备内核 KMI 确实是 `android16-6.12`。
- [ ] 内核启用了模块所需配置。
- [ ] 厂商没有破坏 GKI ABI。
- [ ] Root 权限可用。
- [ ] SELinux 策略允许加载模块。
- [ ] 模块签名强制策略未拒绝该模块。
- [ ] KernelSU / Magisk 等方案允许 `insmod`。

如果不满足这些条件，即使 KMI 匹配，模块也可能无法加载或无法正常工作。

---

## 6. 此前矛盾的原因与修正

### 6.1 错误来源

此前表述将两个场景混淆：

1. **AOSP 整机编译**
   - 需要 `lunch <product>`。
   - 设备代号是关键。

2. **GKI 外部 LKM 编译**
   - 不需要具体设备代号。
   - KMI 是关键。

将第 1 点的要求错误地套用到第 2 点，导致出现“既说需要特定机型，又说可以跨机型通用”的矛盾。

### 6.2 修正结论

正确结论是：

> 对于 `lkm4ctr` 这类 GKI 外部 LKM，兼容单位是 KMI，不是机型。  
> 在 Android 16 / Linux 6.12 下，只要设备 KMI 为 `android16-6.12`，同一构建即可跨多个机型通用。

---

## 7. 最终结论

- `lkm4ctr` 的“通用”指的是：**一套代码库，按 KMI 构建多个版本，同一 KMI 下跨机型通用**。
- Android 16 / Linux 6.12 对应 KMI 为 **`android16-6.12`**。
- 编译或选择 GKI 外部 LKM 时，**不需要**为每个机型单独编译。
- 整机 AOSP 编译才需要 `lunch <product>` 和具体设备代号。
- 跨机型通用的前提是：KMI 完全匹配、内核配置满足、签名与加载策略允许。

---

## 附录：常用命令速查

```bash
# ========== 设备与系统信息 ==========
sudo adb shell uname -r
sudo adb shell getprop ro.build.version.release
sudo adb shell getprop ro.build.version.sdk
sudo adb shell getprop ro.product.device
sudo adb shell getprop ro.product.model
sudo adb shell getprop ro.build.fingerprint

# ========== KMI 判断 ==========
sudo adb shell uname -r
sudo adb shell cat /proc/version
sudo adb shell getprop ro.build.version.release

# ========== 内核配置检查 ==========
sudo adb shell zcat /proc/config.gz | grep -E 'CONFIG_NAMESPACES|CONFIG_NET_NS|CONFIG_TIME_NS'
sudo adb shell zcat /proc/config.gz | grep -E 'CONFIG_MODULES|CONFIG_MODULE_SIG|CONFIG_MODULE_SIG_FORCE'
sudo adb shell zcat /proc/config.gz | grep -E 'CONFIG_KALLSYMS|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'

# ========== 模块路径检查 ==========
sudo adb shell ls -la /vendor/lib/modules/
sudo adb shell ls -la /odm/lib/modules/
sudo adb shell ls -la /system_dlkm/lib/modules/
sudo adb shell lsmod | grep -E 'lkm4ctr|gzvm'

# ========== 模块加载与验证 ==========
sudo adb root
sudo adb push lkm4ctr.ko /data/local/tmp/
sudo adb shell insmod /data/local/tmp/lkm4ctr.ko
sudo adb shell dmesg | tail -30
sudo adb shell lsmod | grep lkm4ctr
sudo adb shell rmmod lkm4ctr

# ========== 诊断工具 ==========
sudo adb push lkm4ctr_checker /data/local/tmp/
sudo adb shell chmod +x /data/local/tmp/lkm4ctr_checker
sudo adb shell /data/local/tmp/lkm4ctr_checker

# ========== 同步 Android 16 / 6.12 GKI 内核源码 ==========
sudo repo init -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12
sudo repo sync -c -j$(nproc) -q

# ========== 编译 LKM ==========
sudo make -C /path/to/common-android16-6.12 M=$PWD modules
sudo make -C /path/to/common-android16-6.12 M=$PWD clean

# ========== Root、签名与 SELinux 检查 ==========
sudo adb shell getenforce
sudo adb shell setenforce 0
sudo adb shell setenforce 1
sudo adb shell cat /proc/sys/kernel/modules_disabled
sudo adb shell cat /proc/sys/kernel/kptr_restrict
sudo adb shell cat /proc/sys/kernel/dmesg_restrict
sudo adb shell dmesg | grep -i avc
sudo adb shell dmesg | grep -i selinux

# ========== KernelSU / Magisk 模块加载方式检查 ==========
sudo adb shell su -v
sudo adb shell which ksud
sudo adb shell which magisk
sudo adb shell ksud module list
sudo adb shell magisk --list
```
  * 如果你需要在设备本地端(手机而非其他设备如电脑)运行, 去除adb shell即可

## 附录: 一些关键疑问解答
- 模块签名: 这个无需担心, 因为永久Root方案修补了内核(区区模块签名, 对于这种情况来说, 基本没有作用), 临时Root方案加载了LKM(也就是说已经允许未签名模块), 所以有Root条件的人都不用担心
- SELinux: 同上(而且加载lkm99%的情况下是不会被SELinux阻挡的)

* 碎碎念: 怎么有人问了AI就说这种方案不可行, 说一些自己都不懂的名词, 就悲哀地说: "厂商堵死了!", 你就不能去试试吗?! 甚至还有人说SELinux限制, 不是你都Root了, 居然还不能用setenforce 0临时切换一下策略吗? 你这个Root是假的吗? 还有什么白名单, 不是我求你了, 要用到的符号都在白名单里面! 你用AI的时候能不能动点脑子? 还有AI说厂商的BL锁定加载LKM你就信吗? 那这样的话, 我还是秦始皇呢, 你信不信? 还有CRC版本, 你信已有项目证明的为6.12编译的LKM可用于6.12.x还是AI随便拍脑袋想出来的? 如果你相信AI说的这些话了, 那你基本就和玩机无缘了, 尽早回锁BL