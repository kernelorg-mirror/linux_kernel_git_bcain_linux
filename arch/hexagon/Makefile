# SPDX-License-Identifier: GPL-2.0
#  Makefile for the Hexagon arch

KBUILD_DEFCONFIG = comet_defconfig

# Hexagon clang doesn't support -gcc-toolchain flag
CLANG_GCC_TC :=

# Do not use GP-relative jumps
KBUILD_CFLAGS += -G0
LDFLAGS_vmlinux += -G0

#HEXAGON_TOOLS_VERSION := $(shell $(CC) --version | grep "Clang" | sed -e 's/.*clang-\([0-9][0-9]\).*/\1/')

#  Only use these if forced; use the compiler default, whatever it is
ifneq ($(CONFIG_HEXAGON_ARCH_VERSION),)
  KBUILD_CFLAGS += -mv$(CONFIG_HEXAGON_ARCH_VERSION)
  LDFLAGS_vmlinux += -mv$(CONFIG_HEXAGON_ARCH_VERSION)
endif

# Workaround for packetizer generating invalid instruction encodings.
# https://github.com/llvm/llvm-project/issues/178338
# https://github.com/llvm/llvm-project/issues/218083
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

KBUILD_CFLAGS += $(cflags-y) -fno-pic
KBUILD_AFLAGS += $(aflags-y)
KBUILD_LDFLAGS += $(ldflags-y)

# Set linker emulation for hexagon (needed for empty archive linking)
LDFLAGS += -m hexagonelf

# Allow multiple definitions (kernel bsearch vs libc bsearch)
LDFLAGS += --allow-multiple-definition

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

# Hexagon builtins are provided by arch/hexagon/lib/ (divsi3, memcpy, etc.)
# so we do not link against libgcc or libc.
LIBGCC :=
LIBC_ARCHIVE :=

head-y := arch/hexagon/kernel/head.o

core-y += arch/hexagon/platform/common/

core-$(CONFIG_HEXAGON_SM8150)		+= arch/hexagon/platform/sm8150/
core-$(CONFIG_HEXAGON_SIMULATOR)	+= arch/hexagon/platform/simulator/

vmlinux.bin:
	$(OBJCOPY) -O binary vmlinux vmlinux.bin

#  Devtree blob targets
boot := arch/hexagon/boot
core-$(CONFIG_DTB_BUILTIN) += arch/hexagon/boot/dts/

PHONY += dtbs
dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs
