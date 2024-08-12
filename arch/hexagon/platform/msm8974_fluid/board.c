/*
 * Copyright (c) 2013,2015 The Linux Foundation. All rights reserved.
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

#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/platform.h>

static const char *msm8974_fluid_dt_compat[] __initconst = {
	"qcom,msm8974_fluid",
	NULL
};

struct of_device_id platform_of_irq_matches[] __initdata  = {
        { .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};

static struct platform_device *devices[] __initdata = {
};

static int __init msm8974_fluid_init(void)
{
	unsigned int i;

	if (strcmp(mdesc->name,"msm8974_fluid")) {
		return 0;
	}

	{
		volatile unsigned int *lpass_pub_base;
		volatile unsigned int *lpass_lpaq6_base;

		//  Slap the PLL around.  It likes it.
		#define LPASS_BASE		0xfe000000
		#define LPASS_LPAQ6_BASE	(LPASS_BASE+0x1000)
		#define LPASS_PUB_BASE		(LPASS_BASE+0x200000)

		//  should probably use sizeof(whatever)
		#define LPASS_LPAQ6_PLL_MODE            (0x000 >> 2)
		#define LPASS_LPAQ6_PLL_L               (0x004 >> 2)
		#define LPASS_LPAQ6_PLL_M               (0x008 >> 2)
		#define LPASS_LPAQ6_PLL_N               (0x00c >> 2)
		#define LPASS_LPAQ6_PLL_USER_CTL        (0x010 >> 2)
		#define LPASS_LPAQ6_PLL_CONFIG_CTL      (0x014 >> 2)
		#define LPASS_LPAQ6_PLL_TEST_CTL        (0x018 >> 2)
		#define LPASS_LPAQ6_PLL_STATUS          (0x01c >> 2)

		lpass_lpaq6_base = ioremap(LPASS_LPAQ6_BASE, PAGE_SIZE);	//  consider restricting to 4k
		lpass_pub_base = ioremap(LPASS_PUB_BASE, PAGE_SIZE);		//  consider restricting to 4k

		if (!lpass_lpaq6_base || !lpass_pub_base) {
			panic("lpass ioremap error\n");
		}

		//  Frequency is CXO * (L + M/N) / 2; CXO is 19.2MHz
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_MODE]		= 0x00000000;
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_L]		= 0x00000053;  // back down to about 758MHz and see if things settle down...
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_M]		= 0x00000000;
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_N]		= 0x00000003;
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_CONFIG_CTL] 	= 0x00341600;
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_USER_CTL]	= 0x0000010f;
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_TEST_CTL]	= 0x00000000;

		lpass_lpaq6_base[LPASS_LPAQ6_PLL_MODE]		= 0x00000006;
		for (i=0; i<100; i++) {}
		lpass_lpaq6_base[LPASS_LPAQ6_PLL_MODE]		= 0x00000007;

		//  Slap the GFMUX around.  It likes it.
		#define GFMUX_SRC_SHIFT		2
		#define LPASS_QDSP6SS_GFMUX_CTL	(0x20 >> 2)
		lpass_pub_base[LPASS_QDSP6SS_GFMUX_CTL] |= (1<<GFMUX_SRC_SHIFT);

		iounmap(lpass_pub_base);
		iounmap(lpass_lpaq6_base);
	}

#ifdef CONFIG_HEXAGON_MSS
	{
		volatile unsigned int *tcsr_phss_regs = ioremap(0xfd4ab000, PAGE_SIZE);
		volatile unsigned int *mss_pub_base = ioremap(0xfc880000, PAGE_SIZE);
		volatile unsigned int *mss_periph_base = ioremap(0xfc981000, PAGE_SIZE);
		u32 temp;

		if (!tcsr_phss_regs || !mss_pub_base || !mss_periph_base) {
			panic("mss ioremap error\n");
		}
		//  enable the blsp1_spi_whatever to trigger the MSS's summary interrupt
		tcsr_phss_regs[0x140 >> 2] = 0x1;

		//  Looks like out of PBL, MSS is running off of clock source A, which might be PLL1

		//  Let's try to set up MPLL2.  All 3 other GFMUX inputs MPLL2 early output
		//  MPLL2 is an SR-type PLL.  Go with the slower one first - 36:0:1:1
		#define MSS_MPLL2_MODE		(0x40 >> 2)
		#define MSS_MPLL2_L_VAL		(0x44 >> 2)
		#define MSS_MPLL2_M_VAL		(0x48 >> 2)
		#define MSS_MPLL2_N_VAL		(0x4C >> 2)
		#define MSS_MPLL2_USER_CTL	(0x50 >> 2)
		#define MSS_MPLL2_CONFIG_CTL	(0x54 >> 2)
		#define MSS_MPLL2_TEST_CTL	(0x58 >> 2)
		#define MSS_MPLL2_STATUS	(0x5C >> 2)

		//  Frequency is CXO * (L + M/N) / 2; CXO is 19.2MHz
		mss_periph_base[MSS_MPLL2_MODE]		= 0x00000000;
		mss_periph_base[MSS_MPLL2_L_VAL]	= 0x0000002a;
		mss_periph_base[MSS_MPLL2_M_VAL]	= 0x00000000;
		mss_periph_base[MSS_MPLL2_N_VAL]	= 0x00000001;
		mss_periph_base[MSS_MPLL2_CONFIG_CTL] 	= 0x00341600;
		mss_periph_base[MSS_MPLL2_USER_CTL]	= 0x0000000f;
		mss_periph_base[MSS_MPLL2_TEST_CTL]	= 0x00000000;

		mss_periph_base[MSS_MPLL2_MODE]		|= 0x00000006;
		while(!(mss_periph_base[MSS_MPLL2_STATUS] & (1<<16))) {
			printk("waiting for PLL2 lock 0x%08x\n", mss_periph_base[MSS_MPLL2_STATUS] );
		}
		mss_periph_base[MSS_MPLL2_MODE]		|= 0x00000001;

		#define GFMUX_SRC_SHIFT 2
		#define MSS_QDSP6SS_GFMUX_CTL (0x20 >> 2)

		printk("mss_qdsp6ss_gfmux_ctl = 0x%08x\n", mss_pub_base[MSS_QDSP6SS_GFMUX_CTL]);
		temp = mss_pub_base[MSS_QDSP6SS_GFMUX_CTL];
		temp &= ~(0x3 << GFMUX_SRC_SHIFT);
		temp |= (0x1 << GFMUX_SRC_SHIFT);

		mss_pub_base[MSS_QDSP6SS_GFMUX_CTL] = temp;

		iounmap(tcsr_phss_regs);
		iounmap(mss_pub_base);
		iounmap(mss_periph_base);
	}

#endif

	hexagon_dma_init();

        of_platform_populate(of_find_node_by_path("/soc"),
		of_default_bus_match_table, NULL, NULL);

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 */
core_initcall(msm8974_fluid_init);

void setup_arch_platform_msm8974_fluid(void)
{
	printk("Platform:  MDP 8974\n");

	bootmem_lastpg = PFN_DOWN(1<<29);
}

MACHINE_START(MSM8974_FLUID, "msm8974_fluid")
	.setup_arch_platform = setup_arch_platform_msm8974_fluid,
	.dt_compat = msm8974_fluid_dt_compat
MACHINE_END

