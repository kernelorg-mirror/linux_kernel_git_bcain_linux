/*
 * Copyright (c) 2014, Code Aurora Forum. All rights reserved.
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
#include <linux/of.h>
#include <linux/of_platform.h>
#include <asm/irq.h>
#include <asm/platform.h>
#include <asm/angel_console.h>
#include "board.h"

static const char *fpga_dt_compat[] __initconst = {
	"qcom,fpga",
	NULL
};

void __init hexagon_dma_init(void);

static int __init fpga_init(void)
{
	if (strcmp(mdesc->name,"fpga")) {
		printk("omg not fpga!!!\n");
		return 0;
	}

	hexagon_dma_init();  //  may need to split out a separate copy for this platform

        of_platform_populate(of_find_node_by_path("/soc"),
		of_default_bus_match_table, NULL, NULL);

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 * map_io is called during paging_init.
 */
arch_initcall(fpga_init);

void setup_arch_platform_fpga(void)
{
#ifdef CONFIG_HEXAGON_ANGEL_TRAPS
	on_simulator = 1;	//  force call-by-value
	register_angel_console();
#endif
}

MACHINE_START(FPGA, "fpga")
	.setup_arch_platform = setup_arch_platform_fpga,
	.dt_compat = fpga_dt_compat
MACHINE_END

