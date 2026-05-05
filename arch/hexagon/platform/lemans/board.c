// SPDX-License-Identifier: GPL-2.0
/*
 * Board support for SA8775P CDSP0
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#include <asm/platform.h>

static const char * const lemans_dt_compat[] __initconst = {
	"qcom,sa8775p-cdsp0",
	NULL
};

const struct of_device_id platform_of_irq_matches[] __initconst = {
	{ .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};

static int __init lemans_init(void)
{
	of_platform_populate(of_find_node_by_path("/soc"), NULL, NULL, NULL);
	return 0;
}

arch_initcall(lemans_init);

static void setup_arch_platform_lemans(void)
{
	bootmem_lastpg = PFN_DOWN(1 << 26);
}

DT_MACHINE_START(HEXAGON_LEMANS, "hexagon-lemans")
	.setup_arch_platform = setup_arch_platform_lemans,
	.dt_compat = lemans_dt_compat
MACHINE_END
