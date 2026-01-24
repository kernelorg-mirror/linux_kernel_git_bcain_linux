/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
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

struct pt_regs;
void arch_do_IRQ(struct pt_regs *);
void vmsetie_rte_disable(void);
void vmsetie_disable(void);
/*  First-level (hvm-pic) domain, for hwirq -> virq lookups  */
extern struct irq_domain *hexagon_irq_domain;

#include <linux/of.h>

/*  Provided by platform  */
extern const struct of_device_id platform_of_irq_matches[] __initdata;

/*  H2 "pic" initialization at init_IRQ time  */
int __init hexagon_pic_of_init(struct device_node *node,
                          struct device_node *parent);

#endif
