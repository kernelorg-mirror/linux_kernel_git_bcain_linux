/*
 * linux/arch/hexagon/platform/sm8150/board.c
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
#include <asm/angel_console.h>
#include <asm/hexagon_vm.h>
#include "board.h"
#include <linux/module.h>

static const char *sm8150_dt_compat[] __initconst = {
	"qcom,sm8150",
	NULL
};

static struct platform_device *devices[] __initdata = {
};

const struct of_device_id platform_of_irq_matches[] __initdata = {
        { .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};


static int __init sm8150_init(void)
{
	printk("%s\n", __func__);

	if (strcmp(mdesc->name,"sm8150")) {
		return 0;
	}

	platform_add_devices(devices, ARRAY_SIZE(devices));
        of_platform_populate(of_find_node_by_path("/soc"),
		NULL, NULL, NULL);

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 */
arch_initcall(sm8150_init);


static void setup_arch_platform_sm8150(void)
{
	printk("%s\n", __func__);
	bootmem_lastpg = PFN_DOWN(1<<26);

	/*  Stuff we probably want super-early, like clock setting */


}

#ifdef CONFIG_HEXAGON_HVX
void hvx_per_cpu_init(void *info)

{
	printk("%s, disabling hvx.ssr_xe\n", __func__);

	/* for now this is not needed as the default is OFF anyway */
	/* xa = 0x4: hvx ctx value does not matter since xe is OFF */
	/* xe = 0x0: disable - cause exception on access */
	__vmhwconfig(HWCONFIG_EXTBITS, 0, 0x4, 0x0);
}

int __init hvx_hw_init(void)

{
	/*  set HVX vector length: 6 = 64B, 7 = 128B mode */
	#ifdef CONFIG_HEXAGON_HVX_64B
	int vlength = 0x6;
	#else
	int vlength = 0x7;
	#endif
	printk("%s\n", __func__);

	/* this sets the vector length for ALL hw threads, not just the current one */
	__vmhwconfig(HWCONFIG_VLENGTH, 0, 0x7, 0x0);

	on_each_cpu(hvx_per_cpu_init, NULL, 0);

	return 0;
}

late_initcall(hvx_hw_init);
#endif

MACHINE_START(SM8150, "sm8150")
	.setup_arch_platform = setup_arch_platform_sm8150,
	.dt_compat = sm8150_dt_compat
MACHINE_END
