/*
 * linux/arch/hexagon/platform/simulator/board.c
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
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/clock.h>
#include <asm/platform.h>
#include <asm/platform/sirc.h>
#include <asm/platform/comet/comet_iomap.h>
#include <asm/angel_console.h>
#include "board.h"
#include <linux/module.h>

static const char *simulator_dt_compat[] __initconst = {
	"qcom,simulator",
	NULL
};

static struct platform_device *devices[] __initdata = {
};

struct of_device_id platform_of_irq_matches[] __initdata  = {
        { .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};

static int __init simulator_init(void)
{
	if (strcmp(mdesc->name,"simulator")) {
		printk("omg not simulator!!!\n");
		return 0;
	}

	platform_add_devices(devices, ARRAY_SIZE(devices));

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 * map_io is called during paging_init.
 */
arch_initcall(simulator_init);


void setup_arch_platform_simulator(void)
{
#ifdef CONFIG_HEXAGON_ANGEL_TRAPS
	on_simulator=1;
	register_angel_console();
#endif
}


MACHINE_START(SIMULATOR, "simulator")
	.setup_arch_platform = setup_arch_platform_simulator,
	.dt_compat = simulator_dt_compat
MACHINE_END


