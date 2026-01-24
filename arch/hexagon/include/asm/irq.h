/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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

#ifndef _ASM_IRQ_H_
#define _ASM_IRQ_H_

#ifdef CONFIG_H2
#ifdef CONFIG_HEXAGON_MSS
//  FIXME:  This seems to freak the 8960 build out.  Need to debug.  Might be H2 related.  Might not.
#define HEXAGON_CPUINTS 480
#else
#define HEXAGON_CPUINTS 160
#endif
#else
#define HEXAGON_CPUINTS 32
#endif

/*
 * Must define NR_IRQS before including <asm-generic/irq.h>
 * On old platform (?) 64 == the two SIRC's, 176 == the two gpio's
 */
#define NR_IRQS 512

#include <asm-generic/irq.h>

#ifdef CONFIG_ARCH_MSM8960
#include <mach/irqs.h>
#endif

#include <linux/of.h>

/*  Provided by platform  */
extern struct of_device_id platform_of_irq_matches[] __initdata;

/*  H2 "pic" initialization at init_IRQ time  */
int __init hexagon_pic_of_init(struct device_node *node,
                          struct device_node *parent);


#endif
