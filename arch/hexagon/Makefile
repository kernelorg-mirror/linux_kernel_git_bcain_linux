# SPDX-License-Identifier: GPL-2.0
#  Makefile for the Hexagon arch

KBUILD_DEFCONFIG = comet_defconfig

# Hexagon clang doesn't support -gcc-toolchain flag
CLANG_GCC_TC :=

# lld prior to 22.0.0 has a bug in the relocation handlers that corrupts
# duplex sub-instruction encodings, turning valid register transfers into
# register pair combines and causing register write conflicts at runtime.
# https://github.com/llvm/llvm-project/issues/179856
# Require lld >= 22.0.0 or use an alternative linker such as ld.eld.
ifeq ($(CONFIG_LD_IS_LLD),y)
  ifneq ($(call test-ge, $(CONFIG_LLD_VERSION), 220000),y)
    $(error Hexagon requires lld >= 22.0.0 due to a duplex relocation bug. Use LD=ld.eld or upgrade lld)
  endif
endif

# Do not use GP-relative jumps
KBUILD_CFLAGS += -G0
LDFLAGS_vmlinux += -G0

# Override arch version only if explicitly set in Kconfig
ifneq ($(CONFIG_HEXAGON_ARCH_VERSION),)
  KBUILD_CFLAGS += -mv$(CONFIG_HEXAGON_ARCH_VERSION)
  LDFLAGS_vmlinux += -mv$(CONFIG_HEXAGON_ARCH_VERSION)
endif

# Workaround for packetizer generating invalid instructions
# https://github.com/llvm/llvm-project/issues/178338
KBUILD_CFLAGS += -mllvm --disable-packetizer -mllvm -disable-nvjump

# Do not use single-byte enums; these will overflow.
KBUILD_CFLAGS += -fno-short-enums

# Modules may have out-of-range calls to kernel symbols.  ld.eld has always
# supported range-extension thunks (trampolines) for Hexagon, and lld gained
# them in 21.0.0.  Add -mlong-calls only for linkers that lack this support.
ifeq ($(CONFIG_LD_IS_ELD),y)
  # eld always generates range-extension thunks
else ifeq ($(CONFIG_LD_IS_LLD),y)
  ifneq ($(call test-ge, $(CONFIG_LLD_VERSION), 210000),y)
    KBUILD_CFLAGS_MODULE += -mlong-calls
  endif
else
  KBUILD_CFLAGS_MODULE += -mlong-calls
endif

KBUILD_CFLAGS += $(cflags-y) -fno-pic
KBUILD_AFLAGS += $(aflags-y)
KBUILD_LDFLAGS += $(ldflags-y)

# Set linker emulation for hexagon (needed for empty archive linking)
LDFLAGS += -m hexagonelf

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

LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name 2>/dev/null)
libs-y += $(LIBGCC)

head-y := arch/hexagon/kernel/head.o

core-y += arch/hexagon/platform/common/

core-$(CONFIG_HEXAGON_QEMU)		+= arch/hexagon/platform/qemu/

vmlinux.bin:
	$(OBJCOPY) -O binary vmlinux vmlinux.bin

#  Devtree blob targets
boot := arch/hexagon/boot
core-$(CONFIG_DTB_BUILTIN) += arch/hexagon/boot/dts/

PHONY += dtbs
dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs
