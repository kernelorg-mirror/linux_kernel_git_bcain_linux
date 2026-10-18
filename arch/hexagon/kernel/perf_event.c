// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon PMU support for perf
 *
 * The core has four counters (PMUCNT0-3) that count events chosen in
 * PMUEVTCFG, one byte per counter, for all of the hardware threads of the
 * core together.  They belong to the hypervisor: a guest reads the counters
 * directly, as global registers, and writes the PMU registers through the
 * hypervisor's PMU call.
 *
 * So this is a PMU of the whole core, like an uncore PMU: it counts, on
 * behalf of CPU 0, what the core does, whichever CPU or task does it, and it
 * raise an interrupt when they overflow.  The driver folds what the counters
 * have counted into the events before it changes what they count and clears
 * them.  The counters are 32 bits wide, so a timer reads them often enough
 * that they do not wrap unseen.
 *
 * Sampling is done by that same timer, which takes a sample for each event
 * that has counted a sample period since the last one.  The sample is taken
 * where the timer interrupted CPU 0, so it is only a rough statistical
 * profile of the core: it skids by up to a timer period, shows CPU 0's
 * context whichever thread caused the count, and takes at most one sample per
 * event per timer period.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/perf_event.h>
#include <linux/spinlock.h>
#include <asm/hexagon_vm.h>

#define PMU_COUNTERS		4
#define PMU_EVENT_MASK		0xff

/* The operations and registers of the hypervisor's PMU call. */
#define PMUCTRL_SETREG		1
#define PMUCTRL_GETREG		2
#define PMU_REG_EVTCFG		8
#define PMU_REG_CFG		10

/* The counters must be read at least this often: a 32 bit counter at 1 GHz. */
#define PMU_POLL_MS		1000
/* While events sample, the counters are checked this often. */
#define PMU_SAMPLE_POLL_US	1000

struct hexagon_pmu {
	struct pmu pmu;
	raw_spinlock_t lock;
	struct hrtimer timer;
	struct perf_event *events[PMU_COUNTERS];
	u32 evtcfg;
	int nr_active;
	int nr_sampling;
};

static struct hexagon_pmu hexagon_pmu;

static inline u32 pmu_getreg(long reg)
{
	return __vmpmucfg(PMUCTRL_GETREG, 0, reg, 0);
}

static inline void pmu_setreg(long reg, u32 val)
{
	__vmpmucfg(PMUCTRL_SETREG, 0, reg, val);
}

/* The counters are global registers, which a guest can read. */
static u32 pmu_read_counter(int idx)
{
	u32 val;

	switch (idx) {
	case 0:
		asm volatile("%0 = g26" : "=r"(val));
		break;
	case 1:
		asm volatile("%0 = g27" : "=r"(val));
		break;
	case 2:
		asm volatile("%0 = g28" : "=r"(val));
		break;
	default:
		asm volatile("%0 = g29" : "=r"(val));
		break;
	}

	return val;
}

/* Add what a counter has counted since it was last read to its event. */
static void pmu_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u32 now = pmu_read_counter(hwc->idx);
	u64 prev = local64_xchg(&hwc->prev_count, now);
	u32 delta = now - prev;

	local64_add(delta, &event->count);
	if (is_sampling_event(event))
		local64_sub(delta, &hwc->period_left);
}

/* Start a new sample period, keeping the part of the last one that overran. */
static void pmu_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 period = hwc->sample_period;
	s64 left = local64_read(&hwc->period_left);

	if (left <= -period)
		left = period;
	else if (left <= 0)
		left += period;
	hwc->last_period = period;
	local64_set(&hwc->period_left, left);
}

static ktime_t pmu_poll_interval(struct hexagon_pmu *pmu)
{
	if (pmu->nr_sampling)
		return ns_to_ktime(PMU_SAMPLE_POLL_US * NSEC_PER_USEC);
	return ms_to_ktime(PMU_POLL_MS);
}

static void pmu_update_all(struct hexagon_pmu *pmu)
{
	int i;

	for (i = 0; i < PMU_COUNTERS; i++)
		if (pmu->events[i] && pmu->events[i]->hw.state == 0)
			pmu_update(pmu->events[i]);
}

/*
 * Program the counters with the events that are counting, and clear them.  The
 * caller has updated the events from the counters, so what they counted is not
 * lost, apart from what they count between that update and the clearing here.
 */
static void pmu_program(struct hexagon_pmu *pmu)
{
	u32 cfg = 0;
	int i;

	for (i = 0; i < PMU_COUNTERS; i++) {
		struct perf_event *event = pmu->events[i];

		if (event && event->hw.state == 0)
			cfg |= (event->hw.config & PMU_EVENT_MASK) << (8 * i);
	}

	pmu_setreg(PMU_REG_EVTCFG, 0);
	for (i = 0; i < PMU_COUNTERS; i++) {
		pmu_setreg(i, 0);
		if (pmu->events[i])
			local64_set(&pmu->events[i]->hw.prev_count, 0);
	}
	pmu_setreg(PMU_REG_EVTCFG, cfg);
	pmu->evtcfg = cfg;
}

static enum hrtimer_restart pmu_timer(struct hrtimer *timer)
{
	struct hexagon_pmu *pmu = container_of(timer, struct hexagon_pmu, timer);
	struct perf_event *overflowed[PMU_COUNTERS];
	struct pt_regs *regs = get_irq_regs();
	unsigned long flags;
	int i, n = 0;

	raw_spin_lock_irqsave(&pmu->lock, flags);
	pmu_update_all(pmu);
	for (i = 0; i < PMU_COUNTERS; i++) {
		struct perf_event *event = pmu->events[i];

		if (event && event->hw.state == 0 && is_sampling_event(event) &&
		    local64_read(&event->hw.period_left) <= 0) {
			pmu_set_period(event);
			overflowed[n++] = event;
		}
	}
	raw_spin_unlock_irqrestore(&pmu->lock, flags);

	/* The overflow handling can stop the event, which takes the lock. */
	while (n--) {
		struct perf_sample_data data;

		perf_sample_data_init(&data, 0, overflowed[n]->hw.last_period);
		perf_event_overflow(overflowed[n], &data, regs);
	}

	hrtimer_forward_now(timer, pmu_poll_interval(pmu));

	return HRTIMER_RESTART;
}

static void pmu_start(struct perf_event *event, int flags)
{
	struct hexagon_pmu *pmu = &hexagon_pmu;
	unsigned long irqflags;

	raw_spin_lock_irqsave(&pmu->lock, irqflags);
	pmu_update_all(pmu);
	if (flags & PERF_EF_RELOAD) {
		local64_set(&event->hw.period_left, 0);
		pmu_set_period(event);
	}
	event->hw.state = 0;
	pmu_program(pmu);
	raw_spin_unlock_irqrestore(&pmu->lock, irqflags);
}

static void pmu_stop(struct perf_event *event, int flags)
{
	struct hexagon_pmu *pmu = &hexagon_pmu;
	unsigned long irqflags;

	raw_spin_lock_irqsave(&pmu->lock, irqflags);
	if (event->hw.state == 0) {
		pmu_update_all(pmu);
		event->hw.state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
		pmu_program(pmu);
	}
	raw_spin_unlock_irqrestore(&pmu->lock, irqflags);
}

static int pmu_add(struct perf_event *event, int flags)
{
	struct hexagon_pmu *pmu = &hexagon_pmu;
	unsigned long irqflags;
	int i, ret = -EAGAIN;

	raw_spin_lock_irqsave(&pmu->lock, irqflags);
	for (i = 0; i < PMU_COUNTERS; i++) {
		if (pmu->events[i])
			continue;

		pmu->events[i] = event;
		event->hw.idx = i;
		event->hw.state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
		if (is_sampling_event(event))
			pmu->nr_sampling++;
		if (!pmu->nr_active++ || (is_sampling_event(event) &&
					  pmu->nr_sampling == 1))
			hrtimer_start(&pmu->timer, pmu_poll_interval(pmu),
				      HRTIMER_MODE_REL_PINNED);
		ret = 0;
		break;
	}
	raw_spin_unlock_irqrestore(&pmu->lock, irqflags);

	if (!ret && (flags & PERF_EF_START))
		pmu_start(event, PERF_EF_START);

	return ret;
}

static void pmu_del(struct perf_event *event, int flags)
{
	struct hexagon_pmu *pmu = &hexagon_pmu;
	unsigned long irqflags;

	pmu_stop(event, PERF_EF_UPDATE);

	raw_spin_lock_irqsave(&pmu->lock, irqflags);
	pmu->events[event->hw.idx] = NULL;
	if (is_sampling_event(event))
		pmu->nr_sampling--;
	if (!--pmu->nr_active)
		hrtimer_try_to_cancel(&pmu->timer);
	raw_spin_unlock_irqrestore(&pmu->lock, irqflags);
}

static void pmu_read(struct perf_event *event)
{
	struct hexagon_pmu *pmu = &hexagon_pmu;
	unsigned long irqflags;

	raw_spin_lock_irqsave(&pmu->lock, irqflags);
	if (event->hw.state == 0)
		pmu_update(event);
	raw_spin_unlock_irqrestore(&pmu->lock, irqflags);
}

static int pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* The sample is where the timer interrupted, not where the event was. */
	if (event->attr.precise_ip)
		return -EOPNOTSUPP;
	if (event->attach_state & PERF_ATTACH_TASK)
		return -EINVAL;
	if (event->cpu < 0)
		return -EINVAL;
	event->cpu = 0;

	hwc->config = event->attr.config & PMU_EVENT_MASK;
	if (!hwc->config)
		return -EINVAL;

	return 0;
}

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%*pbl\n", cpumask_pr_args(cpumask_of(0)));
}
static DEVICE_ATTR_RO(cpumask);

static struct attribute *pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL,
};

static const struct attribute_group pmu_cpumask_group = {
	.attrs = pmu_cpumask_attrs,
};

PMU_FORMAT_ATTR(event, "config:0-7");

static struct attribute *pmu_format_attrs[] = {
	&format_attr_event.attr,
	NULL,
};

static const struct attribute_group pmu_format_group = {
	.name = "format",
	.attrs = pmu_format_attrs,
};

/*
 * Names for some of the core's events.  The event numbers are the ones of the
 * PMUEVTCFG byte.  These were identified by counting them over loops with a
 * known mix of instructions on a v68 core; any other event can be counted by
 * its number: perf stat -a -e hexagon/event=0x40/.
 */
PMU_EVENT_ATTR_STRING(committed_packets, ev_committed_packets, "event=0x03");
PMU_EVENT_ATTR_STRING(committed_insts, ev_committed_insts, "event=0x2a");
PMU_EVENT_ATTR_STRING(committed_loads, ev_committed_loads, "event=0x30");
PMU_EVENT_ATTR_STRING(committed_stores, ev_committed_stores, "event=0x31");
PMU_EVENT_ATTR_STRING(running_cycles, ev_running_cycles, "event=0x34");
PMU_EVENT_ATTR_STRING(endloop_packets, ev_endloop_packets, "event=0x39");
PMU_EVENT_ATTR_STRING(dcache_misses, ev_dcache_misses, "event=0x13");

static struct attribute *pmu_event_attrs[] = {
	&ev_committed_packets.attr.attr,
	&ev_committed_insts.attr.attr,
	&ev_committed_loads.attr.attr,
	&ev_committed_stores.attr.attr,
	&ev_running_cycles.attr.attr,
	&ev_endloop_packets.attr.attr,
	&ev_dcache_misses.attr.attr,
	NULL,
};

static const struct attribute_group pmu_events_group = {
	.name = "events",
	.attrs = pmu_event_attrs,
};

static const struct attribute_group *pmu_attr_groups[] = {
	&pmu_cpumask_group,
	&pmu_format_group,
	&pmu_events_group,
	NULL,
};

static int __init hexagon_pmu_init(void)
{
	struct hexagon_pmu *pmu = &hexagon_pmu;
	int ret;

	raw_spin_lock_init(&pmu->lock);
	hrtimer_setup(&pmu->timer, pmu_timer, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL_PINNED);

	pmu->pmu = (struct pmu) {
		.task_ctx_nr	= perf_invalid_context,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
		.attr_groups	= pmu_attr_groups,
		.event_init	= pmu_event_init,
		.add		= pmu_add,
		.del		= pmu_del,
		.start		= pmu_start,
		.stop		= pmu_stop,
		.read		= pmu_read,
	};

	/* Leave the counters stopped and clear until an event asks for them. */
	pmu_setreg(PMU_REG_EVTCFG, 0);

	ret = perf_pmu_register(&pmu->pmu, "hexagon", -1);
	if (ret)
		pr_err("hexagon PMU: registration failed: %d\n", ret);
	else
		pr_info("hexagon PMU: %d counters, PMUCFG %#x\n", PMU_COUNTERS,
			pmu_getreg(PMU_REG_CFG));

	return ret;
}
early_initcall(hexagon_pmu_init);
