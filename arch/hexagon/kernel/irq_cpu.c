// SPDX-License-Identifier: GPL-2.0-only
/*
 * First-level interrupt controller model for Hexagon.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <asm/irq.h>
#include <asm/hexagon_vm.h>

DEFINE_PER_CPU(long, ie_cached);

/*
 * Caching replacement for the old __vmsetie().  Every local_irq_*() goes
 * through here, so the hypercall is only made when the state actually
 * changes.
 */
long vmsetie_cached(long val)
{
	return __vmsetie_cached(val, this_cpu_ptr(&ie_cached));
}
EXPORT_SYMBOL(vmsetie_cached);

/*
 * Disable interrupts on the way to a vmrte, which restores the interrupt
 * state from the event record; leave the cache holding what vmrte is about
 * to install rather than what we just set.
 */
void vmsetie_rte_disable(void)
{
	__vmsetie_cached(VM_INT_DISABLE, this_cpu_ptr(&ie_cached));
	__this_cpu_write(ie_cached, ints_enabled(current_thread_info()->regs));
}

void vmsetie_disable(void)
{
	__vmsetie_cached(VM_INT_DISABLE, this_cpu_ptr(&ie_cached));
}

long vmgetie_cached(void)
{
	return __this_cpu_read(ie_cached);
}
EXPORT_SYMBOL(vmgetie_cached);

/*  Resynchronize the cache with the VM, for a CPU that has just come up.  */
void load_ie_cache(void)
{
	__this_cpu_write(ie_cached, __vmgetie());
}

/*
 * The VM disables guest interrupts when it delivers an event, so every
 * event handler has to call this before anything reads or updates the
 * cache -- otherwise a later local_irq_enable() sees no change to make
 * and the hypercall is skipped.
 */
void clear_ie_cached(void)
{
	__this_cpu_write(ie_cached, 0);
}

void __init init_IRQ(void)
{
	irqchip_init();
}
