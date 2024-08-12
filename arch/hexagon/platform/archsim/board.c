/*
 * linux/arch/hexagon/platform/r3pc/board.c
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
#include <asm/mem-layout.h>
#include "board.h"


static const char *r3pc_dt_compat[] __initconst = {
	"qcom,r3pc",
	NULL
};

/*  bunch of this stuff probably won't even make sense.  */

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
		.start = 5,
		.end   = 5,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= 0xa9c00000,
		.end	= 0xa9c00000 + 4096 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device msm_serial0_device = {
	.name	= "msm_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(msm_serial0_resources),
	.resource	= msm_serial0_resources,
};

#ifdef CONFIG_MIPSNET
static struct platform_device eth1_device = {
	.name	= "mipsnet",
	.id	= 0,
};
#endif


#ifdef CONFIG_QFEC

# define QFEC_MAC_IRQ           12

# define QFEC_MAC_BASE          0x40000000
# define QFEC_CLK_BASE          0x94020000

# define QFEC_MAC_SIZE          0x2000
# define QFEC_CLK_SIZE          0x18100

# define QFEC_MAC_FUSE_BASE     0x80004210
# define QFEC_MAC_FUSE_SIZE     16

static struct resource qfec_resources[] = {
        [0] = {
                .start = QFEC_MAC_BASE,
                .end   = QFEC_MAC_BASE + QFEC_MAC_SIZE,
                .flags = IORESOURCE_MEM,
        },
        [1] = {
                .start = QFEC_MAC_IRQ,
                .end   = QFEC_MAC_IRQ,
                .flags = IORESOURCE_IRQ,
        },
        [2] = {
                .start = QFEC_CLK_BASE,
                .end   = QFEC_CLK_BASE + QFEC_CLK_SIZE,
                .flags = IORESOURCE_IO,
        },
        [3] = {
                .start = QFEC_MAC_FUSE_BASE,
                .end   = QFEC_MAC_FUSE_BASE + QFEC_MAC_FUSE_SIZE,
                .flags = IORESOURCE_DMA,
        },
};

static struct platform_device qfec_device = {
        .name           = "qfec",
        .id             = 0,
        .num_resources  = ARRAY_SIZE(qfec_resources),
        .resource       = qfec_resources,
};

#endif



static struct platform_device *devices[] __initdata = {
	&msm_serial0_device,
	&clk_ctl_device,
#ifdef CONFIG_MIPSNET
	&eth1_device,
#endif
#ifdef CONFIG_QFEC
	&qfec_device,
#endif
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


void __init hexagon_dma_init(void);


static int __init r3pc_init(void)
{
	if (strcmp(mdesc->name,"r3pc")) {
		printk("omg not r3pc!!!\n");
		return 0;
	}

	msm_clock_init(&comet_clock_data);

	hexagon_dma_init();

	platform_add_devices(devices, ARRAY_SIZE(devices));

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 * map_io is called during paging_init.
 */
arch_initcall(r3pc_init);


void setup_arch_platform_r3pc()
{
	//  this is getting hinky real fast
	bootmem_lastpg = PFN_DOWN(0x10000000 + PHYS_OFFSET);
#ifdef CONFIG_HEXAGON_ANGEL_TRAPS
	on_simulator = 1;
	register_angel_console();
#endif
}


MACHINE_START(R3PC, "r3pc")
	.setup_arch_platform = setup_arch_platform_r3pc,
	.dt_compat = r3pc_dt_compat
MACHINE_END


