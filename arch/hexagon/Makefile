# SPDX-License-Identifier: GPL-2.0
#  Makefile for the Hexagon arch

KBUILD_DEFCONFIG = comet_defconfig

# Do not use GP-relative jumps
KBUILD_CFLAGS += -G0
LDFLAGS_vmlinux += -G0

# The packetizer emits invalid encodings for some of the kernel's inline
# assembly, so it and new-value jumps have to stay off.
# FIXME: gate this on clang-min-version, and eventually drop it, once
# https://github.com/llvm/llvm-project/issues/178338 is fixed and released.
KBUILD_CFLAGS += -mllvm --disable-packetizer -mllvm -disable-nvjump

# Do not use single-byte enums; these will overflow.
KBUILD_CFLAGS += -fno-short-enums

# We must use long-calls:
KBUILD_CFLAGS += -mlong-calls

# Modules must use either long-calls, or use pic/plt.
# Use long-calls for now, it's easier.  And faster.
# KBUILD_CFLAGS_MODULE += -fPIC
# KBUILD_LDFLAGS_MODULE += -shared
KBUILD_CFLAGS_MODULE += -mlong-calls

# The kernel is linked at a fixed address, and the GOT-relative sequences the
# default PIC model produces are not resolvable this early in boot.
KBUILD_CFLAGS += -fno-pic

KBUILD_CFLAGS += $(cflags-y)
KBUILD_AFLAGS += $(aflags-y)
KBUILD_LDFLAGS += $(ldflags-y)

# Thread-info register will be r19.  This value is not configureable;
# it is hard-coded in several files.
TIR_NAME := r19
KBUILD_CFLAGS += -ffixed-$(TIR_NAME) -DTHREADINFO_REG=$(TIR_NAME) -D__linux__
KBUILD_AFLAGS += -DTHREADINFO_REG=$(TIR_NAME)

# Disable HexagonConstExtenders pass for LLVM versions prior to 19.1.0
# https://github.com/llvm/llvm-project/issues/99714
ifneq ($(call clang-min-version, 190100),y)
KBUILD_CFLAGS += -mllvm -hexagon-cext=false
endif
