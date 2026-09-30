# Activating the GenieZone `gzvm` Driver

How a loadable kernel module (LKM) can force the MediaTek GenieZone `gzvm`
driver to `probe()` when the device tree does not describe the hypervisor.

| | |
|---|---|
| Reference device | MediaTek MT6855 (beryl) |
| Reference platform | Android 16 / Linux 6.12 GKI (`android16-6.12`) |
| Kernel module under study | `gzvm.ko` (GenieZone VMM interface) |
| Core statement | When the device tree lacks a `mediatek,geniezone*` node, an LKM can resolve `gzvm_drv_probe` through a `kprobe` and invoke it directly, which creates `/dev/gzvm` — provided the hypervisor is actually running at EL2. |

---

## 1. Problem statement

On the reference device the hardware supports GenieZone and `gzvm.ko` is already
loaded, yet `/dev/gzvm` does not exist:

| Observation | Value |
|---|---|
| SoC | MediaTek MT6855 (hardware supports GenieZone) |
| Kernel | `6.12.30-android16-5-g1ed949324a3e-ab13881345-4k` (GKI) |
| `gzvm.ko` | loaded, visible in `lsmod` |
| `/dev/gzvm` | **absent** |
| Device tree `compatible` | `mediatek,MT6855` only — no `mediatek,geniezone` |
| Kernel log | no `gzvm` / `geniezone` probe messages |
| Partitions | `gz_a -> /dev/block/sdc29`, `gz_b -> /dev/block/sdc56` |
| Reserved memory | `mblock-14-gz`, `mblock-18-gz-log`, `mblock-19-gz_ffa_mailbox` |

The driver is built, shipped and loaded, but never bound to a device, so its
`probe()` callback never runs.

### 1.1 Why the probe never happens

`gzvm.ko` is a platform driver. Its match table is:

```c
static const struct of_device_id gzvm_of_match[] = {
    { .compatible = "mediatek,geniezone-hyp" },
    { /* sentinel */ },
};
```

Platform drivers are only probed when the device tree (or an ACPI/board file)
exposes a matching node. The reference device tree has none, so `probe()` is
never entered.

### 1.2 What `probe()` actually does

```c
static int gzvm_drv_probe(struct platform_device *pdev)
{
    int ret;

    ret = gzvm_arch_probe();        /* HVC into EL2 to detect the hypervisor */
    if (ret)
        return ret;

    ret = misc_register(&gzvm_dev); /* creates /dev/gzvm */
    if (ret)
        return ret;

    return 0;
}
```

The decisive detail: **`pdev` is never dereferenced.** Passing `NULL` is safe
and triggers the complete probe sequence. This makes the device tree
requirement purely a binding artefact, not a functional one.

---

## 2. Feasibility analysis

### 2.1 Symbol visibility

Extracted from `vmlinux` and the module symbol table
(see `docs/assets/sample-gzvm-module-symbols.txt`):

| Symbol | Present | Exported (`__ksymtab_`) | Notes |
|---|---|---|---|
| `misc_register` | yes | yes | callable from a module |
| `platform_device_register` | yes | yes | callable from a module |
| `kallsyms_lookup_name` | yes | no | needs `kprobe` on modern kernels |
| `gzvm_drv_probe` | yes (in `gzvm`) | no | local (`t`), but visible in `kallsyms` |
| `gzvm_arch_probe` | yes (in `gzvm`) | no | local (`t`) |

`gzvm_drv_probe` is a local text symbol, so it cannot be resolved by name from
another module. However it *is* present in `/proc/kallsyms`, and `kprobe`
resolution works by name against `kallsyms`, which is exactly what we need.

### 2.2 Preconditions

| # | Precondition | Why it matters |
|---|---|---|
| 1 | `CONFIG_KALLSYMS_ALL=y` | otherwise `kprobe` cannot resolve local symbols |
| 2 | `CONFIG_KPROBES=y` | required for `register_kprobe()` |
| 3 | `CONFIG_MODULES=y` and `CONFIG_MODULE_SIG_FORCE` unset | unsigned external modules must be loadable |
| 4 | `gzvm.ko` already loaded | its code must be resident to be probed |
| 5 | Hypervisor alive at EL2 | otherwise `gzvm_arch_probe()` returns `-ENODEV` |

On the reference device all kernel-config preconditions are satisfied:
`CONFIG_KALLSYMS_ALL=y`, `CONFIG_KPROBES=y`, `CONFIG_MODULES=y`,
`CONFIG_MODULE_SIG=y` but `CONFIG_MODULE_SIG_FORCE` unset
(`docs/assets/sample-kernel-config-android16-6.12.txt`).

Module signature enforcement is not a real obstacle in practice: a permanent
root solution patches the kernel itself, and a temporary one already proves
that unsigned modules are accepted. SELinux rarely blocks `insmod` for a
root context, and `setenforce 0` remains available as an escape hatch.

### 2.3 Firmware-layer evidence

| Evidence | State | Interpretation |
|---|---|---|
| `gz_a` / `gz_b` partitions | present | the GenieZone image is still shipped |
| `mblock-14-gz` reserved memory | present | memory was reserved for the hypervisor |
| `mblock-18-gz-log` | present | hypervisor log buffer configured |
| `mblock-19-gz_ffa_mailbox` | present | FF-A mailbox configured |
| device tree node | absent | the interface was disabled in software only |

The firmware still carries the hypervisor image and its memory layout, which
strongly suggests the OEM removed the device tree binding rather than GenieZone
itself. The final verdict still depends on whether EL2 answers an HVC — see
[EL2 hypervisor detection](el2-hypervisor-detection.md).

---

## 3. Helper LKM design

### 3.1 Strategy

1. Register a `kprobe` on the symbol name `"gzvm_drv_probe"`.
2. Read back `kp.addr`, i.e. the runtime address of the function.
3. Unregister the `kprobe` immediately (we only wanted the address).
4. Cast the address to a function pointer and call it with `NULL`.
5. Report the return value; on success `/dev/gzvm` exists.

### 3.2 Reference implementation

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

The production version in `kernel/gzvm_unlock.c` extends this with a fallback
symbol list, a retry loop, and a `/dev/gzvm` existence check.

### 3.3 Safety considerations

* The module writes nothing to the device tree and does not patch the kernel.
* `gzvm_drv_probe(NULL)` is only called after the address has been validated
  as non-NULL and the owning module is confirmed resident.
* A non-zero return value is reported and the module init fails, so the kernel
  does not keep a half-initialised module around.
* If `misc_register()` reports `-EBUSY`, the device already exists and the
  module treats that as success.

---

## 4. Procedure

```bash
# 1. build (see ../README.md and the GitHub Actions workflow)
cmake -B build -DMGZ_ANDROID_VERSION=16 -DMGZ_KERNEL_VERSION=6.12
cmake --build build --target gzvm_modules

# 2. push and load once
adb push build/kernel/gzvm_unlock.ko /data/local/tmp/
adb shell sudo insmod /data/local/tmp/gzvm_unlock.ko

# 3. verify
adb shell ls -l /dev/gzvm
adb shell sudo dmesg | tail -20
```

Expected kernel log:

```text
gzvm_unlock: gzvm_drv_probe @ <addr>
gzvm_unlock: gzvm_drv_probe returned 0
gzvm_unlock: /dev/gzvm is now present
```

---

## 5. Failure matrix

| Symptom | Cause | Action |
|---|---|---|
| `register_kprobe()` fails | symbol not in `kallsyms`, or `CONFIG_KALLSYMS_ALL` off | check `/proc/kallsyms`, try the alternate symbol names used by `gzvm_unlock` |
| `gzvm_drv_probe returned -ENODEV` (19) | no hypervisor answers the HVC | see [EL2 detection](el2-hypervisor-detection.md); nothing to activate |
| `gzvm_drv_probe returned -EBUSY` (16) | `misc_register()` refused, device usually already exists | treated as success, verify `/dev/gzvm` |
| returns 0 but no `/dev/gzvm` | SELinux blocked the device node | check `dmesg | grep avc`, retry with permissive SELinux |
| `insmod` rejected outright | signature enforcement or `modules_disabled` | check `/proc/sys/kernel/modules_disabled`, root method |
| kernel crash on load | wrong KMI build | rebuild against the exact KMI, see [GKI compatibility](gki-lkm-compatibility.md) |

---

## 6. Limits and risks

* **Only for devices you own.** Changing kernel behaviour can destabilise the
  system, cause data loss and void warranties.
* **Depends on the hypervisor.** If the OEM disabled GenieZone in ATF/EL2, no
  LKM can revive it.
* **KMI must match exactly.** A module built for another KMI will be rejected,
  or worse, crash the kernel.
* **Address validity.** The resolved address belongs to `gzvm.ko`; if that
  module is unloaded while our module stays, the pointer dangles. The module
  therefore pins a reference by checking `module_refcount` semantics via
  `find_module()`.

---

## 7. Conclusion

When the device tree omits `mediatek,geniezone`, invoking
`gzvm_drv_probe(NULL)` from a helper LKM is a practical way to activate the
interface. It requires no kernel rebuild, no device tree change and no signing
key. Firmware evidence (partitions, reserved memory) indicates the hypervisor
is still present, so the probability of success is high, but the definitive
answer is whether EL2 responds to the GenieZone HVC.

---

## Appendix: command reference

```bash
# environment
uname -r
getprop ro.build.version.release
getprop ro.build.version.sdk
getprop ro.product.device

# device state
lsmod | grep gzvm
ls -l /dev/gzvm
getenforce
cat /proc/sys/kernel/modules_disabled
cat /proc/sys/kernel/kptr_restrict

# symbols
cat /proc/kallsyms | grep -E 'gzvm_drv_probe|gzvm_arch_probe'
cat /proc/kallsyms | grep -E 'misc_register|platform_device_register'

# device tree, partitions, reserved memory
find /proc/device-tree -iname '*genie*' -o -iname '*gz*'
cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2'
ls /proc/device-tree/reserved-memory | grep -i gz

# kernel configuration
zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'

# load and verify
sudo insmod /data/local/tmp/gzvm_unlock.ko
dmesg | tail -30
ls -l /dev/gzvm
sudo rmmod gzvm_unlock
```

> Running on the device itself instead of through a host: drop the `adb shell`
> prefix.
