# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the out-of-tree GenieZone modules against a GKI kernel tree.
#
#   make KERNEL_DIR=/path/to/common-android16-6.12
#
# The file is consumed twice: directly by developers, and by CMake, which
# copies this directory into the build tree before invoking it.

obj-m := gzvm_probe.o gzvm_unlock.o

ARCH ?= arm64
LLVM ?= 1
CROSS_COMPILE ?= aarch64-linux-gnu-
KERNEL_DIR ?= /path/to/gki-kernel

ifeq ($(LLVM),1)
LLVM_ARGS := LLVM=1
else
LLVM_ARGS := CROSS_COMPILE=$(CROSS_COMPILE)
endif

ifeq ($(V),1)
VERBOSE := V=1
endif

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) ARCH=$(ARCH) $(LLVM_ARGS) $(VERBOSE) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(CURDIR) ARCH=$(ARCH) $(LLVM_ARGS) clean

.PHONY: all clean
