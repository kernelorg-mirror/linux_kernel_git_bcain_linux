/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shadow Call Stack support for Hexagon.
 *
 * Copyright (C) 2026 Qualcomm Innovation Center, Inc.
 */
#ifndef _ASM_HEXAGON_SCS_H
#define _ASM_HEXAGON_SCS_H

/*
 * SCSP_REG names the register the compiler uses as the shadow call stack
 * pointer.  It is passed in from arch/hexagon/Makefile (which also reserves
 * it and tells clang about it with -mscs-reg=) so that the C, assembly and
 * compiler views of it can never drift apart.
 */

#ifdef __ASSEMBLY__

#include <asm/asm-offsets.h>

#ifdef CONFIG_SHADOW_CALL_STACK

/*
 * Both of these operate on the thread_info that THREADINFO_REG points at, so
 * the caller has to have it pointing at the task being saved/restored.  The
 * shadow stack grows up, and the pointer is only ever stashed in memory here
 * and in the switch stack, so nothing else has to be spilled.
 */
#define scs_save_current \
	memw(THREADINFO_REG + #_THREAD_INFO_SCS_SP) = SCSP_REG
#define scs_load_current \
	SCSP_REG = memw(THREADINFO_REG + #_THREAD_INFO_SCS_SP)

#else /* CONFIG_SHADOW_CALL_STACK */

#define scs_save_current
#define scs_load_current

#endif /* CONFIG_SHADOW_CALL_STACK */

#else /* __ASSEMBLY__ */

#include <linux/scs.h>
#include <asm/thread_info.h>

/* Same preprocessor trickery as QUOTED_THREADINFO_REG in <asm/thread_info.h> */
#define scs_qqstr(s) scs_qstr(s)
#define scs_qstr(s) #s
#define QUOTED_SCSP_REG scs_qqstr(SCSP_REG)

/*
 * Point the shadow call stack pointer at @ti's shadow stack.  Only for a CPU
 * coming up on a stack the scheduler has not switched to yet: the boot path
 * does it in assembly in head.S, and start_secondary() calls this.
 *
 * It takes the thread_info directly rather than deriving it from current, and
 * the caller must be __noscs: until the register is loaded, *any* call made by
 * this CPU -- including an out-of-line copy of current_thread_info() -- runs
 * an SCS prologue that stores through it.
 */
#ifdef CONFIG_SHADOW_CALL_STACK
static __always_inline void scs_load_thread_info(struct thread_info *ti)
{
	void *sp = ti->scs_sp;

	__asm__ __volatile__(QUOTED_SCSP_REG " = %0;\n" : : "r"(sp));
}
#else
static __always_inline void scs_load_thread_info(struct thread_info *ti) {}
#endif

#endif /* __ASSEMBLY__ */

#endif /* _ASM_HEXAGON_SCS_H */
