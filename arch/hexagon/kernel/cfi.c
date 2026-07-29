// SPDX-License-Identifier: GPL-2.0
/*
 * Clang Control Flow Integrity (CFI) support.
 *
 * Copyright (C) 2026 Qualcomm Innovation Center, Inc.
 */
#include <linux/cfi.h>
#include <linux/uaccess.h>
#include <asm/cfi.h>
#include <asm/registers.h>

/*
 * On Hexagon the compiler lowers a failed KCFI check to a load from a
 * poison address, which faults as an ordinary kernel data access.  The
 * address of the trapping packet is recorded in the __kcfi_traps section,
 * so do_page_fault() routes such faults here.
 *
 * Instructions are grouped into VLIW packets of up to four 32-bit words.
 * Bits [15:14] of each word encode where the packet ends: 0b11 marks the
 * final word (or a single-word packet) and 0b00 marks a terminating duplex
 * word; 0b01 and 0b10 mean more words follow.  The faulting ELR points at
 * the start of the packet, so in permissive mode we must skip the whole
 * packet to resume past the check.
 */
static unsigned long cfi_packet_bytes(unsigned long pc)
{
	unsigned long start = pc;
	u32 insn;
	int i;

	for (i = 0; i < 4; i++) {
		if (get_kernel_nofault(insn, (u32 *)pc))
			break;
		pc += 4;
		if (((insn >> 14) & 0x3) != 0x1 && ((insn >> 14) & 0x3) != 0x2)
			break;
	}

	return pc - start;
}

enum bug_trap_type handle_cfi_failure(struct pt_regs *regs)
{
	unsigned long pc = pt_elr(regs);
	enum bug_trap_type type;

	if (!is_cfi_trap(pc))
		return BUG_TRAP_TYPE_NONE;

	/*
	 * The trapping packet does not carry the target register or the
	 * expected type in a form that is cheap to recover, so report the
	 * failure without them.
	 */
	type = report_cfi_failure_noaddr(regs, pc);

	if (type == BUG_TRAP_TYPE_WARN)
		pt_set_elr(regs, pc + cfi_packet_bytes(pc));

	return type;
}
