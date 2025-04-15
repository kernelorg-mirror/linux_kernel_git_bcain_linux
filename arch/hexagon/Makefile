#  Makefile for the Hexagon arch

KBUILD_DEFCONFIG = comet_defconfig

# Do not use GP-relative jumps
KBUILD_CFLAGS += -G0
LDFLAGS_vmlinux += -G0

#HEXAGON_TOOLS_VERSION := $(shell $(CC) --version | grep "Clang" | sed -e 's/.*clang-\([0-9][0-9]\).*/\1/')

#  Only use these if forced; use the compiler default, whatever it is
ifneq ($(CONFIG_HEXAGON_ARCH_VERSION),)
  KBUILD_CFLAGS += -mv$(CONFIG_HEXAGON_ARCH_VERSION)
  LDFLAGS_vmlinux += -mv$(CONFIG_HEXAGON_ARCH_VERSION)
endif

# Do not use single-byte enums; these will overflow.
KBUILD_CFLAGS += -fno-short-enums

# Modules must use either long-calls, or use pic/plt.
# Use long-calls for now, it's easier.  And faster.
# CFLAGS_MODULE += -fPIC
# LDFLAGS_MODULE += -shared
CFLAGS_MODULE += -mlong-calls

KBUILD_CFLAGS += $(cflags-y) -fno-pic
KBUILD_AFLAGS += $(aflags-y)

#  no KBUILD_LDFLAGS?
LDFLAGS += $(ldflags-y)

# Thread-info register will be r19.  This value is not configureable;
# it is hard-coded in several files.
TIR_NAME := r19
KBUILD_CFLAGS += -ffixed-$(TIR_NAME) -DTHREADINFO_REG=$(TIR_NAME) -D__linux__
KBUILD_AFLAGS += -DTHREADINFO_REG=$(TIR_NAME)

LIBGCC := $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)
libs-y += $(LIBGCC)

head-y := arch/hexagon/kernel/head.o

core-y += arch/hexagon/kernel/ \
	arch/hexagon/mm/ \
	arch/hexagon/lib/ \
	arch/hexagon/platform/common/

drivers-$(CONFIG_OPROFILE)	+= arch/hexagon/oprofile/

core-$(CONFIG_HEXAGON_COMET)		+= arch/hexagon/platform/comet/
machine-$(CONFIG_HEXAGON_COMET)		:= comet

core-$(CONFIG_HEXAGON_SIMULATOR)	+= arch/hexagon/platform/simulator/
machine-$(CONFIG_HEXAGON_SIMULATOR)	:= simulator

core-$(CONFIG_HEXAGON_FPGA)	+= arch/hexagon/platform/fpga/
machine-$(CONFIG_HEXAGON_FPGA)	:= fpga

core-$(CONFIG_HEXAGON_AMAZON)		+= arch/hexagon/platform/amazon/
machine-$(CONFIG_HEXAGON_AMAZON)	:= amazon

core-$(CONFIG_HEXAGON_ARCHSIM)		+= arch/hexagon/platform/archsim/
machine-$(CONFIG_HEXAGON_ARCHSIM)	:= archsim

core-$(CONFIG_HEXAGON_MSM8960_FLUID)	+= arch/hexagon/platform/msm8960_fluid/
machine-$(CONFIG_HEXAGON_MSM8960_FLUID)	:= msm8960_fluid

core-$(CONFIG_HEXAGON_MSM8974_FLUID)	+= arch/hexagon/platform/msm8974_fluid/
core-$(CONFIG_HEXAGON_MSM8996)		+= arch/hexagon/platform/msm8996/
core-$(CONFIG_HEXAGON_MSM8998)		+= arch/hexagon/platform/msm8998/
core-$(CONFIG_HEXAGON_SDM845)		+= arch/hexagon/platform/sdm845/
core-$(CONFIG_HEXAGON_SM8150)		+= arch/hexagon/platform/sm8150/

vmlinux.bin: vmlinux
	$(OBJCOPY) -O binary $^ $@

#  Devtree blob targets
boot := arch/hexagon/boot
core-$(CONFIG_DTB_BUILTIN) += arch/hexagon/boot/dts/

PHONY += dtbs
dtbs: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs
