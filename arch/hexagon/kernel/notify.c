/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/notifier.h>
#include <asm/notify.h>

static ATOMIC_NOTIFIER_HEAD(atomic_thread_notifier_list);
static BLOCKING_NOTIFIER_HEAD(blocking_thread_notifier_list);

atomic_t coproc_notify_cnt;

#define DEFINE_THREAD_EVENT_FUNCS(name) \
int name ## _thread_register_notify(struct notifier_block *nb)\
{\
	return name ## _notifier_chain_register(&name ## _thread_notifier_list,\
						nb);\
}\
\
int name ## _thread_unregister_notify(struct notifier_block *nb)\
{\
	return name ## _notifier_chain_unregister(&name ## _thread_notifier_list,\
							nb);\
}\
\
int name ## _thread_notify(void *arg, unsigned long action)\
{\
	return name ## _notifier_call_chain(&name ## _thread_notifier_list, action,\
						arg);\
}

DEFINE_THREAD_EVENT_FUNCS(atomic);
DEFINE_THREAD_EVENT_FUNCS(blocking);
