#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Fetch the smallest amount of AOSP kernel source that is enough to build
# out-of-tree modules for one KMI generation.
#
#   ./scripts/fetch-kernel.sh --android 16 --kernel 6.12
#
# Only kernel/common is needed; prebuilts, tools and the other projects of
# the manifest are irrelevant for building an external module, so the default
# method clones that single repository with depth 1 instead of running a full
# `repo sync`.
set -euo pipefail

ANDROID_VERSION="16"
KERNEL_VERSION="6.12"
DEST="kernel-src"
METHOD="git"
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
	cat <<'EOF'
Usage: fetch-kernel.sh [options]

  --android <major>   Android major version        (default 16)
  --kernel  <major>   Linux kernel MAJOR version   (default 6.12)
  --dest    <dir>     destination directory        (default kernel-src)
  --method  <git|repo> git: single shallow clone (default, smallest)
                       repo: full manifest via repo
  -j <n>              parallel jobs for repo sync  (default nproc)
  -h, --help          show this help
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--android) ANDROID_VERSION="$2"; shift 2 ;;
	--kernel) KERNEL_VERSION="$2"; shift 2 ;;
	--dest) DEST="$2"; shift 2 ;;
	--method) METHOD="$2"; shift 2 ;;
	-j) JOBS="$2"; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown option: $1" >&2; usage; exit 1 ;;
	esac
done

KMI="android${ANDROID_VERSION}-${KERNEL_VERSION}"
BRANCH="common-${KMI}"

echo "KMI    : ${KMI}"
echo "branch : ${BRANCH}"
echo "dest   : ${DEST}"
echo "method : ${METHOD}"

if [ -d "${DEST}" ]; then
	echo "${DEST} already exists, skipping the fetch"
	exit 0
fi

if [ "${METHOD}" = "repo" ]; then
	# Full manifest, but shallow and current branch only.
	repo init -u https://android.googlesource.com/kernel/manifest \
		-b "${BRANCH}" --depth=1
	repo sync -c -j"${JOBS}" --no-tags --no-clone-bundle -q
	exit 0
fi

# Single project, single branch, single commit: the fastest thing that can
# still produce a working external module build.
git clone --depth=1 --single-branch --no-tags \
	-b "${BRANCH}" \
	https://android.googlesource.com/kernel/common "${DEST}"

echo "done: $(du -sh "${DEST}" 2>/dev/null | cut -f1) in ${DEST}"
