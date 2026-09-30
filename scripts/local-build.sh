#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One-shot local build for a rooted Android device (Termux) or a Linux host.
#
# Detects the device's Android and kernel major versions, checks the
# toolchain, builds the CLI natively and the modules through the strategy
# chain of build-modules.sh (on a phone the DDK strategy simply falls
# through, so release or modules is what runs).
#
# Missing dependencies are printed as the exact command to run. With
# --auto-pkg they are installed through pkg directly, so nothing here ever
# needs an interactive prompt — the tool must work without a tty.
#
#   ./scripts/local-build.sh                  # detect everything, build all
#   ./scripts/local-build.sh --auto-pkg       # also install missing packages
#   ./scripts/local-build.sh --no-modules     # CLI only
#   ./scripts/local-build.sh --abi arm64-v8a  # cross compile the CLI (NDK)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

ABI="host"
STRATEGY="auto"
AUTO_PKG=0
NO_MODULES=0
OUT="dist/local"
JOBS="$(nproc 2>/dev/null || echo 4)"
NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK:-}}"

usage() {
	cat <<'EOF'
Usage: local-build.sh [options]

  --auto-pkg      install missing build tools through pkg instead of just
                  printing the command
  --abi <abi>     CLI ABI: host (default, native build) or arm64-v8a /
                  armeabi-v7a / armeabi (needs the NDK)
  --strategy <s>  module strategy: ddk | release | modules | vmlinux | auto
                  (default auto; a phone has no DDK, so release or modules runs)
  --no-modules    build the CLI only
  --out <dir>     output directory (default dist/local)
  -j <n>          parallel jobs (default nproc)
  -h, --help      show this help
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--auto-pkg) AUTO_PKG=1; shift ;;
	--abi) ABI="$2"; shift 2 ;;
	--strategy) STRATEGY="$2"; shift 2 ;;
	--no-modules) NO_MODULES=1; shift ;;
	--out) OUT="$2"; shift 2 ;;
	-j) JOBS="$2"; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown option: $1" >&2; usage; exit 1 ;;
	esac
done

cd "${REPO_DIR}"

# ---- device facts ---------------------------------------------------------

ANDROID_VERSION="$(getprop ro.build.version.release 2>/dev/null || true)"
KERNEL_RELEASE="$(uname -r)"
KERNEL_MAJOR="$(printf '%s' "${KERNEL_RELEASE}" |
	awk -F. '{print $1"."$2}')"
KMI="android${ANDROID_VERSION:-unknown}-${KERNEL_MAJOR}"

echo "device  : $(getprop ro.product.model 2>/dev/null || echo unknown)"
echo "android : ${ANDROID_VERSION:-unknown}"
echo "kernel  : ${KERNEL_RELEASE} (major ${KERNEL_MAJOR})"
echo "kmi     : ${KMI}"
echo "abi     : ${ABI}"
echo ""

# ---- dependency check -----------------------------------------------------

missing=""
check() {
	if ! command -v "$1" >/dev/null 2>&1; then
		missing="${missing:+$missing }$2"
	fi
}

if [ "${ABI}" = "host" ]; then
	check clang clang
	check cc clang
else
	check clang clang
fi
check cmake cmake
check make make
check zip zip

if [ -n "${missing}" ]; then
	if [ "${AUTO_PKG}" -eq 1 ]; then
		echo ">> installing missing packages: ${missing}"
		pkg install -y ${missing}
	else
		echo "missing build tools: ${missing}" >&2
		echo "install them with:" >&2
		echo "  pkg install -y ${missing}" >&2
		echo "or rerun with --auto-pkg to do that automatically." >&2
		exit 1
	fi
fi

if [ "${ABI}" != "host" ]; then
	if [ -z "${NDK}" ] || [ ! -d "${NDK}" ]; then
		echo "cross compiling to ${ABI} needs the Android NDK." >&2
		echo "install it manually and point ANDROID_NDK_HOME at it," >&2
		echo "for example:" >&2
		echo "  pkg install wget && wget -O ndk.zip \\" >&2
		echo "    https://dl.google.com/android/repository/android-ndk-r27c.zip" >&2
		echo "There is no pkg package for the NDK, so this step cannot" >&2
		echo "be automated." >&2
		exit 1
	fi
	echo "ndk     : ${NDK}"
fi
echo ""

# ---- CLI ------------------------------------------------------------------

echo ">> building the CLI (${ABI})"
CMAKE_ARGS=(-B build-local -DMGZ_ANDROID_ABI="${ABI}")
if [ "${ABI}" = "host" ]; then
	CMAKE_ARGS+=(-DMGZ_STATIC=OFF)
elif [ -n "${NDK}" ]; then
	CMAKE_ARGS+=(-DMGZ_ANDROID_NDK="${NDK}")
fi
cmake "${CMAKE_ARGS[@]}"
cmake --build build-local --target mgz -- -j"${JOBS}"

mkdir -p "${OUT}"
CLI_BIN="$(find build-local/out -name mgz -type f | head -1)"
cp -f "${CLI_BIN}" "${OUT}/mgz"
chmod 755 "${OUT}/mgz"
echo ">> CLI at ${OUT}/mgz"

# ---- kernel modules -------------------------------------------------------

if [ "${NO_MODULES}" -eq 0 ]; then
	echo ""
	echo ">> building the modules for ${KMI}"
	"${SCRIPT_DIR}/build-modules.sh" \
		--kmi "${KMI}" \
		--strategy "${STRATEGY}" \
		--src kernel \
		--out "${OUT}/modules"
	cp -f "${OUT}"/modules/*.ko "${OUT}/"
fi

echo ""
echo "done, everything is in ${OUT}/"
ls -l "${OUT}"
