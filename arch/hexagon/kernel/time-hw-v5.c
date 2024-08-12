/*
 * Copyright (c) 2010-2012, 2015, The Linux Foundation. All rights reserved.
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
 */

#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/export.h>
#include <asm/platform.h>

#define TMR_FRAME	1
#define TMR_FREQ	19200000

#define REG_CNTFRQ	0x0000
#define REG_CNTSR	0x0004
#define REG_CNTACR	(0x0040+TMR_FRAME*4)

#define REG_COUNT_LO	(0x1000 + TMR_FRAME*0x1000)
#define REG_COUNT_HI	(0x1004 + TMR_FRAME*0x1000)
#define REG_MATCH_LO	(0x1020 + TMR_FRAME*0x1000)
#define REG_MATCH_HI	(0x1024 + TMR_FRAME*0x1000)
#define REG_CTL		(0x102C + TMR_FRAME*0x1000)

static __iomem void *rtos_timer;

static cycle_t timer_get_cycles(struct clocksource *cs)
{

	u32 count_hi;
	cycle_t count;

	/*  Still have to check for overflow apparently */
	do {
		count_hi = readl(rtos_timer+REG_COUNT_HI);
		count = readl(rtos_timer+REG_COUNT_LO);
		count |= (u64) readl(rtos_timer+REG_COUNT_HI) << 32;
	} while ((count >> 32) != count_hi);

	return count;
}

static struct clocksource hexagon_clocksource = {
	.name		= "qtimer",
	.rating		= 250,
	.read		= timer_get_cycles,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static int set_next_event(unsigned long delta, struct clock_event_device *evt)
{
	u64 nextticks;

	nextticks = timer_get_cycles(NULL) + delta;

	/*  Setting an event in the past will generate an interrupt  */
	writel(ULONG_MAX, rtos_timer+REG_MATCH_HI);
	writel(nextticks & ULONG_MAX, rtos_timer+REG_MATCH_LO);
	writel(nextticks >> 32, rtos_timer+REG_MATCH_HI);

	return 0;
}

/*
 * Sets the mode (periodic, shutdown, oneshot, etc) of a timer.
 */
static void set_mode(enum clock_event_mode mode,
	struct clock_event_device *evt)
{
}

#ifdef CONFIG_SMP
/*  Broadcast mechanism  */
static void broadcast(const struct cpumask *mask)
{
	send_ipi(mask, IPI_TIMER);
}
#endif

static struct clock_event_device hexagon_clockevent_dev = {
	.name		= "clockevent",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 400,
	.set_next_event = set_next_event,
	.set_mode	= set_mode,
#ifdef CONFIG_SMP
	.broadcast	= broadcast,
#endif
};

#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct clock_event_device, clock_events);

void setup_percpu_clockdev(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *ce_dev = &hexagon_clockevent_dev;
	struct clock_event_device *dummy_clock_dev =
		&per_cpu(clock_events, cpu);

	memcpy(dummy_clock_dev, ce_dev, sizeof(*dummy_clock_dev));
	INIT_LIST_HEAD(&dummy_clock_dev->list);

	dummy_clock_dev->features = CLOCK_EVT_FEAT_DUMMY;
	dummy_clock_dev->cpumask = cpumask_of(cpu);
	dummy_clock_dev->mode = CLOCK_EVT_MODE_UNUSED;

	clockevents_register_device(dummy_clock_dev);
}

/*  Called from smp.c for each CPU's timer ipi call  */
void ipi_timer(void)
{
	int cpu = smp_processor_id();
	struct clock_event_device *ce_dev = &per_cpu(clock_events, cpu);

	ce_dev->event_handler(ce_dev);
}
#endif /* CONFIG_SMP */

static irqreturn_t timer_interrupt(int irq, void *devid)
{
	struct clock_event_device *ce_dev = &hexagon_clockevent_dev;

	ce_dev->event_handler(ce_dev);

	return IRQ_HANDLED;
}

/*  This should also be pulled from devtree  */
static struct irqaction rtos_timer_intdesc = {
	.handler = timer_interrupt,
	.flags = IRQF_TIMER | IRQF_TRIGGER_RISING,
	.name = "rtos_timer"
};

/*
 * time_init_deferred - called by start_kernel to set up timer/clock source
 *
 * Install the IRQ handler for the clock, setup timers.
 * This is done late, as that way, we can use ioremap().
 *
 * This runs just before the delay loop is calibrated, and
 * is used for delay calibration.
 */
void __init time_init_deferred(void)
{
	struct device_node *dn;
	struct resource r;
	struct clock_event_device *ce_dev = &hexagon_clockevent_dev;

	ce_dev->cpumask = cpu_all_mask;

	/*  Probably should search for a device type...  */
	dn = of_find_compatible_node(NULL, NULL, "qcom,qtimer");

	BUG_ON(!dn);

	ce_dev->irq = irq_of_parse_and_map(dn, 0);
	BUG_ON(!ce_dev->irq);

	BUG_ON(of_address_to_resource(dn, 0, &r));

	rtos_timer = ioremap(r.start, resource_size(&r));

	BUG_ON(!rtos_timer);

	clocksource_register_hz(&hexagon_clocksource, TMR_FREQ);

	/*
	 * Last arg is some guaranteed seconds for which the conversion will
	 * work without overflow.
	 */
	clockevents_calc_mult_shift(ce_dev, TMR_FREQ, 4);

	ce_dev->max_delta_ns = clockevent_delta2ns(0x7fffffff, ce_dev);
	ce_dev->min_delta_ns = clockevent_delta2ns(0xf, ce_dev);

#ifdef CONFIG_SMP
	setup_percpu_clockdev();
#endif

	clockevents_register_device(ce_dev);
	setup_irq(ce_dev->irq, &rtos_timer_intdesc);

#ifndef CONFIG_H2
	writel(ULONG_MAX, rtos_timer+REG_MATCH_HI);
	writel(ULONG_MAX, rtos_timer+REG_MATCH_LO);
	writel(ULONG_MAX, rtos_timer+REG_CNTSR);
	writel(TMR_FREQ, rtos_timer+REG_CNTFRQ);
#endif
	writel(ULONG_MAX, rtos_timer+REG_CNTACR);
	writel(1, rtos_timer+REG_CTL);


}

void __init time_init(void)
{
	late_time_init = time_init_deferred;
}

void __delay(unsigned long cycles)
{
	unsigned long long start = timer_get_cycles(NULL);

	while ((timer_get_cycles(NULL) - start) < cycles)
		cpu_relax();
}
EXPORT_SYMBOL(__delay);

void __udelay(unsigned long usecs)
{
	unsigned long long start = timer_get_cycles(NULL);
	unsigned long long finish = (TMR_FREQ / 1000000) * usecs;

	while ((timer_get_cycles(NULL) - start) < finish)
		cpu_relax(); /*  not sure how this improves readability  */
}
EXPORT_SYMBOL(__udelay);
