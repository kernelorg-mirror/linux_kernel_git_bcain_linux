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
#include <asm/irq.h>
#include <asm/hexagon_vm.h>

DEFINE_PER_CPU(long, ie_cached);

/*  caching replacement for the old __vmsetie()  */

long vmsetie_cached(long val)
{
	return __vmsetie_cached(val, this_cpu_ptr(&ie_cached));
}

/*
 * sets the ie state, but fudges the cache val because
 * vmrte can still change ie on us; see vm_entry.S
 */

void vmsetie_rte_disable(void)
{
	/*  the preempt_enable/disable doesn't help us  */
	__vmsetie_cached(VM_INT_DISABLE, this_cpu_ptr(&ie_cached));
	__this_cpu_write(ie_cached, ints_enabled(current_thread_info()->regs));
}

void vmsetie_disable(void)
{
	__vmsetie_cached(VM_INT_DISABLE, this_cpu_ptr(&ie_cached));
}

/*
 * May run with preemption enabled (arch_local_save_flags), so after a
 * migration the value can describe another CPU.  That is acceptable:
 * the result is a 0/1 flag, and restoring it via vmsetie_cached()
 * operates on whichever CPU the task is on by then.
 */
long vmgetie_cached(void)
{
	return __this_cpu_read(ie_cached);
}

void load_ie_cache(void)
{
	__this_cpu_write(ie_cached, __vmgetie());
}

void clear_ie_cached(void)
{
	__this_cpu_write(ie_cached, 0);
}

void __init init_IRQ(void)
{
	irqchip_init();
}
