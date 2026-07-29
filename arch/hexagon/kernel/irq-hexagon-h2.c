// SPDX-License-Identifier: GPL-2.0-only
/*
 * First-level interrupt controller model for Hexagon.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <asm/irq.h>
#include <asm/hexagon_vm.h>

#include <linux/irqchip.h>

/*  This takes the hardware IRQ  */
static void mask_irq_num(unsigned int hwirq)
{
	__vmintop_locdis((long) hwirq);
}

static void mask_irq(struct irq_data *data)
{
	mask_irq_num(data->hwirq);
}

static void unmask_irq(struct irq_data *data)
{
	__vmintop_locen((long) data->hwirq);
}

/*  This is actually all we need for handle_fasteoi_irq  */
static void eoi_irq(struct irq_data *data)
{
	__vmintop_globen((long) data->hwirq);
}

static struct irq_chip hexagon_irq_chip = {
	.name		= "HEXAGON",
	.irq_mask	= mask_irq,
	.irq_unmask	= unmask_irq,
	.irq_eoi	= eoi_irq
};

/*  Warning:  irq domain ahead.  */
struct irq_domain *hexagon_irq_domain;

int __init hexagon_pic_of_init(struct device_node *node,
			  struct device_node *parent)
{
	int irq;	/*  hardware IRQ's  */
	int virq;	/*  linux IRQ's  */

	load_ie_cache();

	/*  Not sure what the correct host data for this is yet  */
	hexagon_irq_domain = irq_domain_add_linear(node, HEXAGON_CPUINTS,
			&irq_domain_simple_ops, &hexagon_irq_chip);
	if (!hexagon_irq_domain) {
		WARN(1, "Cannot allocate irq_domain\n");
		return -ENOMEM;
	}

	irq_set_default_domain(hexagon_irq_domain);

	/**
	 * The first-level interrupt controller is wrapped by the VM, which
	 * virtualizes the interrupt controller for us.  It provides a very
	 * simple, fast & efficient API, and so the fasteoi handler is
	 * appropriate for this case.
	 */

	for (irq = 0; irq < HEXAGON_CPUINTS; irq++) {
		mask_irq_num(irq);  // this is already hwirq
#ifdef CONFIG_H2
		__vmintop_globen((long) irq);
#endif
		virq = irq_create_mapping(hexagon_irq_domain, irq);
		irq_set_chip_and_handler(virq, &hexagon_irq_chip,
						 handle_fasteoi_irq);
	}

	return 0;
}

IRQCHIP_DECLARE(hexagon_h2_pic, "qcom,h2-pic", hexagon_pic_of_init);
