#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the gzvm modules against the prepared build directory of one KMI.
#
# The only thing needed from the kernel is its build directory: the config,
# the generated headers and Module.symvers, which is where the export CRCs
# come from. Nothing of the kernel itself has to be compiled.
#
# Strategies, in the default order:
#
#   ddk      use the Driver Development Kit image's /opt/ddk/kdir/<kmi>;
#            it already contains everything, so this step is instant
#   release  download the Module.symvers of the matching GKI release build
#            from ci.android.com and drop it into a modules_prepare'd tree
#   modules  build the =m parts of a shallow kernel/common clone, which is
#            what produces Module.symvers
#   vmlinux  last resort, the whole core kernel; only used when explicitly
#            requested through --strategy vmlinux or --with-vmlinux
#
#   ./scripts/build-modules.sh --kmi android16-6.12 --src kernel --out dist/modules
set -euo pipefail

ANDROID_VERSION="16"
KERNEL_VERSION="6.12"
KMI=""
KDIR="${KDIR:-}"
SYMVERS=""
STRATEGY="auto"
WITH_DDK=1
WITH_RELEASE=1
WITH_VMLINUX=0
KERNEL_SRC="kernel-src"
SRC="kernel"
OUT="dist/modules"
JOBS="$(nproc 2>/dev/null || echo 4)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() {
	cat <<'EOF'
Usage: build-modules.sh [options]

  --kmi <kmi>           KMI generation          (default android16-6.12)
  --kdir <dir>          use exactly this prepared build directory
  --symvers <file>      use exactly this Module.symvers
  --strategy <s>        ddk | release | modules | vmlinux | auto (default)
  --no-ddk              drop ddk from the auto chain
  --no-release          drop release from the auto chain
  --with-vmlinux        append vmlinux to the auto chain as the last resort
  --kernel-src <dir>    where fetch-kernel.sh puts the source (kernel-src)
  --src <dir>           module sources          (default kernel)
  --out <dir>           where the .ko files go  (default dist/modules)
  -j <n>                parallel jobs           (default nproc)
  -h, --help            show this help

auto resolves to: ddk -> release -> modules, with vmlinux appended only
when --with-vmlinux is given.
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--kmi) KMI="$2"; shift 2 ;;
	--kdir) KDIR="$2"; shift 2 ;;
	--symvers) SYMVERS="$2"; shift 2 ;;
	--strategy) STRATEGY="$2"; shift 2 ;;
	--no-ddk) WITH_DDK=0; shift ;;
	--no-release) WITH_RELEASE=0; shift ;;
	--with-vmlinux) WITH_VMLINUX=1; shift ;;
	--kernel-src) KERNEL_SRC="$2"; shift 2 ;;
	--src) SRC="$2"; shift 2 ;;
	--out) OUT="$2"; shift 2 ;;
	-j) JOBS="$2"; shift 2 ;;
	-h|--help) usage; exit 0 ;;
	*) echo "unknown option: $1" >&2; usage; exit 1 ;;
	esac
done

[ -n "${KMI}" ] || KMI="android${ANDROID_VERSION}-${KERNEL_VERSION}"

echo "kmi      : ${KMI}"
echo "strategy : ${STRATEGY}"

# ---- helpers -------------------------------------------------------------

ensure_kernel_src() {
	if [ ! -d "${KERNEL_SRC}" ]; then
		echo ">> fetching the kernel source into ${KERNEL_SRC}"
		"${SCRIPT_DIR}/fetch-kernel.sh" \
			--android "${ANDROID_VERSION}" \
			--kernel "${KERNEL_VERSION}" \
			--dest "${KERNEL_SRC}"
	fi
}

# modules_prepare is what generates the config, the generated headers and the
# scripts an external module needs; it never produces a symvers itself.
prepare_tree() {
	local out="$1" shift_args="$2"

	if [ -f "${out}/.config" ] && [ -f "${out}/include/generated/autoconf.h" ]; then
		echo ">> build tree already prepared at ${out}"
	else
		"${SCRIPT_DIR}/prepare-kernel.sh" \
			--src "${KERNEL_SRC}" --out "${out}" ${shift_args}
	fi
}

# Download the symvers of the official GKI build for this KMI.
fetch_gki_symvers() {
	local out="$1"
	local branch="aosp_kernel-common-${KMI}"
	local api="https://ci.android.com/builds/branches/${branch}?format=json"
	local json build_id base url f

	echo ">> looking up the latest GKI build of ${branch}"
	json="$(curl -sf --max-time 60 "${api}")" || {
		echo "   cannot reach ci.android.com" >&2
		return 1
	}

	build_id="$(printf '%s' "${json}" |
		grep -oE '"buildId"[[:space:]]*:[[:space:]]*"[0-9]+"' |
		head -1 | grep -oE '[0-9]+')"
	[ -n "${build_id}" ] || {
		echo "   no build id found in the response" >&2
		return 1
	}

	base="https://ci.android.com/builds/submitted/${build_id}/kernel_aarch64/latest"
	for f in dist/Module.symvers dist/vmlinux.symvers Module.symvers; do
		url="${base}/${f}"
		echo ">> trying ${url}"
		if curl -sf --max-time 120 -o "${out}/Module.symvers" "${url}"; then
			[ "$(wc -c <"${out}/Module.symvers")" -gt 0 ] &&
				return 0
		fi
		rm -f "${out}/Module.symvers"
	done

	echo "   no symvers found among the release artifacts" >&2
	return 1
}

# ---- strategies ----------------------------------------------------------

strategy_ddk() {
	local ddk_root="${DDK_ROOT:-/opt/ddk}"

	if [ -d "${ddk_root}/kdir/${KMI}" ]; then
		KDIR="${ddk_root}/kdir/${KMI}"
		return 0
	fi
	for candidate in "${ddk_root}"/kdir/*; do
		if [ -d "${candidate}" ]; then
			KDIR="${candidate}"
			return 0
		fi
	done
	return 1
}

strategy_release() {
	local out="${PWD}/.gki-build"

	ensure_kernel_src
	prepare_tree "${out}" "--quick"
	fetch_gki_symvers "${out}"
	KDIR="${out}"
}

strategy_modules() {
	local out="${PWD}/.gki-build"

	if [ -f "${out}/Module.symvers" ]; then
		echo ">> Module.symvers already present at ${out}"
	else
		ensure_kernel_src
		prepare_tree "${out}" ""
	fi
	KDIR="${out}"
}

strategy_vmlinux() {
	local out="${PWD}/.gki-build"

	ensure_kernel_src
	prepare_tree "${out}" "--quick"
	if [ ! -f "${out}/Module.symvers" ]; then
		echo ">> building vmlinux (last resort)"
		make -C "${KERNEL_SRC}" ARCH=arm64 LLVM=1 O="${out}" \
			-j"${JOBS}" vmlinux
		[ -f "${out}/vmlinux.symvers" ] &&
			cp "${out}/vmlinux.symvers" "${out}/Module.symvers"
	fi
	KDIR="${out}"
}

# ---- resolve the chain ---------------------------------------------------

case "${STRATEGY}" in
auto)
	CHAIN=""
	[ "${WITH_DDK}" -eq 1 ] && CHAIN="${CHAIN} ddk"
	[ "${WITH_RELEASE}" -eq 1 ] && CHAIN="${CHAIN} release"
	CHAIN="${CHAIN} modules"
	[ "${WITH_VMLINUX}" -eq 1 ] && CHAIN="${CHAIN} vmlinux"
	;;
ddk|release|modules|vmlinux)
	CHAIN=" ${STRATEGY}"
	;;
*)
	echo "unknown strategy: ${STRATEGY}" >&2
	exit 1
	;;
esac
echo "chain    :${CHAIN}"

if [ -n "${KDIR}" ]; then
	echo ">> --kdir given, skipping the chain"
elif [ -n "${SYMVERS}" ]; then
	echo ">> --symvers given, preparing a tree to hold it"
	ensure_kernel_src
	prepare_tree "${PWD}/.gki-build" "--quick"
	cp "${SYMVERS}" "${PWD}/.gki-build/Module.symvers"
	KDIR="${PWD}/.gki-build"
else
	FOUND=""
	for s in ${CHAIN}; do
		echo ">> strategy: ${s}"
		if "strategy_${s}"; then
			FOUND="${s}"
			break
		fi
		echo "   ${s} not available, trying the next one"
	done
	[ -n "${FOUND}" ] || {
		echo "no strategy succeeded; see the messages above" >&2
		exit 1
	}
	echo ">> using ${FOUND}"
fi

[ -d "${KDIR}" ] || {
	echo "kernel build directory not found: ${KDIR}" >&2
	exit 1
}

echo "kdir     : ${KDIR}"
echo "out      : ${OUT}"

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
