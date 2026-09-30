#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the gzvm modules against a prepared kernel build directory.
#
# The only thing needed from the kernel is its build directory: the config,
# the generated headers and Module.symvers, which is where the export CRCs
# come from. Nothing of the kernel itself has to be compiled here.
#
# Preferred source for that directory is the Driver Development Kit (DDK),
# which ships one per KMI at /opt/ddk/kdir/<kmi>. Alternatively point
# --kdir at a tree prepared by prepare-kernel.sh.
#
#   ./scripts/build-modules.sh --kmi android16-6.12 --out dist
set -euo pipefail

ANDROID_VERSION="16"
KERNEL_VERSION="6.12"
KMI=""
SRC="kernel"
OUT="dist/modules"
KDIR="${KDIR:-}"
BUILD=""
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
	cat <<'EOF'
Usage: build-modules.sh [options]

  --kmi  <kmi>    KMI generation          (default android16-6.12)
  --kdir <dir>    prepared kernel build directory
                  (default: $DDK_ROOT/kdir/<kmi>, i.e. the DDK one)
  --src  <dir>    module sources          (default kernel)
  --out  <dir>    where the .ko files go  (default dist/modules)
  -j <n>          parallel jobs           (default nproc)
  -h, --help      show this help
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--kmi) KMI="$2"; shift 2 ;;
	--kdir) KDIR="$2"; shift 2 ;;
	--src) SRC="$2"; shift 2 ;;
	--out) OUT="$2"; shift 2 ;;
	-j) JOBS="$2"; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown option: $1" >&2; usage; exit 1 ;;
	esac
done

[ -n "${KMI}" ] || KMI="android${ANDROID_VERSION}-${KERNEL_VERSION}"

# Locate the DDK kernel directory when none was given.
if [ -z "${KDIR}" ]; then
	DDK_ROOT="${DDK_ROOT:-/opt/ddk}"
	if [ -d "${DDK_ROOT}/kdir/${KMI}" ]; then
		KDIR="${DDK_ROOT}/kdir/${KMI}"
	else
		for candidate in "${DDK_ROOT}"/kdir/*; do
			if [ -d "${candidate}" ]; then
				KDIR="${candidate}"
				break
			fi
		done
	fi
fi

if [ -z "${KDIR}" ] || [ ! -d "${KDIR}" ]; then
	echo "no prepared kernel build directory found." >&2
	echo "install the DDK, or run scripts/prepare-kernel.sh first." >&2
	exit 1
fi

echo "kmi    : ${KMI}"
echo "kdir   : ${KDIR}"
echo "out    : ${OUT}"

mkdir -p "${OUT}"
OUT="$(cd "${OUT}" && pwd)"

# The kernel build writes next to the sources, so build from a copy.
BUILD="$(mktemp -d)"
trap 'rm -rf "${BUILD}"' EXIT
cp -r "${SRC}/." "${BUILD}/"

make -C "${KDIR}" M="${BUILD}" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
	-j"${JOBS}" modules

cp -f "${BUILD}"/*.ko "${OUT}/"
ls -l "${OUT}"
