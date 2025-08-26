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

#ifndef _ASM_NOTIFY_H
#define _ASM_NOTIFY_H

#define THREAD_EVENT_COPY		0
#define THREAD_EVENT_FLUSH		1
#define THREAD_EVENT_RELEASE		2
#define THREAD_EVENT_EXIT		3
#define THREAD_EVENT_ENTRY		4
#define THREAD_EVENT_FAULT_COPROC 	5
#define THREAD_EVENT_SWITCH		6
#define THREAD_EVENT_DOWORK		7
#define THREAD_EVENT_RESTOREALL		8

#ifndef __ASSEMBLY__

extern atomic_t coproc_notify_cnt;

struct notifier_block;

/*
 * Different events will pass different arg pointers; most will just use
 * current_thread_info(), but:
 * THREAD_EVENT_COPY will pass the new thread's pointer
 * THREAD_EVENT_SWITCH will pass the next task to be scheduled
 */

#define DECLARE_THREAD_EVENT_FUNCS(name) \
int name ## _thread_register_notify(struct notifier_block *nb);\
int name ## _thread_unregister_notify(struct notifier_block *nb);\
int name ## _thread_notify(void *arg, unsigned long action);

DECLARE_THREAD_EVENT_FUNCS(atomic);
DECLARE_THREAD_EVENT_FUNCS(blocking);

#endif

#endif
