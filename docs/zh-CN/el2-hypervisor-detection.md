# 探测 EL2 是否运行 GenieZone Hypervisor

即使 `dmesg` 中看不到任何相关信息，如何判断 MediaTek 设备的 EL2 是否仍在运行 GenieZone Hypervisor。

| | |
|---|---|
| 参考设备 | MediaTek MT6855（beryl） |
| 参考平台 | Android 16 / Linux 6.12 GKI |
| 核心问题 | 设备树没有 `mediatek,geniezone` 节点 —— EL2 是否仍有 Hypervisor 在运行？ |
| 核心结论 | 唯一直接证据是自己发起 HVC；其余方法只能提供旁证。需要多证据交叉验证。 |

---

## 1. 为什么 `dmesg` 看不到 EL2

ARM64 异常级别：

```text
EL0   用户空间
EL1   Linux 内核
EL2   Hypervisor（GenieZone / pKVM）
EL3   安全监控器（ATF）
```

存在 Hypervisor 时，EL1 与 EL2 严格隔离：

* `dmesg` 读取的是 EL1 日志缓冲区，EL2 的写入不会落到这里。
* EL2 内的 `WARN_ON()` 通常直接导致 `hyp_panic()`，表现为重启而非日志行。
* EL2 早期初始化信息只可能出现在串口日志或专用 tracepoint 中。

因此，EL2 必须主动探测，而不能被动读取。

---

## 2. 方法一：主动 HVC 探测（直接证据）

### 2.1 原理

`HVC` 指令会陷入 EL2。若已安装 Hypervisor，它会处理该调用并返回状态；否则该陷入无人处理。GenieZone 驱动内部正是通过 `gzvm_arch_probe()` 发起这个调用（`MT_HVC_GZVM_PROBE`）。

### 2.2 探测模块

```c
#define GZVM_HCALL_ID(func) \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
                       ARM_SMCCC_OWNER_VENDOR_HYP, func)

#define MT_HVC_GZVM_PROBE GZVM_HCALL_ID(0)

static int __init gzvm_hvc_probe_init(void)
{
    struct arm_smccc_res res;

    arm_smccc_hvc(MT_HVC_GZVM_PROBE, 0, 0, 0, 0, 0, 0, 0, &res);

    pr_info("gzvm_probe: a0=0x%lx a1=0x%lx a2=0x%lx a3=0x%lx\n",
            res.a0, res.a1, res.a2, res.a3);

    return 0;
}
```

正式模块为 `kernel/gzvm_probe.c`，它还会在同一次运行中一并报告设备树、分区与保留内存证据。

### 2.3 结果解读

| 结果 | 含义 |
|---|---|
| `a0 == 0` | 调用到达 EL2 且 Hypervisor 作出了应答 —— Hypervisor 存活 |
| `a0 != 0` | EL2 无响应，或 Hypervisor 拒绝了该调用 |
| 加载导致内核崩溃 | EL2 未为 HVC 做准备，视为“不存在” |

这是**唯一能直接证明** EL2 存在 Hypervisor 的方法。

---

## 3. 方法二：Tracepoint（旁证）

GenieZone 提供 `mtk_hypcall_enter`、`mtk_hypcall_leave`、`mtk_vcpu_exit`。任何成功进入并返回 EL2 的 HVC 调用都会留下记录。

```bash
echo 0 > /sys/kernel/tracing/events/enable
echo > /sys/kernel/tracing/trace
echo 1 > /sys/kernel/tracing/events/geniezone/enable
echo 1 > /sys/kernel/tracing/tracing_on

sudo insmod /data/local/tmp/gzvm_probe.ko   # 触发 HVC

echo 0 > /sys/kernel/tracing/tracing_on
cat /sys/kernel/tracing/trace
```

* 出现 `mtk_hypcall_enter` / `mtk_hypcall_leave` → HVC 到达了 EL2。
* trace 为空 → 没有发生 HVC，或 tracepoint 未启用成功。

仅仅加载 `gzvm.ko` **不会**发起 HVC，必须主动触发调用。

---

## 4. 方法三：设备树（间接证据）

```bash
find /proc/device-tree -iname '*genie*' -o -iname '*gz*'
cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
```

| 结果 | 含义 |
|---|---|
| 存在 `mediatek,geniezone` 或 `mediatek,geniezone-hyp` | 固件已暴露该接口 |
| 仅有 `mediatek,MT6855` | 绑定在软件层被移除 |

节点缺失解释了 `probe()` 为何不执行，但对 EL2 状态不提供任何信息。

---

## 5. 方法四：保留内存（间接证据）

```bash
cat /proc/iomem | grep -iE 'genie|gz|hyp'
ls /proc/device-tree/reserved-memory | grep -i gz
```

参考设备实测：

* `/proc/iomem`：无 `genie`/`gz`/`hyp` 条目。
* 保留内存节点：`mblock-14-gz`、`mblock-18-gz-log`、`mblock-19-gz_ffa_mailbox`。

固件为 GenieZone 保留了内存、日志缓冲区与 FF-A mailbox，说明 Hypervisor 并未从固件中剥离。

---

## 6. 方法五：分区与 Preloader（间接证据）

```bash
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2'
```

参考设备：

```text
gz_a -> /dev/block/sdc29
gz_b -> /dev/block/sdc56
```

GenieZone 镜像仍在随包提供。是否加载由 preloader 决定；把分区 LBA 指向存储容量之外，是官方用来置起 `EL2_BOOTING_DISABLED` 标志位的安全禁用方式。

---

## 7. 方法六：启动日志与 VHE 模式（间接证据）

```bash
dmesg | grep -iE 'VHE|Hyp mode|EL2|kvm|pKVM|geniezone|gzvm'
```

| 日志 | 含义 |
|---|---|
| `VHE mode initialized successfully` | 内核自身运行在 EL2（VHE） |
| `pKVM: Initializing...` | pKVM 占据 EL2 |
| `CPU: All CPU(s) started at EL1` | EL2 空出，可供独立 Hypervisor 使用 |

启用 pKVM 时，Host 内核会被刻意降级到 EL1，因此看不到“内核在 EL2”的日志是正常的，本身不能证明任何事情。

---

## 8. 方法七：硬件能力（间接证据）

GenieZone 依赖 Stage-2 MMU。MediaTek M-TEE 文档列出了集成 Stage-2 MMU 的 SoC 型号，MT6855 在列表中。硬件能力只是前提条件，不等于软件已启用。

---

## 9. 用户空间辅助工具（弱信号）

| 工具 | 命令 | 能说明什么 |
|---|---|---|
| `systemd-detect-virt` | `systemd-detect-virt` | 是否运行在虚拟化环境中 |
| `virt-what` | `sudo virt-what` | 虚拟化技术类型 |
| `/proc/cpuinfo` | `grep hypervisor /proc/cpuinfo` | CPU 特性中的 hypervisor 标志 |

这些工具无法区分 EL1 与 EL2，仅供点缀。

---

## 10. 综合判定矩阵

| 证据 | 参考设备状态 | 结论 |
|---|---|---|
| 硬件 Stage-2 MMU | MT6855 在列表中 | 硬件支持 |
| `gz_a` / `gz_b` 分区 | 存在 | Hypervisor 镜像随包提供 |
| 保留内存 | `mblock-14-gz`、`-18-gz-log`、`-19-gz_ffa_mailbox` | 固件为 Hypervisor 预留了内存 |
| 设备树节点 | 缺失 | 软件接口被关闭 |
| `gzvm.ko` | 已加载 | 驱动就绪 |
| `/dev/gzvm` | 不存在 | 从未 probe |
| Tracepoint 目录 | 存在 | 追踪代码已编译进内核 |
| **HVC 探测** | **待执行** | **决定性** |
| `dmesg` 中的 EL2 记录 | 无 | 正常，EL2 与 EL1 隔离 |

---

## 11. 结论

1. **唯一直接证据**是主动 HVC 探测。`a0 == 0` 说明 EL2 存在存活的 Hypervisor。
2. Tracepoint 可佐证一次 HVC 往返，但必须在调用**之前**启用。
3. 设备树、保留内存、分区、启动日志与硬件清单均为旁证，不能单独定论。
4. 若 HVC 成功，说明厂商只是移除了设备树绑定，[驱动激活](gzvm-driver-activation.md)中的辅助 LKM 可以激活该接口。
5. 若 HVC 失败或崩溃，说明 GenieZone 在 Linux 之下（ATF/EL2）被禁用，任何 LKM 都无法复活它。

---

## 附录：命令速查

```bash
# 符号
cat /proc/kallsyms | grep -E 'gzvm|geniezone'

# 设备树
find /proc/device-tree -iname '*genie*' -o -iname '*gz*'
cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
ls /proc/device-tree/reserved-memory | grep -i gz

# 保留内存
cat /proc/iomem | grep -iE 'genie|gz|hyp'
dmesg | grep -i reserved

# 分区
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2'

# 启动日志与 VHE
dmesg | grep -iE 'VHE|Hyp mode|pKVM|geniezone|gzvm'
dmesg | grep -iE 'CPU: All CPU'

# Tracepoint
ls /sys/kernel/tracing/events/geniezone/
cat /sys/kernel/tracing/trace

# 探测模块
sudo insmod /data/local/tmp/gzvm_probe.ko
dmesg | tail -20
sudo rmmod gzvm_probe
```

> 如果在设备本地端（手机而非电脑）运行，去掉 `adb shell` 前缀即可。
