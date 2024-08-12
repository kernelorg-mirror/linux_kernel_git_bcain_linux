/*
 * Copyright (c) 2010-2011 QUALCOMM Incorporated.
 * Author: Saravana Kannan <skannan@qualcomm.com>
 *
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
#include <linux/gpio.h>
#include <linux/module.h>

#include <asm/platform.h>
#include <asm/clock.h>
#include <asm/angel_console.h>

#include "irq.h"
#include "iomap.h"
#include "board.h"
#include "gpio.h"
//#include "asm/gpio.h"
#include "gpio-tlmm.h"
#include "tlmm.h"

#define gpio_set_value __gpio_set_value

static const char *amazon_dt_compat[] __initconst = {
	"qualcomm,amazon",
	NULL
};



void __iomem *gpio_base;

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
	.name		= "clk_ctl",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(clkctl_resources),
	.resource	= clkctl_resources
};

/*  MSM Serial */
#if CONFIG_SERIAL_MSM_CONSOLE_PORT == 0
static struct resource msm_serial0_resources[] = {

	{
		.start	= INT_UART1,  // UART_1 in Peripheral Subsystem SLIC (bit 5)
		.end	= INT_UART1,
		.flags	= IORESOURCE_IRQ,
	},

	{
		.start	= MSM_UART1_PHYS,  // UART 1 register space
		.end	= MSM_UART1_PHYS + MSM_UART1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},

	{
		.start	= INT_PERPHSS, // Hexagon PIC bit which the Peripheral Subsystem SLIC drives (bit 10)
		.end	= INT_PERPHSS,
		.flags	= IORESOURCE_IRQ,
	},

	{
		.start	= MSM_PERPHSS_IRQ_CTL_PHYS, // INTCTL2 register space ( Peripheral Subsystem SLIC regs )
		.end	= MSM_PERPHSS_IRQ_CTL_PHYS + MSM_PERPHSS_IRQ_CTL_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},

};

static struct platform_device msm_serial0_device = {
	.name		= "msm_serial",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(msm_serial0_resources),
	.resource	= msm_serial0_resources,
};
#endif

#if CONFIG_SERIAL_MSM_CONSOLE_PORT == 1
static struct resource msm_serial1_resources[] = {

	{
		.start	= INT_UART2,  // UART_2 in Peripheral Subsystem SLIC (bit 5)
		.end	= INT_UART2,
		.flags	= IORESOURCE_IRQ,
	},

	{
		.start	= MSM_UART2_PHYS,  // UART 2 register space
		.end	= MSM_UART2_PHYS + MSM_UART2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},

	{
		.start	= INT_PERPHSS, // Hexagon PIC bit which the Peripheral Subsystem SLIC drives (bit 10)
		.end	= INT_PERPHSS,
		.flags	= IORESOURCE_IRQ,
	},

	{
		.start	= MSM_PERPHSS_IRQ_CTL_PHYS, // INTCTL2 register space ( Peripheral Subsystem SLIC regs )
		.end	= MSM_PERPHSS_IRQ_CTL_PHYS + MSM_PERPHSS_IRQ_CTL_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},

};

static struct platform_device msm_serial1_device = {
	.name		= "msm_serial",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(msm_serial1_resources),
	.resource	= msm_serial1_resources,
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

static struct msm_gpio phy_config_data[] = {
	{ GPIO_CFG(GPIO_MAC_RST_N, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_2MA), "MAC_RST_N"},
	{ GPIO_CFG(GPIO_MAC_TXD_3, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_8MA), "MAC_TXD_3"},
	{ GPIO_CFG(GPIO_MAC_TXD_2, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_8MA), "MAC_TXD_2"},
	{ GPIO_CFG(GPIO_MAC_TXD_1, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_8MA), "MAC_TXD_1"},
	{ GPIO_CFG(GPIO_MAC_TXD_0, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_8MA), "MAC_TXD_0"},
	{ GPIO_CFG(GPIO_MAC_TX_EN, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_8MA), "MAC_TX_EN"},
	{ GPIO_CFG(GPIO_MAC_TX_CLK, 0, GPIO_CFG_OUTPUT,
			GPIO_CFG_NO_PULL, GPIO_CFG_8MA), "MAC_TX_CLK"},
};

static int __init phy_init(void)
{
	msm_gpios_request_enable(phy_config_data, ARRAY_SIZE(phy_config_data));
	gpio_direction_output(GPIO_MAC_RST_N, 0);
	udelay(100);
	gpio_set_value(GPIO_MAC_RST_N, 1);

	return 0;
}

#endif /* CONFIG_QFEC */

static struct platform_device *devices[] __initdata = {
	&msm_gpio_devices[0],
	&msm_gpio_devices[1],
	&msm_gpio_devices[2],
	&msm_gpio_devices[3],
	&msm_gpio_devices[4],
	&msm_gpio_devices[5],
#if CONFIG_SERIAL_MSM_CONSOLE_PORT == 0
	&msm_serial0_device,
#elif CONFIG_SERIAL_MSM_CONSOLE_PORT == 1
	&msm_serial1_device,
#endif
#ifndef CONFIG_QFEC
	&clk_ctl_device,
#else
        &qfec_device,
#endif

};

static struct clk amazon_clocks[] = {
	CLOCK("uart1_clk",	UART1_CLK,	OFF),
	CLOCK("uart2_clk",	UART2_CLK,	0),
	CLOCK("uart3_clk",	UART3_CLK,	OFF),
};

static struct msm_clock_platform_data amazon_clock_data = {
	.active_clocks = amazon_clocks,
	.active_clock_count = ARRAY_SIZE(amazon_clocks),
};

#ifdef CONFIG_SERIAL_MSM_CONSOLE
static struct msm_gpio uart1_config_data[] = {
	{ GPIO_CFG(UART1_RXD_GPIO, 1, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		"UART1_Rx"},
	{ GPIO_CFG(UART1_TXD_GPIO, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		"UART1_Tx"},
	{ GPIO_CFG(UART2_RXD_GPIO, 1, GPIO_CFG_INPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		"UART2_Rx"},
	{ GPIO_CFG(UART2_TXD_GPIO, 1, GPIO_CFG_OUTPUT, GPIO_CFG_PULL_DOWN, GPIO_CFG_2MA),
		"UART2_Tx"},
};

static void init_uart1(void)
{
	msm_gpios_request_enable(uart1_config_data,
				 ARRAY_SIZE(uart1_config_data));
}
#endif

void __init hexagon_dma_init(void);

static void __init amazon_init(void)
{
	void __iomem *tmp_clk;

	if (strcmp(mdesc->name,"amazon")) {
		printk("omg not amazon!!!\n");
		return 0;
	}

	/* Enable UART1 via CLK_CTL */
	tmp_clk = ioremap(CLK_CTL_PHYS, PAGE_SIZE);
	if (!tmp_clk)
		panic("ERROR:  problem mapping clock control\n");
	writel(UART_NS_REG__UART1_CLK_BRANCH_ENA___M,
		tmp_clk + UART1_NS_REG);
	iounmap(tmp_clk);

	hexagon_dma_init();

	/* Add our devices */
	platform_add_devices(devices, ARRAY_SIZE(devices));

	/* Setup GPIO address space */
	gpio_base = ioremap(MSM_TLMM_BASE, PAGE_SIZE);
	if (!gpio_base)
		panic("Error: Could not ioremap GPIO Base\n");

#ifdef CONFIG_QFEC
	/* initialize the Ethernet phy */
	phy_init();
#endif

	/* Initialize the UART */
	init_uart1();
}
arch_initcall(amazon_init);

static void __init amazon_clock_init(void)
{
	msm_clock_init(&amazon_clock_data);
}

//void __init amazon_setup_ops(struct platform_ops_t * ops)
//{
//        ops->clock_init = amazon_clock_init;
//	bootmem_lastpg = PFN_DOWN(0x1b000000);
//}

void setup_arch_platform_amazon()
{
	//  Where's the right place for the clock init?
	printk("%s\n",__FUNCTION__);
	//  Probably should treat this like a device or something instead of ifdef'ing it
#ifdef CONFIG_HEXAGON_ANGEL_TRAPS
	register_angel_console();
#endif
	amazon_clock_init();
	bootmem_lastpg = PFN_DOWN(0x1b000000);
}


MACHINE_START(COMET, "amazon")
	.setup_arch_platform = setup_arch_platform_amazon,
	.dt_compat = amazon_dt_compat
MACHINE_END


