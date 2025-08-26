/*
 * Time related functions for Hexagon architecture
 *
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

#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/delay.h>
#include <asm/hexagon_vm.h>
#include <asm/time.h>

/*
 * For now, hard wire the simulated CPU/pcycle frequency.
 */

cycles_t        pcycle_freq_mhz;
cycles_t        thread_freq_mhz;
cycles_t        sleep_clk_freq;
cycles_t        hvm_timer_freq;

#ifdef CONFIG_HEXAGON_TMR_LAT
unsigned long long timer_expected_now;
unsigned long long lat_sum;
unsigned long long lat_cnt;
unsigned long long lat_min = ~0ULL;
unsigned long long lat_max;  //  how do we account for totally missed timer intervals?
unsigned long long lat_avg;
unsigned long lat_tracking_enable;
#endif

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

static int set_next_event(unsigned long delta, struct clock_event_device *evt)
{
	unsigned long long ret;

#ifdef CONFIG_H2
	if (smp_processor_id() != 0) {
		return 0;
	} /*  this version of the timer should only be set by CPU 0  */
#endif

	/* ret == 0 means that the requested time has already passed, so we were
		 preempted on the gettime trap (which returns the time it was called, not
		 the time it completes.  So we have to try again */
	do {
		ret = __vmtimerop(deltatimeout, 0, clockevent_delta2ns(delta, evt));
	} while (ret == 0);

#ifdef CONFIG_HEXAGON_TMR_LAT
	if (lat_tracking_enable)
		timer_expected_now = ret; //  plz don't be a race plz don't be a race
#endif

	return 0;
}

/*
 * Sets the state (shutdown) of a timer.
 */
static int set_state_shutdown(struct clock_event_device *evt)
{
	/* XXX implement me */
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
#ifdef CONFIG_HEXAGON_TMR_LAT
	unsigned long long now = __vmtimerop(gettime, 0 , 0);
	unsigned long long latency = now - timer_expected_now;

	if (lat_tracking_enable && timer_expected_now) {
		lat_sum += latency;
		lat_cnt++;
		lat_avg = lat_sum / lat_cnt;

		if (latency < lat_min) {
			lat_min = latency;
		}

		if (latency > lat_max) {
			lat_max = latency;
		}
	}
#endif
	ce_dev->event_handler(ce_dev);

	return IRQ_HANDLED;
}

/*  This should also be pulled from devtree  */

/*
 * time_init_deferred - called by start_kernel to set up timer/clock source
 *
 * Install the IRQ handler for the clock, setup timers.
 * This is done late, as that way, we can use ioremap().
 *
 * This runs just before the delay loop is calibrated, and
 * is used for delay calibration.
 */
void __init time_init(void)
{

	struct clock_event_device *ce_dev = &hexagon_clockevent_dev;

	struct device_node *dn;
	ce_dev->cpumask = cpu_all_mask;

	hvm_timer_freq = (cycles_t)__vmtimerop(getfreq, 0, 0);

	/*  Probably should search for a device type...  */
	dn = of_find_compatible_node(NULL, NULL, "qcom,h2-timer");
	if (dn) {
			ce_dev->irq = irq_of_parse_and_map(dn,0);
	}
	else {
		panic("%s could not find device\n", __func__);
	}

	ce_dev->cpumask = cpu_all_mask;

	/*  normally do an ioremap here of resources  */

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

/*
 * Some architectures inline this, which seems slightly insane.
 * Why burn memory to speed up a delay function?
 *
 * Note that there are hooks for MP systems to have different
 * delay values for different CPUs.  We should ultimately replace
 * the CPU_MHZ constant below with a reference to whatever per-CPU
 * state is dedicated to that role.
 */

void __udelay(unsigned long usecs)
{
	unsigned long long start = __vmtimerop(gettime, 0 , 0);
	unsigned long long finish = (hvm_timer_freq * usecs / (1000 * 1000));

	while ((__vmtimerop(gettime, 0, 0) - start) < finish)
		cpu_relax(); /*  not sure how this improves readability  */
}
EXPORT_SYMBOL(__udelay);
