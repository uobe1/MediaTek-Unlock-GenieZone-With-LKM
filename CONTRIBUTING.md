# Contributing

## How do I... <a name="toc"></a>

- [Use This Guide](#introduction)?
- Ask or Say Something? 🤔🐛😱
  - [Request Support](#request-support)
  - [Report an Error or Bug](#report-an-error-or-bug)
  - [Request a Feature](#request-a-feature)
- Make Something? 🤓👩🏽‍💻📜🍳
  - [Project Setup](#project-setup)
  - [Contribute Documentation](#contribute-documentation)
  - [Contribute Code](#contribute-code)
  - [Report Your Device](#report-your-device)
- Manage Something ✅🙆🏼💃👔
  - [Review Pull Requests](#review-pull-requests)
- Add a Guide Like This One [To My Project](#attribution)? 🤖😻👻

## Introduction

Thank you so much for your interest in contributing! All types of
contributions are encouraged and valued. See the [table of contents](#toc) for
the different ways to help and for details about how this project handles them.
📝

Please make sure to read the relevant section before making your contribution!
It will make it a lot easier for us maintainers to make the most of it and
smooth out the experience for all involved. 💚

One rule before anything else: this project is about **hardware you own**.
Contributions that exist to bypass a bootloader lock, to defeat licensing, or
to target devices the contributor does not own will not be accepted.

## Request Support

If you have a question about this project, how to use it, or just need
clarification about something:

- Open an Issue at
  https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/issues
- Attach the output of `mgz check --show-debug-details`. It contains the
  platform, kernel, KMI, hypervisor and firmware facts in one consistent form,
  and it answers most questions by itself.
- State your Android version, kernel version (`uname -r`) and root solution
  (KernelSU / Magisk / other).

Once it's filed:

- A maintainer will try to respond soon.
- If neither you nor the maintainers respond for 30 days, the issue will be
  closed. Reply once if you want it reopened; please avoid filing a new issue
  as an extension of an old one.

## Report an Error or Bug

If you run into an error or bug with the project:

- Open an Issue at
  https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/issues
- Include *reproduction steps* someone else can follow.
- Attach `mgz check --show-debug-details` output and the `dmesg` lines around
  the failed load.

A useful report looks like this:

```text
Device (model / codename):
SoC (getprop ro.board.platform):
Android version / kernel (uname -r):
KMI the modules were built for:
Root solution (KernelSU version / Magisk version):
Command that failed:
mgz check --show-debug-details output:
Relevant dmesg:
SELinux state (getenforce):
```

Once it's filed:

- A maintainer will try to reproduce it. Without reproduction steps the issue
  is marked `needs-repro` and stays there until someone reproduces it.
- Reproducible bugs are marked `needs-fix` and left to be
  [implemented by someone](#contribute-code).

## Request a Feature

If you have an idea, or a device you want supported:

- Open an Issue describing the feature and the problem it solves.
- For a new device, use [Report Your Device](#report-your-device) instead — it
  collects exactly what is needed.
- For a new KMI generation, say which Android and kernel major versions you
  need; the build already takes both as parameters.

## Project Setup

So you wanna contribute some code! That's great! This project uses GitHub Pull
Requests to manage contributions, so
[read up on how to fork a GitHub project and file a PR](https://guides.github.com/activities/forking)
if you've never done it before.

If this seems like a lot, you can also
[edit the files directly](https://help.github.com/articles/editing-files-in-another-user-s-repository/)
without any of this setup. Yes, [even code](#contribute-code).

To build everything locally you need CMake, a C compiler and a prepared GKI
kernel tree:

```bash
git clone git@github.com:uobe1/MediaTek-Unlock-GenieZone-With-LKM.git
cd MediaTek-Unlock-GenieZone-With-LKM

# smallest possible kernel fetch, then prepare the tree (builds vmlinux)
./scripts/fetch-kernel.sh --android 16 --kernel 6.12
./scripts/prepare-kernel.sh --src kernel-src --out kbuild

cmake -B build -DMGZ_ANDROID_VERSION=16 -DMGZ_KERNEL_VERSION=6.12 \
  -DMGZ_KERNEL_DIR="$PWD/kbuild"
cmake --build build --target dist
```

Just the CLI, no kernel tree needed:

```bash
cmake -B build -DMGZ_ANDROID_ABI=host   # on the device itself
cmake -B build -DMGZ_ANDROID_ABI=arm64-v8a -DMGZ_ANDROID_NDK=/path/to/ndk
cmake --build build --target mgz
```

On a rooted phone with a compiler (Termux), `MGZ_ANDROID_ABI=host` produces a
binary you can run right away.

## Contribute Documentation

Documentation is a super important, critical part of this project. Docs are how
we keep track of what we're doing, how, and why — for a kernel-level project
they are also how readers stay safe. So thank you in advance.

Documentation contributions of any size are welcome! Feel free to file a PR
even if you're just rewording a sentence to be more clear, or fixing a spelling
mistake!

To contribute documentation:

- [Set up the project](#project-setup).
- Edit or add any relevant documentation.
- **Every document exists in English and Simplified Chinese** (`docs/en/` is
  the default language, `docs/zh-CN/` the translation). If you change one,
  change the other in the same pull request. The same applies to `README.md`
  and `README.zh-CN.md`.
- Keep the formatting consistent with the rest of the documentation.
- Re-read what you wrote, and run a spellchecker on it.
- Write clear, concise commit message(s) using
  [conventional-changelog format](https://github.com/conventional-changelog/conventional-changelog-angular/blob/master/convention.md).
  Documentation commits should use `docs(<component>): <message>`.
- Go to https://github.com/uobe1/MediaTek-Unlock-GenieZone-With-LKM/pulls and
  open a new pull request. If your PR is connected to an open issue, add
  `Fixes: #123` to the description.

## Contribute Code

Code contributions follow the layout of the repository:

| Area | Language / style |
|---|---|
| `kernel/` | C, Linux kernel coding style: tabs, 80 columns, `pr_fmt` per file |
| `cli/` | C99, tabs, 80 columns, no external dependencies |
| `module/` | POSIX `sh` for KernelSU's busybox `ash`, mirrored verbatim by `cli/ksu.c` |
| `scripts/` | POSIX shell, must run on Linux and Android |

Rules that matter here:

- **Kernel code must stay version agnostic.** Resolve symbols at runtime
  (`kprobe`), never assume an address, never assume a struct layout that
  belongs to one vendor tree.
- **Only exported symbols.** Before using a kernel symbol, check that it is
  exported for modules; if it is not, find another way.
- **Do not break the KMI contract.** Anything that changes the required KMI
  generation must be reflected in the build parameters and the docs.
- **`module/` and `cli/ksu.c` must stay identical.** Change one boot script,
  change the other.
- Add a `Signed-off-by` line if you want the commit attributed to you under
  the GPLv3 grant.
- Commits use conventional-changelog format, for example
  `feat(kernel): ...`, `fix(cli): ...`, `ci: ...`.

Before opening the PR:

```bash
cmake --build build --target mgz      # must compile without new warnings
mgz --help                            # sanity check the option parsing
```

Once you've filed the PR:

- One or more maintainers will review it.
- If a maintainer asks for changes, edit, push, and ask for another review.
- If a maintainer passes on your PR, they will thank you and explain why.
  That's ok! We still really appreciate you taking the time to do it. 💚

## Report Your Device

The most valuable contribution is a verified device report. Read the
[device adaptation guide](docs/en/device-adaptation.md), run the checks, then
open an issue (or a PR editing that guide) with:

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

Please attach `mgz check --show-debug-details`; it already contains all of the
above in a consistent form.

## Review Pull Requests

When reviewing someone else's PR:

- Be kind. Assume good faith.
- Check that kernel code cannot crash a device harder than a failed `insmod`
  would: no writes to memory you did not allocate, no assumptions about vendor
  struct layouts.
- Check that documentation changes land in **both** languages.
- Check that a new KMI generation, symbol name or HVC number is documented in
  the adaptation guide.

## Attribution

This guide was generated using the WeAllJS `CONTRIBUTING.md` generator.
[Make your own](https://npm.im/weallcontribute)!
