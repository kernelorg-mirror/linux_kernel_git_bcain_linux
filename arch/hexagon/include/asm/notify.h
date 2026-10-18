/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thread event notification interface for Hexagon, used by the coprocessor
 * (HVX, HMX) context managers
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_NOTIFY_H
#define _ASM_NOTIFY_H

#define THREAD_EVENT_COPY		0
#define THREAD_EVENT_RELEASE		1
#define THREAD_EVENT_ENTRY		2
#define THREAD_EVENT_FAULT_COPROC	3
#define THREAD_EVENT_DOWORK		4
#define THREAD_EVENT_RESTOREALL		5
#define THREAD_EVENT_FAULT_HLX		6

#ifndef __ASSEMBLY__

#include <linux/types.h>

struct notifier_block;

/*
 * A coprocessor context manager that takes a THREAD_EVENT_FAULT_COPROC calls
 * coproc_fault_claim(); the faulting code finds out whether anyone did with
 * coproc_fault_claimed().
 */
void coproc_fault_claim(void);
void coproc_fault_reset(void);
bool coproc_fault_claimed(void);

/*
 * The events pass the thread_info of the thread concerned: the current one, or
 * for THREAD_EVENT_COPY the new one, or for THREAD_EVENT_RELEASE the one that
 * is being freed.
 *
 * The atomic chain is called with interrupts off.  The blocking chain is
 * called with interrupts on, and may sleep: THREAD_EVENT_DOWORK.
 */
int atomic_thread_register_notify(struct notifier_block *nb);
int atomic_thread_unregister_notify(struct notifier_block *nb);
int atomic_thread_notify(void *arg, unsigned long action);

int blocking_thread_register_notify(struct notifier_block *nb);
int blocking_thread_unregister_notify(struct notifier_block *nb);
int blocking_thread_notify(void *arg, unsigned long action);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_NOTIFY_H */
