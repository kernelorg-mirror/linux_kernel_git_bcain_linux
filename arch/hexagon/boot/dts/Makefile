# SPDX-License-Identifier: GPL-2.0

dtb-${CONFIG_HEXAGON_QEMU}	+= qemu_virt.dtb
obj-${CONFIG_HEXAGON_QEMU}	+= qemu_virt.dtb.o

targets += dtbs
targets += $(dtb-y)

dtbs: $(addprefix $(obj)/, $(dtb-y))
