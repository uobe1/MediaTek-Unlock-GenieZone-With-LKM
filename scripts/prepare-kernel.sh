#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prepare a GKI kernel tree for out-of-tree module builds.
#
#   ./scripts/prepare-kernel.sh --src kernel-src --out kbuild
#
# Building vmlinux is what produces the Module.symvers of the kernel, and
# external modules need it: without it modpost cannot stamp the modules with
# the symbol CRCs, and a GKI kernel with CONFIG_MODVERSIONS then refuses to
# load them. `modules_prepare` alone is not enough -- it never runs modpost on
# vmlinux. Use --quick when a Module.symvers is already available.
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
	# Produces ${OUT}/Module.symvers, which the modules are stamped against.
	make -C "${SRC}" ARCH="${ARCH}" LLVM=1 O="${OUT}" -j"${JOBS}" vmlinux
fi

echo "kernel build tree ready at ${OUT}"
[ -f "${OUT}/Module.symvers" ] && echo "Module.symvers: present" ||
	echo "Module.symvers: MISSING (modules will lack symbol CRCs)"
