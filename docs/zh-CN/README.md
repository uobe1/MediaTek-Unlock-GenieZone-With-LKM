# 文档（简体中文）

**MediaTek-Unlock-GenieZone-With-LKM** 的文档目录。仓库默认语言为英文，英文文档位于 [`../en/`](../en/)。

## 目录

| 文档 | 内容 |
|---|---|
| [机型适配指南](device-adaptation.md) | **建议先读。** 如何判断自己的机型是否支持，以及如何为其他 SoC、内核或 Android 版本适配构建。 |
| [激活 gzvm 驱动](gzvm-driver-activation.md) | 设备树缺少 GenieZone 节点时 `probe()` 为何不执行，以及辅助 LKM 如何强制触发它。 |
| [EL2 Hypervisor 探测](el2-hypervisor-detection.md) | 判断 EL2 是否仍在运行 GenieZone Hypervisor 的七类方法，以及为何只有 HVC 探测才是决定性证据。 |
| [GKI LKM 兼容性](gki-lkm-compatibility.md) | KMI 作为兼容性单位、Android/内核版本对照表，以及常见误解。 |

## 证据样本

| 文件 | 说明 |
|---|---|
| [`../assets/sample-kernel-config-android16-6.12.txt`](../assets/sample-kernel-config-android16-6.12.txt) | 参考设备（Android 16 / 6.12）的 `zcat /proc/config.gz` 输出。 |
| [`../assets/sample-gzvm-module-symbols.txt`](../assets/sample-gzvm-module-symbols.txt) | `gzvm.ko` 符号表，可见 `gzvm_drv_probe` 为局部符号。 |

## 推荐阅读顺序

1. [机型适配指南](device-adaptation.md) —— 先判断是否值得投入时间。
2. [EL2 Hypervisor 探测](el2-hypervisor-detection.md) —— 确认 Hypervisor 是否仍然存在。
3. [激活 gzvm 驱动](gzvm-driver-activation.md) —— 理解模块做了什么。
4. [GKI LKM 兼容性](gki-lkm-compatibility.md) —— 针对正确的 KMI 构建。

## 术语

| 术语 | 含义 |
|---|---|
| GenieZone（gz） | MediaTek 的 EL2 Hypervisor |
| `gzvm` | 通过 `/dev/gzvm` 暴露 GenieZone 的 Linux 驱动 |
| KMI | Kernel Module Interface，形如 `android<版本>-<内核大版本>` |
| GKI | Generic Kernel Image，通用内核镜像 |
| HVC | Hypervisor Call，EL1 → EL2 的陷入指令 |
| LKM | Loadable Kernel Module，可加载内核模块 |
