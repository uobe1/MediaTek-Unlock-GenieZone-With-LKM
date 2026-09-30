# 激活 GenieZone `gzvm` 驱动

当设备树未描述 Hypervisor 时，如何通过可加载内核模块（LKM）强制 MediaTek GenieZone `gzvm` 驱动执行 `probe()`。

| | |
|---|---|
| 参考设备 | MediaTek MT6855（beryl） |
| 参考平台 | Android 16 / Linux 6.12 GKI（`android16-6.12`） |
| 研究对象 | `gzvm.ko`（GenieZone VMM 接口） |
| 核心结论 | 设备树缺少 `mediatek,geniezone*` 节点时，LKM 可通过 `kprobe` 解析 `gzvm_drv_probe` 并直接调用，从而创建 `/dev/gzvm`；前提是 Hypervisor 确实运行在 EL2。 |

---

## 1. 问题描述

参考设备硬件支持 GenieZone，`gzvm.ko` 也已加载，但 `/dev/gzvm` 并不存在：

| 观测项 | 值 |
|---|---|
| SoC | MediaTek MT6855（硬件支持 GenieZone） |
| 内核 | `6.12.30-android16-5-g1ed949324a3e-ab13881345-4k`（GKI） |
| `gzvm.ko` | 已加载，`lsmod` 可见 |
| `/dev/gzvm` | **不存在** |
| 设备树 `compatible` | 仅有 `mediatek,MT6855`，无 `mediatek,geniezone` |
| 内核日志 | 无 `gzvm` / `geniezone` 的 probe 记录 |
| 分区 | `gz_a -> /dev/block/sdc29`、`gz_b -> /dev/block/sdc56` |
| 保留内存 | `mblock-14-gz`、`mblock-18-gz-log`、`mblock-19-gz_ffa_mailbox` |

驱动已构建、随包提供并加载，但从未绑定到设备，因此 `probe()` 回调不会执行。

### 1.1 为什么 probe 从未发生

`gzvm.ko` 是一个 platform 驱动，其匹配表为：

```c
static const struct of_device_id gzvm_of_match[] = {
    { .compatible = "mediatek,geniezone-hyp" },
    { /* sentinel */ },
};
```

platform 驱动只有在设备树（或 ACPI / board file）暴露匹配节点时才会 probe。参考设备的设备树中没有该节点，因此 `probe()` 永不进入。

### 1.2 `probe()` 究竟做了什么

```c
static int gzvm_drv_probe(struct platform_device *pdev)
{
    int ret;

    ret = gzvm_arch_probe();        /* 通过 HVC 进入 EL2 探测 Hypervisor */
    if (ret)
        return ret;

    ret = misc_register(&gzvm_dev); /* 创建 /dev/gzvm */
    if (ret)
        return ret;

    return 0;
}
```

决定性细节：**`pdev` 从未被解引用。** 传入 `NULL` 是安全的，并且会触发完整的 probe 流程。这说明设备树要求只是一个绑定层面的产物，而非功能上的必要条件。

---

## 2. 可行性分析

### 2.1 符号可见性

从 `vmlinux` 与模块符号表提取（见 `docs/assets/sample-gzvm-module-symbols.txt`）：

| 符号 | 存在 | 已导出（`__ksymtab_`） | 说明 |
|---|---|---|---|
| `misc_register` | 是 | 是 | 模块可直接调用 |
| `platform_device_register` | 是 | 是 | 模块可直接调用 |
| `kallsyms_lookup_name` | 是 | 否 | 现代内核需用 `kprobe` 获取 |
| `gzvm_drv_probe` | 是（`gzvm` 内） | 否 | 局部符号（`t`），但在 `kallsyms` 中可见 |
| `gzvm_arch_probe` | 是（`gzvm` 内） | 否 | 局部符号（`t`） |

`gzvm_drv_probe` 是局部文本符号，其他模块无法按名解析。但它**确实**出现在 `/proc/kallsyms` 中，而 `kprobe` 正是按名字在 `kallsyms` 中查找 —— 这正是我们需要的机制。

### 2.2 前置条件

| # | 前置条件 | 重要性 |
|---|---|---|
| 1 | `CONFIG_KALLSYMS_ALL=y` | 否则 `kprobe` 无法解析局部符号 |
| 2 | `CONFIG_KPROBES=y` | `register_kprobe()` 依赖它 |
| 3 | `CONFIG_MODULES=y` 且未开启 `CONFIG_MODULE_SIG_FORCE` | 未签名外部模块必须可加载 |
| 4 | `gzvm.ko` 已加载 | 其代码必须驻留才能被探测 |
| 5 | EL2 存在存活的 Hypervisor | 否则 `gzvm_arch_probe()` 返回 `-ENODEV` |

参考设备满足全部内核配置前置条件：`CONFIG_KALLSYMS_ALL=y`、`CONFIG_KPROBES=y`、`CONFIG_MODULES=y`，`CONFIG_MODULE_SIG=y` 但未开启 `CONFIG_MODULE_SIG_FORCE`（见 `docs/assets/sample-kernel-config-android16-6.12.txt`）。

模块签名在实践中并非障碍：永久 Root 方案本身就修补了内核，临时 Root 方案则已经证明未签名模块可被接受。SELinux 极少阻止 root 上下文执行 `insmod`，必要时 `setenforce 0` 仍是兜底手段。

### 2.3 固件层证据

| 证据 | 状态 | 解读 |
|---|---|---|
| `gz_a` / `gz_b` 分区 | 存在 | GenieZone 镜像仍在随包提供 |
| `mblock-14-gz` 保留内存 | 存在 | 已为 Hypervisor 预留内存 |
| `mblock-18-gz-log` | 存在 | 已配置 Hypervisor 日志缓冲区 |
| `mblock-19-gz_ffa_mailbox` | 存在 | 已配置 FF-A mailbox |
| 设备树节点 | 缺失 | 接口仅在软件层被关闭 |

固件仍携带 Hypervisor 镜像及其内存布局，这强烈暗示厂商只是移除了设备树绑定，而非移除 GenieZone 本身。但最终结论仍取决于 EL2 是否响应 HVC —— 参见[EL2 Hypervisor 探测](el2-hypervisor-detection.md)。

---

## 3. 辅助 LKM 设计

### 3.1 策略

1. 针对符号名 `"gzvm_drv_probe"` 注册 `kprobe`。
2. 读回 `kp.addr`，即该函数的运行时地址。
3. 立即注销 `kprobe`（我们只需要地址）。
4. 将地址转换为函数指针并以 `NULL` 调用。
5. 报告返回值；成功时 `/dev/gzvm` 即存在。

### 3.2 参考实现

```c
static int (*gzvm_drv_probe_ptr)(struct platform_device *pdev);

static int __init gzvm_aux_init(void)
{
    struct kprobe kp = { .symbol_name = "gzvm_drv_probe" };
    int ret;

    ret = register_kprobe(&kp);
    if (ret < 0) {
        pr_err("gzvm_aux: kprobe failed: %d\n", ret);
        return ret;
    }

    gzvm_drv_probe_ptr = (void *)kp.addr;
    unregister_kprobe(&kp);

    pr_info("gzvm_aux: gzvm_drv_probe @ %px\n", gzvm_drv_probe_ptr);

    ret = gzvm_drv_probe_ptr(NULL);
    pr_info("gzvm_aux: gzvm_drv_probe returned %d\n", ret);

    return ret;
}
```

`kernel/gzvm_unlock.c` 中的正式实现在此基础上增加了候选符号回退、重试循环以及 `/dev/gzvm` 存在性校验。

### 3.3 安全考量

* 模块不写设备树，也不修补内核。
* 只有在地址校验为非空、且确认所属模块仍驻留后，才会调用 `gzvm_drv_probe(NULL)`。
* 返回非 0 时模块 init 失败，内核不会残留半初始化状态。
* 若 `misc_register()` 返回 `-EBUSY`，说明设备已存在，模块将其视为成功。

---

## 4. 操作流程

```bash
# 1. 构建（见仓库 README 与 GitHub Actions 工作流）
cmake -B build -DMGZ_ANDROID_VERSION=16 -DMGZ_KERNEL_VERSION=6.12
cmake --build build --target gzvm_modules

# 2. 推送并单次加载
adb push build/kernel/gzvm_unlock.ko /data/local/tmp/
adb shell sudo insmod /data/local/tmp/gzvm_unlock.ko

# 3. 验证
adb shell ls -l /dev/gzvm
adb shell sudo dmesg | tail -20
```

预期内核日志：

```text
gzvm_unlock: gzvm_drv_probe @ <addr>
gzvm_unlock: gzvm_drv_probe returned 0
gzvm_unlock: /dev/gzvm is now present
```

---

## 5. 失败矩阵

| 现象 | 原因 | 处置 |
|---|---|---|
| `register_kprobe()` 失败 | 符号不在 kallsyms 中，或未开启 `CONFIG_KALLSYMS_ALL` | 检查 `/proc/kallsyms`，尝试 `gzvm_unlock` 内置的候选符号名 |
| `gzvm_drv_probe returned -ENODEV`(19) | 没有 Hypervisor 响应 HVC | 参见[EL2 探测](el2-hypervisor-detection.md)，此路不通 |
| `gzvm_drv_probe returned -EBUSY`(16) | `misc_register()` 被拒，通常设备已存在 | 视为成功，验证 `/dev/gzvm` |
| 返回 0 但无 `/dev/gzvm` | SELinux 阻止了设备节点创建 | 检查 `dmesg | grep avc`，在宽容模式下重试 |
| `insmod` 直接被拒 | 签名强制或 `modules_disabled` | 检查 `/proc/sys/kernel/modules_disabled`、Root 方案 |
| 加载时内核崩溃 | KMI 构建不匹配 | 针对精确 KMI 重新构建，参见[GKI 兼容性](gki-lkm-compatibility.md) |

---

## 6. 限制与风险

* **仅限自有设备。** 改变内核行为可能导致系统不稳定、数据丢失并失去保修。
* **依赖 Hypervisor 状态。** 若厂商在 ATF/EL2 层禁用了 GenieZone，任何 LKM 都无法复活它。
* **KMI 必须精确匹配。** 为其他 KMI 构建的模块会被拒绝，甚至导致内核崩溃。
* **地址有效性。** 解析出的地址属于 `gzvm.ko`；若该模块在期间被卸载而我们的模块仍在，指针将悬空。因此模块会通过 `find_module()` 确认其驻留状态。

---

## 7. 结论

当设备树缺少 `mediatek,geniezone` 时，从辅助 LKM 调用 `gzvm_drv_probe(NULL)` 是激活该接口的实用方案：无需重新编译内核、无需修改设备树、无需签名密钥。固件证据（分区、保留内存）表明 Hypervisor 仍在，成功概率较高，但最终答案是 EL2 是否响应 GenieZone HVC。

---

## 附录：命令速查

```bash
# 环境
uname -r
getprop ro.build.version.release
getprop ro.build.version.sdk
getprop ro.product.device

# 设备状态
lsmod | grep gzvm
ls -l /dev/gzvm
getenforce
cat /proc/sys/kernel/modules_disabled
cat /proc/sys/kernel/kptr_restrict

# 符号
cat /proc/kallsyms | grep -E 'gzvm_drv_probe|gzvm_arch_probe'
cat /proc/kallsyms | grep -E 'misc_register|platform_device_register'

# 设备树、分区、保留内存
find /proc/device-tree -iname '*genie*' -o -iname '*gz*'
cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2'
ls /proc/device-tree/reserved-memory | grep -i gz

# 内核配置
zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'

# 加载与验证
sudo insmod /data/local/tmp/gzvm_unlock.ko
dmesg | tail -30
ls -l /dev/gzvm
sudo rmmod gzvm_unlock
```

> 如果在设备本地端（手机而非电脑）运行，去掉 `adb shell` 前缀即可。
