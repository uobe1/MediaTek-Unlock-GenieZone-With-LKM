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
	verity.c
)

# ---- embedded module scripts ---------------------------------------------
#
# ksu.c writes the KernelSU module scripts out verbatim when persisting the
# module. The scripts under module/ are the single source of truth; this
# turns each of them into a header with one C string so the two can never
# drift apart.

set(MGZ_EMBED_DIR "${CMAKE_CURRENT_BINARY_DIR}/embedded")
file(MAKE_DIRECTORY "${MGZ_EMBED_DIR}")

foreach(mgz_script IN ITEMS
		"load.sh:load_sh"
		"status.sh:status_sh"
		"service.sh:service_sh"
		"late-load.sh:late_load_sh"
		"boot-completed.sh:boot_completed_sh"
		"action.sh:action_sh"
		"uninstall.sh:uninstall_sh")
	string(REPLACE ":" ";" mgz_script_pair "${mgz_script}")
	list(GET mgz_script_pair 0 mgz_script_file)
	list(GET mgz_script_pair 1 mgz_script_var)
	string(REPLACE ".sh" "" mgz_stem "${mgz_script_file}")
	string(REPLACE "-" "_" mgz_stem "${mgz_stem}")
	set(mgz_header "${MGZ_EMBED_DIR}/${mgz_stem}_sh.h")
	add_custom_command(
		OUTPUT "${mgz_header}"
		COMMAND ${CMAKE_COMMAND}
			-DMGZ_INPUT=${CMAKE_CURRENT_SOURCE_DIR}/../module/${mgz_script_file}
			-DMGZ_OUTPUT=${mgz_header}
			-DMGZ_VAR=${mgz_script_var}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/embed_file.cmake
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../module/${mgz_script_file}
		COMMENT "Embedding module/${mgz_script_file}"
		VERBATIM
	)
	list(APPEND MGZ_EMBED_HEADERS "${mgz_header}")
endforeach()

# Listing the generated headers as sources makes CMake run the custom
# commands before compiling ksu.c.
add_executable(mgz ${MGZ_SOURCES} ${MGZ_EMBED_HEADERS})

target_include_directories(mgz PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${MGZ_EMBED_DIR}
)

target_compile_options(mgz PRIVATE
	-Wall
	-Wextra
	-Wno-unused-parameter
	-Os
)

# Prefer a static binary: no shared loader surprises on old devices. Some
# toolchains (for example a Termux shell without the NDK static crt) cannot
# link statically, so fall back to dynamic linking when that is the case.
# Off by default: an NDK static binary carries a PT_TLS segment aligned to 8,
# and bionic on Android 15+ refuses to run it ("executable's TLS segment is
# underaligned"). Dynamic linking against bionic is fine on the device.
option(MGZ_STATIC "Link mgz statically (not usable on Android 15+)" OFF)

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
