# Welcome to MediaTek-Unlock-GenieZone-With-LKM 👋

![Version](https://img.shields.io/badge/version-0.1.0-blue.svg?cacheSeconds=2592000)
[![Documentation](https://img.shields.io/badge/documentation-yes-brightgreen.svg)](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM#readme)
[![Maintenance](https://img.shields.io/badge/Maintained%3F-yes-green.svg)](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/graphs/commit-activity)
[![License: GPL-3.0-or-later](https://img.shields.io/github/license/uobe1/MediaTek-Unlock-GenieZone-With-LKM)](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/blob/main/LICENSE)

> Unlock MediaTek GenieZone (gzvm) with a loadable kernel module

English (default) | [简体中文](README.zh-CN.md)

---

## Disclaimer

> [!WARNING]
> This project runs code in kernel context and changes what your device
> exposes. Use it only on hardware you own. It can destabilise the system,
> cause data loss and void warranties. Nothing here bypasses a locked
> bootloader: it requires root and the ability to load kernel modules.

---

## Why this exists

On many MediaTek devices GenieZone is present in silicon and firmware, the
`gzvm.ko` driver is shipped and loaded, yet `/dev/gzvm` never appears. The
reason is a missing device tree binding:

```c
static const struct of_device_id gzvm_of_match[] = {
    { .compatible = "mediatek,geniezone-hyp" },
    { /* sentinel */ },
};
```

No matching node means no `probe()`, and `gzvm_drv_probe()` never runs. It
does not dereference its `platform_device` argument, so calling it with `NULL`
performs the full probe sequence anyway: it asks the hypervisor through an HVC
and then registers `/dev/gzvm`.

That is all this project does — no kernel rebuild, no device tree patching, no
signing keys.

---

## What is in here

| Component | Role |
|---|---|
| `kernel/gzvm_probe.ko` | Detection. Issues the GenieZone HVC to see whether a hypervisor is alive at EL2, and resolves the driver probe entry point through `kallsyms`. |
| `kernel/gzvm_unlock.ko` | Activation. Resolves `gzvm_drv_probe` with a `kprobe` and calls it with `NULL`, then verifies that the device node appeared. |
| `mgz` (CLI) | Detection, one-shot loading, removal and KernelSU persistence. |

Results of the probe module are published as read-only module parameters under
`/sys/module/gzvm_probe/parameters/`, so userspace never has to parse `dmesg`.

---

## Is my device supported?

Read the [device adaptation guide](docs/en/device-adaptation.md) — it explains
how to judge a device you have never seen before. In short:

```bash
adb push mgz /data/local/tmp/
adb shell chmod +x /data/local/tmp/mgz
adb shell /data/local/tmp/mgz check --show-debug-details
```

`check` reports a verdict per layer and an overall one:

| Verdict | Meaning |
|---|---|
| `SUPPORTED` | MediaTek platform, hypervisor alive at EL2, probe symbol reachable, unsigned modules loadable |
| `LIKELY` | Firmware still carries GenieZone but the HVC probe was inconclusive — usually the modules were built for another KMI |
| `UNSUPPORTED` | Not a GenieZone platform, or EL2 is confirmed dead, or modules cannot be loaded |

Real output from the reference device (MT6855, Android 16, kernel 6.12.30):

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

## Quick start

```bash
# 1. get the artifacts (CI build, or build them yourself as shown below)
#    gzvm_probe.ko, gzvm_unlock.ko and mgz
adb push gzvm_probe.ko gzvm_unlock.ko mgz /data/local/tmp/lkm_build/

# 2. does it apply to this device?
adb shell /data/local/tmp/lkm_build/mgz check

# 3. activate once, until the next reboot
adb shell /data/local/tmp/lkm_build/mgz install

# 4. verify
adb shell ls -l /dev/gzvm

# 5. make it survive reboots (needs KernelSU / ksud)
adb shell /data/local/tmp/lkm_build/mgz ksu-keep-alive
```

---

## CLI reference

```
mgz <command> [options] [module-path]
```

### Commands

| Command | What it does |
|---|---|
| `check` | Collect platform, kernel, hypervisor and firmware facts and print a verdict |
| `install` | Ensure `gzvm.ko` is loaded, then load `gzvm_unlock.ko` once and verify `/dev/gzvm` |
| `remove` | Unload `gzvm_unlock.ko` and drop the persisted KernelSU module |
| `ksu-keep-alive` | Install a KernelSU module that reloads the `.ko` on every boot |
| `help` | Show usage |

### Options

| Long | Short | Effect |
|---|---|---|
| `--help` | `-h` | Show usage |
| `--version` | `-V` | Show version and licence |
| `--show-debug-details` | `-d` | Print every detection step and command output |
| `--quiet` | `-q` | Only print errors |
| `--no-color` | `-n` | Disable ANSI colours |

`[module-path]` is a `.ko` file or a directory holding one. Search order when
it is omitted: current directory, `/data/local/tmp/lkm_build`,
`/data/local/tmp`, and the directory of the executable.

### Privileges

Root is taken through `sudo`, falling back to `su -c`, because `sudo` is the
more compatible of the two on modern setups. `ksud` lives in
`/data/adb/ksu/bin`, which is normally only visible from a `su -c` shell, so
KernelSU operations always run through it. When `ksud` is present the module is
loaded with `ksud insmod`, which loads it **with kallsyms access** — exactly
what `kprobe` based symbol resolution needs.

---

## Persistence

`mgz ksu-keep-alive` writes a KernelSU module to
`/data/adb/modules/mgz_unlock/`:

```
/data/adb/modules/mgz_unlock
├── module.prop          module metadata
├── gzvm_unlock.ko       the module itself
├── load.sh              shared loader implementation
├── service.sh           late_start service stage (standard boot)
├── late-load.sh         late-load mode (replaces post-fs-data.sh)
├── boot-completed.sh    verify and retry once after boot
├── action.sh            manual trigger from the manager
├── uninstall.sh         drop our module on removal
└── skip_mount           nothing to overlay
```

Both boot flows are covered:

* **standard boot** — `service.sh` runs in the late_start service stage, which
  is non-blocking and the stage KernelSU recommends for most scripts. It waits
  for `gzvm.ko` to appear before loading ours.
* **late-load** — `late-load.sh` runs instead of `post-fs-data.sh` for root
  solutions that load `kernelsu.ko` only after boot.

Only available when `ksud` exists; the CLI refuses otherwise.

---

## Building

Two version inputs define the KMI generation. Both are **major** versions:
Google guarantees KMI stability inside one `android<version>-<kernel>` pair, so
`6.12.x` all belong to `android16-6.12`.

```bash
cmake -B build \
  -DMGZ_ANDROID_VERSION=16 \
  -DMGZ_KERNEL_VERSION=6.12 \
  -DMGZ_KERNEL_DIR=/path/to/prepared/gki/tree
cmake --build build --target dist
```

| Android | Kernel | KMI | Branch |
|---|---|---|---|
| 15 | 6.6 | `android15-6.6` | `android15-6.6` |
| 16 | 6.12 | `android16-6.12` | `android16-6.12` |

Targets:

| Target | Output |
|---|---|
| `mgz` | the CLI (static when the toolchain allows it) |
| `gzvm_modules` | `gzvm_probe.ko`, `gzvm_unlock.ko` |
| `ksu_module` | flashable KernelSU zip |
| `dist` | all of the above |

The kernel tree only needs `modules_prepare`, and only `kernel/common` has to
be fetched — the helper scripts do the minimum:

```bash
./scripts/fetch-kernel.sh --android 16 --kernel 6.12   # shallow, one project
./scripts/prepare-kernel.sh --src kernel-src --out kbuild
```

The GitHub Actions workflow does the same and takes both versions as inputs
(defaults `16` and `6.12`); artifacts are `gzvm-modules-<kmi>`, `mgz-<abi>-<kmi>`
and `mgz-bundle-<kmi>`.

Cross compilation for the CLI:

```bash
cmake -B build \
  -DMGZ_ANDROID_ABI=arm64-v8a \
  -DMGZ_ANDROID_NDK=/path/to/ndk
```

`arm64-v8a`, `armeabi-v7a` and legacy `armeabi` are supported; a native build
(`-DMGZ_ANDROID_ABI=host`) works on the device itself.

---

## Repository layout

```
kernel/     gzvm_probe.c, gzvm_unlock.c, shared header, kernel Makefile
cli/        mgz: main, root handling, detection, KernelSU persistence
module/     KernelSU module template (mirrored by cli/ksu.c)
scripts/    fetch-kernel.sh, prepare-kernel.sh
docs/en/    documentation, English (default)
docs/zh-CN/ documentation, Simplified Chinese
```

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `kprobe failed` | `gzvm.ko` not loaded, or `CONFIG_KALLSYMS_ALL` off |
| `probe(NULL) returned -19` | No hypervisor answers the HVC — disabled below Linux |
| `probe(NULL) returned -16` | The node already exists; treated as success |
| `insmod` rejected | Signature enforcement or `modules_disabled=1` |
| `verdict: LIKELY` | Build the modules for this device's KMI and re-run `check` |
| Kernel crash on load | Wrong KMI, rebuild against the exact kernel generation |

See [activating the gzvm driver](docs/en/gzvm-driver-activation.md) for the
full failure matrix and
[EL2 hypervisor detection](docs/en/el2-hypervisor-detection.md) for what the
HVC result means.

---

## Documentation

| Document | Content |
|---|---|
| [Device adaptation guide](docs/en/device-adaptation.md) | How to judge a new device and adapt the build |
| [Activating the `gzvm` driver](docs/en/gzvm-driver-activation.md) | Why `probe()` never runs and how the module forces it |
| [EL2 hypervisor detection](docs/en/el2-hypervisor-detection.md) | Seven ways to look at EL2, and why only HVC counts |
| [GKI LKM compatibility](docs/en/gki-lkm-compatibility.md) | KMI as the unit of compatibility |

---

## Author

👤 **uobe1 <uobe1@users.noreply.github.com>**

- GitHub: [@uobe1](https://github.com/uobe1)

## Show your support

Give a ⭐️ if this project helped you!

## 📝 License

Copyright © 2026 [uobe1 <uobe1@users.noreply.github.com>](https://github.com/uobe1).

This project is [GPL-3.0-or-later](https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/blob/main/LICENSE) licensed.

---

_This README was generated with ❤️ by [readme-md-generator](https://github.com/kefranabg/readme-md-generator)_
