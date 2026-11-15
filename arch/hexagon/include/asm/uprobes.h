/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_HEXAGON_UPROBES_H
#define _ASM_HEXAGON_UPROBES_H

#include <linux/types.h>

/* One complete packet, followed by a completion trap in a 32-byte slot. */
#define MAX_UINSN_BYTES		16
#define UPROBE_XOL_SLOT_BYTES	32
#define UPROBE_SWBP_INSN_SIZE	4
#define UPROBE_TRAP_BREAK	0xd9
#define UPROBE_TRAP_STEP		0xda
#define UPROBE_SWBP_INSN		0x5400db04 /* trap0(#0xd9) */
#define UPROBE_STEP_INSN		0x5400db08 /* trap0(#0xda) */

typedef u32 uprobe_opcode_t;

struct arch_uprobe {
	u32 insn[MAX_UINSN_BYTES / sizeof(u32)];
	u32 ixol[UPROBE_XOL_SLOT_BYTES / sizeof(u32)];
	unsigned int nwords;
};

struct arch_uprobe_task {
	unsigned long xol_end;
	bool trapped;
	bool saved_singlestep;
};

struct pt_regs;
#ifdef CONFIG_UPROBES
bool hexagon_uprobe_breakpoint(struct pt_regs *regs);
bool hexagon_uprobe_step(struct pt_regs *regs);
void hexagon_uprobe_exception(struct pt_regs *regs);
#else
static inline bool hexagon_uprobe_breakpoint(struct pt_regs *regs)
{
	return false;
}

static inline bool hexagon_uprobe_step(struct pt_regs *regs)
{
	return false;
}

static inline void hexagon_uprobe_exception(struct pt_regs *regs) {}
#endif

#endif
