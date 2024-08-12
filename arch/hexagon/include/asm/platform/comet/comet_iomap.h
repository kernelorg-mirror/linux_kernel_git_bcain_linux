/*
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef COMET_IOMAP_H
#define COMET_IOMAP_H

/*
 * The "machine" io starts at UART1, but UART3 is the one that's signed up
 * for console.
 *
 * If you change UART3, you need to change hexagon_cosimcfg for
 * software simulation.
 */

#define EBI1_CS_N1_TOP		0x30000000

#define SMC911X_PHYS		0x84000000
#define AUDIO_PHYS		0xa0700000
#define SSBI_PHYS		0xa8100000
#define CLK_CTL_PHYS		0xa8600000

#define TLMMADDR_GPIO1_BASE		0xa8e00000
#define TLMMADDR_GPIO2_BASE		0xa8f00000
#define TLMMADDR_GPIO1SHDW1_BASE	0xa9000000
#define TLMMADDR_GPIO2SHDW1_BASE	0xa9100000

#define MSM_UART1_PHYS		0xA9A00000
#define MSM_UART1_SIZE		(1<<12)

#define MSM_UART2_PHYS		0xA9B00000
#define MSM_UART2_SIZE		(1<<12)

#define MSM_UART3_PHYS		0xA9C00000
#define MSM_UART3_SIZE		(1<<12)

#define ADSP6_SIRC		0xab010000

#define MSS_PERIPH_PHYS		0xb8000000

/* Second-level interrupt registers */
/* These all sit on thier own 4K page */
/* These are relative to ADSP6_SIRC */
#define HEXSS_SIRC0_INT_ENABLE		0x0000
#define HEXSS_SIRC0_INT_ENABLE_CLEAR	0x0004
#define HEXSS_SIRC0_INT_ENABLE_SET	0x0008
#define HEXSS_SIRC0_INT_TYPE		0x000C
#define HEXSS_SIRC0_INT_POLARITY	0x0010
#define HEXSS_SIRC0_IRQ_STATUS		0x0014
#define HEXSS_SIRC0_INT_CLEAR		0x0018
#define HEXSS_SIRC0_SOFT_INT		0x001C
#define HEXSS_SIRC0_IRQ_PENDING		0x0020
#define HEXSS_SIRC1_INT_ENABLE		0x0400
#define HEXSS_SIRC1_INT_ENABLE_CLEAR	0x0404
#define HEXSS_SIRC1_INT_ENABLE_SET	0x0408
#define HEXSS_SIRC1_INT_TYPE		0x040C
#define HEXSS_SIRC1_INT_POLARITY	0x0410
#define HEXSS_SIRC1_IRQ_STATUS		0x0414
#define HEXSS_SIRC1_INT_CLEAR		0x0418
#define HEXSS_SIRC1_SOFT_INT		0x041C
#define HEXSS_SIRC1_IRQ_PENDING		0x0420

/* QSD8x50 ADSP6 Level One Interrupt Mapping */
#define L1_ADSP6_ETM		0
#define L1_ADSP6_ISDB		1
#define L1_ADSP6_MPRPH		2
#define L1_ADSP6_RGPTIMER	3
#define L1_ADSP6_UGPTIMER	4
#define L1_ADSP6_IPC0		5
#define L1_ADSP6_IPC1		6
#define L1_ADSP6_IPC2		7
#define L1_ADSP6_IPC3		8
#define L1_ADSP6_IPC4		9
#define L1_ADSP6_IPC5		10
#define L1_ADSP6_SPSS		11
#define L1_ADSP6_AUDIO		12
#define L1_ADSP6_VIDEO_FE	13
#define L1_ADSP6_ADM		14
#define L1_ADSP6_VIDEO_ENC	15
#define L1_ADSP6_VIDEO_DEC	16
#define L1_ADSP6_GRAPHICS	17
#define L1_ADSP6_MDP		18
#define L1_ADSP6_MDSP4		19
#define L1_ADSP6_MARM_FIQ	20
#define L1_ADSP6_MARM_IRQ	21
#define L1_ADSP6_MARM_RESET	22
#define L1_ADSP6_SIRC0		23
#define L1_ADSP6_SIRC1		24
#define L1_ADSP6_AVS_DONE	25

/* Max number of interrupts per secondary controller */
#define L2_GROUP_SIZE		32

/* QSD8x50 ADSP6 Level Two Group Zero Interrupt Mapping */
#define L2_SIRC0_EBI1			0
#define L2_SIRC0_IMEM			1
#define L2_SIRC0_SMI			2
#define L2_SIRC0_AXI			3
#define L2_SIRC0_PBUS_ERROR		4
#define L2_SIRC0_TV_ENC			5
#define L2_SIRC0_EBI2_OP_DONE		6
#define L2_SIRC0_EBI2_WR_ER_DONE	7
#define L2_SIRC0_A0_PEN			15
#define L2_SIRC0_CRYPTO			16
#define L2_SIRC0_GPIO_GROUP2		18
#define L2_SIRC0_GPIO_GROUP1		19
#define L2_SIRC0_MDDI_EXTERN		20
#define L2_SIRC0_MDDI_PRIMARY		21
#define L2_SIRC0_MDDI_CLIENT		22
#define L2_SIRC0_TCHSCRN_I2C		23
#define L2_SIRC0_TCHSCRN_SRC1		24
#define L2_SIRC0_TCHSCRN_SRC2		25
#define L2_SIRC0_TCHSCRN_SSBI		26

/* QSD8x50 ADSP6 Level Two Group One Interrupt Mapping */
#define L2_SIRC1_USB_FS1	0
#define L2_SIRC1_USB_HS		1
#define L2_SIRC1_USB_FS2	2
#define L2_SIRC1_SDC1_0		3
#define L2_SIRC1_SDC1_1		4
#define L2_SIRC1_SDC2_0		5
#define L2_SIRC1_SDC2_1		6
#define L2_SIRC1_SDC3_0		7
#define L2_SIRC1_SDC3_1		8
#define L2_SIRC1_SDC4_0		9
#define L2_SIRC1_SDC4_1		10
#define L2_SIRC1_SPI_OUT	11
#define L2_SIRC1_SPI_IN		12
#define L2_SIRC1_SPI_ERR	13
#define L2_SIRC1_UART1		14
#define L2_SIRC1_UART1_RX	15
#define L2_SIRC1_UART2		16
#define L2_SIRC1_UART2_RX	17
#define L2_SIRC1_UART3		18
#define L2_SIRC1_UART3_RX	19
#define L2_SIRC1_UARTDM1	20
#define L2_SIRC1_UARTDM1_RX	21
#define L2_SIRC1_UARTDM2	22
#define L2_SIRC1_UARTDM2_RX	23
#define L2_SIRC1_TSIF		24

/* GPIO register offsets. These are relative to TLMMADDR_GPIO1_BASE */
#define GPIO_OUT_0		0x0000
#define GPIO_OUT_2		0x0004
#define GPIO_OUT_3		0x0008
#define GPIO_OUT_4		0x000C
#define GPIO_OUT_5		0x0010
#define GPIO_OUT_6		0x0014
#define GPIO_OUT_7		0x0018
#define GPIO_OE_0		0x0020
#define GPIO_OE_2		0x0024
#define GPIO_OE_3		0x0028
#define GPIO_OE_4		0x002C
#define GPIO_OE_5		0x0030
#define GPIO_OE_6		0x0034
#define GPIO_OE_7		0x0038
#define GPIO1_PAGE		0x0040
#define GPIO1_CFG		0x0044
#define GPIO_IN_0		0x0050
#define GPIO_IN_2		0x0054
#define GPIO_IN_3		0x0058
#define GPIO_IN_4		0x005C
#define GPIO_IN_5		0x0060
#define GPIO_IN_6		0x0064
#define GPIO_IN_7		0x0068
#define GPIO_INT_DETECT_CTL_0	0x0070
#define GPIO_INT_DETECT_CTL_2	0x0074
#define GPIO_INT_DETECT_CTL_3	0x0078
#define GPIO_INT_DETECT_CTL_4	0x007C
#define GPIO_INT_DETECT_CTL_5	0x0080
#define GPIO_INT_DETECT_CTL_6	0x0084
#define GPIO_INT_DETECT_CTL_7	0x0088
#define GPIO_INT_POLARITY_0	0x0090
#define GPIO_INT_POLARITY_2	0x0094
#define GPIO_INT_POLARITY_3	0x0098
#define GPIO_INT_POLARITY_4	0x009C
#define GPIO_INT_POLARITY_5	0x00A0
#define GPIO_INT_POLARITY_6	0x00A4
#define GPIO_INT_POLARITY_7	0x00A8
#define GPIO_INT_EN_0		0x00B0
#define GPIO_INT_EN_2		0x00B4
#define GPIO_INT_EN_3		0x00B8
#define GPIO_INT_EN_4		0x00BC
#define GPIO_INT_EN_5		0x00C0
#define GPIO_INT_EN_6		0x00C4
#define GPIO_INT_EN_7		0x00C8
#define GPIO_INT_CLEAR_0	0x00D0
#define GPIO_INT_CLEAR_2	0x00D4
#define GPIO_INT_CLEAR_3	0x00D8
#define GPIO_INT_CLEAR_4	0x00DC
#define GPIO_INT_CLEAR_5	0x00E0
#define GPIO_INT_CLEAR_6	0x00E4
#define GPIO_INT_CLEAR_7	0x00E8
#define GPIO_INT_STATUS_0	0x00F0
#define GPIO_INT_STATUS_2	0x00F4
#define GPIO_INT_STATUS_3	0x00F8
#define GPIO_INT_STATUS_4	0x00FC
#define GPIO_INT_STATUS_5	0x0100
#define GPIO_INT_STATUS_6	0x0104
#define GPIO_INT_STATUS_7	0x0108


/* Things that are wired up to the GPIO pins */
#define SPI_CLK_GPIO		17
#define SPI_DATA_MO_SI		18
#define SPI_DATA_MI_SO		19
#define SPI_CS0_N		20
#define IDE_INT_N		99
#define EBI2_CS4_N		102
#define LINE_OUT_HP_DET_N	104
#define ACCEL_INT		106
#define USB_DAT			139
#define USB_SE0			140
#define USB_OE			141
#define AUDIO_SDAC_WSOUT	143
#define AUDIO_SDAC_DOUT		145
#define AUDIO_MASTER_CLKOUT	146
#define SPI_CS2_N		147
#define ENET_MSM_INT		156
#define SD_CARD_DET_N		157

#endif

