/*
 * SPDX-License-Identifier: GPL-2.0
 * Board support for QEMU Hexagon virtual machine
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/*
 * Board support for QEMU Hexagon virtual machine
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#include <asm/platform.h>
#include <asm/platform/sirc.h>

static const char *qemu_dt_compat[] __initconst = {
	"qcom,hexagon-virt",
	NULL
};

static struct platform_device *devices[] __initdata = {
};

const struct of_device_id platform_of_irq_matches[] = {
	{ .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};

static int __init qemu_init(void)
{
	pr_info("%s\n", __func__);

	if (strcmp(mdesc->name, "hexagon-qemu"))
		return 0;

	platform_add_devices(devices, ARRAY_SIZE(devices));
	of_platform_populate(of_find_node_by_path("/soc"), NULL, NULL, NULL);

	return 0;
}

arch_initcall(qemu_init);

static void setup_arch_platform_qemu(void)
{
	pr_info("%s\n", __func__);
	bootmem_lastpg = PFN_DOWN(1 << 26);
}

MACHINE_START(HEXAGON_QEMU, "hexagon-qemu")
	.setup_arch_platform = setup_arch_platform_qemu,
	.dt_compat = qemu_dt_compat
MACHINE_END
