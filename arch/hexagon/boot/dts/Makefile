# SPDX-License-Identifier: GPL-2.0

dtb-$(CONFIG_HEXAGON_QEMU)		+= qemu_virt.dtb
dtb-$(CONFIG_HEXAGON_SA8775P_CDSP0)	+= sa8775p_cdsp0.dtb

obj-$(CONFIG_HEXAGON_QEMU)		+= qemu_virt.dtb.o
obj-$(CONFIG_HEXAGON_SA8775P_CDSP0)	+= sa8775p_cdsp0.dtb.o

targets += dtbs
targets += $(dtb-y)

dtbs: $(addprefix $(obj)/, $(dtb-y))
