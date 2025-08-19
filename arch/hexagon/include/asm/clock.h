/*
 * MSM architecture clock driver header
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2007-2011, Code Aurora Forum
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ARCH_ARM_MACH_MSM_CLOCK_H
#define __ARCH_ARM_MACH_MSM_CLOCK_H

#include <linux/list.h>
/*
 * High level clock API support
 */
struct module;
#define CLKFLAG_INVERT   0x00000001 /* invert the clock */
#define CLKFLAG_NOINVERT 0x00000002 /* do not invert */
#define CLKFLAG_NONEST   0x00000004 /* disable nesting of enable/disable */
#define CLKFLAG_NORESET  0x00000008 /* Prohibit resetting clock */

#define CLK_FIRST_AVAILABLE_FLAG 0x00000100
#define CLKFLAG_USE_MIN_TO_SET   0x00000200
#define CLKFLAG_USE_MAX_TO_SET   0x00000400
#define CLKFLAG_USE_NO_SET       0x00000800
#define CLKFLAG_AUTO_OFF         0x00001000

struct clk {
	spinlock_t              lock;
	uint32_t                id;
	uint32_t                count;
	const char              *name;
	uint32_t                flags;
	struct list_head        list;
	struct clk		*parent;
	struct module           *owner;
};

#define A11S_CLK_CNTL_ADDR (MSM_CSR_BASE + 0x100)
#define A11S_CLK_SEL_ADDR (MSM_CSR_BASE + 0x104)
#define A11S_VDD_SVS_PLEVEL_ADDR (MSM_CSR_BASE + 0x124)

#define SPSS_CLK_CNTL_ADDR (MSM_CSR_BASE + 0x100)
#define SPSS_CLK_SEL_ADDR (MSM_CSR_BASE + 0x104)

#define PLLn_L_VAL(n)	(MSM_CLK_CTL_BASE + 0x304 + 28 * (n))

/* Scorpion PLL registers */
#define PLL_CTL_ADDR         (MSM_SCPLL_BASE + 0x4)
#define PLL_STATUS_ADDR      (MSM_SCPLL_BASE + 0x18)
#define PLL_FSM_CTL_EXT_ADDR (MSM_SCPLL_BASE + 0x10)

/*
 * Supported clocks
 */
#define ACPU_CLK           0  /* Applications processor clock */
#define ADM_CLK            1  /* Applications data mover clock */
#define ADSP_CLK           2  /* ADSP clock */
#define EBI1_CLK           3  /* External bus interface 1 clock */
#define EBI2_CLK           4  /* External bus interface 2 clock */
#define ECODEC_CLK         5  /* External CODEC clock */
#define EMDH_CLK           6  /* External MDDI host clock */
#define GP_CLK             7  /* General purpose clock */
#define GRP_CLK            8  /* Graphics clock */
#define I2C_CLK            9  /* I2C clock */
#define ICODEC_RX_CLK     10  /* Internal CODEX RX clock */
#define ICODEC_TX_CLK     11  /* Internal CODEX TX clock */
#define IMEM_CLK          12  /* Internal graphics memory clock */
#define MDC_CLK           13  /* MDDI client clock */
#define MDP_CLK           14  /* Mobile display processor clock */
#define PBUS_CLK          15  /* Peripheral bus clock */
#define PCM_CLK           16  /* PCM clock */
#define PMDH_CLK          17  /* Primary MDDI host clock */
#define SDAC_CLK          18  /* Stereo DAC clock */
#define SDC1_CLK          19  /* Secure Digital Card clocks */
#define SDC1_PCLK         20
#define SDC2_CLK          21
#define SDC2_PCLK         22
#define SDC3_CLK          23
#define SDC3_PCLK         24
#define SDC4_CLK          25
#define SDC4_PCLK         26
#define TSIF_CLK          27  /* Transport Stream Interface clocks */
#define TSIF_REF_CLK      28
#define TV_DAC_CLK        29  /* TV clocks */
#define TV_ENC_CLK        30
#define UART1_CLK         31  /* UART clocks */
#define UART2_CLK         32
#define UART3_CLK         33
#define UART1DM_CLK       34
#define UART2DM_CLK       35
#define USB_HS_CLK        36  /* High speed USB core clock */
#define USB_HS_PCLK       37  /* High speed USB pbus clock */
#define USB_OTG_CLK       38  /* Full speed USB clock */
#define VDC_CLK           39  /* Video controller clock */
#define VFE_CLK           40  /* Camera / Video Front End clock */
#define VFE_MDC_CLK       41  /* VFE MDDI client clock */

#define MDP_LCDC_PCLK_CLK 42
#define MDP_LCDC_PAD_PCLK_CLK 43
#define MDP_VSYNC_CLK     44

#define SPI_CLK           45
#define VFE_AXI_CLK       46

#ifdef CONFIG_HEXAGON_COMET
#define NR_CLOCKS	3
#endif

/*  Pretty much bogus  */
#ifdef CONFIG_HEXAGON_SIMULATOR
#define NR_CLOCKS	3
#endif

#ifdef CONFIG_HEXAGON_AMAZON
#define NR_CLOCKS	3
#endif

/*  Man we gotta put a stop to this.  */
#ifndef NR_CLOCKS
#define NR_CLOCKS	3
#endif


#define CLOCK(clk_name, clk_id, clk_flags) {	\
	.name = clk_name, \
	.owner = THIS_MODULE, \
	.id = clk_id, \
	.flags = clk_flags, \
	}

#define OFF CLKFLAG_AUTO_OFF
#define MIN_SET CLKFLAG_USE_MIN_TO_SET
#define MAX_SET CLKFLAG_USE_MAX_TO_SET
#define NO_SET  CLKFLAG_USE_NO_SET
#define MIN_MAX (MIN_SET | MAX_SET)

/*
 * ARM11 clock configuration for specific ACPU speeds
 */

#define ACPU_PLL_TCXO	-1
#define ACPU_PLL_0	0
#define ACPU_PLL_1	1
#define ACPU_PLL_2	2
#define ACPU_PLL_3	3

struct clkctl_acpu_speed {
	unsigned int     use_for_scaling;
	unsigned int     a11clk_khz;
	int              pll;
	unsigned int     a11clk_src_sel;
	unsigned int     a11clk_src_div;
	unsigned int     ahbclk_khz;
	unsigned int     ahbclk_div;
#if defined(CONFIG_ARCH_QSD)
	unsigned int     sc_core_src_sel_mask;
	unsigned int     sc_l_value;
#else
	unsigned int     ebi1clk_khz;
#endif
	int              vdd;
};

struct msm_clock_platform_data {
	uint32_t acpu_switch_time_us;
	uint32_t max_speed_delta_khz;
	uint32_t vdd_switch_time_us;
	struct clk *active_clocks;
	int active_clock_count;
};

extern struct clkctl_acpu_speed acpu_freq_tbl[];

void __init msm_clock_init(struct msm_clock_platform_data *);
int clk_register(struct clk *);


#endif
