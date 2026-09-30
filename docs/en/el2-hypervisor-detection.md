# Detecting a GenieZone Hypervisor at EL2

How to find out whether a MediaTek device still runs the GenieZone hypervisor at
EL2, even though `dmesg` shows nothing about it.

| | |
|---|---|
| Reference device | MediaTek MT6855 (beryl) |
| Reference platform | Android 16 / Linux 6.12 GKI |
| Core question | The device tree has no `mediatek,geniezone` node — is a hypervisor still running at EL2? |
| Core statement | The only direct proof is issuing an HVC yourself; everything else is circumstantial. Judge by combining several independent signals. |

---

## 1. Why `dmesg` cannot see EL2

ARM64 exception levels:

```text
EL0   user space
EL1   Linux kernel
EL2   hypervisor (GenieZone / pKVM)
EL3   secure monitor (ATF)
```

With a hypervisor present, EL1 and EL2 are strictly isolated:

* `dmesg` reads the EL1 log buffer; EL2 writes never land there.
* `WARN_ON()` inside EL2 typically ends in `hyp_panic()`, i.e. a reboot instead
  of a log line.
* Early EL2 initialisation only appears on the serial console or through
  dedicated tracepoints.

Consequently, EL2 must be probed actively rather than read passively.

---

## 2. Method 1 — active HVC probe (direct evidence)

### 2.1 Principle

The `HVC` instruction traps into EL2. If a hypervisor is installed it handles
the call and returns a status; if not, the trap is unresolved. The GenieZone
driver performs exactly this call (`MT_HVC_GZVM_PROBE`) inside
`gzvm_arch_probe()`.

### 2.2 Probe module

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

The production module is `kernel/gzvm_probe.c`; it additionally reports the
device tree, partition and reserved-memory evidence in one pass.

### 2.3 Interpretation

| Result | Meaning |
|---|---|
| `a0 == 0` | the call reached EL2 and the hypervisor answered — hypervisor alive |
| `a0 != 0` | no EL2 response, or the hypervisor rejected the call |
| load crashes the kernel | EL2 is not prepared for HVC; treat as "absent" |

This is the **only direct proof** that a hypervisor occupies EL2.

---

## 3. Method 2 — tracepoints (corroboration)

GenieZone exposes `mtk_hypcall_enter`, `mtk_hypcall_leave` and `mtk_vcpu_exit`.
Successful HVC round-trips leave records there.

```bash
echo 0 > /sys/kernel/tracing/events/enable
echo > /sys/kernel/tracing/trace
echo 1 > /sys/kernel/tracing/events/geniezone/enable
echo 1 > /sys/kernel/tracing/tracing_on

sudo insmod /data/local/tmp/gzvm_probe.ko   # triggers the HVC

echo 0 > /sys/kernel/tracing/tracing_on
cat /sys/kernel/tracing/trace
```

* `mtk_hypcall_enter` / `mtk_hypcall_leave` present → the HVC reached EL2.
* trace empty → no HVC occurred, or the tracepoint was not enabled.

Merely loading `gzvm.ko` does **not** issue an HVC; the call must be triggered.

---

## 4. Method 3 — device tree (indirect)

```bash
find /proc/device-tree -iname '*genie*' -o -iname '*gz*'
cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
```

| Result | Meaning |
|---|---|
| `mediatek,geniezone` or `mediatek,geniezone-hyp` present | firmware exposes the interface |
| only `mediatek,MT6855` | the binding was removed in software |

A missing node explains why `probe()` never runs; it says nothing about EL2.

---

## 5. Method 4 — reserved memory (indirect)

```bash
cat /proc/iomem | grep -iE 'genie|gz|hyp'
ls /proc/device-tree/reserved-memory | grep -i gz
```

Measured on the reference device:

* `/proc/iomem`: no `genie`/`gz`/`hyp` entry.
* reserved-memory nodes: `mblock-14-gz`, `mblock-18-gz-log`,
  `mblock-19-gz_ffa_mailbox`.

The firmware reserves memory, a log buffer and an FF-A mailbox for GenieZone,
which means the hypervisor was not stripped from the firmware.

---

## 6. Method 5 — partitions and preloader (indirect)

```bash
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2'
```

Reference device:

```text
gz_a -> /dev/block/sdc29
gz_b -> /dev/block/sdc56
```

The GenieZone image is still shipped. The preloader decides whether to load it;
pointing the partition LBA beyond the storage capacity is the documented way to
raise `EL2_BOOTING_DISABLED` and disable it safely.

---

## 7. Method 6 — boot log and VHE mode (indirect)

```bash
dmesg | grep -iE 'VHE|Hyp mode|EL2|kvm|pKVM|geniezone|gzvm'
```

| Log line | Meaning |
|---|---|
| `VHE mode initialized successfully` | the kernel itself runs at EL2 (VHE) |
| `pKVM: Initializing...` | pKVM occupies EL2 |
| `CPU: All CPU(s) started at EL1` | EL2 is free for a standalone hypervisor |

With pKVM the host kernel is deliberately de-privileged to EL1, so the absence
of "kernel at EL2" messages is expected and proves nothing by itself.

---

## 8. Method 7 — hardware capability (indirect)

GenieZone depends on a Stage-2 MMU. MediaTek's M-TEE documentation lists the
SoCs that integrate it; MT6855 is on that list. Hardware capability is a
precondition, not proof of enablement.

---

## 9. Userspace helpers (weak signals)

| Tool | Command | What it shows |
|---|---|---|
| `systemd-detect-virt` | `systemd-detect-virt` | whether we run inside a VM |
| `virt-what` | `sudo virt-what` | virtualisation technology |
| `/proc/cpuinfo` | `grep hypervisor /proc/cpuinfo` | hypervisor flag in the CPU features |

These cannot distinguish EL1 from EL2; treat them as flavour only.

---

## 10. Combined verdict matrix

| Evidence | Reference device | Verdict |
|---|---|---|
| Stage-2 MMU in silicon | MT6855 is listed | hardware capable |
| `gz_a` / `gz_b` partitions | present | hypervisor image shipped |
| reserved memory | `mblock-14-gz`, `-18-gz-log`, `-19-gz_ffa_mailbox` | firmware reserves hypervisor memory |
| device tree node | absent | software interface disabled |
| `gzvm.ko` | loaded | driver ready |
| `/dev/gzvm` | absent | never probed |
| tracepoint directory | present | tracing code compiled in |
| **HVC probe** | **to be executed** | **decisive** |
| EL2 entries in `dmesg` | none | normal, EL2 is isolated |

---

## 11. Conclusion

1. The **only direct proof** is an active HVC probe. `a0 == 0` means a
   hypervisor is alive at EL2.
2. Tracepoints corroborate an HVC round-trip but must be enabled *before* the
   call.
3. Device tree, reserved memory, partitions, boot logs and hardware listings are
   circumstantial and cannot decide the question alone.
4. If the HVC succeeds, the OEM only removed the device tree binding and the
   helper LKM described in
   [driver activation](gzvm-driver-activation.md) can activate the interface.
5. If the HVC fails or crashes, GenieZone is disabled below Linux (ATF/EL2) and
   no LKM can revive it.

---

## Appendix: command reference

```bash
# symbols
cat /proc/kallsyms | grep -E 'gzvm|geniezone'

# device tree
find /proc/device-tree -iname '*genie*' -o -iname '*gz*'
cat /proc/device-tree/compatible | tr '\0' '\n' | grep -i mediatek
ls /proc/device-tree/reserved-memory | grep -i gz

# reserved memory
cat /proc/iomem | grep -iE 'genie|gz|hyp'
dmesg | grep -i reserved

# partitions
ls -l /dev/block/by-name/ | grep -E 'gz_a|gz_b|gz1|gz2'

# boot log / VHE
dmesg | grep -iE 'VHE|Hyp mode|pKVM|geniezone|gzvm'
dmesg | grep -iE 'CPU: All CPU'

# tracepoints
ls /sys/kernel/tracing/events/geniezone/
cat /sys/kernel/tracing/trace

# probe module
sudo insmod /data/local/tmp/gzvm_probe.ko
dmesg | tail -20
sudo rmmod gzvm_probe
```

> Running on the device itself instead of through a host: drop the `adb shell`
> prefix.
