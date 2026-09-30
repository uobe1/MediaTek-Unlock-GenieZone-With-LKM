# Documentation (English)

Documentation root for **MediaTek-Unlock-GenieZone-With-LKM**. This is the
default language; the Chinese translation lives in [`../zh-CN/`](../zh-CN/).

## Contents

| Document | What it covers |
|---|---|
| [Device adaptation guide](device-adaptation.md) | **Start here.** How to tell whether your device is supported, and how to adapt the build to another SoC, kernel or Android generation. |
| [Activating the `gzvm` driver](gzvm-driver-activation.md) | Why `probe()` never runs when the device tree omits GenieZone, and how the helper LKM forces it. |
| [EL2 hypervisor detection](el2-hypervisor-detection.md) | Seven ways to judge whether a GenieZone hypervisor still runs at EL2, and why only an HVC probe is conclusive. |
| [GKI LKM compatibility](gki-lkm-compatibility.md) | KMI as the unit of compatibility, the Android/kernel generation table, and common myths. |

## Evidence samples

| File | Description |
|---|---|
| [`../assets/sample-kernel-config-android16-6.12.txt`](../assets/sample-kernel-config-android16-6.12.txt) | `zcat /proc/config.gz` from the reference device (Android 16 / 6.12). |
| [`../assets/sample-gzvm-module-symbols.txt`](../assets/sample-gzvm-module-symbols.txt) | `gzvm.ko` symbol table, showing that `gzvm_drv_probe` exists as a local symbol. |

## Reading order

1. [Device adaptation guide](device-adaptation.md) — decide whether to invest
   time at all.
2. [EL2 hypervisor detection](el2-hypervisor-detection.md) — find out whether
   the hypervisor is still alive.
3. [Activating the `gzvm` driver](gzvm-driver-activation.md) — understand what
   the module does.
4. [GKI LKM compatibility](gki-lkm-compatibility.md) — build for the right KMI.

## Terminology

| Term | Meaning |
|---|---|
| GenieZone (gz) | MediaTek's EL2 hypervisor |
| `gzvm` | the Linux driver exposing GenieZone through `/dev/gzvm` |
| KMI | Kernel Module Interface; `android<version>-<kernel-major>` |
| GKI | Generic Kernel Image |
| HVC | Hypervisor Call, the EL1 → EL2 trap instruction |
| LKM | Loadable Kernel Module |
