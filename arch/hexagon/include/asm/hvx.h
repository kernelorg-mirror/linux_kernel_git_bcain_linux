/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HVX coprocessor context definitions for Hexagon
 *
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#ifndef __ASM_HVX_H
#define __ASM_HVX_H

#define HVX_CTXT_LOCK_MASK 0x1

struct hvx_ctxt_status {
	atomic_t flags;  /*  Only manipulate with atomic_cmpxchg  */
	struct thread_info *thread;
	unsigned long generation;
};

struct hvx_threadinfo {
	int cnum;	/*  Context # if thread is holding a ctxt reservation; -1 if not  */
	int prev_cnum;
	struct HVX_Vectors *vregs;
	unsigned long generation;
};

extern void restore_hvx_context(struct HVX_Vectors *vregs);
extern void save_hvx_context(struct HVX_Vectors *vregs);

#endif /* __ASM_HVX_H */
