# SPDX-License-Identifier: GPL-3.0-or-later
#
# mgz — the userspace CLI.
#
# Built as a static binary so it runs on the device without any runtime
# dependency.  Native build (Termux, or any Android shell with a compiler)
# just works; cross compilation is driven from the top level CMakeLists.txt
# through MGZ_CROSS_TRIPLE / MGZ_CROSS_SYSROOT.

set(MGZ_SOURCES
	mgz.c
	root.c
	detect.c
	ksu.c
)

add_executable(mgz ${MGZ_SOURCES})

target_include_directories(mgz PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(mgz PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Os
)

# Prefer a static binary: no shared loader surprises on old devices. Some
# toolchains (for example a Termux shell without the NDK static crt) cannot
# link statically, so fall back to dynamic linking when that is the case.
option(MGZ_STATIC "Link mgz statically" ON)

if(MGZ_STATIC)
	include(CheckCSourceCompiles)
	set(CMAKE_REQUIRED_LINK_OPTIONS "-static")
	check_c_source_compiles("int main(void){return 0;}" MGZ_HAVE_STATIC)
	unset(CMAKE_REQUIRED_LINK_OPTIONS)
	if(NOT MGZ_HAVE_STATIC)
		message(STATUS "static linking unavailable, linking dynamically")
		set(MGZ_STATIC OFF)
	endif()
endif()

if(MGZ_STATIC)
	target_link_options(mgz PRIVATE -static)
endif()

set_target_properties(mgz PROPERTIES
	C_STANDARD 99
	C_STANDARD_REQUIRED ON
	OUTPUT_NAME "mgz"
	RUNTIME_OUTPUT_DIRECTORY "${MGZ_OUTPUT_DIR}"
)

install(TARGETS mgz RUNTIME DESTINATION bin)
