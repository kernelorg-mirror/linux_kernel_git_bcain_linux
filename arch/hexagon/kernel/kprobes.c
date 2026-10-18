// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel probes for Hexagon
 *
 * The first word of the probed packet is replaced by a trap0.  A packet's
 * first word alone says whether the packet goes on, so replacing it with a
 * word that ends the packet is safe for anything that is running through it.
 * The packet itself is run from a slot, followed by another trap0 that brings
 * execution back to the packet after the probed one.
 *
 * A packet is only probed if running it from elsewhere does not matter: it
 * must not read the PC, or branch, or end a hardware loop.
 */

#include <linux/kprobes.h>
#include <linux/extable.h>
#include <linux/kdebug.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>
#include <asm/hexagon_vm.h>

#include <asm/ptrace.h>

DEFINE_PER_CPU(struct kprobe *, current_kprobe) = NULL;
DEFINE_PER_CPU(struct kprobe_ctlblk, kprobe_ctlblk);

#define INSN_PARSE_MASK		0x0000c000
#define INSN_PARSE_MORE		0x00004000
#define INSN_PARSE_END		0x0000c000

#define INSN_ICLASS(insn)	((insn) >> 28)
#define INSN_ICLASS_J_CMP	0x1	/* compare and jump */
#define INSN_ICLASS_J_NV	0x2	/* new-value compare and jump */
#define INSN_ICLASS_J		0x5	/* jumps, calls and traps */
#define INSN_ICLASS_CR		0x6	/* loops, control registers, add(pc) */
#define INSN_DEALLOC_RETURN	0x96	/* top byte of the dealloc_returns */

/*
 * A duplex packs two sub-instructions in a word, and the parse bits of that
 * word are zero.  The types of duplex are in the bits 31:29 and 13, and the
 * sub-instruction groups L2 are the only ones with a jump, jumpr r31 and
 * dealloc_return.
 */
#define DUPLEX_TYPE(insn)	((((insn) >> 29) << 1) | (((insn) >> 13) & 1))
#define DUPLEX_HAS_L2(insn)	(BIT(DUPLEX_TYPE(insn)) & \
				 (BIT(0x1) | BIT(0x2) | BIT(0x5) | BIT(0x9) | \
				  BIT(0xd)))

/* The number of words of the packet at addr, or -EINVAL if it is unusable. */
static int probed_packet_words(const kprobe_opcode_t *addr)
{
	int i;

	for (i = 0; i < MAX_INSN_SIZE - 1; i++) {
		kprobe_opcode_t insn = addr[i];

		if (!(insn & INSN_PARSE_MASK))
			return DUPLEX_HAS_L2(insn) ? -EINVAL : i + 1;

		switch (INSN_ICLASS(insn)) {
		case INSN_ICLASS_J_CMP:
		case INSN_ICLASS_J_NV:
		case INSN_ICLASS_J:
		case INSN_ICLASS_CR:
			return -EINVAL;
		}
		if ((insn >> 24) == INSN_DEALLOC_RETURN)
			return -EINVAL;

		switch (insn & INSN_PARSE_MASK) {
		case INSN_PARSE_MORE:
			break;
		case INSN_PARSE_END:
			return i + 1;
		default:
			/* the end of a hardware loop */
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static void patch_insn(kprobe_opcode_t *addr, kprobe_opcode_t insn)
{
	WRITE_ONCE(*addr, insn);
	flush_icache_range((unsigned long)addr,
			   (unsigned long)addr + sizeof(insn));
}

int __kprobes arch_prepare_kprobe(struct kprobe *p)
{
	kprobe_opcode_t *slot;
	int nwords;

	if ((unsigned long)p->addr & (sizeof(kprobe_opcode_t) - 1))
		return -EINVAL;

	/* The word before must end a packet, or this is not a packet's start. */
	if ((p->addr[-1] & INSN_PARSE_MASK) == INSN_PARSE_MORE)
		return -EINVAL;

	nwords = probed_packet_words(p->addr);
	if (nwords < 0)
		return nwords;

	slot = get_insn_slot();
	if (!slot)
		return -ENOMEM;

	memcpy(slot, p->addr, nwords * sizeof(kprobe_opcode_t));
	slot[nwords] = KPROBE_STEP_DONE_INSN;
	flush_icache_range((unsigned long)slot,
			   (unsigned long)(slot + nwords + 1));

	p->ainsn.insn = slot;
	p->ainsn.nwords = nwords;
	p->opcode = *p->addr;
	return 0;
}

void __kprobes arch_arm_kprobe(struct kprobe *p)
{
	patch_insn(p->addr, KPROBE_BREAK_INSN);
}

void __kprobes arch_disarm_kprobe(struct kprobe *p)
{
	patch_insn(p->addr, p->opcode);
}

void __kprobes arch_remove_kprobe(struct kprobe *p)
{
	if (p->ainsn.insn) {
		free_insn_slot(p->ainsn.insn, 0);
		p->ainsn.insn = NULL;
	}
}

static void __kprobes save_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	kcb->prev_kprobe.kp = kprobe_running();
	kcb->prev_kprobe.status = kcb->kprobe_status;
}

static void __kprobes restore_previous_kprobe(struct kprobe_ctlblk *kcb)
{
	__this_cpu_write(current_kprobe, kcb->prev_kprobe.kp);
	kcb->kprobe_status = kcb->prev_kprobe.status;
}

static void __kprobes set_current_kprobe(struct kprobe *p)
{
	__this_cpu_write(current_kprobe, p);
}

/*
 * Interrupts must stay disabled from the return from the trap to the one that
 * ends the step, or the interrupt would run between them.  The VM restores
 * the interrupt state from the saved status on return.
 */
static void __kprobes save_local_irqflag(struct kprobe_ctlblk *kcb,
					 struct pt_regs *regs)
{
	kcb->saved_vmest = regs->hvmer.vmest;
	regs->hvmer.vmest &= ~(HVM_VMEST_IE_MSK << HVM_VMEST_IE_SFT);
}

static void __kprobes restore_local_irqflag(struct kprobe_ctlblk *kcb,
					    struct pt_regs *regs)
{
	regs->hvmer.vmest = kcb->saved_vmest;
}

static void __kprobes setup_singlestep(struct kprobe *p, struct pt_regs *regs,
				       struct kprobe_ctlblk *kcb, int reenter)
{
	if (reenter) {
		save_previous_kprobe(kcb);
		set_current_kprobe(p);
		kcb->kprobe_status = KPROBE_REENTER;
	} else {
		kcb->kprobe_status = KPROBE_HIT_SS;
	}

	save_local_irqflag(kcb, regs);
	instruction_pointer_set(regs, (unsigned long)p->ainsn.insn);
}

static int __kprobes reenter_kprobe(struct kprobe *p, struct pt_regs *regs,
				    struct kprobe_ctlblk *kcb)
{
	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SSDONE:
	case KPROBE_HIT_ACTIVE:
		kprobes_inc_nmissed_count(p);
		setup_singlestep(p, regs, kcb, 1);
		return 1;
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		pr_warn("Failed to recover from reentered kprobes.\n");
		dump_kprobe(p);
		BUG();
		break;
	default:
		WARN_ON(1);
	}
	return 0;
}

static void __kprobes post_kprobe_handler(struct kprobe *cur,
					  struct kprobe_ctlblk *kcb,
					  struct pt_regs *regs)
{
	instruction_pointer_set(regs, (unsigned long)cur->addr +
				cur->ainsn.nwords * sizeof(kprobe_opcode_t));

	if (kcb->kprobe_status == KPROBE_REENTER) {
		restore_previous_kprobe(kcb);
		return;
	}

	kcb->kprobe_status = KPROBE_HIT_SSDONE;
	if (cur->post_handler)
		cur->post_handler(cur, regs, 0);

	reset_current_kprobe();
}

int __kprobes kprobe_fault_handler(struct pt_regs *regs, unsigned int trapnr)
{
	struct kprobe *cur = kprobe_running();
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	const struct exception_table_entry *fixup;

	switch (kcb->kprobe_status) {
	case KPROBE_HIT_SS:
	case KPROBE_REENTER:
		/*
		 * The packet being run from the slot faulted.  Go back to the
		 * probed address, and let the fault be handled as one of the
		 * packet there.
		 */
		restore_local_irqflag(kcb, regs);
		instruction_pointer_set(regs, (unsigned long)cur->addr);

		if (kcb->kprobe_status == KPROBE_REENTER)
			restore_previous_kprobe(kcb);
		else
			reset_current_kprobe();
		break;
	case KPROBE_HIT_ACTIVE:
	case KPROBE_HIT_SSDONE:
		/* The handler of the probe faulted. */
		fixup = search_exception_tables(instruction_pointer(regs));
		if (fixup) {
			instruction_pointer_set(regs, fixup->fixup);
			return 1;
		}
	}
	return 0;
}
NOKPROBE_SYMBOL(kprobe_fault_handler);

/* The trap0 left ELR at the packet after it. */
bool __kprobes kprobe_breakpoint_handler(struct pt_regs *regs)
{
	kprobe_opcode_t *addr = (kprobe_opcode_t *)instruction_pointer(regs) - 1;
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *cur = kprobe_running();
	struct kprobe *p = get_kprobe(addr);

	if (!p) {
		/*
		 * Another CPU removed the probe after this one trapped.  Run
		 * the original packet.
		 */
		if (*addr == KPROBE_BREAK_INSN)
			return false;
		instruction_pointer_set(regs, (unsigned long)addr);
		return true;
	}

	if (cur) {
		return reenter_kprobe(p, regs, kcb);
	}

	set_current_kprobe(p);
	kcb->kprobe_status = KPROBE_HIT_ACTIVE;

	/*
	 * A pre-handler that returns non-zero has changed where execution
	 * goes, and the packet is not run.
	 */
	if (!p->pre_handler || !p->pre_handler(p, regs))
		setup_singlestep(p, regs, kcb, 0);
	else
		reset_current_kprobe();
	return true;
}
NOKPROBE_SYMBOL(kprobe_breakpoint_handler);

bool __kprobes kprobe_step_done_handler(struct pt_regs *regs)
{
	struct kprobe_ctlblk *kcb = get_kprobe_ctlblk();
	struct kprobe *cur = kprobe_running();

	if (!cur || !(kcb->kprobe_status & (KPROBE_HIT_SS | KPROBE_REENTER)))
		return false;
	if (instruction_pointer(regs) != (unsigned long)(cur->ainsn.insn +
							 cur->ainsn.nwords + 1))
		return false;

	restore_local_irqflag(kcb, regs);
	post_kprobe_handler(cur, kcb, regs);
	return true;
}
NOKPROBE_SYMBOL(kprobe_step_done_handler);

int __kprobes arch_trampoline_kprobe(struct kprobe *p)
{
	return 0;
}

int __init arch_init_kprobes(void)
{
	return 0;
}

/*
 * A kretprobe makes the probed function return to a trap0, which gets the
 * kretprobe's handler to run, and then goes on to the real return address.
 */
asm(".pushsection .kprobes.text, \"ax\"\n"
    ".global __kretprobe_trampoline\n"
    "__kretprobe_trampoline:\n"
    "	trap0(#" __stringify(KPROBE_TRAP_KRETPROBE) ")\n"
    ".popsection\n");

void __kprobes arch_prepare_kretprobe(struct kretprobe_instance *ri,
				      struct pt_regs *regs)
{
	ri->ret_addr = (kprobe_opcode_t *)regs->r31;
	ri->fp = (void *)regs->r30;
	regs->r31 = (unsigned long)&__kretprobe_trampoline;
}

bool __kprobes kretprobe_trampoline_trap_handler(struct pt_regs *regs)
{
	instruction_pointer_set(regs, (unsigned long)&__kretprobe_trampoline);
	instruction_pointer_set(regs,
		kretprobe_trampoline_handler(regs, (void *)regs->r30));
	return true;
}
NOKPROBE_SYMBOL(kretprobe_trampoline_trap_handler);
