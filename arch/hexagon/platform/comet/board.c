/*
 * linux/arch/hexagon/platform/comet/board.c
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
#include <linux/smsc911x.h>
#include <linux/io.h>
#include <linux/module.h>
#include <asm/irq.h>
#include <asm/clock.h>
#include <asm/platform.h>
#include <asm/platform/sirc.h>
#include <asm/platform/comet/irq.h>
#include <asm/platform/comet/comet_iomap.h>
#include <asm/angel_console.h>
#include "board.h"


static const char *comet_dt_compat[] __initconst = {
	"qcom,comet",
	NULL
};

/*  Clock Control */
static struct resource clkctl_resources[] = {
	{
		.name  = "clk_ctl_phys",
		.start = CLK_CTL_PHYS,
		.end   = CLK_CTL_PHYS+PAGE_SIZE-1,
		.flags = IORESOURCE_MEM
	}
};

static struct platform_device clk_ctl_device = {
	.name = "clk_ctl",
	.id = 0,
	.num_resources = ARRAY_SIZE(clkctl_resources),
	.resource = clkctl_resources
};

/*  MSM Serial */
static struct resource msm_serial0_resources[] = {
	{
		/* The irq is on the second controller */
		.start = HEXAGON_CPUINTS + L2_GROUP_SIZE + L2_SIRC1_UART3,
		.end   = HEXAGON_CPUINTS + L2_GROUP_SIZE + L2_SIRC1_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= MSM_UART3_PHYS,
		.end	= MSM_UART3_PHYS + MSM_UART3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device msm_serial0_device = {
	.name	= "msm_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(msm_serial0_resources),
	.resource	= msm_serial0_resources,
};

/*  SMSC911X (Ethernet) */
/*  Driver set the chip for push/pull, active low by default.  */
static struct smsc911x_platform_config comet_smsc911x_config = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
/*	.phy_interface	= PHY_INTERFACE_MODE_MII  */
};

static struct resource smsc911x_resources[] = {
	[0] = {
		.start	= SMC911X_PHYS,
		.end	= SMC911X_PHYS+PAGE_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
/*
 * XXX ethernet is actually on gpio 156 which is routed
 * the GPIO group one, which is on SIRC group zero.
 * So, for now, just route it to gpio_group_1; fix later!
 */
		.start   = HEXAGON_CPUINTS + L2_SIRC0_GPIO_GROUP1,
		.end     = HEXAGON_CPUINTS + L2_SIRC0_GPIO_GROUP1,
		.flags	= IORESOURCE_IRQ,
	},
};

/* Devices */
static struct platform_device smc911x_device = {
	.name		= "smsc911x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
	.dev		= {
		.platform_data = &comet_smsc911x_config,
	},
};

static struct platform_device *devices[] __initdata = {
	&msm_serial0_device,
	&smc911x_device,
	&clk_ctl_device
};

static struct clk comet_clocks[] = {
	CLOCK("uart1_clk",	UART1_CLK,	OFF),
	CLOCK("uart2_clk",	UART2_CLK,	0),
	CLOCK("uart3_clk",	UART3_CLK,	OFF),
};

static struct msm_clock_platform_data comet_clock_data = {
	.active_clocks = comet_clocks,
	.active_clock_count = ARRAY_SIZE(comet_clocks),
};


struct of_device_id platform_of_irq_matches[] __initdata  = {
        { .compatible = "qcom,minivm-pic", .data = hexagon_pic_of_init, },
        { .compatible = "qcom,sirc", .data = hexss_init_sirc_of, },
	{},
};



static int __init comet_init(void)
{
	void __iomem *tmp_clk;

	if (strcmp(mdesc->name,"comet")) {
		printk("omg not comet!!!\n");
		return 0;
	}

	msm_clock_init(&comet_clock_data);

	//hexss_init_sirc(regs_table, ARRAY_SIZE(regs_table));

	printk(KERN_INFO "%s: Setting UART3 to TCX0\n", __func__);
	tmp_clk = ioremap(CLK_CTL_PHYS, PAGE_SIZE);

	if (!tmp_clk)
		panic("ERROR:  problem mapping clock control\n");

	writel(UART_NS_REG__UART3_CLK_BRANCH_ENA___M,
		tmp_clk + 0xc0);

	iounmap(tmp_clk);

	//  We should really populate the device resource for the ethernet from the devicetree information.
	//  Screw it just whack it.  All of this belongs in devtree.
	smsc911x_resources[1].start = HEXAGON_CPUINTS + L2_SIRC0_GPIO_GROUP1 + 1;
	smsc911x_resources[1].end = HEXAGON_CPUINTS + L2_SIRC0_GPIO_GROUP1 + 1;

	msm_serial0_resources[0].start = HEXAGON_CPUINTS + L2_GROUP_SIZE + L2_SIRC1_UART3 + 1;
	msm_serial0_resources[0].end = HEXAGON_CPUINTS + L2_GROUP_SIZE + L2_SIRC1_UART3 + 1;

	platform_add_devices(devices, ARRAY_SIZE(devices));

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 * map_io is called during paging_init.
 */
arch_initcall(comet_init);


void setup_arch_platform_comet()
{
#ifdef CONFIG_HEXAGON_ANGEL_TRAPS
	register_angel_console();
#endif
}


MACHINE_START(COMET, "comet")
	.setup_arch_platform = setup_arch_platform_comet,
	.dt_compat = comet_dt_compat
MACHINE_END


