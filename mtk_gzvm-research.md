# 报告：编写 LKM 强行激活 GenieZone gzvm 接口

## 元数据

| 项目 | 内容 |
|---|---|
| 报告日期 | 2026-09-27 |
| 主题 | 通过辅助 LKM 绕过设备树缺失，强制触发 gzvm 驱动 probe |
| 目标设备 | MediaTek MT6855 / Android 16 / Linux 6.12 GKI |
| 涉及模块 | `gzvm.ko`（GenieZone VMM 接口） |
| 核心结论 | 在设备树缺少 `mediatek,geniezone` 节点时，可用 LKM 通过 `kprobe` 获取 `gzvm_drv_probe` 地址并直接调用，从而创建 `/dev/gzvm`；成功与否取决于 hypervisor 是否在 EL2 运行 |

---

## 摘要

设备硬件（MT6855）支持 GenieZone，`gzvm.ko` 模块已加载，但因设备树中缺少 `mediatek,geniezone` 或 `mediatek,geniezone-hyp` 节点，驱动从未触发 `probe`，导致 `/dev/gzvm` 不存在。

本文提出一种辅助 LKM 方案：利用 `kprobe` 按符号名查找 `gzvm_drv_probe` 的运行时地址，然后直接调用 `gzvm_drv_probe(NULL)`。该函数不访问 `platform_device` 参数，内部依次执行 `gzvm_arch_probe()` 和 `misc_register(&gzvm_dev)`，若 hypervisor 可用，则成功创建 `/dev/gzvm`。

该方案无需重新编译内核、无需修改设备树、无需内核签名密钥，前提是内核允许加载未签名 LKM 且 SELinux 策略不拦截。

---

## 1. 背景与问题

### 1.1 设备状态

- SoC：MediaTek MT6855（硬件支持 GenieZone）
- 内核：`6.12.30-android16-5-g1ed949324a3e-ab13881345-4k`（GKI）
- 模块：`/system_dlkm/lib/modules/gzvm.ko` 已加载（`lsmod` 可见）
- 设备节点：`/dev/gzvm` 不存在
- 设备树 compatible：仅有 `mediatek,MT6855`，无 `mediatek,geniezone`
- 内核日志：无 `gzvm` 或 `geniezone` 的 probe 记录
- 分区：`gz_a -> /dev/block/sdc29`、`gz_b -> /dev/block/sdc56` 存在
- 保留内存：`mblock-14-gz`、`mblock-18-gz-log`、`mblock-19-gz_ffa_mailbox` 存在

### 1.2 驱动 probe 机制

GenieZone 驱动 `gzvm.ko` 的匹配表为：

```c
static const struct of_device_id gzvm_of_match[] = {
    { .compatible = "mediatek,geniezone-hyp" },
    { /* sentinel */ },
};
```

`gzvm_drv_probe()` 简化逻辑：

```c
static int gzvm_drv_probe(struct platform_device *pdev)
{
    int ret;

    ret = gzvm_arch_probe();      // 通过 HVC 探测 hypervisor
    if (ret)
        return ret;

    ret = misc_register(&gzvm_dev); // 创建 /dev/gzvm
    if (ret)
        return ret;

    return 0;
}
```

关键点：**`pdev` 未被使用**，因此传入 `NULL` 不会导致崩溃。

---

## 2. 关键发现

### 2.1 符号可见性

从 `vmlinux` 和 `kallsyms.txt` 分析：

| 符号 | 存在 | 导出 (`__ksymtab_`) | 说明 |
|---|---|---|---|
| `misc_register` | ✅ | ✅ | 模块可直接调用 |
| `platform_device_register` | ✅ | ✅ | 模块可直接调用 |
| `kallsyms_lookup_name` | ✅ | ❌ | 未导出，需 kprobe |
| `gzvm_drv_probe` | ✅（模块内） | ❌ | 局部符号，但 `kallsyms` 可见 |
| `gzvm_arch_probe` | ✅（模块内） | ❌ | 局部符号 |

`gzvm_drv_probe` 类型为 `t`（局部文本），未导出，但出现在 `/proc/kallsyms` 中，因此 **`kprobe` 可按符号名查找其地址**。

### 2.2 绕过设备树的可行性

由于 `gzvm_drv_probe` 不依赖 `pdev`，直接调用即可触发完整 probe 流程。这完全绕过了设备树匹配。

### 2.3 成功的前提

- hypervisor 必须在 EL2 运行，`gzvm_arch_probe()` 的 HVC 调用才能成功。
- 若 OEM 在更底层（ATF/EL2）禁用了 GenieZone，`gzvm_arch_probe()` 返回 `-ENODEV`，则 probe 失败，`/dev/gzvm` 不会出现。
- 固件层证据增强：`gz_a`/`gz_b` 分区和 `mblock-*-gz*` 保留内存的存在，说明 GenieZone 镜像与内存配置仍在固件中，底层并未完全删除 GenieZone。但 `gzvm_arch_probe()` 能否成功仍取决于 EL2 是否响应 HVC。

---

## 3. 辅助 LKM 设计

### 3.1 核心思路

1. 使用 `kprobe` 按符号名 `"gzvm_drv_probe"` 获取地址。
2. 将地址转换为函数指针。
3. 调用 `gzvm_drv_probe_ptr(NULL)`。
4. 检查返回值及 `/dev/gzvm` 是否创建。

### 3.2 完整代码

```c
// gzvm_aux.c
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/platform_device.h>

static int (*gzvm_drv_probe_ptr)(struct platform_device *pdev);

static int __init gzvm_aux_init(void)
{
    struct kprobe kp = {
        .symbol_name = "gzvm_drv_probe",
    };
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

static void __exit gzvm_aux_exit(void)
{
    pr_info("gzvm_aux: exit\n");
}

module_init(gzvm_aux_init);
module_exit(gzvm_aux_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aux LKM to trigger gzvm probe");
```

### 3.3 Makefile

```makefile
obj-m += gzvm_aux.o
KERNEL_DIR ?= /path/to/common-android16-6.12

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
```

---

## 4. 操作流程

### 4.1 准备内核源码

同步 Android 16 / 6.12 GKI 源码：

```bash
repo init -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12
repo sync -c -j$(nproc) -q
```

### 4.2 编译 LKM

```bash
make KERNEL_DIR=/path/to/common-android16-6.12
```

### 4.3 加载模块

```bash
adb push gzvm_aux.ko /data/local/tmp/
adb shell su -c "insmod /data/local/tmp/gzvm_aux.ko"
```

### 4.4 验证

```bash
adb shell su -c "dmesg | tail -20"
adb shell ls -l /dev/gzvm
```

预期输出：

- `dmesg` 出现 `gzvm_aux: gzvm_drv_probe returned 0`
- `/dev/gzvm` 存在

若返回非 0，查看 `dmesg` 中 `gzvm_arch_probe` 的具体错误。

---

## 5. 通用性与边界条件

### 5.1 跨机型通用性

该 LKM 的兼容性由 **KMI** 决定，而非具体机型。对于 Android 16 / Linux 6.12，KMI 为 `android16-6.12`。只要目标设备内核属于同一 KMI，且允许加载外部模块，该 LKM 即可通用。

### 5.2 必须满足的条件

- [ ] 设备内核 KMI 为 `android16-6.12`
- [ ] 内核允许加载未签名 LKM（`CONFIG_MODULE_SIG_FORCE` 未开启）
- [ ] SELinux 为宽容模式或策略允许 `insmod`
- [ ] `gzvm.ko` 已加载（`lsmod | grep gzvm`）
- [ ] hypervisor 在 EL2 运行（否则 `gzvm_arch_probe` 失败）

### 5.3 失败情形

| 现象 | 原因 |
|---|---|
| `kprobe` 注册失败 | 符号名不在 kallsyms 中（内核未开 `CONFIG_KALLSYMS_ALL`） |
| `gzvm_drv_probe returned -ENODEV` | hypervisor 未运行，HVC 探测失败 |
| `gzvm_drv_probe returned -EBUSY` | `misc_register` 失败，可能已注册 |
| `/dev/gzvm` 未出现但返回 0 | SELinux 拦截设备节点创建 |
| 模块加载被拒绝 | 签名强制或 SELinux 策略 |

---

## 6. 风险与限制

- **仅限自有设备**：修改内核行为可能导致系统不稳定、数据丢失或保修失效。
- **依赖 hypervisor 状态**：若 OEM 在 EL2 禁用 GenieZone，LKM 无法激活。
- **内核崩溃风险**：直接调用内核函数指针需确保地址正确；`kprobe` 获取的地址在模块卸载后可能失效。
- **KMI 严格匹配**：LKM 必须使用与设备内核完全一致的 KMI 编译，否则加载失败。
- **法律与合规**：请遵守当地法律法规及设备厂商条款。

---

## 7. 结论

在设备树缺少 `mediatek,geniezone` 节点的情况下，通过辅助 LKM 调用 `gzvm_drv_probe(NULL)` 是一种可行的绕过方案。固件层证据（`gz_a`/`gz_b` 分区、`mblock-14-gz`、`mblock-18-gz-log`、`mblock-19-gz_ffa_mailbox` 保留内存）表明 GenieZone 镜像与内存配置仍在，厂商更可能只是在设备树层关闭了接口，因此成功概率提高。但其最终成功与否仍取决于 hypervisor 是否在 EL2 运行并响应 HVC。若成功，`/dev/gzvm` 将被创建，硬件级虚拟化接口即可用。

该方案无需重新编译内核或修改设备树，但要求内核允许加载外部模块，且 KMI 匹配。

---

## 附录：常用命令速查

```bash
# ========== 基本环境 ==========
sudo uname -r
sudo getprop ro.build.version.release
sudo getprop ro.build.version.sdk
sudo getprop ro.product.device
sudo getprop ro.product.model

# ========== 设备状态 ==========
sudo lsmod | grep gzvm
sudo ls -l /dev/gzvm
sudo getenforce
sudo cat /proc/sys/kernel/modules_disabled
sudo cat /proc/sys/kernel/kptr_restrict
sudo cat /proc/sys/kernel/dmesg_restrict

# ========== gzvm 符号与 kprobe 目标 ==========
sudo cat /proc/kallsyms | grep gzvm_drv_probe
sudo cat /proc/kallsyms | grep gzvm_arch_probe
sudo cat /proc/kallsyms | grep -E 'misc_register|platform_device_register|kallsyms_lookup_name'

# ========== 设备树、分区、保留内存 ==========
sudo find /proc/device-tree -iname "*genie*" -o -iname "*gz*"
sudo cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
sudo find /proc/device-tree/reserved-memory \( -iname "*genie*" -o -iname "*gz*" \) 2>/dev/null
sudo ls -l /dev/block/by-name/ | grep -E "gz_a|gz_b|gz1|gz2"
sudo cat /proc/iomem | grep -i -E "genie|gz|hyp"

# ========== 内核配置检查 ==========
sudo zcat /proc/config.gz | grep -E 'CONFIG_MODULES|CONFIG_MODULE_SIG|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'

# ========== 辅助 LKM 编译 ==========
sudo make -C /path/to/common-android16-6.12 M=$PWD modules
sudo make -C /path/to/common-android16-6.12 M=$PWD clean

# ========== 辅助 LKM 加载与验证 ==========
sudo adb root
sudo adb push gzvm_aux.ko /data/local/tmp/
sudo adb shell insmod /data/local/tmp/gzvm_aux.ko
sudo adb shell dmesg | tail -30
sudo adb shell ls -l /dev/gzvm
sudo adb shell lsmod | grep gzvm
sudo adb shell rmmod gzvm_aux

# ========== 失败排查 ==========
sudo adb shell dmesg | grep -i gzvm
sudo adb shell dmesg | grep -i geniezone
sudo adb shell dmesg | grep -i hyp
sudo adb shell dmesg | grep -i -E 'ENODEV|EBUSY|EPERM|EACCES'
sudo adb shell cat /proc/kallsyms | grep gzvm_drv_probe
sudo adb shell cat /proc/kallsyms | grep gzvm_arch_probe
sudo adb shell ls /sys/kernel/tracing/events/geniezone/

# ========== 同步 Android 16 / 6.12 GKI 源码 ==========
sudo repo init -u https://android.googlesource.com/kernel/manifest -b common-android16-6.12
sudo repo sync -c -j$(nproc) -q

# ========== SELinux 与加载策略 ==========
sudo getenforce
sudo setenforce 0
sudo setenforce 1
sudo dmesg | grep -i avc
sudo dmesg | grep -i selinux
```
  * 如果你需要在设备本地端(手机而非其他设备如电脑)运行, 去除adb shell即可