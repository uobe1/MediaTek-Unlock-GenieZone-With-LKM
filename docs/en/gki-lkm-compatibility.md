# GKI LKM Compatibility

Why an external Android GKI kernel module is portable across devices of the
same **KMI**, and what this project requires when building.

| | |
|---|---|
| Reference platform | Android 16 / Linux 6.12 GKI |
| Core statement | Compatibility is decided by the KMI (Kernel Module Interface), not by the device codename. Modules still have to be built against one exact KMI generation. |

---

## 1. Two different build scenarios

Confusing these two is the usual source of contradictory advice:

| Scenario | Needs a device codename? | What decides compatibility |
|---|---|---|
| Full AOSP system image (`lunch <product>`) | yes | product config, partition layout, vendor modules |
| Vendor kernel tree | usually yes | vendor branch, device tree, config fragments |
| **External GKI LKM (this project)** | **no** | **KMI generation, kernel config, signing policy** |

This project builds an external GKI LKM, so no `lunch <product>` step exists.

---

## 2. KMI is the unit of compatibility

Android GKI splits the core kernel from vendor modules. External modules must
match the KMI of the running kernel exactly.

| Android version | Linux kernel | KMI | Kernel branch |
|---|---|---|---|
| Android 12 | 5.10 | `android12-5.10` | `common-android12-5.10` |
| Android 13 | 5.10 | `android13-5.10` | `common-android13-5.10` |
| Android 13 | 5.15 | `android13-5.15` | `common-android13-5.15` |
| Android 14 | 5.15 | `android14-5.15` | `common-android14-5.15` |
| Android 14 | 6.1 | `android14-6.1` | `common-android14-6.1` |
| Android 15 | 6.6 | `android15-6.6` | `common-android15-6.6` |
| Android 16 | 6.12 | `android16-6.12` | `common-android16-6.12` |

Therefore:

* an `android15-6.6` module cannot be loaded on an `android16-6.12` kernel;
* one `android16-6.12` build works across **different devices** that share that
  KMI.

"Portable" here means *one source tree, one build per KMI generation, shared by
all devices of that generation* — not a single binary for all Android devices.

### 2.1 Major versions only

Google guarantees KMI stability within one `<android>-<kernel>` generation. The
guarantee covers the **major** kernel version, so `6.12.x` all belong to
`android16-6.12`; a build made against `6.12.30` loads on any `6.12.y` kernel of
the same Android generation. This is why the build takes two inputs — the
Android major version and the kernel major version — and defaults to
`16` and `6.12`.

---

## 3. Choosing or building the right LKM

1. **Read the device facts**

   ```bash
   uname -r                       # 6.12.30-android16-5-g...-4k
   getprop ro.build.version.release   # 16
   ```

2. **Derive the KMI** — Android 16 + Linux 6.12 → `android16-6.12`.

3. **Build against that KMI** — `ANDROID_VERSION=16 KERNEL_VERSION=6.12`, which
   selects branch `common-android16-6.12`.

4. **Check the kernel configuration** the module relies on:

   ```bash
   zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG_FORCE|CONFIG_KALLSYMS_ALL|CONFIG_KPROBES'
   ```

5. **Load and verify**

   ```bash
   sudo insmod /data/local/tmp/gzvm_unlock.ko
   dmesg | tail -30
   ```

---

## 4. Boundaries of portability

"Same KMI ⇒ portable" holds only when all of the following are true:

* [ ] the running kernel really is `android16-6.12`;
* [ ] required kernel options are enabled (`MODULES`, `KALLSYMS_ALL`, `KPROBES`);
* [ ] the vendor did not break the GKI ABI;
* [ ] root is available;
* [ ] SELinux policy permits loading the module;
* [ ] signature enforcement does not reject the module;
* [ ] the root solution (KernelSU / Magisk) allows `insmod`.

---

## 5. Frequently repeated myths

| Claim | Reality |
|---|---|
| "Module signing makes this impossible" | irrelevant for anyone with root: a permanent root solution patches the kernel, a temporary one already loads an LKM — unsigned modules are accepted |
| "SELinux blocks it" | `insmod` from a root context is almost never denied, and `setenforce 0` exists |
| "A locked bootloader forbids LKM loading" | the bootloader lock controls what you may *flash*, not what a running root context may `insmod` |
| "The symbol is not on the export whitelist" | every symbol used here (`misc_register`, `register_kprobe`, `arm_smccc_hvc`) is exported |
| "6.12 builds do not work on 6.12.x" | KMI stability is guaranteed across `6.12.y` within one Android generation; existing projects demonstrate it |

---

## 6. Conclusion

* Portability for an external GKI LKM means *one build per KMI generation*, not
  one binary for everything.
* Android 16 / Linux 6.12 → KMI `android16-6.12` → branch
  `common-android16-6.12`.
* Building an external LKM needs no `lunch <product>`; full AOSP builds do.
* Portability still requires an exact KMI match, the right kernel options and a
  permissive load policy.

---

## Appendix: command reference

```bash
# device and system
uname -r
cat /proc/version
getprop ro.build.version.release
getprop ro.build.version.sdk
getprop ro.product.device

# kernel configuration
zcat /proc/config.gz | grep -E 'CONFIG_MODULES=|CONFIG_MODULE_SIG|CONFIG_KALLSYMS|CONFIG_KPROBES|CONFIG_IKCONFIG_PROC'

# module locations
ls /vendor/lib/modules/
ls /odm/lib/modules/
ls /system_dlkm/lib/modules/
lsmod | grep -E 'gzvm'

# load and verify
sudo insmod /data/local/tmp/gzvm_unlock.ko
dmesg | tail -30
lsmod | grep gzvm_unlock
sudo rmmod gzvm_unlock

# root / SELinux
getenforce
setenforce 0
cat /proc/sys/kernel/modules_disabled
dmesg | grep -i avc

# root solution
sudo -V
command -v ksud        # usually only visible through `su -c`
ksud module list
```

> Running on the device itself instead of through a host: drop the `adb shell`
> prefix.
