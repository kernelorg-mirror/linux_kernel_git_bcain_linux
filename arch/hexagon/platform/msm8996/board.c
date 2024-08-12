/*
 * linux/arch/hexagon/platform/msm8996/board.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/clock.h>
#include <asm/platform.h>
#include <asm/platform/sirc.h>
#include <asm/platform/comet/comet_iomap.h>
#include <asm/angel_console.h>
#include <asm/hexagon_vm.h>
#include "board.h"
#include <linux/module.h>

static const char *msm8996_dt_compat[] __initconst = {
	"qcom,msm8996",
	NULL
};

static struct platform_device *devices[] __initdata = {
};

struct of_device_id platform_of_irq_matches[] __initdata  = {
        { .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};


static int __init msm8996_init(void)
{
	printk("%s\n", __func__);

	if (strcmp(mdesc->name,"msm8996")) {
		printk("omg not msm8996!!!\n");
		return 0;
	}

	platform_add_devices(devices, ARRAY_SIZE(devices));

        of_platform_populate(of_find_node_by_path("/soc"),
		of_default_bus_match_table, NULL, NULL);

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 */
arch_initcall(msm8996_init);

void setup_arch_platform_msm8996(void)
{
	printk("%s\n", __func__);
	bootmem_lastpg = PFN_DOWN(1<<26);

	/*  Stuff we probably want super-early, like clock setting */


}

MACHINE_START(MSM8996, "msm8996")
	.setup_arch_platform = setup_arch_platform_msm8996,
	.dt_compat = msm8996_dt_compat
MACHINE_END


