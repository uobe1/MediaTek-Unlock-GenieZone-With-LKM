# 研究报告：如何探测 EL2 阶段是否支持 GenieZone Hypervisor

## 元数据

| 项目 | 内容 |
|---|---|
| 报告日期 | 2026-09-27 |
| 主题 | 在 Android GKI 设备上探测 EL2 阶段是否运行 GenieZone Hypervisor |
| 目标设备 | MediaTek MT6855 / Android 16 / Linux 6.12 GKI |
| 核心问题 | 设备树缺少 `mediatek,geniezone` 节点时，EL2 是否仍有 Hypervisor 在运行？ |
| 核心结论 | 唯一直接证据是主动发起 HVC 探测；其余方法只能提供间接旁证。综合判断需多证据交叉验证。 |

---

## 摘要

在 ARM64 架构中，Linux 内核运行在 EL1，Hypervisor 运行在 EL2。由于 EL2 与 EL1 存在严格隔离，`dmesg` 默认无法看到 EL2 阶段的日志。要判断 EL2 是否支持并运行 GenieZone Hypervisor，必须使用专门的探测手段。

本报告系统整理七类探测方法：主动 HVC 探测、Tracepoint 追踪、设备树检查、保留内存检查、分区与 Preloader 标志、内核启动日志与 VHE 模式、硬件 Stage-2 MMU 证据。每类方法均给出命令、预期结果与解读。最后给出综合判断矩阵与结论。

对于 MT6855 设备，已知事实是：硬件支持 GenieZone、`gzvm.ko` 已加载、设备树缺少 `mediatek,geniezone` 节点、`/dev/gzvm` 不存在。因此，探测重点在于确认 EL2 是否仍有 Hypervisor 响应 HVC 调用。

---

## 1. 背景：为什么 `dmesg` 看不到 EL2 日志

ARM64 异常级别：

```text
EL0  用户空间
EL1  Linux 内核
EL2  Hypervisor（GenieZone / pKVM）
EL3  Secure Monitor（ATF）
```

在 protected pKVM 模式下，Host 内核（EL1）无法直接访问 EL2 内存，EL2 也无法将日志写入 EL1 的 `tracefs` 环形缓冲区。因此：

- `dmesg` 读取的是 EL1 内核日志，EL2 日志不在这里。
- EL2 的 `WARN_ON()` 通常直接触发 `hyp_panic()`，表现为系统崩溃或重启，而不是留下日志。
- EL2 早期初始化信息只可能出现在**串口日志**或**专用 tracepoint**中。

因此，探测 EL2 必须使用主动调用或专用追踪通道。

---

## 2. 方法一：主动 HVC 探测（最直接、最确凿）

### 2.1 原理

HVC（Hypervisor Call）指令本身会陷入 EL2。如果 EL2 有 Hypervisor 响应并返回成功，则证明 Hypervisor 在运行。GenieZone 驱动内部就是通过 HVC 调用 `MT_HVC_GZVM_PROBE` 来探测 Hypervisor 的。

### 2.2 LKM 探测代码

```c
// gzvm_hvc_probe.c
#include <linux/module.h>
#include <linux/arm-smccc.h>

#define GZVM_HCALL_ID(func) \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
                       ARM_SMCCC_OWNER_VENDOR_HYP, func)

#define MT_HVC_GZVM_PROBE GZVM_HCALL_ID(0)

static int __init gzvm_hvc_probe_init(void)
{
    struct arm_smccc_res res;

    pr_info("gzvm_hvc_probe: probing GenieZone via HVC...\n");

    arm_smccc_hvc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);

    pr_info("gzvm_hvc_probe: a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx\n",
            res.a0, res.a1, res.a2, res.a3);

    if (res.a0 == 0)
        pr_info("gzvm_hvc_probe: SUCCESS, hypervisor likely at EL2\n");
    else
        pr_warn("gzvm_hvc_probe: FAILED, no EL2 response (a0=0x%lx)\n", res.a0);

    return 0;
}

static void __exit gzvm_hvc_probe_exit(void)
{
    pr_info("gzvm_hvc_probe: exit\n");
}

module_init(gzvm_hvc_probe_init);
module_exit(gzvm_hvc_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Probe GenieZone EL2 via HVC");
```

### 2.3 Makefile

```makefile
obj-m += gzvm_hvc_probe.o
KERNEL_DIR ?= /path/to/common-android16-6.12

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) \
		LLVM=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
```

### 2.4 加载与解读

```bash
sudo insmod gzvm_hvc_probe.ko
sudo dmesg | tail -20
```

| 返回值 | 含义 |
|---|---|
| `a0 == 0` | HVC 成功陷入 EL2，Hypervisor 在运行 |
| `a0 != 0` | EL2 无响应，或 Hypervisor 未初始化 |
| 模块加载导致内核崩溃 | EL2 没有为 HVC 准备好，指令异常 |

这是**唯一直接证明 EL2 有 Hypervisor 的方法**。

---

## 3. 方法二：Tracepoint 间接追踪

### 3.1 原理

GenieZone 驱动提供 tracepoint：`mtk_hypcall_enter`、`mtk_hypcall_leave`、`mtk_vcpu_exit`。任何 HVC 调用成功进入 EL2 并返回，都会留下记录。

### 3.2 操作步骤

```bash
# 清空并只启用 geniezone
sudo sh -c 'echo 0 > /sys/kernel/tracing/events/enable'
sudo sh -c 'echo > /sys/kernel/tracing/trace'
sudo sh -c 'echo 1 > /sys/kernel/tracing/events/geniezone/enable'

# 开启追踪
sudo sh -c 'echo 1 > /sys/kernel/tracing/tracing_on'

# 触发 HVC —— 加载方法一中的 LKM
sudo insmod gzvm_hvc_probe.ko

# 停止
sudo sh -c 'echo 0 > /sys/kernel/tracing/tracing_on'

# 查看
sudo cat /sys/kernel/tracing/trace
```

### 3.3 解读

- 出现 `mtk_hypcall_enter` / `mtk_hypcall_leave`：HVC 到达 EL2。
- 为空：没有 HVC 调用发生，或 tracepoint 未启用成功。
- 注意：仅加载 `gzvm.ko` 不会触发 HVC，必须主动调用 `gzvm_arch_probe()` 或直接发起 HVC。

---

## 4. 方法三：设备树检查（间接证据）

```bash
sudo find /proc/device-tree -iname "*genie*" -o -iname "*gz*"
sudo cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
```

| 结果 | 含义 |
|---|---|
| 有 `mediatek,geniezone` 或 `mediatek,geniezone-hyp` | 固件已配置 GenieZone 接口 |
| 只有 `mediatek,MT6855` | 设备树未配置 GenieZone |

设备树缺失意味着 `gzvm` 驱动无法 probe，但不直接说明 EL2 是否有 Hypervisor。它只证明**软件配置层关闭了接口**。

---

## 5. 方法四：保留内存检查

GenieZone 在启动早期由 preloader 加载到 EL2，会占用特定物理内存。

```bash
sudo cat /proc/iomem | grep -i -E "genie|gz|hyp"
sudo find /proc/device-tree/reserved-memory -iname "*genie*" -o -iname "*gz*" 2>/dev/null
sudo dmesg | grep -i "reserved"
```

| 结果 | 含义 |
|---|---|
| 存在为 GenieZone 保留的内存区域 | 固件为 Hypervisor 分配了内存 |
| 无任何保留区域 | 固件可能未加载 GenieZone |

### MT6855 实测结果

在目标设备上执行以下命令：

```bash
sudo cat /proc/mtk_memcfg/reserve_memory
sudo sh -c 'cat /proc/iomem | grep -i -E "genie|gz|hyp"'
sudo find /proc/device-tree/reserved-memory \( -iname "*genie*" -o -iname "*gz*" \) 2>/dev/null
```

结果：
- `/proc/mtk_memcfg/reserve_memory`：不存在。
- `/proc/iomem`：无 `genie`、`gz`、`hyp` 相关输出。
- `/proc/device-tree/reserved-memory`：存在三个节点：
  - `mblock-14-gz`
  - `mblock-18-gz-log`
  - `mblock-19-gz_ffa_mailbox`

结论：固件已为 GenieZone 保留了内存、日志区和 FFA mailbox，说明底层固件并未完全移除 GenieZone。

---

## 6. 方法五：分区与 Preloader 标志

GenieZone 镜像通常存储在 `gz1` / `gz2` 分区。

```bash
ls /dev/block/by-name/ | grep -E "gz1|gz2"
```

| 结果 | 含义 |
|---|---|
| 存在 `gz1` / `gz2` | 固件包含 Hypervisor 镜像 |
| 不存在 | 固件未包含镜像 |

Preloader 内部有控制 EL2 加载的开关。将分区 LBA 指向存储容量外，可触发 `EL2_BOOTING_DISABLED` 标志位来安全禁用。这说明 preloader 有加载/禁用机制。

### MT6855 实测结果

```bash
sudo sh -c 'ls -l /dev/block/by-name/ | grep -E "gz_a|gz_b|gz1|gz2"'
```

结果：

```text
lrwxrwxrwx. 1 root root 16 Sep 26 11:12 gz_a -> /dev/block/sdc29
lrwxrwxrwx. 1 root root 16 Sep 26 11:12 gz_b -> /dev/block/sdc56
```

结论：固件包含 GenieZone 镜像，分区名为 `gz_a` 和 `gz_b`。

---

## 7. 方法六：内核启动日志与 VHE 模式

```bash
sudo dmesg | grep -iE "VHE|Hyp mode|EL2|kvm|pKVM"
```

| 日志 | 含义 |
|---|---|
| `VHE mode initialized successfully` | Host 内核运行在 EL2（VHE） |
| `pKVM: Initializing...` | pKVM Hypervisor 在 EL2 |
| `CPU: All CPU(s) started at EL1` | EL2 未被内核占用，可能留给独立 Hypervisor |

注意：如果设备启用 pKVM，Host 内核被特意降级到 EL1 以受 Stage-2 页表管控，此时看不到内核在 EL2 的日志是正常的。因此日志法只能辅助。

---

## 8. 方法七：硬件 Stage-2 MMU 证据

GenieZone 的核心安全功能依赖 **Stage-2 MMU**。MediaTek M-TEE 安全认证文档明确列出集成 Stage-2 MMU 的 SoC 型号，MT6855 在列表中。

这从**硬件层面**证明芯片具备运行 GenieZone 的物理基础。但硬件支持不等于软件启用。

---

## 9. 用户空间辅助工具

| 工具 | 命令 | 能验证什么 |
|---|---|---|
| `systemd-detect-virt` | `systemd-detect-virt` | 是否在虚拟化环境中 |
| `virt-what` | `sudo virt-what` | 虚拟化技术类型 |
| `/proc/cpuinfo` | `grep -E 'hypervisor' /proc/cpuinfo` | 是否运行在 VM 内 |
| `lscpu` | `lscpu | grep -i hypervisor` | CPU 特性标志 |

这些工具通常无法区分 EL1 与 EL2，只能作为旁证。

---

## 10. 综合判断矩阵

| 证据 | 你的设备状态 | 结论 |
|---|---|---|
| 硬件 Stage-2 MMU | MT6855 在支持列表 | 硬件支持 |
| `gz_a`/`gz_b` 分区 | 存在 | 固件包含 GenieZone 镜像 |
| 保留内存 | 存在 `mblock-14-gz`、`mblock-18-gz-log`、`mblock-19-gz_ffa_mailbox` | 固件为 Hypervisor 保留了内存、日志和 FFA mailbox |
| 设备树节点 | 无 `mediatek,geniezone` | 软件接口未配置 |
| `gzvm.ko` | 已加载 | 内核驱动就绪 |
| `/dev/gzvm` | 不存在 | 驱动未 probe |
| Tracepoint 目录 | 存在 | 内核包含追踪代码 |
| HVC 探测 | 待执行 | **决定性证据** |
| `dmesg` EL2 日志 | 无 | 正常，EL2 与 EL1 隔离 |

---

## 11. 结论

1. **唯一直接证明 EL2 有 Hypervisor 的方法**是主动发起 HVC 探测（方法一）。若 `a0 == 0`，则 Hypervisor 在 EL2 运行；若失败或崩溃，则 EL2 无响应。
2. **Tracepoint（方法二）**可作为 HVC 调用的旁证，但必须在触发 HVC 前开启追踪。
3. **设备树、保留内存、分区、日志、硬件证据**均为间接旁证，不能单独证明 EL2 状态。
4. 对于 MT6855 设备，已知设备树缺少 `mediatek,geniezone`，但硬件支持、内核驱动就绪，且固件层保留了 `gz_a`/`gz_b` 分区以及 `mblock-14-gz`、`mblock-18-gz-log`、`mblock-19-gz_ffa_mailbox` 等保留内存。这说明厂商更可能只是在设备树软件层关闭了接口，而非彻底移除 GenieZone。因此，**HVC 探测是最终判断手段**。
5. 若 HVC 返回 `a0 == 0`，则说明 OEM 只是在设备树层面关闭了接口，EL2 Hypervisor 仍在运行，辅助 LKM 调用 `gzvm_drv_probe(NULL)` 有成功可能。
6. 若 HVC 失败或导致崩溃，则说明 OEM 在更底层（ATF/EL2）彻底禁用了 GenieZone，LKM 方案无法激活。

---

## 附录：常用命令速查

```bash
# ========== 基本环境 ==========
sudo uname -r
sudo getprop ro.build.version.release
sudo getprop ro.build.version.sdk
sudo getprop ro.product.device
sudo getprop ro.product.model

# ========== 模块与符号 ==========
sudo lsmod | grep gzvm
sudo cat /proc/kallsyms | grep -E 'gzvm|geniezone'
sudo cat /proc/kallsyms | grep gzvm_drv_probe
sudo cat /proc/kallsyms | grep gzvm_arch_probe

# ========== 设备树检查 ==========
sudo find /proc/device-tree -iname "*genie*" -o -iname "*gz*"
sudo cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
sudo find /proc/device-tree/reserved-memory \( -iname "*genie*" -o -iname "*gz*" \) 2>/dev/null
sudo ls -l /proc/device-tree/reserved-memory/
sudo ls -l /proc/device-tree/reserved-memory/mblock-14-gz
sudo ls -l /proc/device-tree/reserved-memory/mblock-18-gz-log
sudo ls -l /proc/device-tree/reserved-memory/mblock-19-gz_ffa_mailbox

# ========== 保留内存检查 ==========
sudo cat /proc/iomem | grep -i -E "genie|gz|hyp"
sudo cat /proc/mtk_memcfg/reserve_memory
sudo dmesg | grep -i "reserved"
sudo dmesg | grep -i -E "genie|gz|hyp"

# ========== 分区检查 ==========
sudo ls -l /dev/block/by-name/ | grep -E "gz_a|gz_b|gz1|gz2"
sudo blockdev --getsize64 /dev/block/sdc29
sudo blockdev --getsize64 /dev/block/sdc56

# ========== 启动日志与 VHE ==========
sudo dmesg | grep -iE "VHE|Hyp mode|EL2|kvm|pKVM|geniezone|gzvm"
sudo dmesg | grep -iE "CPU: All CPU"
sudo dmesg | grep -iE "pKVM|Protected KVM"
sudo dmesg | grep -iE "VHE mode"

# ========== Tracepoint ==========
sudo ls /sys/kernel/tracing/events/geniezone/
sudo cat /sys/kernel/tracing/events/geniezone/enable
sudo ls /sys/kernel/tracing/events/geniezone/mtk_hypcall_enter/
sudo ls /sys/kernel/tracing/events/geniezone/mtk_hypcall_leave/
sudo ls /sys/kernel/tracing/events/geniezone/mtk_vcpu_exit/

# 开启追踪
echo 0 | sudo tee /sys/kernel/tracing/events/enable
sudo truncate -s 0 /sys/kernel/tracing/trace
echo 1 | sudo tee /sys/kernel/tracing/events/geniezone/enable
echo 1 | sudo tee /sys/kernel/tracing/tracing_on

# 触发 HVC 探测
sudo insmod gzvm_hvc_probe.ko

# 停止追踪
echo 0 | sudo tee /sys/kernel/tracing/tracing_on

# 查看追踪结果
sudo cat /sys/kernel/tracing/trace

# ========== HVC 探测 LKM 编译与加载 ==========
sudo make -C /path/to/common-android16-6.12 M=$PWD LLVM=1 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules
sudo make -C /path/to/common-android16-6.12 M=$PWD clean

sudo adb root
sudo adb push gzvm_hvc_probe.ko /data/local/tmp/
sudo adb shell insmod /data/local/tmp/gzvm_hvc_probe.ko
sudo adb shell dmesg | tail -20
sudo adb shell rmmod gzvm_hvc_probe

# ========== 用户空间辅助工具 ==========
sudo systemd-detect-virt
sudo virt-what
sudo grep -E 'hypervisor' /proc/cpuinfo
sudo lscpu | grep -i hypervisor

# ========== 内核配置检查 ==========
sudo zcat /proc/config.gz | grep -E 'CONFIG_MODULES|CONFIG_MODULE_SIG|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'
```
  * 如果你需要在设备本地端(手机而非其他设备如电脑)运行, 去除adb shell即可