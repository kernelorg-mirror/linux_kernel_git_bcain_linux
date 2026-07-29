/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Thread support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#include <asm/page.h>

#ifndef __ASSEMBLY__
#include <asm/processor.h>
#include <asm/registers.h>
#endif

/*  If the shift is less than the kernel page size, use the page size.  */

#define ARCH_THREAD_SHIFT	13
#define THREAD_SHIFT		(ARCH_THREAD_SHIFT < PAGE_SHIFT ? PAGE_SHIFT : ARCH_THREAD_SHIFT)
#define THREAD_SIZE		(1<<THREAD_SHIFT)
#define THREAD_SIZE_ORDER	(THREAD_SHIFT - PAGE_SHIFT)

#ifndef __ASSEMBLY__

/*
 * This is union'd with the "bottom" of the kernel stack.
 * It keeps track of thread info which is handy for routines
 * to access quickly.
 */

struct thread_info {
	struct task_struct	*task;		/* main task structure */
	unsigned long		flags;          /* low level flags */
	__u32                   cpu;            /* current cpu */
	int                     preempt_count;  /* 0=>preemptible,<0=>BUG */
	/*
	 * used for syscalls somehow;
	 * seems to have a function pointer and four arguments
	 */
	/* Points to the current pt_regs frame  */
	struct pt_regs		*regs;
	/*
	 * saved kernel sp at switch_to time;
	 * not sure if this is used (it's not in the VM model it seems;
	 * see thread_struct)
	 */
	unsigned long		sp;
};

#else /* !__ASSEMBLY__ */

#include <asm/asm-offsets.h>

#endif  /* __ASSEMBLY__  */

#ifndef __ASSEMBLY__

#define INIT_THREAD_INFO(tsk)                   \
{                                               \
	.task           = &tsk,                 \
	.flags          = 0,                    \
	.cpu            = 0,                    \
	.preempt_count  = 1,                    \
	.sp = 0,				\
	.regs = NULL,			\
}

/* Tacky preprocessor trickery */
#define	qqstr(s) qstr(s)
#define qstr(s) #s
#define QUOTED_THREADINFO_REG qqstr(THREADINFO_REG)

/*
 * The thread-info register changes under us at every context switch, so the
 * read must not be treated as a pure expression the compiler may hoist or
 * reuse across a call to switch_to().
 */
static inline struct thread_info *current_thread_info(void)
{
	struct thread_info *ti;

	asm volatile("%0 = " QUOTED_THREADINFO_REG : "=r"(ti));

	return ti;
}

#endif /* __ASSEMBLY__ */

/*
 * thread information flags
 * - these are process state flags that various assembly files
 *   may need to access
 * - pending work-to-be-done flags are in LSW
 * - other flags in MSW
 */

#define TIF_SYSCALL_TRACE       0       /* syscall trace active */
#define TIF_NOTIFY_RESUME       1       /* resumption notification requested */
#define TIF_SIGPENDING          2       /* signal pending */
#define TIF_NEED_RESCHED        3       /* rescheduling necessary */
#define TIF_SINGLESTEP          4       /* restore ss @ return to usr mode */
#define TIF_RESTORE_SIGMASK     6       /* restore sig mask in do_signal() */
#define TIF_NOTIFY_SIGNAL	7       /* signal notifications exist */
/* true if poll_idle() is polling TIF_NEED_RESCHED */
#define TIF_MEMDIE              17      /* OOM killer killed process */

#define _TIF_SYSCALL_TRACE      (1 << TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME      (1 << TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING         (1 << TIF_SIGPENDING)
#define _TIF_NEED_RESCHED       (1 << TIF_NEED_RESCHED)
#define _TIF_SINGLESTEP         (1 << TIF_SINGLESTEP)
#define _TIF_NOTIFY_SIGNAL	(1 << TIF_NOTIFY_SIGNAL)

/* work to do on interrupt/exception return - All but TIF_SYSCALL_TRACE */
#define _TIF_WORK_MASK          (0x0000FFFF & ~_TIF_SYSCALL_TRACE & ~_TIF_SINGLESTEP)

/* work to do on any return to u-space */
#define _TIF_ALLWORK_MASK       0x0000FFFF

#endif /* __KERNEL__ */

#endif
