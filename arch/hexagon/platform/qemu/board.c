// SPDX-License-Identifier: GPL-2.0
/*
 * Board support for QEMU Hexagon virtual machine
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#include <asm/platform.h>

static const char * const qemu_dt_compat[] __initconst = {
	"qcom,hexagon-virt",
	NULL
};

const struct of_device_id platform_of_irq_matches[] __initconst = {
	{ .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};

static int __init qemu_init(void)
{
	if (strcmp(mdesc->name, "hexagon-qemu"))
		return 0;

	of_platform_populate(of_find_node_by_path("/soc"), NULL, NULL, NULL);

	return 0;
}

arch_initcall(qemu_init);

static void setup_arch_platform_qemu(void)
{
	/* Default to 64MB; overridden by mem= on the kernel command line */
	bootmem_lastpg = PFN_DOWN(1 << 26);
}

DT_MACHINE_START(HEXAGON_QEMU, "hexagon-qemu")
	.setup_arch_platform = setup_arch_platform_qemu,
	.dt_compat = qemu_dt_compat
MACHINE_END
