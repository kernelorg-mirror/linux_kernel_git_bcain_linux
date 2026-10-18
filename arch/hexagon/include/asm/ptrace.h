/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ptrace definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_HEXAGON_PTRACE_H
#define _ASM_HEXAGON_PTRACE_H

#include <uapi/asm/ptrace.h>
#include <asm/thread_info.h>

/* kprobe-based event tracer support */
extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);

/*
 * The event entry code saves the interrupted SP in pt_regs, whether the event
 * came from user or kernel mode.
 */
#define kernel_stack_pointer(regs)	((regs)->r29)

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->r00;
}

static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	pt_set_elr(regs, val);
}

static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (offset > sizeof(struct pt_regs) - sizeof(unsigned long))
		return 0;
	return *(unsigned long *)((unsigned long)regs + offset);
}

static inline bool regs_within_kernel_stack(struct pt_regs *regs,
					    unsigned long addr)
{
	return (addr & ~(THREAD_SIZE - 1)) ==
	       (kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1));
}

static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
						      unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	return 0;
}

/* The first six arguments are passed in R0-R5. */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
						     unsigned int n)
{
	if (n < 6)
		return (&regs->r00)[n];
	return regs_get_kernel_stack_nth(regs, n - 6);
}

#define current_pt_regs() \
	((struct pt_regs *) \
	 ((unsigned long)current_thread_info() + THREAD_SIZE) - 1)

#if CONFIG_HEXAGON_ARCH_VERSION >= 4
#define arch_has_single_step()	(1)
#endif

#endif
