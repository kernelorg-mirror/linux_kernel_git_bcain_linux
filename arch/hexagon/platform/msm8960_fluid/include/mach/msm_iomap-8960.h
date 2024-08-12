/*
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2008-2011, Code Aurora Forum. All rights reserved.
 * Author: Brian Swetland <swetland@google.com>
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
 *
 * The MSM peripherals are spread all over across 768MB of physical
 * space, which makes just having a simple IO_ADDRESS macro to slide
 * them into the right virtual location rough.  Instead, we will
 * provide a master phys->virt mapping for peripherals here.
 *
 */

#ifndef __ASM_ARCH_MSM_IOMAP_8960_H
#define __ASM_ARCH_MSM_IOMAP_8960_H

/* Physical base address and size of peripherals.
 * Ordered by the virtual base addresses they will be mapped at.
 *
 * If you add or remove entries here, you'll want to edit the
 * msm_io_desc array in arch/arm/mach-msm/io.c to reflect your
 * changes.
 *
 */

#define MSM8960_TMR_PHYS		0x0200A000
#define MSM8960_TMR_SIZE		SZ_4K

#define MSM8960_TMR0_PHYS		0x0208A000
#define MSM8960_TMR0_SIZE		SZ_4K

#define MSM8960_RPM_PHYS		0x00108000
#define MSM8960_RPM_SIZE		SZ_4K

#define MSM8960_RPM_MPM_PHYS		0x00200000
#define MSM8960_RPM_MPM_SIZE		SZ_4K

#define MSM8960_TCSR_PHYS		0x1A400000
#define MSM8960_TCSR_SIZE		SZ_4K

#define MSM8960_APCS_GCC_PHYS		0x02011000
#define MSM8960_APCS_GCC_SIZE		SZ_4K

#define MSM8960_SAW_L2_PHYS		0x02012000
#define MSM8960_SAW_L2_SIZE		SZ_4K

#define MSM8960_SAW0_PHYS		0x02089000
#define MSM8960_SAW0_SIZE		SZ_4K

#define MSM8960_SAW1_PHYS		0x02099000
#define MSM8960_SAW1_SIZE		SZ_4K

#define MSM8960_IMEM_PHYS		0x2A03F000
#define MSM8960_IMEM_SIZE		SZ_4K

#define MSM8960_ACC0_PHYS		0x02088000
#define MSM8960_ACC0_SIZE		SZ_4K

#define MSM8960_ACC1_PHYS		0x02098000
#define MSM8960_ACC1_SIZE		SZ_4K

#define MSM8960_QGIC_DIST_PHYS		0x02000000
#define MSM8960_QGIC_DIST_SIZE		SZ_4K

#define MSM8960_QGIC_CPU_PHYS		0x02002000
#define MSM8960_QGIC_CPU_SIZE		SZ_4K

#define MSM8960_CLK_CTL_PHYS		0x00900000
#define MSM8960_CLK_CTL_SIZE		SZ_16K

#define MSM8960_MMSS_CLK_CTL_PHYS	0x04000000
#define MSM8960_MMSS_CLK_CTL_SIZE	SZ_4K

#define MSM8960_LPASS_CLK_CTL_PHYS	0x28000000
#define MSM8960_LPASS_CLK_CTL_SIZE	SZ_4K

#define MSM8960_HFPLL_PHYS		0x00903000
#define MSM8960_HFPLL_SIZE		SZ_4K

#define MSM8960_TLMM_PHYS		0x00800000
#define MSM8960_TLMM_SIZE		SZ_16K

#define MSM8960_SIC_NON_SECURE_PHYS	0x12100000
#define MSM8960_SIC_NON_SECURE_SIZE	SZ_64K

#define MSM_GPT_BASE			(MSM_TMR_BASE + 0x4)
#define MSM_DGT_BASE			(MSM_TMR_BASE + 0x24)

#define MSM8960_HDMI_PHYS		0x04A00000
#define MSM8960_HDMI_SIZE		SZ_4K

#ifdef CONFIG_DEBUG_MSM8960_UART
#define MSM_DEBUG_UART_BASE		IOMEM(0xFA740000)
#define MSM_DEBUG_UART_PHYS		0x16440000
#endif

#define MSM8960_QFPROM_PHYS		0x00700000
#define MSM8960_QFPROM_SIZE		SZ_4K

#ifdef CONFIG_DEBUG_MSM8960_UART
#define MSM_DEBUG_UART_BASE	0xE1040000
#define MSM_DEBUG_UART_PHYS	0x16440000
#endif


//  Thoughtfully these were in a different file.
/* Address of GSBI blocks */
#define MSM_GSBI1_PHYS          0x16000000
#define MSM_GSBI2_PHYS          0x16100000
#define MSM_GSBI3_PHYS          0x16200000
#define MSM_GSBI4_PHYS          0x16300000
#define MSM_GSBI5_PHYS          0x16400000
#define MSM_GSBI6_PHYS          0x16500000
#define MSM_GSBI7_PHYS          0x16600000
#define MSM_GSBI8_PHYS          0x1A000000
#define MSM_GSBI9_PHYS          0x1A100000
#define MSM_GSBI10_PHYS         0x1A200000
#define MSM_GSBI11_PHYS         0x12440000
#define MSM_GSBI12_PHYS         0x12480000

#define MSM_UART2DM_PHYS        (MSM_GSBI2_PHYS + 0x40000)
#define MSM_UART5DM_PHYS        (MSM_GSBI5_PHYS + 0x40000)
#define MSM_UART6DM_PHYS        (MSM_GSBI6_PHYS + 0x40000)
#define MSM_UART8DM_PHYS        (MSM_GSBI8_PHYS + 0x40000)
#define MSM_UART9DM_PHYS        (MSM_GSBI9_PHYS + 0x40000)

/* GSBI QUP devices */
#define MSM_GSBI1_QUP_PHYS      (MSM_GSBI1_PHYS + 0x80000)
#define MSM_GSBI2_QUP_PHYS      (MSM_GSBI2_PHYS + 0x80000)
#define MSM_GSBI3_QUP_PHYS      (MSM_GSBI3_PHYS + 0x80000)
#define MSM_GSBI4_QUP_PHYS      (MSM_GSBI4_PHYS + 0x80000)
#define MSM_GSBI5_QUP_PHYS      (MSM_GSBI5_PHYS + 0x80000)
#define MSM_GSBI6_QUP_PHYS      (MSM_GSBI6_PHYS + 0x80000)
#define MSM_GSBI7_QUP_PHYS      (MSM_GSBI7_PHYS + 0x80000)
#define MSM_GSBI8_QUP_PHYS      (MSM_GSBI8_PHYS + 0x80000)
#define MSM_GSBI9_QUP_PHYS      (MSM_GSBI9_PHYS + 0x80000)
#define MSM_GSBI10_QUP_PHYS     (MSM_GSBI10_PHYS + 0x80000)
#define MSM_GSBI11_QUP_PHYS     (MSM_GSBI11_PHYS + 0x20000)
#define MSM_GSBI12_QUP_PHYS     (MSM_GSBI12_PHYS + 0x20000)
#define MSM_QUP_SIZE            SZ_4K

#define MSM_PMIC1_SSBI_CMD_PHYS 0x00500000
#define MSM_PMIC2_SSBI_CMD_PHYS 0x00C00000
#define MSM_PMIC_SSBI_SIZE      SZ_4K

#define MSM8960_HSUSB_PHYS              0x12500000
#define MSM8960_HSUSB_SIZE              SZ_4K




#ifndef __ASSEMBLY__
extern void msm_map_msm8960_io(void);
#endif

#endif
