#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prepare a GKI kernel tree for out-of-tree module builds.
#
#   ./scripts/prepare-kernel.sh --src kernel-src --out kbuild
#
# This script exists only to obtain the Module.symvers of a KMI generation:
# the export list with the symbol CRCs that external modules are stamped
# against. A module without them is refused by a kernel built with
# CONFIG_MODVERSIONS, which is what GKI ships.
#
# Building the kernel itself is never the point. The cheapest path is the DDK
# (see build-modules.sh); this script is the fallback for when you want the
# tree yourself, and it only builds the =m parts. `--modules_prepare` alone
# is not enough: since 6.12 it never runs modpost, so no symvers appears.
set -euo pipefail

SRC="kernel-src"
OUT="kbuild"
ARCH="arm64"
JOBS="$(nproc 2>/dev/null || echo 4)"
QUICK=0

usage() {
	cat <<'EOF'
Usage: prepare-kernel.sh [options]

  --src  <dir>   kernel source tree      (default kernel-src)
  --out  <dir>   build output directory  (default kbuild)
  --arch <arch>  target architecture     (default arm64)
  -j <n>         parallel jobs           (default nproc)
  --quick        only run modules_prepare (no Module.symvers)
  -h, --help     show this help
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--src) SRC="$2"; shift 2 ;;
	--out) OUT="$2"; shift 2 ;;
	--arch) ARCH="$2"; shift 2 ;;
	-j) JOBS="$2"; shift 2 ;;
	--quick) QUICK=1; shift ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown option: $1" >&2; usage; exit 1 ;;
	esac
done

mkdir -p "${OUT}"
OUT="$(cd "${OUT}" && pwd)"

make -C "${SRC}" ARCH="${ARCH}" LLVM=1 O="${OUT}" distclean >/dev/null 2>&1 || true

# gki_defconfig is what GKI publishes; fall back to defconfig if a branch
# does not carry it.
if [ -f "${SRC}/arch/${ARCH}/configs/gki_defconfig" ]; then
	CONFIG="gki_defconfig"
else
	CONFIG="defconfig"
fi

echo "config : ${CONFIG}"
make -C "${SRC}" ARCH="${ARCH}" LLVM=1 O="${OUT}" "${CONFIG}"
make -C "${SRC}" ARCH="${ARCH}" LLVM=1 O="${OUT}" -j"${JOBS}" modules_prepare

if [ "${QUICK}" -eq 0 ]; then
	# The goal is only to obtain Module.symvers, so build the smallest thing
	# that produces it: the =m parts of the tree.
	make -C "${SRC}" ARCH="${ARCH}" LLVM=1 O="${OUT}" -j"${JOBS}" modules
fi

# Since 6.12 modpost writes vmlinux.symvers, while external modules look for
# Module.symvers. Fall back to the vmlinux export list, and only build vmlinux
# itself when nothing else yielded a symvers.
if [ ! -f "${OUT}/Module.symvers" ] && [ -f "${OUT}/vmlinux.symvers" ]; then
	cp "${OUT}/vmlinux.symvers" "${OUT}/Module.symvers"
fi

if [ ! -f "${OUT}/Module.symvers" ] && [ "${QUICK}" -eq 0 ]; then
	echo "still no symvers, falling back to a full vmlinux build"
	make -C "${SRC}" ARCH="${ARCH}" LLVM=1 O="${OUT}" -j"${JOBS}" vmlinux
	[ -f "${OUT}/Module.symvers" ] ||
		cp "${OUT}/vmlinux.symvers" "${OUT}/Module.symvers"
fi

echo "kernel build tree ready at ${OUT}"
if [ -f "${OUT}/Module.symvers" ]; then
	echo "Module.symvers: present"
else
	echo "Module.symvers: MISSING (modules will lack symbol CRCs)"
fi
