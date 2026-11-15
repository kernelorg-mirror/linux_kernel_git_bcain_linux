/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LLVM's -pg hook runs after allocframe, including when allocframe and the
 * call share a packet. The caller's original LR is at FP+4. Preserve the
 * argument/scratch registers and control state across the tracing callback.
 * Callee-saved registers (including the thread-info and SCS pointers) follow
 * the ordinary C ABI.
 */
#include <linux/linkage.h>
#include <linux/export.h>
#include <asm/ftrace.h>

	.text
	.macro trace_save
	allocframe(#112)
	memd(r29 + #0) = r1:0
	memd(r29 + #8) = r3:2
	memd(r29 + #16) = r5:4
	memd(r29 + #24) = r7:6
	memd(r29 + #32) = r9:8
	memd(r29 + #40) = r11:10
	memd(r29 + #48) = r13:12
	memd(r29 + #56) = r15:14
	memw(r29 + #64) = r28
	r1:0 = c1:0
	memd(r29 + #72) = r1:0
	r1:0 = c3:2
	memd(r29 + #80) = r1:0
	r1:0 = c7:6
	memd(r29 + #88) = r1:0
	r0 = usr
	memw(r29 + #96) = r0
	r0 = p3:0
	memw(r29 + #100) = r0
#if CONFIG_HEXAGON_ARCH_VERSION >= 4
	r1:0 = c13:12
	memd(r29 + #104) = r1:0
#endif
	.endm

	.macro trace_restore
#if CONFIG_HEXAGON_ARCH_VERSION >= 4
	r1:0 = memd(r29 + #104)
	c13:12 = r1:0
#endif
	r0 = memw(r29 + #100)
	p3:0 = r0
	r0 = memw(r29 + #96)
	usr = r0
	r1:0 = memd(r29 + #88)
	c7:6 = r1:0
	r1:0 = memd(r29 + #80)
	c3:2 = r1:0
	r1:0 = memd(r29 + #72)
	c1:0 = r1:0
	r28 = memw(r29 + #64)
	r15:14 = memd(r29 + #56)
	r13:12 = memd(r29 + #48)
	r11:10 = memd(r29 + #40)
	r9:8 = memd(r29 + #32)
	r7:6 = memd(r29 + #24)
	r5:4 = memd(r29 + #16)
	r3:2 = memd(r29 + #8)
	r1:0 = memd(r29 + #0)
	dealloc_return
	.endm

SYM_FUNC_START(mcount)
	trace_save
	r2 = memw(r30)
	r1 = memw(r2 + #4)
	r2 = add(r2, #4)
	r0 = memw(r30 + #4)
	r0 = add(r0, #-MCOUNT_INSN_SIZE)
	call hexagon_ftrace_caller
	trace_restore
SYM_FUNC_END(mcount)
SYM_FUNC_ALIAS(ftrace_caller, mcount)
EXPORT_SYMBOL(mcount)
