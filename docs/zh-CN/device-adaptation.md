# 机型适配指南

如何判断**你的**设备能否使用本项目解锁，以及当 SoC、内核或 Android 版本与参考设备不同时需要改动哪些地方。

| | |
|---|---|
| 参考设备 | MediaTek MT6855（beryl），Android 16，内核 6.12.30 |
| 参考 KMI | `android16-6.12` |
| 最快结论 | 把 CLI 推送到设备后运行 `mgz check` |

---

## 1. 三步得出结论

```bash
adb push mgz /data/local/tmp/
adb shell chmod +x /data/local/tmp/mgz
adb shell /data/local/tmp/mgz check --show-debug-details
```

`mgz check` 会逐层给出结论：

| 层级 | 检查内容 |
|---|---|
| 平台层 | 是否 MediaTek SoC、是否具备 GenieZone 能力 |
| 内核层 | KMI 代次、`CONFIG_KALLSYMS_ALL`、`CONFIG_KPROBES`、模块可加载性 |
| Hypervisor 层 | `gzvm.ko` 是否驻留、`gzvm_drv_probe` 是否可解析、EL2 是否响应 HVC |
| 固件层 | `gz_a`/`gz_b` 分区、`mblock-*-gz*` 保留内存 |

总体结论：

* **SUPPORTED（支持）** —— 各层全部通过，`mgz install` 应当可用。
* **LIKELY（可能支持）** —— 固件层表明 GenieZone 仍在，但 HVC 探测尚无定论（例如探测模块未按本机 KMI 构建）。
* **UNSUPPORTED（不支持）** —— 平台非 MediaTek/不具备 GenieZone 能力，或 EL2 已确认失效，或无法加载未签名模块。

---

## 2. 硬件层：SoC 是否具备 GenieZone 能力

读取平台标识：

```bash
getprop ro.board.platform          # mt6855
getprop ro.hardware                # mt6855
getprop ro.mediatek.platform       # MT6855（各厂商可能不同）
getprop ro.product.board
cat /proc/cpuinfo | grep Hardware
cat /sys/devices/soc0/machine 2>/dev/null
cat /sys/devices/soc0/soc_id 2>/dev/null
```

具备 GenieZone 能力的 MediaTek 平台通常具有如下特征：

* 属于 MT68xx / MT69xx（天玑）系列；
* SoC 内集成 Stage-2 MMU —— 这是 GenieZone 隔离模型的基础，MediaTek M-TEE 资料中有对应型号列表；
* ARMv8 核心，EL2 可交由独立 Hypervisor 使用。

> 维护者的参考设备（MT6855）已实测可用。其他 SoC 请勿仅凭型号列表判断，而应以第 3、4 节的固件与内核证据为准，并把结果反馈出来，让第 7 节的表格持续增长。

硬件层最可靠的信号**不是** SoC 名称，而是 Hypervisor 镜像与其保留内存是否存在。

---

## 3. 固件层：厂商是否保留了 GenieZone

```bash
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2|gz'
ls /proc/device-tree/reserved-memory | grep -i gz
```

| 发现 | 含义 |
|---|---|
| 存在 `gz_a`/`gz_b`（或 `gz1`/`gz2`）分区 | Hypervisor 镜像仍在固件中 |
| 存在 `mblock-*-gz*` 保留内存节点 | 已配置内存、日志缓冲区与 FF-A mailbox |
| 两者都不存在 | 固件已移除 GenieZone，LKM 无能为力 |

**设备树节点缺失是预期情况**，并不构成反证 —— 这正是本项目存在的意义。

---

## 4. 内核层：能否运行我们的模块

```bash
uname -r                                    # 据此推导 KMI
ls /system_dlkm/lib/modules/gzvm.ko /vendor/lib/modules/gzvm.ko
lsmod | grep gzvm
cat /proc/kallsyms | grep gzvm_drv_probe
zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES'
cat /proc/sys/kernel/modules_disabled
```

| 条件 | 原因 |
|---|---|
| `gzvm.ko` 存在且已加载 | 我们要执行的正是它的 `probe()` |
| `gzvm_drv_probe` 在 kallsyms 中可见 | `kprobe` 需要按名字解析它 |
| `CONFIG_KALLSYMS_ALL=y` | 局部符号必须可按名解析 |
| `CONFIG_KPROBES=y` | `register_kprobe()` 依赖它 |
| `CONFIG_MODULES=y` 且未开启 `CONFIG_MODULE_SIG_FORCE` | 未签名外部模块必须可加载 |
| `modules_disabled == 0` | 否则任何加载都会被拒绝 |

---

## 5. 判定矩阵

| 平台 | `gzvm.ko` | `gzvm_drv_probe` | EL2 HVC | 结论 | 处置 |
|---|---|---|---|---|---|
| 具备 GenieZone 能力 | 有 | 有 | `a0 == 0` | SUPPORTED | `mgz install` |
| 具备 GenieZone 能力 | 有 | 有 | 未探测 | LIKELY | 按此 KMI 构建探测模块后复查 |
| 具备 GenieZone 能力 | 有 | 无 | – | LIKELY | 符号名可能不同，见 §6.2 |
| 具备 GenieZone 能力 | 无 | – | – | UNSUPPORTED | 未随包提供模块，无从探测 |
| 具备 GenieZone 能力 | 有 | 有 | `a0 != 0` | UNSUPPORTED | 已在 Linux 之下（ATF/EL2）禁用 |
| 非 MediaTek / 无 Stage-2 | – | – | – | UNSUPPORTED | 不在本项目范围内 |

---

## 6. 为其他设备适配

### 6.1 不同的 Android 或内核代次

按设备的两个大版本构建：

```bash
cmake -B build -DANDROID_VERSION=15 -DKERNEL_VERSION=6.6
cmake --build build --target gzvm_modules
```

在 CI 中则以 `android_version` / `kernel_version` 入参触发工作流。只有内核**大版本**有意义：`6.12.x` 全部归入 `android16-6.12`。参见[GKI LKM 兼容性](gki-lkm-compatibility.md)。

### 6.2 不同的符号名

部分内核会重命名或内联该 probe 入口。先确认实际存在的符号：

```bash
cat /proc/kallsyms | grep -iE 'gzvm|geniezone'
```

`kernel/gzvm_unlock.c` 维护了一个候选符号列表并按顺序尝试。若你的内核使用了其他名字，把它加入该列表即可；模块会依次回退并报告最终解析到的符号。

### 6.3 不同的 Hypervisor 调用号

探测使用 MediaTek 的 vendor-hyp SMCCC 区间：

```c
#define GZVM_HCALL_ID(func) \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
                       ARM_SMCCC_OWNER_VENDOR_HYP, func)
#define MT_HVC_GZVM_PROBE GZVM_HCALL_ID(0)
```

若你的平台使用其他功能号，可作为模块参数传入 —— `insmod gzvm_probe.ko hvc_fn=<n>` —— 并与 `gzvm.ko` 中使用的值比对。编译进模块的默认值由 `kernel/gzvm_common.h` 中的 `GZVM_HVC_PROBE_FN_DEFAULT` 决定。

### 6.4 不同的设备节点

节点名来自 `gzvm.ko` 中的 `gzvm_dev`，目前观测到的平台均为 `/dev/gzvm`。若你的设备不同，可在 CLI 中设置期望路径（`MGZ_DEVICE_NODE`）—— CLI 依据节点是否存在来验证，而非硬编码名称。

### 6.5 不同的 CPU 架构

CLI 为静态二进制，主目标为 `arm64-v8a`（`aarch64`），同时支持 32 位 `armeabi-v7a` 及更旧设备；内核模块为架构无关的 C 代码，随所构建的内核树而定。

---

## 7. 反馈新机型

请提交 issue（或直接 PR 修改本文件），内容包含：

```text
设备（型号 / 代号）：
SoC（getprop ro.board.platform）：
Android 版本（getprop ro.build.version.release）：
内核（uname -r）/ KMI：
gzvm.ko 是否存在（ls /system_dlkm/lib/modules/gzvm.ko）：
gzvm_drv_probe 是否在 kallsyms 中：
gz 分区（ls /dev/block/by-name | grep gz）：
保留内存（ls /proc/device-tree/reserved-memory | grep gz）：
HVC 探测结果（dmesg 中的 a0 值）：
执行 `mgz install` 后 /dev/gzvm 是否存在：
```

请附上 `mgz check --show-debug-details` 的输出，它已按统一格式包含上述全部信息。

---

## 8. 求助前的自检清单

* [ ] 已附上 `mgz check --show-debug-details` 输出
* [ ] 已确认 KMI，且模块正是针对该 KMI 构建的
* [ ] `gzvm.ko` 已加载（`lsmod | grep gzvm`）
* [ ] Root 可用，且 `ksud`/`su` 可达
* [ ] 已附上失败加载时的 `dmesg` 片段
* [ ] 已说明 SELinux 状态（`getenforce`）
