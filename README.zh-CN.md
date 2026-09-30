# MediaTek-Unlock-GenieZone-With-LKM 欢迎 👋

![Version](https://img.shields.io/badge/version-0.1.0-blue.svg?cacheSeconds=2592000)
[![文档](https://img.shields.io/badge/documentation-yes-brightgreen.svg)](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM#readme)
[![维护状态](https://img.shields.io/badge/Maintained%3F-yes-green.svg)](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/graphs/commit-activity)
[![许可证: GPL-3.0-or-later](https://img.shields.io/github/license/uobe1/MediaTek-Unlock-GenieZone-With-LKM)](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/blob/main/LICENSE)

> 用可加载内核模块解锁 MediaTek GenieZone（gzvm）

[English](README.md)（默认） | 简体中文

---

## 免责声明

> [!WARNING]
> 本项目会在内核上下文执行代码并改变设备对外暴露的接口。请仅在你自己拥有的硬件上使用。它可能导致系统不稳定、数据丢失并失去保修。本项目不绕过 Bootloader 锁：它需要 Root 权限以及加载内核模块的能力。

---

## 为什么需要它

许多 MediaTek 设备上，GenieZone 在硬件与固件中都存在，`gzvm.ko` 驱动也随包提供并已加载，但 `/dev/gzvm` 从不出现。原因是设备树缺少绑定节点：

```c
static const struct of_device_id gzvm_of_match[] = {
    { .compatible = "mediatek,geniezone-hyp" },
    { /* sentinel */ },
};
```

没有匹配节点就没有 `probe()`，`gzvm_drv_probe()` 永不执行。而它并不解引用 `platform_device` 参数，因此传入 `NULL` 也能完成整个 probe 流程：先通过 HVC 询问 Hypervisor，再注册 `/dev/gzvm`。

本项目做的就只有这件事 —— 不重新编译内核、不修改设备树、不需要签名密钥。

---

## 包含内容

| 组件 | 职责 |
|---|---|
| `kernel/gzvm_probe.ko` | 探测。发起 GenieZone HVC 判断 EL2 是否有 Hypervisor 存活，并通过 `kallsyms` 解析驱动 probe 入口。 |
| `kernel/gzvm_unlock.ko` | 激活。用 `kprobe` 解析 `gzvm_drv_probe` 并以 `NULL` 调用，然后校验设备节点是否出现。 |
| `mgz`（CLI） | 自动检测、单次加载、移除与 KernelSU 持久化。 |

探测模块的结果以只读模块参数形式发布在 `/sys/module/gzvm_probe/parameters/`，用户空间无需解析 `dmesg`。

---

## 我的设备支持吗？

请阅读[机型适配指南](docs/zh-CN/device-adaptation.md)，其中说明了如何判断一台从未见过的设备。简而言之：

```bash
adb push mgz /data/local/tmp/
adb shell chmod +x /data/local/tmp/mgz
adb shell /data/local/tmp/mgz check --show-debug-details
```

`check` 会逐层输出结论并给出总体判定：

| 结论 | 含义 |
|---|---|
| `SUPPORTED` | MediaTek 平台、EL2 Hypervisor 存活、probe 符号可达、未签名模块可加载 |
| `LIKELY` | 固件仍携带 GenieZone，但 HVC 探测尚无定论 —— 通常是模块未按本机 KMI 构建 |
| `UNSUPPORTED` | 非 GenieZone 平台，或 EL2 已确认失效，或模块无法加载 |

参考设备（MT6855，Android 16，内核 6.12.30）的实测输出：

```text
kernel
  release           6.12.30-android16-5-g1ed949324a3e-ab13881345-4k
  kmi               android16-6.12
  kallsyms_all      yes
  kprobes           yes
  sig_force         no

hypervisor
  gzvm.ko file      yes
  gzvm loaded       yes
  probe symbol      yes
  EL2 HVC           alive (a0=0)
```

---

## 快速开始

```bash
# 1. 获取构建产物（CI 构建，或按下文自行构建）
#    gzvm_probe.ko、gzvm_unlock.ko、mgz
adb push gzvm_probe.ko gzvm_unlock.ko mgz /data/local/tmp/lkm_build/

# 2. 本机是否适用？
adb shell /data/local/tmp/lkm_build/mgz check

# 3. 单次激活，重启前有效
adb shell /data/local/tmp/lkm_build/mgz install

# 4. 验证
adb shell ls -l /dev/gzvm

# 5. 让它在重启后依然生效（需要 KernelSU / ksud）
adb shell /data/local/tmp/lkm_build/mgz ksu-keep-alive
```

---

## CLI 参考

```
mgz <命令> [选项] [模块路径]
```

### 命令

| 命令 | 作用 |
|---|---|
| `check` | 采集平台、内核、Hypervisor 与固件事实并给出结论 |
| `install` | 确保 `gzvm.ko` 已加载，然后单次加载 `gzvm_unlock.ko` 并校验 `/dev/gzvm` |
| `remove` | 卸载 `gzvm_unlock.ko` 并移除已持久化的 KernelSU 模块 |
| `ksu-keep-alive` | 安装 KernelSU 模块，使每次开机自动重新加载 `.ko` |
| `help` | 显示用法 |

### 选项

| 长选项 | 短选项 | 作用 |
|---|---|---|
| `--help` | `-h` | 显示用法 |
| `--version` | `-V` | 显示版本与许可证 |
| `--show-debug-details` | `-d` | 打印每一步检测细节与命令输出 |
| `--quiet` | `-q` | 仅打印错误 |
| `--no-color` | `-n` | 关闭 ANSI 颜色 |

`[模块路径]` 可以是 `.ko` 文件，也可以是包含它的目录。省略时的搜索顺序为：当前目录、`/data/local/tmp/lkm_build`、`/data/local/tmp`、可执行文件所在目录。

### 权限获取

Root 通过 `sudo` 获取，失败时退化为 `su -c` —— 在现代环境中 `sudo` 的兼容性更好。`ksud` 位于 `/data/adb/ksu/bin`，通常只有 `su -c` 的 shell 才能看到，因此 KernelSU 相关操作一律走 `su -c`。存在 `ksud` 时使用 `ksud insmod` 加载模块，它会**保留 kallsyms 访问能力**，而这正是基于 `kprobe` 的符号解析所需要的。

---

## 持久化

`mgz ksu-keep-alive` 会把 KernelSU 模块写入 `/data/adb/modules/mgz_unlock/`：

```
/data/adb/modules/mgz_unlock
├── module.prop          模块元数据
├── gzvm_unlock.ko       模块本体
├── load.sh              公共加载实现
├── service.sh           late_start service 阶段（常规启动）
├── late-load.sh         late-load 模式（替代 post-fs-data.sh）
├── boot-completed.sh    开机完成后校验并重试一次
├── action.sh            管理器中的手动触发入口
├── uninstall.sh         移除时卸载我们的模块
└── skip_mount           无需 overlay
```

两种 Root 方案的启动流程都被覆盖：

* **常规启动** —— `service.sh` 运行在 late_start service 阶段，该阶段非阻塞，也是 KernelSU 推荐的脚本阶段。它会先等待 `gzvm.ko` 出现再加载我们的模块。
* **late-load** —— 对于开机后才加载 `kernelsu.ko` 的 Root 方案，`late-load.sh` 会替代 `post-fs-data.sh` 执行。

该功能仅在 `ksud` 存在时可用，否则 CLI 会直接拒绝。

---

## 构建

两个版本入参决定 KMI 代次，二者都是**大版本**：Google 只在同一个 `android<版本>-<内核>` 组合内保证 KMI 稳定性，因此 `6.12.x` 全部属于 `android16-6.12`。

```bash
cmake -B build \
  -DMGZ_ANDROID_VERSION=16 \
  -DMGZ_KERNEL_VERSION=6.12 \
  -DMGZ_KERNEL_DIR=/path/to/prepared/gki/tree
cmake --build build --target dist
```

| Android | 内核 | KMI | 分支 |
|---|---|---|---|
| 15 | 6.6 | `android15-6.6` | `android15-6.6` |
| 16 | 6.12 | `android16-6.12` | `android16-6.12` |

构建目标：

| 目标 | 产物 |
|---|---|
| `mgz` | CLI（工具链支持时为静态链接） |
| `gzvm_modules` | `gzvm_probe.ko`、`gzvm_unlock.ko` |
| `ksu_module` | 可刷入的 KernelSU zip |
| `dist` | 以上全部 |

只需拉取 `kernel/common`，辅助脚本会以"仍能产出可加载模块"的最小代价准备内核树 —— vmlinux 构建正是外部模块所依赖的 `Module.symvers` 的来源：

```bash
./scripts/fetch-kernel.sh --android 16 --kernel 6.12   # 浅克隆，单个项目
./scripts/prepare-kernel.sh --src kernel-src --out kbuild
```

GitHub Actions 工作流做同样的事，并把两个版本作为入参（默认 `16` 与 `6.12`）；产物为 `gzvm-modules-<kmi>`、`mgz-<abi>-<kmi>` 与 `mgz-bundle-<kmi>`。

CLI 交叉编译：

```bash
cmake -B build \
  -DMGZ_ANDROID_ABI=arm64-v8a \
  -DMGZ_ANDROID_NDK=/path/to/ndk
```

支持 `arm64-v8a`、`armeabi-v7a` 以及旧版 `armeabi`；本机构建（`-DMGZ_ANDROID_ABI=host`）可直接在设备上完成。

---

## 仓库结构

```
kernel/     gzvm_probe.c、gzvm_unlock.c、公共头文件、内核 Makefile
cli/        mgz：主程序、权限处理、检测、KernelSU 持久化
module/     KernelSU 模块模板（与 cli/ksu.c 中内容一致）
scripts/    fetch-kernel.sh、prepare-kernel.sh
docs/en/    英文文档（默认）
docs/zh-CN/ 简体中文文档
```

---

## 故障排查

| 现象 | 可能原因 |
|---|---|
| `kprobe failed` | `gzvm.ko` 未加载，或未开启 `CONFIG_KALLSYMS_ALL` |
| `probe(NULL) returned -19` | 没有 Hypervisor 响应 HVC —— 已在 Linux 之下被禁用 |
| `probe(NULL) returned -16` | 设备节点已存在，视为成功 |
| `insmod` 被拒 | 签名强制或 `modules_disabled=1` |
| `verdict: LIKELY` | 按本机 KMI 重新构建模块后再执行 `check` |
| 加载时内核崩溃 | KMI 不匹配，需针对精确的内核代次重新构建 |

完整失败矩阵见[激活 gzvm 驱动](docs/zh-CN/gzvm-driver-activation.md)，HVC 结果的含义见[EL2 Hypervisor 探测](docs/zh-CN/el2-hypervisor-detection.md)。

---

## 文档

| 文档 | 内容 |
|---|---|
| [机型适配指南](docs/zh-CN/device-adaptation.md) | 如何判断新设备并适配构建 |
| [激活 gzvm 驱动](docs/zh-CN/gzvm-driver-activation.md) | `probe()` 为何不执行，以及模块如何强制触发 |
| [EL2 Hypervisor 探测](docs/zh-CN/el2-hypervisor-detection.md) | 观察 EL2 的七类方法，以及为何只有 HVC 才算数 |
| [GKI LKM 兼容性](docs/zh-CN/gki-lkm-compatibility.md) | KMI 作为兼容性单位 |

---

## 作者

👤 **uobe1 <uobe1@users.noreply.github.com>**

- GitHub: [@uobe1](https://github.com/uobe1)

## 支持一下

如果本项目帮到了你，点个 ⭐️ 吧！

## 📝 许可证

Copyright © 2026 [uobe1 <uobe1@users.noreply.github.com>](https://github.com/uobe1).

本项目采用 [GPL-3.0-or-later](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/blob/main/LICENSE) 许可。

---

_本 README 由 [readme-md-generator](https://github.com/kefranabg/readme-md-generator) 生成 ❤️_
