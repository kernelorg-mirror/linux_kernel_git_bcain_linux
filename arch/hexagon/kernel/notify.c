// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon thread event notification support
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/notifier.h>
#include <asm/notify.h>

static ATOMIC_NOTIFIER_HEAD(atomic_thread_notifier_list);
static BLOCKING_NOTIFIER_HEAD(blocking_thread_notifier_list);

atomic_t coproc_notify_cnt;

#define DEFINE_THREAD_EVENT_FUNCS(name)						\
int name ## _thread_register_notify(struct notifier_block *nb)			\
{										\
	return name ## _notifier_chain_register(				\
			&name ## _thread_notifier_list, nb);			\
}										\
										\
int name ## _thread_unregister_notify(struct notifier_block *nb)		\
{										\
	return name ## _notifier_chain_unregister(				\
			&name ## _thread_notifier_list, nb);			\
}										\
										\
int name ## _thread_notify(void *arg, unsigned long action)			\
{										\
	return name ## _notifier_call_chain(					\
			&name ## _thread_notifier_list, action, arg);		\
}

DEFINE_THREAD_EVENT_FUNCS(atomic);
DEFINE_THREAD_EVENT_FUNCS(blocking);
