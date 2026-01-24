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

/*
 * CONFIG_DEBUG_PREEMPT uses per-cpu tracking internally which manipulates
 * interrupts, creating a circular dependency with the cached IE mechanism.
 * Bypass the cache and use direct VM traps in that case.
 */
#ifdef CONFIG_DEBUG_PREEMPT
#define IE_CACHE_DISABLE 1
#endif

long vmsetie_cached(long val)
{
#ifdef IE_CACHE_DISABLE
	return __vmsetie(val);
#else
	return __vmsetie_cached(val, this_cpu_ptr(&ie_cached));
#endif
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
#ifdef IE_CACHE_DISABLE
	__vmsetie(VM_INT_DISABLE);
#else
	__vmsetie_cached(VM_INT_DISABLE, this_cpu_ptr(&ie_cached));
#endif
}

long vmgetie_cached(void)
{
#ifdef IE_CACHE_DISABLE
	return __vmgetie();
#else
	return __this_cpu_read(ie_cached);
#endif
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
