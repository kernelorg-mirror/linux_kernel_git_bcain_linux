/*
 * linux/arch/hexagon/platform/msm8960_fluid/board.c
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
#include <linux/ks8851.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <asm/irq.h>
#include <linux/regulator/msm-gpio-regulator.h>
#include <linux/mfd/pm8xxx/pm8921.h>
#include <asm/platform.h>
#include <asm/angel_console.h>
#include <mach/gpio.h>
#include <mach/msm_spi.h>
#include <mach/msm_iomap.h>
#include <mach/irqs.h>
#include "clock.h"
#include <mach/rpm.h>
#include "board-8960.h"

#define KS8851_RST_GPIO         89
#define KS8851_IRQ_GPIO         90


struct msm_rpm_platform_data msm8960_rpm_data __initdata = {
	.reg_base_addrs = {
		[MSM_RPM_PAGE_STATUS] = MSM_RPM_BASE,
		[MSM_RPM_PAGE_CTRL] = MSM_RPM_BASE + 0x400,
		[MSM_RPM_PAGE_REQ] = MSM_RPM_BASE + 0x600,
		[MSM_RPM_PAGE_ACK] = MSM_RPM_BASE + 0xa00,
	},
	.irq_ack = RPM_LPASS_GP_HIGH_IRQ,
	.irq_err = RPM_LPASS_GP_LOW_IRQ,
	.irq_wakeup = RPM_LPASS_Q6SS_WAKE_UP_IRQ,
	.ipc_rpm_reg = MSM_APCS_GCC_BASE + 0x008,
	.ipc_rpm_val = 4,
	.target_id = {
		MSM_RPM_MAP(8960, NOTIFICATION_CONFIGURED_0, NOTIFICATION, 4),
		MSM_RPM_MAP(8960, NOTIFICATION_REGISTERED_0, NOTIFICATION, 4),
		MSM_RPM_MAP(8960, INVALIDATE_0, INVALIDATE, 8),
		MSM_RPM_MAP(8960, TRIGGER_TIMED_TO, TRIGGER_TIMED, 1),
		MSM_RPM_MAP(8960, TRIGGER_TIMED_SCLK_COUNT, TRIGGER_TIMED, 1),
		MSM_RPM_MAP(8960, RPM_CTL, RPM_CTL, 1),
		MSM_RPM_MAP(8960, CXO_CLK, CXO_CLK, 1),
		MSM_RPM_MAP(8960, PXO_CLK, PXO_CLK, 1),
		MSM_RPM_MAP(8960, APPS_FABRIC_CLK, APPS_FABRIC_CLK, 1),
		MSM_RPM_MAP(8960, SYSTEM_FABRIC_CLK, SYSTEM_FABRIC_CLK, 1),
		MSM_RPM_MAP(8960, MM_FABRIC_CLK, MM_FABRIC_CLK, 1),
		MSM_RPM_MAP(8960, DAYTONA_FABRIC_CLK, DAYTONA_FABRIC_CLK, 1),
		MSM_RPM_MAP(8960, SFPB_CLK, SFPB_CLK, 1),
		MSM_RPM_MAP(8960, CFPB_CLK, CFPB_CLK, 1),
		MSM_RPM_MAP(8960, MMFPB_CLK, MMFPB_CLK, 1),
		MSM_RPM_MAP(8960, EBI1_CLK, EBI1_CLK, 1),
		MSM_RPM_MAP(8960, APPS_FABRIC_CFG_HALT_0,
				APPS_FABRIC_CFG_HALT, 2),
		MSM_RPM_MAP(8960, APPS_FABRIC_CFG_CLKMOD_0,
				APPS_FABRIC_CFG_CLKMOD, 3),
		MSM_RPM_MAP(8960, APPS_FABRIC_CFG_IOCTL,
				APPS_FABRIC_CFG_IOCTL, 1),
		MSM_RPM_MAP(8960, APPS_FABRIC_ARB_0, APPS_FABRIC_ARB, 12),
		MSM_RPM_MAP(8960, SYS_FABRIC_CFG_HALT_0,
				SYS_FABRIC_CFG_HALT, 2),
		MSM_RPM_MAP(8960, SYS_FABRIC_CFG_CLKMOD_0,
				SYS_FABRIC_CFG_CLKMOD, 3),
		MSM_RPM_MAP(8960, SYS_FABRIC_CFG_IOCTL,
				SYS_FABRIC_CFG_IOCTL, 1),
		MSM_RPM_MAP(8960, SYSTEM_FABRIC_ARB_0,
				SYSTEM_FABRIC_ARB, 29),
		MSM_RPM_MAP(8960, MMSS_FABRIC_CFG_HALT_0,
				MMSS_FABRIC_CFG_HALT, 2),
		MSM_RPM_MAP(8960, MMSS_FABRIC_CFG_CLKMOD_0,
				MMSS_FABRIC_CFG_CLKMOD, 3),
		MSM_RPM_MAP(8960, MMSS_FABRIC_CFG_IOCTL,
				MMSS_FABRIC_CFG_IOCTL, 1),
		MSM_RPM_MAP(8960, MM_FABRIC_ARB_0, MM_FABRIC_ARB, 23),
		MSM_RPM_MAP(8960, PM8921_S1_0, PM8921_S1, 2),
		MSM_RPM_MAP(8960, PM8921_S2_0, PM8921_S2, 2),
		MSM_RPM_MAP(8960, PM8921_S3_0, PM8921_S3, 2),
		MSM_RPM_MAP(8960, PM8921_S4_0, PM8921_S4, 2),
		MSM_RPM_MAP(8960, PM8921_S5_0, PM8921_S5, 2),
		MSM_RPM_MAP(8960, PM8921_S6_0, PM8921_S6, 2),
		MSM_RPM_MAP(8960, PM8921_S7_0, PM8921_S7, 2),
		MSM_RPM_MAP(8960, PM8921_S8_0, PM8921_S8, 2),
		MSM_RPM_MAP(8960, PM8921_L1_0, PM8921_L1, 2),
		MSM_RPM_MAP(8960, PM8921_L2_0, PM8921_L2, 2),
		MSM_RPM_MAP(8960, PM8921_L3_0, PM8921_L3, 2),
		MSM_RPM_MAP(8960, PM8921_L4_0, PM8921_L4, 2),
		MSM_RPM_MAP(8960, PM8921_L5_0, PM8921_L5, 2),
		MSM_RPM_MAP(8960, PM8921_L6_0, PM8921_L6, 2),
		MSM_RPM_MAP(8960, PM8921_L7_0, PM8921_L7, 2),
		MSM_RPM_MAP(8960, PM8921_L8_0, PM8921_L8, 2),
		MSM_RPM_MAP(8960, PM8921_L9_0, PM8921_L9, 2),
		MSM_RPM_MAP(8960, PM8921_L10_0, PM8921_L10, 2),
		MSM_RPM_MAP(8960, PM8921_L11_0, PM8921_L11, 2),
		MSM_RPM_MAP(8960, PM8921_L12_0, PM8921_L12, 2),
		MSM_RPM_MAP(8960, PM8921_L13_0, PM8921_L13, 2),
		MSM_RPM_MAP(8960, PM8921_L14_0, PM8921_L14, 2),
		MSM_RPM_MAP(8960, PM8921_L15_0, PM8921_L15, 2),
		MSM_RPM_MAP(8960, PM8921_L16_0, PM8921_L16, 2),
		MSM_RPM_MAP(8960, PM8921_L17_0, PM8921_L17, 2),
		MSM_RPM_MAP(8960, PM8921_L18_0, PM8921_L18, 2),
		MSM_RPM_MAP(8960, PM8921_L19_0, PM8921_L19, 2),
		MSM_RPM_MAP(8960, PM8921_L20_0, PM8921_L20, 2),
		MSM_RPM_MAP(8960, PM8921_L21_0, PM8921_L21, 2),
		MSM_RPM_MAP(8960, PM8921_L22_0, PM8921_L22, 2),
		MSM_RPM_MAP(8960, PM8921_L23_0, PM8921_L23, 2),
		MSM_RPM_MAP(8960, PM8921_L24_0, PM8921_L24, 2),
		MSM_RPM_MAP(8960, PM8921_L25_0, PM8921_L25, 2),
		MSM_RPM_MAP(8960, PM8921_L26_0, PM8921_L26, 2),
		MSM_RPM_MAP(8960, PM8921_L27_0, PM8921_L27, 2),
		MSM_RPM_MAP(8960, PM8921_L28_0, PM8921_L28, 2),
		MSM_RPM_MAP(8960, PM8921_L29_0, PM8921_L29, 2),
		MSM_RPM_MAP(8960, PM8921_CLK1_0, PM8921_CLK1, 2),
		MSM_RPM_MAP(8960, PM8921_CLK2_0, PM8921_CLK2, 2),
		MSM_RPM_MAP(8960, PM8921_LVS1, PM8921_LVS1, 1),
		MSM_RPM_MAP(8960, PM8921_LVS2, PM8921_LVS2, 1),
		MSM_RPM_MAP(8960, PM8921_LVS3, PM8921_LVS3, 1),
		MSM_RPM_MAP(8960, PM8921_LVS4, PM8921_LVS4, 1),
		MSM_RPM_MAP(8960, PM8921_LVS5, PM8921_LVS5, 1),
		MSM_RPM_MAP(8960, PM8921_LVS6, PM8921_LVS6, 1),
		MSM_RPM_MAP(8960, PM8921_LVS7, PM8921_LVS7, 1),
		MSM_RPM_MAP(8960, NCP_0, NCP, 2),
		MSM_RPM_MAP(8960, CXO_BUFFERS, CXO_BUFFERS, 1),
		MSM_RPM_MAP(8960, USB_OTG_SWITCH, USB_OTG_SWITCH, 1),
		MSM_RPM_MAP(8960, HDMI_SWITCH, HDMI_SWITCH, 1),
		MSM_RPM_MAP(8960, DDR_DMM_0, DDR_DMM, 2),
		MSM_RPM_MAP(8960, QDSS_CLK, QDSS_CLK, 1),
	},
	.target_status = {
		MSM_RPM_STATUS_ID_MAP(8960, VERSION_MAJOR),
		MSM_RPM_STATUS_ID_MAP(8960, VERSION_MINOR),
		MSM_RPM_STATUS_ID_MAP(8960, VERSION_BUILD),
		MSM_RPM_STATUS_ID_MAP(8960, SUPPORTED_RESOURCES_0),
		MSM_RPM_STATUS_ID_MAP(8960, SUPPORTED_RESOURCES_1),
		MSM_RPM_STATUS_ID_MAP(8960, SUPPORTED_RESOURCES_2),
		MSM_RPM_STATUS_ID_MAP(8960, RESERVED_SUPPORTED_RESOURCES_0),
		MSM_RPM_STATUS_ID_MAP(8960, SEQUENCE),
		MSM_RPM_STATUS_ID_MAP(8960, RPM_CTL),
		MSM_RPM_STATUS_ID_MAP(8960, CXO_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, PXO_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, APPS_FABRIC_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, SYSTEM_FABRIC_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, MM_FABRIC_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, DAYTONA_FABRIC_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, SFPB_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, CFPB_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, MMFPB_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, EBI1_CLK),
		MSM_RPM_STATUS_ID_MAP(8960, APPS_FABRIC_CFG_HALT),
		MSM_RPM_STATUS_ID_MAP(8960, APPS_FABRIC_CFG_CLKMOD),
		MSM_RPM_STATUS_ID_MAP(8960, APPS_FABRIC_CFG_IOCTL),
		MSM_RPM_STATUS_ID_MAP(8960, APPS_FABRIC_ARB),
		MSM_RPM_STATUS_ID_MAP(8960, SYS_FABRIC_CFG_HALT),
		MSM_RPM_STATUS_ID_MAP(8960, SYS_FABRIC_CFG_CLKMOD),
		MSM_RPM_STATUS_ID_MAP(8960, SYS_FABRIC_CFG_IOCTL),
		MSM_RPM_STATUS_ID_MAP(8960, SYSTEM_FABRIC_ARB),
		MSM_RPM_STATUS_ID_MAP(8960, MMSS_FABRIC_CFG_HALT),
		MSM_RPM_STATUS_ID_MAP(8960, MMSS_FABRIC_CFG_CLKMOD),
		MSM_RPM_STATUS_ID_MAP(8960, MMSS_FABRIC_CFG_IOCTL),
		MSM_RPM_STATUS_ID_MAP(8960, MM_FABRIC_ARB),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S1_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S1_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S2_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S2_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S3_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S3_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S4_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S4_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S5_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S5_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S6_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S6_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S7_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S7_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S8_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_S8_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L1_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L1_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L2_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L2_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L3_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L3_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L4_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L4_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L5_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L5_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L6_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L6_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L7_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L7_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L8_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L8_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L9_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L9_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L10_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L10_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L11_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L11_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L12_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L12_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L13_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L13_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L14_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L14_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L15_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L15_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L16_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L16_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L17_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L17_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L18_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L18_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L19_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L19_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L20_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L20_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L21_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L21_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L22_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L22_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L23_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L23_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L24_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L24_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L25_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L25_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L26_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L26_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L27_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L27_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L28_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L28_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L29_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_L29_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_CLK1_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_CLK1_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_CLK2_0),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_CLK2_1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS1),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS2),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS3),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS4),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS5),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS6),
		MSM_RPM_STATUS_ID_MAP(8960, PM8921_LVS7),
		MSM_RPM_STATUS_ID_MAP(8960, NCP_0),
		MSM_RPM_STATUS_ID_MAP(8960, NCP_1),
		MSM_RPM_STATUS_ID_MAP(8960, CXO_BUFFERS),
		MSM_RPM_STATUS_ID_MAP(8960, USB_OTG_SWITCH),
		MSM_RPM_STATUS_ID_MAP(8960, HDMI_SWITCH),
		MSM_RPM_STATUS_ID_MAP(8960, DDR_DMM_0),
		MSM_RPM_STATUS_ID_MAP(8960, DDR_DMM_1),
		MSM_RPM_STATUS_ID_MAP(8960, EBI1_CH0_RANGE),
		MSM_RPM_STATUS_ID_MAP(8960, EBI1_CH1_RANGE),
	},
	.target_ctrl_id = {
		MSM_RPM_CTRL_MAP(8960, VERSION_MAJOR),
		MSM_RPM_CTRL_MAP(8960, VERSION_MINOR),
		MSM_RPM_CTRL_MAP(8960, VERSION_BUILD),
		MSM_RPM_CTRL_MAP(8960, REQ_CTX_0),
		MSM_RPM_CTRL_MAP(8960, REQ_SEL_0),
		MSM_RPM_CTRL_MAP(8960, ACK_CTX_0),
		MSM_RPM_CTRL_MAP(8960, ACK_SEL_0),
	},
	.sel_invalidate = MSM_RPM_8960_SEL_INVALIDATE,
	.sel_notification = MSM_RPM_8960_SEL_NOTIFICATION,
	.sel_last = MSM_RPM_8960_SEL_LAST,
	.ver = {3, 0, 0},
};

struct platform_device msm8960_rpm_device = {
	.name   = "msm_rpm",
	.id     = -1,
};
#if 0
static struct msm_rpm_log_platform_data msm_rpm_log_pdata = {
	.phys_addr_base = 0x0010C000,
	.reg_offsets = {
		[MSM_RPM_LOG_PAGE_INDICES] = 0x00000080,
		[MSM_RPM_LOG_PAGE_BUFFER]  = 0x000000A0,
	},
	.phys_size = SZ_8K,
	.log_len = 4096,		  /* log's buffer length in bytes */
	.log_len_mask = (4096 >> 2) - 1,  /* length mask in units of u32 */
};

struct platform_device msm8960_rpm_log_device = {
	.name	= "msm_rpm_log",
	.id	= -1,
	.dev	= {
		.platform_data = &msm_rpm_log_pdata,
	},
};

static struct msm_rpmstats_platform_data msm_rpm_stat_pdata = {
	.phys_addr_base = 0x0010D204,
	.phys_size = SZ_8K,
};

struct platform_device msm8960_rpm_stat_device = {
	.name = "msm_rpm_stat",
	.id = -1,
	.dev = {
		.platform_data = &msm_rpm_stat_pdata,
	},
};
#endif


static const char *msm8960_fluid_dt_compat[] __initconst = {
	"qcom,msm8960_fluid",
	NULL
};

static struct msm_spi_platform_data msm8960_qup_spi_gsbi1_pdata = {
        .max_clock_speed = 15060000,
};

struct of_device_id platform_of_irq_matches[] __initdata  = {
	{ .compatible = "qcom,msm-gpio", .data = msm_gpio_of_init, },
        { .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};


//  Aux data for devtree population
static struct of_dev_auxdata msm8960_auxdata_lookup[] = {
	{}
};

static struct resource resources_qup_spi_gsbi1[] = {
	{
		.name   = "spi_base",
		.start  = MSM_GSBI1_QUP_PHYS,
		.end    = MSM_GSBI1_QUP_PHYS + SZ_4K - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "gsbi_base",
		.start  = MSM_GSBI1_PHYS,
		.end    = MSM_GSBI1_PHYS + 4 - 1,
		.flags  = IORESOURCE_MEM,
	},
	{
		.name   = "spi_irq_in",
		.start  = MSM8960_GSBI1_QUP_IRQ,
		.end    = MSM8960_GSBI1_QUP_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.name   = "spi_clk",
		.start  = 9,
		.end    = 9,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "spi_miso",
		.start  = 7,
		.end    = 7,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "spi_mosi",
		.start  = 6,
		.end    = 6,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "spi_cs",
		.start  = 8,
		.end    = 8,
		.flags  = IORESOURCE_IO,
	},
	{
		.name   = "spi_cs1",
		.start  = 14,
		.end    = 14,
		.flags  = IORESOURCE_IO,
	},
};


struct platform_device msm8960_device_qup_spi_gsbi1 = {
	.name   = "spi_qsd",
	.id     = 0,
	.num_resources  = ARRAY_SIZE(resources_qup_spi_gsbi1),
	.resource       = resources_qup_spi_gsbi1,
};

//  Why is IRQ_GPIO defined in two places?  Is this for the TLMM configuration?
static struct ks8851_pdata spi_eth_pdata = {
	.irq_gpio = KS8851_IRQ_GPIO,
	.rst_gpio = KS8851_RST_GPIO,
};

static struct spi_board_info spi_board_info[] __initdata = {
	{
		.modalias               = "ks8851",
		//This really should be pulled from devicetree/irqdomains.  Figure out where it's used...
		//.irq                    = MSM_GPIO_TO_INT(KS8851_IRQ_GPIO),
		.max_speed_hz           = 19200000,
		.bus_num                = 0,
		.chip_select            = 0,
		.mode                   = SPI_MODE_0,
		.platform_data          = &spi_eth_pdata
	},
#if 0
	{
		.modalias               = "dsi_novatek_3d_panel_spi",
		.max_speed_hz           = 10800000,
		.bus_num                = 0,
		.chip_select            = 1,
		.mode                   = SPI_MODE_0,
	},
#endif
};

static struct platform_device *devices[] __initdata = {
	&msm8960_device_qup_spi_gsbi1,
};

//  This is kind of a hack.  We need to populate the KS8851's irq entry when
//  we register it.  However we don't know the Linux IRQ, which is handled
//  by irqdomain.   It's setup in gpio-msm-common.c, but we need to get at it
//  here.

struct irq_domain *gpio_irqdomain;

static int __init msm8960_fluid_init(void)
{
	if (strcmp(mdesc->name,"msm8960_fluid")) {
		printk("omg not msm8960_fluid!!!\n");
		return 0;
	}

	/*  make the clock go ludicrous speed  */
	{
		unsigned int i;
		volatile unsigned int *lpass_csr_base = (void *) 0x28000000;
		volatile unsigned int *lpass_pub_base = (void *) 0x28800000;
		#define LPASS_GFMUX_CTL (0x30 >> 2)

		lpass_csr_base = ioremap(0x28000000, PAGE_SIZE);  //  Might actually want to make this 4k instead to avoid trampling
		lpass_pub_base = ioremap(0x28800000, PAGE_SIZE);

		if (!lpass_csr_base || !lpass_pub_base)
			panic("at the disco!\n");

		printk("Set Up Clocks\n");

		#define LCC_PLL0_MODE		(0x00 >> 2)
		#define LCC_PLL0_L_VAL		(0x04 >> 2)
		#define LCC_PLL0_M_VAL		(0x08 >> 2)
		#define LCC_PLL0_N_VAL		(0x0c >> 2)
		#define LCC_PLL0_TEST_CTL	(0x10 >> 2)
		#define LCC_PLL0_CONFIG		(0x14 >> 2)
		#define LCC_PLL0_STATUS		(0x18 >> 2)

#if 0
		//  400MHz
		lpass_csr_base[LCC_PLL0_L_VAL]		= 0x0000000e;
		lpass_csr_base[LCC_PLL0_M_VAL]		= 0x0000027a;
		lpass_csr_base[LCC_PLL0_N_VAL]		= 0x00000465;
		lpass_csr_base[LCC_PLL0_CONFIG]		= 0x00c00000;
		lpass_csr_base[LCC_PLL0_TEST_CTL]	= 0x00000000;
#endif
		lpass_csr_base[LCC_PLL0_L_VAL]		= 0x00000012;
		lpass_csr_base[LCC_PLL0_M_VAL]		= 0x00000001;
		lpass_csr_base[LCC_PLL0_N_VAL]		= 0x00000002;
		lpass_csr_base[LCC_PLL0_CONFIG]		= 0x00c00000;
		lpass_csr_base[LCC_PLL0_TEST_CTL]	= 0x00000000;



		lpass_csr_base[LCC_PLL0_MODE] = 0;
		for (i=0; i<100; i++);
		lpass_csr_base[LCC_PLL0_MODE] = 6;
		lpass_csr_base[LCC_PLL0_MODE] = 7;
		//  is anyone going to be surprised that this doesn't work?
		//printk("checking for 'lock_detect'\n");
		//if (!((lpass_csr_base[LCC_PLL0_STATUS] >> 16) & 1)) {
		//	printk("fail.  clock, you fail at life.\n");
		//}

		lpass_pub_base[LPASS_GFMUX_CTL] |= (1<<9) | (1<<2);  //  What's clk src B?  AWESOME DOCS!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
		printk("ss_pub_base[LPASS_GFMUX_CTL] = 0x%08x\n", lpass_pub_base[LPASS_GFMUX_CTL]);

		iounmap(lpass_pub_base);
		iounmap(lpass_csr_base);
	}

	/*
		maps all the IO into the page table using the big set of defines
		from msm_iomap.h and friends.
	 */
	msm_map_msm8960_io();

	BUG_ON(msm_rpm_init(&msm8960_rpm_data));
	msm_clock_init(&msm8960_clock_init_data);

	/*
		this actually walks through the device data and sets the specific
		GPIO requirements like signal pulling, drive strength, and other
		functions
	 */
	msm8960_init_gpiomux();

	msm8960_device_qup_spi_gsbi1.dev.platform_data =
			&msm8960_qup_spi_gsbi1_pdata;

	spi_board_info[0].irq = irq_find_mapping(gpio_irqdomain, KS8851_IRQ_GPIO);

	spi_register_board_info(spi_board_info, ARRAY_SIZE(spi_board_info));

	platform_add_devices(devices, ARRAY_SIZE(devices));

	of_platform_populate(of_find_node_by_path("/soc"),
	of_default_bus_match_table, msm8960_auxdata_lookup, NULL);
	/*
		msm_gpio_init() is run at postcore_initcall level
		that runs msm_gpio_probe().  like a proper driver.

		sets up msm_gpio, adds it as chip
		sets up IRQ cascade for summary handler

		doesn't seem to do anything else

	*/

	return 0;
}

/*
 * Bypass all the machine garbage and stuff this right into initcalls.
 */
core_initcall(msm8960_fluid_init);


void setup_arch_platform_msm8960_fluid()
{
	printk("Platform:  MDP 8960\n");

#ifdef CONFIG_HEXAGON_ANGEL_TRAPS
	on_simulator=1;
	register_angel_console();
#endif
	/*  Can be overridden via boot args  */
	bootmem_lastpg = PFN_DOWN(1<<29);
}

MACHINE_START(MSM8960_FLUID, "msm8960_fluid")
	.setup_arch_platform = setup_arch_platform_msm8960_fluid,
	.dt_compat = msm8960_fluid_dt_compat
MACHINE_END


