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

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <asm/irq.h>
#include <asm/platform/sirc.h>
#include <asm/platform/comet/comet_iomap.h>

//  Probably doesn't belong in comet_iomap.h
#define L2_GROUP_SIZE 32

static struct sirc_cascade_regs regs_table[] = {
	{
		.cascade_irq      = L1_ADSP6_SIRC0+1,
		.sirq_base        = HEXAGON_CPUINTS,
		.group_size       = L2_GROUP_SIZE,
		.base_addr        = ADSP6_SIRC,
		.regs.int_status       = (void *) HEXSS_SIRC0_IRQ_STATUS,
		.regs.int_polarity     = (void *) HEXSS_SIRC0_INT_POLARITY,
		.regs.int_type         = (void *) HEXSS_SIRC0_INT_TYPE,
		.regs.int_enable       = (void *) HEXSS_SIRC0_INT_ENABLE,
		.regs.int_enable_set   = (void *) HEXSS_SIRC0_INT_ENABLE_SET,
		.regs.int_enable_clear = (void *) HEXSS_SIRC0_INT_ENABLE_CLEAR,
		.regs.int_clear        = (void *) HEXSS_SIRC0_INT_CLEAR,
	},
	{
		.cascade_irq      = L1_ADSP6_SIRC1+1,
		.sirq_base        = HEXAGON_CPUINTS + L2_GROUP_SIZE,
		.group_size       = L2_GROUP_SIZE,
		.base_addr        = ADSP6_SIRC,
		.regs.int_status       = (void *) HEXSS_SIRC1_IRQ_STATUS,
		.regs.int_polarity     = (void *) HEXSS_SIRC1_INT_POLARITY,
		.regs.int_type         = (void *) HEXSS_SIRC1_INT_TYPE,
		.regs.int_enable       = (void *) HEXSS_SIRC1_INT_ENABLE,
		.regs.int_enable_set   = (void *) HEXSS_SIRC1_INT_ENABLE_SET,
		.regs.int_enable_clear = (void *) HEXSS_SIRC1_INT_ENABLE_CLEAR,
		.regs.int_clear        = (void *) HEXSS_SIRC1_INT_CLEAR,
	},
};

/* Mask off the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_mask(struct irq_data *data)
{
	struct sirc_cascade_regs *regs_table = irq_data_get_irq_chip_data(data);

	unsigned int mask = 1UL << (data->hwirq);

	writel(mask, regs_table->regs.int_enable_clear);

	regs_table->save.int_enable &= ~mask;
}

/* Unmask the given interrupt. Keep the int_enable mask in sync with
   the enable reg, so it can be restored after power collapse. */
static void sirc_irq_unmask(struct irq_data *data)
{
	struct sirc_cascade_regs *regs_table = irq_data_get_irq_chip_data(data);

	unsigned int mask = 1UL << (data->hwirq);

	writel(mask, regs_table->regs.int_enable_set);

	regs_table->save.int_enable |= mask;
}

static void sirc_irq_ack(struct irq_data *data)
{
	struct sirc_cascade_regs *regs_table = irq_data_get_irq_chip_data(data);

	unsigned int mask = 1UL << (data->hwirq);

	/* XXX TODO: Only the edge interrupts need to be acked;
	 * we can save some overhead by not ack'ing the level irqs */
	writel(mask, regs_table->regs.int_clear);
}

static int sirc_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct sirc_cascade_regs *regs_table = irq_data_get_irq_chip_data(data);

	unsigned int mask = 1UL << (data->hwirq);

	/* Used to set the interrupt enable mask during power collapse. */
	if (on)
		regs_table->save.wake_enable |= mask;
	else
		regs_table->save.wake_enable &= ~mask;

	return 0;
}

static int sirc_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct sirc_cascade_regs *regs_table = irq_data_get_irq_chip_data(data);
	unsigned int mask = 1UL << (data->hwirq);
	unsigned int val = readl(regs_table->regs.int_polarity);
	unsigned int irq = data->irq;

	if (flow_type & (IRQF_TRIGGER_LOW | IRQF_TRIGGER_FALLING))
		val |= mask;
	else
		val &= ~mask;

	writel(val, regs_table->regs.int_polarity);

	val = readl(regs_table->regs.int_type);
	if (flow_type & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) {
		val |= mask;
		//irq_desc[irq].handle_irq = handle_edge_irq;
		//  apparently we already hold the desc lock at this point
		irq_to_desc(irq)->handle_irq = handle_edge_irq;
	} else {
		val &= ~mask;
		//irq_desc[irq].handle_irq = handle_level_irq;
		//  apparently we already hold the desc lock at this point
		irq_to_desc(irq)->handle_irq = handle_level_irq;
	}

	writel(val, regs_table->regs.int_type);

	return 0;
}

/* Finds the pending interrupt on the passed cascade irq and redrives it */
static void sirc_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct sirc_cascade_regs *regs_table = irq_desc_get_handler_data(desc);
	unsigned int status;
	unsigned int bitpos;
	unsigned int virq;

	/* Search group for bits that are set */
	status = readl(regs_table->regs.int_status);
	if (status == 0)
		goto ack;

	/* Which bit was set in the status register? */
	bitpos = 0;
	while ((bitpos < regs_table->group_size) &&
	      ((status & (1U << bitpos)) == 0))
		bitpos++;
	
	virq = irq_find_mapping(regs_table->irqd, bitpos);

	/* Redrive to software irq */
	generic_handle_irq(virq);

	/* Ack the first-level interrupt */
ack:
	irq_desc_get_chip(desc)->irq_eoi(&desc->irq_data);
}

void hexss_sirc_enter_sleep(void)
{
#ifdef NOT_TODAY
	int i;
	for (i = 0; i < NR_SIRC_GROUPS; i++) {
		regs_table[i].save.type	=
			readl(regs_table[i].regs.int_type);
		regs_table[i].save.polarity =
			readl(regs_table[i].regs.int_polarity);
		writel(regs_table[i].save.wake_enable,
			regs_table[i].regs.int_enable);
	}
#endif
}

void hexss_sirc_exit_sleep(void)
{
#ifdef NOT_TODAY
	int i;
	for (i = 0; i < NR_SIRC_GROUPS; i++) {
		writel(regs_table[i].save.type, regs_table[i].regs.int_type);
		writel(regs_table[i].save.polarity,
			regs_table[i].regs.int_polarity);
		writel(regs_table[i].save.int_enable,
			regs_table[i].regs.int_enable);
	}
#endif
}

// do I need multiples of these now?
static struct irq_chip sirc_irq_chip = {
	.name          = "sirc",
	.irq_ack       = sirc_irq_ack,
	.irq_mask      = sirc_irq_mask,
	.irq_unmask    = sirc_irq_unmask,
	.irq_set_wake  = sirc_irq_set_wake,
	.irq_set_type  = sirc_irq_set_type,
};

void __init hexss_init_sirc(struct sirc_cascade_regs *regs_table, int regs_sz)
{
	int i;

	printk("%s called\n", __func__);
	/* Set up status register base addresses */
	for (i = 0; i < regs_sz; i++) {
		unsigned int sirc_base_addr =
			(unsigned int) ioremap(regs_table[i].base_addr,
						PAGE_SIZE);
		regs_table[i].regs.int_status       += sirc_base_addr;
		regs_table[i].regs.int_polarity     += sirc_base_addr;
		regs_table[i].regs.int_type         += sirc_base_addr;
		regs_table[i].regs.int_enable       += sirc_base_addr;
		regs_table[i].regs.int_enable_set   += sirc_base_addr;
		regs_table[i].regs.int_enable_clear += sirc_base_addr;
		regs_table[i].regs.int_clear        += sirc_base_addr;
	}

	/* Install a handler for each secondary */
	for (i = 0; i < regs_sz; i++) {
		unsigned int sirq = regs_table[i].sirq_base;
		unsigned int last = sirq + regs_table[i].group_size;
		while (sirq < last) {
			irq_set_chip(sirq, &sirc_irq_chip);
			irq_set_chip_data(sirq, &regs_table[i]);
			irq_set_handler(sirq, handle_level_irq);
			sirq++;
		}
	}

	/* Turn on the cascade handlers */
	for (i = 0; i < regs_sz; i++) {
		unsigned int irq = regs_table[i].cascade_irq;

		irq_set_handler_data(irq, &regs_table[i]);
		irq_set_chained_handler(irq, sirc_irq_handler);
		irq_set_irq_wake(irq, 1);
	}
}


//  (Mostly) Devicetree based init.


//  Do I need to save these or what?
//struct irq_domain *sirc_irq_domains[2];

int __init hexss_init_sirc_of(struct device_node *node,
			  struct device_node *parent)
{
	int irq;	/*  hardware IRQ's  */
	int virq;	/*  linux IRQ's  */

	struct property *pp;
	int id;  //  from alias
	unsigned int sirc_base_addr; 

	printk("%s called\n", __func__);

	id = of_alias_get_id(node, "sirc");

	sirc_base_addr = (unsigned int) ioremap(regs_table[id].base_addr, PAGE_SIZE);

	regs_table[id].regs.int_status       += sirc_base_addr;
	regs_table[id].regs.int_polarity     += sirc_base_addr;
	regs_table[id].regs.int_type         += sirc_base_addr;
	regs_table[id].regs.int_enable       += sirc_base_addr;
	regs_table[id].regs.int_enable_set   += sirc_base_addr;
	regs_table[id].regs.int_enable_clear += sirc_base_addr;
	regs_table[id].regs.int_clear        += sirc_base_addr;

	regs_table[id].irqd = irq_domain_add_linear(node, L2_GROUP_SIZE,
		&irq_domain_simple_ops, &sirc_irq_chip);

	//  Weird that the irq_domain_add_linear didn't actually map the irq's.  nor did it set the chip...  -_-
	//  Chip should have been set via irq_domain_add_linear (why the hell else would I pass the chip in otherwise).
	//  set up the handler type

	for (irq = 0; irq<L2_GROUP_SIZE; irq++) {
		virq = irq_create_mapping(regs_table[id].irqd, irq);
		// Zero is error!
		printk("SIRC%d HWIRQ %d -> %d\n", id, irq, virq);
		irq_set_chip(virq, &sirc_irq_chip);
		irq_set_chip_data(virq, &regs_table[id]);
		irq_set_handler(virq, handle_level_irq);
	}

	//  might be overreaching to do all the device setup here

	virq = irq_of_parse_and_map(node, 0);
	irq_set_handler_data(virq, &regs_table[id]);
	irq_set_chained_handler(virq, sirc_irq_handler);
	irq_set_irq_wake(virq, 1);

	return 0;
}




