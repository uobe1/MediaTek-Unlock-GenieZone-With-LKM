# 贡献指南

## 我能做什么？<a name="toc"></a>

- [如何使用本指南](#introduction)？
- 提问或反馈？🤔🐛😱
  - [请求支持](#request-support)
  - [报告错误](#report-an-error-or-bug)
  - [请求新功能](#request-a-feature)
- 动手做点什么？🤓👩🏽‍💻📜🍳
  - [项目搭建](#project-setup)
  - [贡献文档](#contribute-documentation)
  - [贡献代码](#contribute-code)
  - [反馈你的机型](#report-your-device)
- 参与管理 ✅🙆🏼💃👔
  - [评审 Pull Request](#review-pull-requests)
- [把这样的指南加到我的项目](#attribution)？🤖😻👻

> 本指南的英文版见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 简介

非常感谢你有兴趣参与贡献！我们欢迎并重视各种类型的贡献。请对照[目录](#toc)了解有哪些参与方式，以及本项目如何处理它们。📝

提交贡献前请先阅读相关章节！这会让维护者更容易充分利用你的贡献，也让所有人的体验更顺畅。💚

在所有事情之前有一条规则：本项目只面向**你自己拥有的硬件**。用于绕过 Bootloader 锁、规避授权，或针对贡献者并不拥有的设备的贡献，都不会被接受。

## 请求支持

如果你对本项目的用法有疑问，或者需要澄清某些内容：

- 在 https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/issues 提交 Issue
- 附上 `mgz check --show-debug-details` 的输出。它以统一格式包含平台、内核、KMI、Hypervisor 与固件事实，多数问题看它就能回答。
- 说明你的 Android 版本、内核版本（`uname -r`）以及 Root 方案（KernelSU / Magisk / 其他）。

提交之后：

- 维护者会尽快回应。
- 若你与维护者 30 天内都没有回应，Issue 会被关闭。想继续请回复一次以请求重开；请不要用新 Issue 来延续旧 Issue。

## 报告错误

如果你遇到了错误或 Bug：

- 在 https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/issues 提交 Issue
- 提供他人可以照做的**复现步骤**。
- 附上 `mgz check --show-debug-details` 输出，以及加载失败前后 `dmesg` 的相关行。

一份有用的报告长这样：

```text
设备（型号 / 代号）：
SoC（getprop ro.board.platform）：
Android 版本 / 内核（uname -r）：
模块构建所针对的 KMI：
Root 方案（KernelSU 版本 / Magisk 版本）：
失败的命令：
mgz check --show-debug-details 输出：
相关 dmesg：
SELinux 状态（getenforce）：
```

提交之后：

- 维护者会尝试复现。没有复现步骤的 Issue 会标记为 `needs-repro`，直到有人复现为止。
- 可复现的 Bug 会标记为 `needs-fix`，交由[有人实现](#contribute-code)。

## 请求新功能

如果你有想法，或者希望支持某台设备：

- 提交 Issue，说明该功能以及它解决的问题。
- 若是新设备，请改用[反馈你的机型](#report-your-device)，那里收集的信息正是我们所需要的。
- 若是新的 KMI 代次，请说明需要的 Android 与内核大版本；构建已经把两者作为入参。

## 项目搭建

想贡献代码了，这很好！本项目使用 GitHub Pull Request 管理贡献，如果你还没做过，请先[了解如何 fork 项目并提交 PR](https://guides.github.com/activities/forking)。

如果这些看起来太多，也可以[直接编辑文件](https://help.github.com/articles/editing-files-in-another-user-s-repository/)而无需任何本地搭建。是的，[代码也一样](#contribute-code)。

构建模块只需要某个 KMI 代次的已准备好的内核构建目录，因为承载导出符号 CRC 的 `Module.symvers` 就在其中；内核本体从来不需要编译。DDK 按 KMI 逐个提供该目录，是推荐做法：

```bash
git clone git@github.com:uobe1/MediaTek-Unlock-GenieZone-With-LKM.git
cd MediaTek-Unlock-GenieZone-With-LKM

# 在对应 KMI 的 DDK 容器内，例如 ghcr.io/ylarod/ddk:android16-6.12
./scripts/build-modules.sh --kmi android16-6.12 --src kernel --out dist/modules
```

没有 DDK 时，可以用 `kernel/common` 的浅克隆自行产出同一目录；此时只构建 `=m` 部分，因为它们才是符号表的来源：

```bash
./scripts/fetch-kernel.sh --android 16 --kernel 6.12
./scripts/prepare-kernel.sh --src kernel-src --out kbuild
./scripts/build-modules.sh --kdir "$PWD/kbuild" --src kernel --out dist/modules
```

两者也可以通过 CMake 配合已准备好的构建目录完成：

```bash
cmake -B build -DMGZ_ANDROID_VERSION=16 -DMGZ_KERNEL_VERSION=6.12 \
  -DMGZ_KERNEL_DIR="$PWD/kbuild"
cmake --build build --target dist
```

只构建 CLI，不需要内核树：

```bash
cmake -B build -DMGZ_ANDROID_ABI=host   # 在设备本机
cmake -B build -DMGZ_ANDROID_ABI=arm64-v8a -DMGZ_ANDROID_NDK=/path/to/ndk
cmake --build build --target mgz
```

在有编译器（Termux）的已 Root 手机上，`MGZ_ANDROID_ABI=host` 产出的二进制可以立即运行。

## 贡献文档

文档是本项目极其重要、关键的一部分。文档是我们记录"在做什么、怎么做、为什么"的方式 —— 对一个内核层级的项目来说，它也是读者保持安全的方式。在此先行感谢。

任何规模的文档贡献都受欢迎！哪怕只是重写一句话让它更清楚，或修正一个拼写错误，都欢迎直接提 PR！

贡献文档的步骤：

- [完成项目搭建](#project-setup)。
- 编辑或新增相应文档。
- **每份文档都有英文与简体中文两个版本**（`docs/en/` 为默认语言，`docs/zh-CN/` 为翻译）。改动其中一个，就要在同一个 PR 中改动另一个。`README.md` 与 `README.zh-CN.md` 同理。
- 保持与其余文档一致的格式。
- 重读你写的内容，并用拼写检查工具过一遍。
- 使用[conventional-changelog 格式](https://github.com/conventional-changelog/conventional-changelog-angular/blob/master/convention.md)撰写清晰简练的提交信息。文档提交应使用 `docs(<组件>): <信息>`。
- 前往 https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/pulls 提交 PR。若与某个 Issue 相关，请在描述中加入 `Fixes: #123`。

## 贡献代码

代码贡献遵循仓库的结构：

| 区域 | 语言 / 风格 |
|---|---|
| `kernel/` | C，Linux 内核编码风格：制表符缩进、80 列、每个文件定义 `pr_fmt` |
| `cli/` | C99，制表符缩进、80 列、无外部依赖 |
| `module/` | POSIX `sh`，运行于 KernelSU 的 busybox `ash`；由 `cli/ksu.c` 逐字镜像 |
| `scripts/` | POSIX shell，需同时能在 Linux 与 Android 上运行 |

这里特别重要的规则：

- **内核代码必须保持版本无关。** 在运行时解析符号（`kprobe`），不要假定任何地址，不要假定属于某个厂商内核树的结构体布局。
- **只使用已导出的符号。** 使用某个内核符号前，先确认它对模块是导出的；若未导出就换一种做法。
- **不要破坏 KMI 约定。** 任何改变所需 KMI 代次的改动，都必须同步反映到构建参数与文档中。
- **`module/` 与 `cli/ksu.c` 必须保持一致。** 改了一个启动脚本，就要改另一处。
- 若希望按 GPLv3 授权署名，请在提交中加入 `Signed-off-by` 行。
- 提交信息使用 conventional-changelog 格式，例如 `feat(kernel): ...`、`fix(cli): ...`、`ci: ...`。

提交 PR 之前：

```bash
cmake --build build --target mgz      # 必须无新增警告地编译通过
mgz --help                            # 检查参数解析是否正常
```

提交 PR 之后：

- 会有一位或多位维护者进行评审。
- 若维护者要求修改，请修改、推送并请求再次评审。
- 若维护者决定不接受，他们会感谢你并说明原因。这没关系！我们依然非常感谢你付出的时间。💚

## 反馈你的机型

最有价值的贡献是一份经过验证的机型报告。请先阅读[机型适配指南](docs/zh-CN/device-adaptation.md)、执行检查，然后提交 Issue（或直接 PR 修改该指南），内容包含：

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

请附上 `mgz check --show-debug-details`，它已按统一格式包含上述全部信息。

## 评审 Pull Request

评审他人 PR 时：

- 保持友善。默认对方出于善意。
- 检查内核代码不会造成比"insmod 失败"更严重的后果：不写入未分配的内存，不假定厂商结构体布局。
- 检查文档改动是否同时落到**两种语言**。
- 检查新的 KMI 代次、符号名或 HVC 调用号是否已写入适配指南。

## 致谢

本指南由 WeAllJS 的 `CONTRIBUTING.md` 生成器生成。[也为你自己的项目生成一份](https://npm.im/weallcontribute)！
