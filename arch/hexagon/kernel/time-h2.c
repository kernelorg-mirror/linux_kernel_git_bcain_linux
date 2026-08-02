// SPDX-License-Identifier: GPL-2.0-only
/*
 * Time related functions for the Hexagon H2 hypervisor timer
 *
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/delay.h>
#include <asm/hexagon_vm.h>
#include <asm/time.h>

cycles_t	hvm_timer_freq;

static u64 timer_get_cycles(struct clocksource *cs)
{
	return (u64) __vmtimerop(gettime, 0, 0);
}

static struct clocksource hexagon_clocksource = {
	.name		= "HVM timer",
	.rating		= 250,
	.read		= timer_get_cycles,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

/* Bound on how many times a deadline may land in the past before we give up. */
#define SET_NEXT_EVENT_RETRIES	16

static int set_next_event(unsigned long delta, struct clock_event_device *evt)
{
	unsigned int i;

	/*
	 * The timeout belongs to the virtual thread that sets it and the
	 * timer interrupt is routed to CPU 0, so only CPU 0 can arm the real
	 * device.  The other CPUs reach their clockevent through the
	 * IPI_TIMER broadcast below, so report success and leave the deadline
	 * to CPU 0 rather than failing the programming.
	 *
	 * FIXME: the device is registered with cpumask_of(0), so the core
	 * should not be programming it from anywhere else in the first place.
	 */
	if (smp_processor_id() != 0)
		return 0;

	/*
	 * A return of zero means the hypervisor found the deadline already
	 * past and left the timer disarmed: the gettime it does inside the
	 * hypercall reports the time it was entered rather than the time it
	 * completed, so being preempted there can put the target behind us.
	 * Each retry recomputes the deadline from a fresh now, so this
	 * converges; bound it anyway rather than spinning here forever, and
	 * let the core widen the delta if it somehow does not.
	 */
	for (i = 0; i < SET_NEXT_EVENT_RETRIES; i++) {
		if (__vmtimerop(deltatimeout, 0, clockevent_delta2ns(delta, evt)))
			return 0;
	}

	return -ETIME;
}

/*
 * Disarm the timer.  A timeout of "forever" is how the hypervisor is told
 * there is no deadline; it removes the thread from the timeout tree.
 */
static int set_state_shutdown(struct clock_event_device *evt)
{
	__vmtimerop(settimeout, 0, HVM_TIMER_FOREVER);

	return 0;
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
	.set_state_shutdown = set_state_shutdown,
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

/*
 * time_init - set up the HVM timer as clocksource and clockevent
 *
 * Install the IRQ handler for the clock, setup timers.  This runs just
 * before the delay loop is calibrated, and is used for delay calibration.
 */
void __init time_init(void)
{
	struct clock_event_device *ce_dev = &hexagon_clockevent_dev;
	struct device_node *dn;

	hvm_timer_freq = (cycles_t)__vmtimerop(getfreq, 0, 0);

	dn = of_find_compatible_node(NULL, NULL, "qcom,h2-timer");
	if (!dn)
		panic("%s could not find device\n", __func__);

	ce_dev->irq = irq_of_parse_and_map(dn, 0);
	ce_dev->cpumask = cpumask_of(smp_processor_id());

	clocksource_register_khz(&hexagon_clocksource, hvm_timer_freq / 1000);

	/*
	 * Last arg is some guaranteed seconds for which the conversion will
	 * work without overflow.
	 */
	clockevents_calc_mult_shift(ce_dev, hvm_timer_freq, 4);

	ce_dev->max_delta_ns = clockevent_delta2ns(0x7fffffff, ce_dev);
	ce_dev->min_delta_ns = clockevent_delta2ns(0xf, ce_dev);

#ifdef CONFIG_SMP
	setup_percpu_clockdev();
#endif

	clockevents_register_device(ce_dev);
	if (request_irq(ce_dev->irq, timer_interrupt,
			IRQF_TIMER | IRQF_TRIGGER_RISING, "rtos_timer", NULL))
		pr_err("Failed to request timer irq\n");
}

void __delay(unsigned long loops)
{
	asm volatile(
		"loop0(1f, %0);"
		"1:	{ nop; } :endloop0"
		:
		: "r" (loops)
	);
}
EXPORT_SYMBOL(__delay);

void __udelay(unsigned long usecs)
{
	unsigned long long start = __vmtimerop(gettime, 0, 0);
	unsigned long long finish = (hvm_timer_freq * usecs / (1000 * 1000));

	while ((__vmtimerop(gettime, 0, 0) - start) < finish)
		cpu_relax();
}
EXPORT_SYMBOL(__udelay);
