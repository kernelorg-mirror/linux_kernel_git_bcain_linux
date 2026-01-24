/*
 * First-level interrupt controller model for Hexagon.
 *
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
