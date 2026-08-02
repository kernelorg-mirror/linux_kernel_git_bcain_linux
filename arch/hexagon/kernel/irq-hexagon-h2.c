// SPDX-License-Identifier: GPL-2.0-only
/*
 * First-level interrupt controller model for Hexagon.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <asm/hexagon_vm.h>
#include <asm/irq.h>

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

/*
 * The first-level interrupt controller is wrapped by the VM, which
 * virtualizes it for us.  It provides a very simple, fast and efficient
 * API, so the fasteoi handler is appropriate for this case.
 *
 * The VM delivers an interrupt to the guest at all only once it has been
 * enabled globally; leave it masked on this CPU until a handler asks for it.
 */
static int hexagon_irq_domain_map(struct irq_domain *d, unsigned int virq,
				  irq_hw_number_t hwirq)
{
	mask_irq_num(hwirq);
	if (IS_ENABLED(CONFIG_HEXAGON_H2))
		__vmintop_globen((long) hwirq);

	irq_set_chip_and_handler(virq, &hexagon_irq_chip, handle_fasteoi_irq);

	return 0;
}

static const struct irq_domain_ops hexagon_irq_domain_ops = {
	.map	= hexagon_irq_domain_map,
	.xlate	= irq_domain_xlate_onetwocell,
};

struct irq_domain *hexagon_irq_domain;

int __init hexagon_pic_of_init(struct device_node *node,
			       struct device_node *parent)
{
	int hwirq;

	load_ie_cache();

	hexagon_irq_domain = irq_domain_create_linear(of_fwnode_handle(node),
						      HEXAGON_CPUINTS,
						      &hexagon_irq_domain_ops,
						      NULL);
	if (!hexagon_irq_domain) {
		pr_err("%pOF: unable to allocate irq domain\n", node);
		return -ENOMEM;
	}

	irq_set_default_domain(hexagon_irq_domain);

	/*
	 * Map every line up front: an event arrives as a hardware IRQ that
	 * arch_do_IRQ() has to be able to resolve without allocating.
	 */
	for (hwirq = 0; hwirq < HEXAGON_CPUINTS; hwirq++)
		irq_create_mapping(hexagon_irq_domain, hwirq);

	return 0;
}

IRQCHIP_DECLARE(hexagon_h2_pic, "qcom,h2-pic", hexagon_pic_of_init);
