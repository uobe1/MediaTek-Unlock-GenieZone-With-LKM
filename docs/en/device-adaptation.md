# Device Adaptation Guide

How to find out whether **your** device can be unlocked with this project, and
what to change when your SoC, kernel or Android generation differs from the
reference one.

| | |
|---|---|
| Reference device | MediaTek MT6855 (beryl), Android 16, kernel 6.12.30 |
| Reference KMI | `android16-6.12` |
| Fastest answer | push the CLI to the device and run `mgz check` |

---

## 1. Three-step answer

```bash
adb push mgz /data/local/tmp/
adb shell chmod +x /data/local/tmp/mgz
adb shell /data/local/tmp/mgz check --show-debug-details
```

`mgz check` prints a verdict for each layer:

| Layer | What it verifies |
|---|---|
| platform | MediaTek SoC, GenieZone-capable platform string |
| kernel | KMI generation, `CONFIG_KALLSYMS_ALL`, `CONFIG_KPROBES`, module loading |
| hypervisor | `gzvm.ko` resident, `gzvm_drv_probe` resolvable, EL2 answers the HVC |
| firmware | `gz_a`/`gz_b` partitions, `mblock-*-gz*` reserved memory |

Overall verdicts:

* **SUPPORTED** — every layer passes; `mgz install` should work.
* **LIKELY** — firmware says GenieZone exists but the HVC probe was not
  conclusive (for example the probe module was not built for this KMI).
* **UNSUPPORTED** — the platform is not MediaTek/GenieZone capable, or EL2 is
  confirmed dead, or unsigned modules cannot be loaded.

---

## 2. Hardware layer — is the SoC GenieZone capable?

Read the platform identity:

```bash
getprop ro.board.platform          # mt6855
getprop ro.hardware                # mt6855
getprop ro.mediatek.platform       # MT6855  (varies by vendor)
getprop ro.product.board
cat /proc/cpuinfo | grep Hardware
cat /sys/devices/soc0/machine 2>/dev/null
cat /sys/devices/soc0/soc_id 2>/dev/null
```

GenieZone-capable MediaTek platforms share these properties:

* part of the MT68xx / MT69xx (Dimensity) family;
* a Stage-2 MMU integrated in the SoC — required by GenieZone's isolation model
  and listed in MediaTek's M-TEE material;
* an ARMv8 core complex with EL2 available to a standalone hypervisor.

> The maintainer's reference device (MT6855) is confirmed working. For other
> SoCs, do not trust a name list — trust the firmware and kernel evidence in
> sections 3 and 4, then report your result so the table in section 7 can grow.

The most reliable hardware-side signal is **not** the SoC name but the presence
of the hypervisor image and its memory reservation.

---

## 3. Firmware layer — did the OEM keep GenieZone?

```bash
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2|gz'
ls /proc/device-tree/reserved-memory | grep -i gz
```

| Finding | Meaning |
|---|---|
| `gz_a` / `gz_b` (or `gz1` / `gz2`) partitions | the hypervisor image is still shipped |
| `mblock-*-gz*` reserved-memory nodes | memory, log buffer and FF-A mailbox are configured |
| neither present | the firmware dropped GenieZone; an LKM cannot help |

A missing **device tree node** is expected and is *not* a counter-indication —
that is exactly the situation this project exists for.

---

## 4. Kernel layer — can we run our module?

```bash
uname -r                                    # -> derive the KMI
ls /system_dlkm/lib/modules/gzvm.ko /vendor/lib/modules/gzvm.ko
lsmod | grep gzvm
cat /proc/kallsyms | grep gzvm_drv_probe
zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES'
cat /proc/sys/kernel/modules_disabled
```

| Requirement | Why |
|---|---|
| `gzvm.ko` present and loaded | its `probe()` is the code we want to run |
| `gzvm_drv_probe` visible in `kallsyms` | it is what the `kprobe` resolves |
| `CONFIG_KALLSYMS_ALL=y` | local symbols must be resolvable by name |
| `CONFIG_KPROBES=y` | required by `register_kprobe()` |
| `CONFIG_MODULES=y`, `CONFIG_MODULE_SIG_FORCE` unset | unsigned external module must load |
| `modules_disabled == 0` | otherwise every load is refused |

---

## 5. Deciding matrix

| Platform | `gzvm.ko` | `gzvm_drv_probe` | EL2 HVC | Verdict | Action |
|---|---|---|---|---|---|
| GenieZone capable | yes | yes | `a0 == 0` | SUPPORTED | `mgz install` |
| GenieZone capable | yes | yes | not probed | LIKELY | build probe for this KMI and re-check |
| GenieZone capable | yes | no | – | LIKELY | symbol name may differ; see §6.2 |
| GenieZone capable | no | – | – | UNSUPPORTED | module not shipped; nothing to probe |
| GenieZone capable | yes | yes | `a0 != 0` | UNSUPPORTED | disabled below Linux (ATF/EL2) |
| not MediaTek / no Stage-2 | – | – | – | UNSUPPORTED | out of scope |

---

## 6. Adapting to a different device

### 6.1 Different Android or kernel generation

Build with the two major versions of your device:

```bash
cmake -B build -DMGZ_ANDROID_VERSION=15 -DMGZ_KERNEL_VERSION=6.6
cmake --build build --target gzvm_modules
```

or in CI, dispatch the workflow with `android_version` / `kernel_version`.
Only the **major** kernel version matters: `6.12.x` all map to
`android16-6.12`. See
[GKI LKM compatibility](gki-lkm-compatibility.md).

### 6.2 Different symbol names

Some kernels rename or inline the probe entry point. Check what exists:

```bash
cat /proc/kallsyms | grep -iE 'gzvm|geniezone'
```

`kernel/gzvm_unlock.c` keeps a candidate list and tries each one in order.
Add your symbol to that list if a kernel you own uses another name; the module
falls back through the list and reports what it resolved.

### 6.3 Different hypervisor call number

The probe uses MediaTek's vendor-hyp SMCCC range:

```c
#define GZVM_HCALL_ID(func) \
    ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64, \
                       ARM_SMCCC_OWNER_VENDOR_HYP, func)
#define MT_HVC_GZVM_PROBE GZVM_HCALL_ID(0)
```

If your platform uses a different function number, pass it as a module
parameter — `insmod gzvm_probe.ko hvc_fn=<n>` — and compare against the value
your `gzvm.ko` uses. `GZVM_HVC_PROBE_FN_DEFAULT` in
`kernel/gzvm_common.h` holds the compiled-in default.

### 6.4 Different device node

The node name comes from `gzvm_dev` inside `gzvm.ko` and is `/dev/gzvm` on all
observed platforms. If yours differs, set the expected path in the CLI
(`MGZ_DEVICE_NODE`) — the CLI verifies by node existence, not by hardcoded name.

### 6.5 Different CPU architecture

The CLI is a static binary; `arm64-v8a` (`aarch64`) is the primary target,
32-bit `armeabi-v7a` and older devices are supported. The kernel modules are
architecture-independent C and follow the kernel tree you build against.

---

## 7. Reporting a new device

Open an issue (or a pull request editing this file) with:

```text
Device (model / codename):
SoC (getprop ro.board.platform):
Android version (getprop ro.build.version.release):
Kernel (uname -r) / KMI:
gzvm.ko present (ls /system_dlkm/lib/modules/gzvm.ko):
gzvm_drv_probe in kallsyms:
gz partitions (ls /dev/block/by-name | grep gz):
reserved memory (ls /proc/device-tree/reserved-memory | grep gz):
HVC probe result (a0 value from dmesg):
/dev/gzvm after `mgz install`:
```

Please attach the output of `mgz check --show-debug-details`; it contains
everything above in a consistent form.

---

## 8. Checklist before asking for help

* [ ] `mgz check --show-debug-details` output attached
* [ ] KMI confirmed and the module was built for exactly that KMI
* [ ] `gzvm.ko` loaded (`lsmod | grep gzvm`)
* [ ] root works and `ksud`/`su` is reachable
* [ ] `dmesg` snippet from the failed load attached
* [ ] SELinux state (`getenforce`) mentioned
