# SPDX-License-Identifier: GPL-3.0-or-later
#
# MediaTek-Unlock-GenieZone-With-LKM
#
# Two version inputs define the KMI (Kernel Module Interface) generation:
#
#   MGZ_ANDROID_VERSION   Android major version      (default 16)
#   MGZ_KERNEL_VERSION    Linux kernel MAJOR version (default 6.12)
#
# Google only guarantees KMI stability inside one <android>-<kernel-major>
# generation, so both are major versions and 6.12.y all map to android16-6.12.
#
#   cmake -B build -DMGZ_ANDROID_VERSION=16 -DMGZ_KERNEL_VERSION=6.12
#   cmake --build build --target mgz
#   cmake --build build --target gzvm_modules   (needs -DMGZ_KERNEL_DIR=...)
#   cmake --build build --target ksu_module     (KernelSU flashable zip)

cmake_minimum_required(VERSION 3.20)

# The NDK compiler has to be selected before project() runs, otherwise CMake
# picks the host gcc, which does not understand --target=<triple>.
set(MGZ_ANDROID_NDK "" CACHE PATH "Android NDK root (optional)")

if(MGZ_ANDROID_NDK AND NOT CMAKE_C_COMPILER)
	file(GLOB MGZ_NDK_CLANG
		"${MGZ_ANDROID_NDK}/toolchains/llvm/prebuilt/*/bin/clang")
	list(GET MGZ_NDK_CLANG 0 MGZ_NDK_CLANG)
	if(MGZ_NDK_CLANG)
		set(CMAKE_C_COMPILER "${MGZ_NDK_CLANG}")
		message(STATUS "NDK compiler   : ${MGZ_NDK_CLANG}")
	endif()
endif()

project(MediaTekUnlockGenieZoneWithLKM
	VERSION 0.1.0
	DESCRIPTION "Unlock MediaTek GenieZone (gzvm) with a loadable kernel module"
	LANGUAGES C
)

# ---- KMI generation ------------------------------------------------------

set(MGZ_ANDROID_VERSION "16" CACHE STRING
	"Android major version of the target device")
set(MGZ_KERNEL_VERSION "6.12" CACHE STRING
	"Linux kernel MAJOR version of the target device")

set(MGZ_KMI "android${MGZ_ANDROID_VERSION}-${MGZ_KERNEL_VERSION}")
set(MGZ_KERNEL_BRANCH "common-${MGZ_KMI}")

message(STATUS "KMI generation : ${MGZ_KMI}")
message(STATUS "Kernel branch  : ${MGZ_KERNEL_BRANCH}")

# ---- Android ABI ---------------------------------------------------------

set(MGZ_ANDROID_ABI "arm64-v8a" CACHE STRING
	"Target ABI: arm64-v8a, armeabi-v7a, armeabi, host")
set_property(CACHE MGZ_ANDROID_ABI
	PROPERTY STRINGS "arm64-v8a" "armeabi-v7a" "armeabi" "host")

set(MGZ_ANDROID_API "23" CACHE STRING
	"Android API level to compile against (23 keeps support for old ARMv7)")

if(MGZ_ANDROID_ABI STREQUAL "arm64-v8a")
	set(MGZ_TRIPLE "aarch64-linux-android${MGZ_ANDROID_API}")
elseif(MGZ_ANDROID_ABI STREQUAL "armeabi-v7a")
	set(MGZ_TRIPLE "armv7a-linux-androideabi${MGZ_ANDROID_API}")
elseif(MGZ_ANDROID_ABI STREQUAL "armeabi")
	set(MGZ_TRIPLE "arm-linux-androideabi${MGZ_ANDROID_API}")
else()
	set(MGZ_TRIPLE "")
endif()

# ---- cross compilation ---------------------------------------------------
#
# With an NDK the sysroot is derived automatically; when cross compiling
# without one, point MGZ_SYSROOT at the sysroot yourself.

set(MGZ_SYSROOT "" CACHE PATH "Sysroot to compile against (optional)")

if(MGZ_ANDROID_NDK AND NOT MGZ_SYSROOT)
	set(MGZ_SYSROOT "${MGZ_ANDROID_NDK}/toolchains/llvm/prebuilt/linux-x86_64/sysroot")
endif()

set(MGZ_CROSS_FLAGS "")
if(MGZ_TRIPLE)
	list(APPEND MGZ_CROSS_FLAGS "--target=${MGZ_TRIPLE}")
	message(STATUS "Cross target    : ${MGZ_TRIPLE}")
else()
	message(STATUS "Cross target    : host (native build)")
endif()

if(MGZ_SYSROOT)
	list(APPEND MGZ_CROSS_FLAGS "--sysroot=${MGZ_SYSROOT}")
	message(STATUS "Sysroot         : ${MGZ_SYSROOT}")
endif()

# Join explicitly: an unjoined list expands to a semicolon separated string,
# which the shell would then treat as a command separator.
string(REPLACE ";" " " MGZ_CROSS_FLAGS_STR "${MGZ_CROSS_FLAGS}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MGZ_CROSS_FLAGS_STR}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${MGZ_CROSS_FLAGS_STR}")

# ---- output layout -------------------------------------------------------

set(MGZ_OUTPUT_DIR "${CMAKE_BINARY_DIR}/out/${MGZ_ANDROID_ABI}-${MGZ_KMI}")
file(MAKE_DIRECTORY "${MGZ_OUTPUT_DIR}")

add_subdirectory(cli)

# ---- kernel modules ------------------------------------------------------
#
# The kernel build system writes object files next to the sources, so the
# kernel/ directory is copied into the build tree first and compiled there.

set(MGZ_KERNEL_DIR "" CACHE PATH
	"GKI kernel source tree to build the modules against")
set(MGZ_KERNEL_BUILD_DIR "${CMAKE_BINARY_DIR}/kernel")
set(MGZ_LLVM "1" CACHE STRING "Use the LLVM toolchain (1) or CROSS_COMPILE (0)")

add_custom_target(gzvm_modules
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${MGZ_KERNEL_BUILD_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/kernel" "${MGZ_KERNEL_BUILD_DIR}"
	COMMAND ${CMAKE_MAKE_PROGRAM} -C "${MGZ_KERNEL_BUILD_DIR}"
		KERNEL_DIR="${MGZ_KERNEL_DIR}"
		ARCH=arm64
		LLVM=${MGZ_LLVM}
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${MGZ_KERNEL_BUILD_DIR}/gzvm_probe.ko"
		"${MGZ_KERNEL_BUILD_DIR}/gzvm_unlock.ko"
		"${MGZ_OUTPUT_DIR}"
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	COMMENT "Building gzvm_probe.ko and gzvm_unlock.ko for ${MGZ_KMI}"
	VERBATIM
)

# ---- KernelSU module zip -------------------------------------------------

set(MGZ_MODULE_ZIP "${MGZ_OUTPUT_DIR}/mgz_unlock-${MGZ_KMI}.zip")

# The CLI is optional inside the zip: the package job has it, a module-only
# build does not.
set(MGZ_MODULE_EXTRA "")
if(EXISTS "${MGZ_OUTPUT_DIR}/mgz")
	list(APPEND MGZ_MODULE_EXTRA
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${MGZ_OUTPUT_DIR}/mgz"
			"${CMAKE_BINARY_DIR}/module-stage/mgz")
endif()

add_custom_target(ksu_module
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/module-stage"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/module"
		"${CMAKE_BINARY_DIR}/module-stage"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${MGZ_OUTPUT_DIR}/gzvm_unlock.ko"
		"${CMAKE_BINARY_DIR}/module-stage/gzvm_unlock.ko"
	${MGZ_MODULE_EXTRA}
	COMMAND ${CMAKE_COMMAND} -E rm -f "${MGZ_MODULE_ZIP}"
	COMMAND ${CMAKE_COMMAND} -E tar cf "${MGZ_MODULE_ZIP}" --format=zip
		-- .
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/module-stage"
	COMMENT "Packaging the KernelSU module: ${MGZ_MODULE_ZIP}"
	VERBATIM
)

add_custom_target(dist DEPENDS mgz gzvm_modules ksu_module)

message(STATUS "")
message(STATUS "Targets:")
message(STATUS "  mgz           userspace CLI      -> ${MGZ_OUTPUT_DIR}/mgz")
message(STATUS "  gzvm_modules  kernel modules     -> ${MGZ_OUTPUT_DIR}/*.ko")
message(STATUS "  ksu_module    KernelSU zip       -> ${MGZ_MODULE_ZIP}")
message(STATUS "  dist          all of the above")
message(STATUS "")
