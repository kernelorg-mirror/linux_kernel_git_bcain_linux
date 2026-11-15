// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hexagon user probes. Replace a packet's first word with a standalone trap,
 * run position-independent packets out of line, and trap back at their end.
 * Control flow, PC reads, hardware-loop endings and L2 duplexes are rejected.
 * No hardware single-step support is required from the hypervisor.
 */
#include <linux/highmem.h>
#include <linux/kdebug.h>
#include <linux/ptrace.h>
#include <linux/sched/signal.h>
#include <linux/uprobes.h>
#include <asm/cacheflush.h>
#include <asm/hexagon_vm.h>

#define PARSE_MASK	0xc000
#define PARSE_MORE	0x4000
#define PARSE_END	0xc000

static bool packet_end(u32 insn)
{
	return !(insn & PARSE_MASK) || (insn & PARSE_MASK) == PARSE_END;
}

int arch_uprobe_analyze_insn(struct arch_uprobe *aup, struct mm_struct *mm,
			     unsigned long addr)
{
	unsigned int i;

	if (addr & 3)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(aup->insn); i++) {
		u32 insn = aup->insn[i];
		unsigned int iclass = insn >> 28;

		if (!(insn & PARSE_MASK)) {
			unsigned int type = ((insn >> 29) << 1) |
					    ((insn >> 13) & 1);

			/* Duplex groups containing L2 may branch or return. */
			if (BIT(type) & (BIT(1) | BIT(2) | BIT(5) |
					 BIT(9) | BIT(13)))
				return -EINVAL;
		} else {
			if (iclass == 1 || iclass == 2 || iclass == 5 ||
			    iclass == 6 || (insn >> 24) == 0x96)
				return -EINVAL;
			/* Parse 10 marks a hardware-loop ending. */
			if ((insn & PARSE_MASK) != PARSE_MORE &&
			    (insn & PARSE_MASK) != PARSE_END)
				return -EINVAL;
		}

		if (packet_end(insn)) {
			aup->nwords = i + 1;
			memcpy(aup->ixol, aup->insn, (i + 1) * sizeof(u32));
			aup->ixol[i + 1] = UPROBE_STEP_INSN;
			return 0;
		}
	}
	return -EINVAL;
}

/* Check the boundary on every mapping, even if analysis was cached. */
int set_swbp(struct arch_uprobe *aup, struct vm_area_struct *vma,
	     unsigned long addr)
{
	struct page *page;
	u32 prev;
	int ret;

	if (addr & 3)
		return -EINVAL;
	if (addr > vma->vm_start) {
		ret = get_user_pages_remote(vma->vm_mm, addr - 4, 1,
					    FOLL_FORCE, &page, NULL);
		if (ret != 1)
			return ret < 0 ? ret : -EFAULT;
		uprobe_copy_from_page(page, addr - 4, &prev, sizeof(prev));
		put_page(page);
		/* A probe could hide a continuing packet's original first word. */
		if (!packet_end(prev) || prev == UPROBE_SWBP_INSN)
			return -EINVAL;
	} else if (vma->vm_pgoff) {
		/* Cannot establish a boundary before the executable mapping. */
		return -EINVAL;
	}
	return uprobe_write_opcode(aup, vma, addr, UPROBE_SWBP_INSN, true);
}

bool is_swbp_insn(uprobe_opcode_t *insn)
{
	return *insn == UPROBE_SWBP_INSN;
}

void arch_uprobe_copy_ixol(struct page *page, unsigned long vaddr,
			   void *src, unsigned long len)
{
	void *map = kmap_local_page(page);
	void *dst = map + offset_in_page(vaddr);

	memcpy(dst, src, len);
	flush_icache_range((unsigned long)dst, (unsigned long)dst + len);
	kunmap_local(map);
}

int arch_uprobe_pre_xol(struct arch_uprobe *aup, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	utask->autask.trapped = false;
	utask->autask.xol_end = utask->xol_vaddr +
				(aup->nwords + 1) * sizeof(u32);
	utask->autask.saved_singlestep =
		!!(regs->hvmer.vmest & BIT(HVM_VMEST_SS_SFT));
	pt_clr_singlestep(regs);
	instruction_pointer_set(regs, utask->xol_vaddr);
	return 0;
}

static void restore_singlestep(struct pt_regs *regs)
{
	if (current->utask->autask.saved_singlestep)
		pt_set_singlestep(regs);
}

int arch_uprobe_post_xol(struct arch_uprobe *aup, struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;

	instruction_pointer_set(regs, utask->vaddr + aup->nwords * sizeof(u32));
	restore_singlestep(regs);
	if (utask->autask.saved_singlestep)
		force_sig_fault(SIGTRAP, TRAP_TRACE,
				(void __user *)instruction_pointer(regs));
	return 0;
}

void arch_uprobe_abort_xol(struct arch_uprobe *aup, struct pt_regs *regs)
{
	instruction_pointer_set(regs, current->utask->vaddr);
	restore_singlestep(regs);
}

bool arch_uprobe_xol_was_trapped(struct task_struct *task)
{
	return task->utask->autask.trapped;
}

bool arch_uprobe_skip_sstep(struct arch_uprobe *aup, struct pt_regs *regs)
{
	return false;
}

void hexagon_uprobe_exception(struct pt_regs *regs)
{
	if (user_mode(regs) && current->utask && current->utask->active_uprobe)
		current->utask->autask.trapped = true;
}

bool hexagon_uprobe_breakpoint(struct pt_regs *regs)
{
	return user_mode(regs) && uprobe_pre_sstep_notifier(regs);
}

bool hexagon_uprobe_step(struct pt_regs *regs)
{
	struct uprobe_task *utask = current->utask;
	unsigned long pc = instruction_pointer(regs);

	if (!user_mode(regs) || !utask || !utask->active_uprobe ||
	    pc != utask->autask.xol_end)
		return false;
	return uprobe_post_sstep_notifier(regs);
}

bool arch_uretprobe_is_alive(struct return_instance *ret, enum rp_check ctx,
			     struct pt_regs *regs)
{
	if (ctx == RP_CHECK_CHAIN_CALL)
		return regs->r29 <= ret->stack;
	return regs->r29 < ret->stack;
}

unsigned long
arch_uretprobe_hijack_return_addr(unsigned long trampoline, struct pt_regs *regs)
{
	unsigned long ra = regs->r31;

	regs->r31 = trampoline;
	return ra;
}

int arch_uprobe_exception_notify(struct notifier_block *self,
				 unsigned long val, void *data)
{
	return NOTIFY_DONE;
}
