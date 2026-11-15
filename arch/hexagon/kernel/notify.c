// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon thread event notification support
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/percpu.h>
#include <linux/context_tracking_irq.h>
#include <linux/irqflags.h>
#include <linux/kprobes.h>
#include <asm/notify.h>

static ATOMIC_NOTIFIER_HEAD(atomic_thread_notifier_list);
static BLOCKING_NOTIFIER_HEAD(blocking_thread_notifier_list);

static DEFINE_PER_CPU(unsigned int, coproc_claims);

/* These run on the CPU that took the fault, with interrupts off. */
void coproc_fault_claim(void)
{
	this_cpu_inc(coproc_claims);
}
EXPORT_SYMBOL(coproc_fault_claim);

void coproc_fault_reset(void)
{
	this_cpu_write(coproc_claims, 0);
}

bool coproc_fault_claimed(void)
{
	return this_cpu_read(coproc_claims) != 0;
}

int atomic_thread_register_notify(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&atomic_thread_notifier_list, nb);
}

int atomic_thread_unregister_notify(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&atomic_thread_notifier_list,
						nb);
}

int notrace atomic_thread_notify(void *arg, unsigned long action)
{
	unsigned long flags;
	int ret;

	/* Restore notifications can run after irq_exit() has returned to idle. */
	local_irq_save(flags);
	ct_irq_enter();
	ret = atomic_notifier_call_chain(&atomic_thread_notifier_list, action, arg);
	ct_irq_exit();
	local_irq_restore(flags);
	return ret;
}
NOKPROBE_SYMBOL(atomic_thread_notify);

int blocking_thread_register_notify(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&blocking_thread_notifier_list,
						nb);
}

int blocking_thread_unregister_notify(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&blocking_thread_notifier_list,
						  nb);
}

int blocking_thread_notify(void *arg, unsigned long action)
{
	return blocking_notifier_call_chain(&blocking_thread_notifier_list,
					    action, arg);
}
