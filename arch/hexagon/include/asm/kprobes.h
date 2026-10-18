/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_HEXAGON_KPROBES_H
#define _ASM_HEXAGON_KPROBES_H

#include <asm-generic/kprobes.h>

/* Immediates of the trap0 instructions that kprobes uses. */
#define KPROBE_TRAP_BREAK		0xdc
#define KPROBE_TRAP_STEP_DONE		0xdd
#define KPROBE_TRAP_KRETPROBE		0xde

#ifdef CONFIG_KPROBES
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/percpu.h>

/*
 * A probe replaces the first word of a packet with a trap0.  The packet is
 * run from a slot instead, where a second trap0 follows it.  These are the
 * words of the longest packet, and that trap0.
 */
#define __ARCH_WANT_KPROBES_INSN_SLOT
#define MAX_INSN_SIZE			5

#define flush_insn_slot(p)		do { } while (0)
#define kretprobe_blacklist_size	0

/* trap0(#KPROBE_TRAP_BREAK), and the same for the other two */
#define KPROBE_BREAK_INSN		0x5400db10
#define KPROBE_STEP_DONE_INSN		0x5400db14

typedef u32 kprobe_opcode_t;

struct arch_specific_insn {
	/* The slot where the probed packet is run from */
	kprobe_opcode_t *insn;
	/* The probed packet's size in words */
	int nwords;
};

struct prev_kprobe {
	struct kprobe *kp;
	unsigned int status;
};

/* per-cpu kprobe control block */
struct kprobe_ctlblk {
	unsigned int kprobe_status;
	unsigned long saved_vmest;
	struct prev_kprobe prev_kprobe;
};

void arch_remove_kprobe(struct kprobe *p);
int kprobe_fault_handler(struct pt_regs *regs, unsigned int trapnr);
bool kprobe_breakpoint_handler(struct pt_regs *regs);
bool kprobe_step_done_handler(struct pt_regs *regs);
bool kretprobe_trampoline_trap_handler(struct pt_regs *regs);
#else
static inline bool kprobe_breakpoint_handler(struct pt_regs *regs)
{
	return false;
}

static inline bool kprobe_step_done_handler(struct pt_regs *regs)
{
	return false;
}

static inline bool kretprobe_trampoline_trap_handler(struct pt_regs *regs)
{
	return false;
}
#endif /* CONFIG_KPROBES */
#endif /* _ASM_HEXAGON_KPROBES_H */
