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
#include <linux/preempt.h>
#include <asm/irq.h>
#include <asm/hexagon_vm.h>

/* These helpers are also used by tracing itself and before IRQ entry. */
DEFINE_PER_CPU(long, ie_cached);

/*  caching replacement for the old __vmsetie()  */

long notrace vmsetie_cached(long val)
{
	long old;

	/* The pointer and hypercall must refer to the same CPU. */
	preempt_disable_notrace();
	old = __vmsetie_cached(val, this_cpu_ptr(&ie_cached));
	preempt_enable_notrace();
	return old;
}

/*
 * sets the ie state, but fudges the cache val because
 * vmrte can still change ie on us; see vm_entry.S
 */

void notrace vmsetie_rte_disable(void)
{
	/*  the preempt_enable/disable doesn't help us  */
	__vmsetie_cached(VM_INT_DISABLE, this_cpu_ptr(&ie_cached));
	raw_cpu_write(ie_cached, ints_enabled(current_thread_info()->regs));
}

void notrace vmsetie_disable(void)
{
	vmsetie_cached(VM_INT_DISABLE);
}

long notrace vmgetie_cached(void)
{
	long flags;

	/* Reading flags must not itself schedule or migrate between CPU and load. */
	preempt_disable_notrace();
	flags = raw_cpu_read(ie_cached);
	preempt_enable_no_resched_notrace();
	return flags;
}

void notrace load_ie_cache(void)
{
	raw_cpu_write(ie_cached, __vmgetie());
}

void notrace clear_ie_cached(void)
{
	/* The VM disabled IRQs, although the cached flags still say otherwise. */
	raw_cpu_write(ie_cached, 0);
}

void __init init_IRQ(void)
{
	irqchip_init();
}
