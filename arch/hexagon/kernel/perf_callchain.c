// SPDX-License-Identifier: GPL-2.0-only
/*
 * Callchains for perf, and so for BPF's stack helpers
 *
 * Every Hexagon frame has the caller's frame pointer and the return address
 * stored at the frame pointer, by allocframe.
 */

#include <linux/perf_event.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>
#include <asm/ptrace.h>

struct stackframe {
	unsigned long fp;
	unsigned long lr;
};

void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
			 struct pt_regs *regs)
{
	unsigned long fp = regs->r30;
	struct stackframe frame;

	perf_callchain_store(entry, instruction_pointer(regs));

	while (entry->nr < entry->max_stack) {
		if (fp & 0x3 || !fp)
			break;
		if (copy_from_user_nofault(&frame, (void __user *)fp,
					   sizeof(frame)))
			break;
		if (!frame.lr || perf_callchain_store(entry, frame.lr))
			break;
		/* Frames are linked towards the base of the stack. */
		if (frame.fp <= fp)
			break;
		fp = frame.fp;
	}
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	unsigned long low = (unsigned long)task_stack_page(current);
	unsigned long high = low + THREAD_SIZE - sizeof(struct stackframe);
	unsigned long fp = regs->r30;

	low += sizeof(struct thread_info);
	perf_callchain_store(entry, instruction_pointer(regs));

	while (entry->nr < entry->max_stack) {
		const struct stackframe *frame = (const void *)fp;
		unsigned long lr, next;

		if (fp & 0x3 || fp < low || fp > high)
			break;
		next = frame->fp;
		lr = frame->lr;

		/*
		 * A zero frame pointer is the bottom of the frames of one
		 * event, with the pt_regs of the interrupted context above it.
		 */
		if (!next) {
			const struct pt_regs *saved = (const void *)(fp + 8);

			if (user_mode(saved) || (unsigned long)(saved + 1) > high)
				break;
			lr = instruction_pointer(saved);
			next = saved->r30;
		}

		if (!lr || perf_callchain_store(entry, lr))
			break;
		if (next <= fp)
			break;
		fp = next;
	}
}
