/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Event entry/exit for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#include <asm/asm-offsets.h>  /*  assembly-safer versions of C defines */
#include <asm/mem-layout.h>   /*  sigh, except for page_offset  */
#include <asm/hexagon_vm.h>
#include <asm/thread_info.h>

/*
 * Entry into guest-mode Linux under Hexagon Virtual Machine.
 * Stack pointer points to event record - build pt_regs on top of it,
 * set up a plausible C stack frame, and dispatch to the C handler.
 * On return, do vmrte virtual instruction with SP where we started.
 *
 * VM Spec 0.5 uses a trap to fetch HVM record now.
 */

/*
 * Save full register state, while setting up thread_info struct
 * pointer derived from kernel stack pointer in THREADINFO_REG
 * register, putting prior thread_info.regs pointer in a callee-save
 * register (R24, which had better not ever be assigned to THREADINFO_REG),
 * and updating thread_info.regs to point to current stack frame,
 * so as to support nested events in kernel mode.
 *
 * As this is common code, we set the pt_regs system call number
 * to -1 for all events.  It will be replaced with the system call
 * number in the case where we decode a system call (trap0(#1)).
 */

/* the # ## # syntax inserts a literal ## */
#define save_pt_regs()\
	{ memd(R0 + #_PT_R3130) = R31:30; \
		R30 = memw(R0 + #_PT_ER_VMPSP); }\
	{ memw(R0 + #_PT_R2928) = R28; \
		memw(R0 + #(_PT_R2928 + 4)) = R30; }\
	{ R31:30 = C11:10; \
		memd(R0 + #_PT_R2726) = R27:26; \
		memd(R0 + #_PT_R2524) = R25:24; }\
	{ memd(R0 + #_PT_R2322) = R23:22; \
		memd(R0 + #_PT_R2120) = R21:20; }\
	{ memd(R0 + #_PT_R1918) = R19:18; \
		memd(R0 + #_PT_R1716) = R17:16; }\
	{ memd(R0 + #_PT_R1514) = R15:14; \
		memd(R0 + #_PT_R1312) = R13:12; \
		R17:16 = C13:12; }\
	{ memd(R0 + #_PT_R1110) = R11:10; \
		memd(R0 + #_PT_R0908) = R9:8; \
	  R15:14 = C1:0; } \
	{ memd(R0 + #_PT_R0706) = R7:6; \
		memd(R0 + #_PT_R0504) = R5:4; \
    R13:12 = C3:2; } \
	{ memd(R0 + #_PT_GPUGP) = R31:30; \
		memd(R0 + #_PT_LC0SA0) = R15:14; \
	  R11:10 = C7:6; }\
	{	THREADINFO_REG = and(R0, # ## #-_THREAD_SIZE); \
		memd(R0 + #_PT_LC1SA1) = R13:12; \
	  R15 = p3:0; }\
	{ memd(R0 + #_PT_M1M0) = R11:10; \
		memw(R0 + #_PT_PREDSUSR + 4) =  R15; }\
	{ r24 = memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS); \
	  memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS) = R0; \
	  R2 = #-1; } \
	{ memw(R0 + #_PT_SYSCALL_NR) = R2; \
		memd(R0 + #_PT_CS1CS0) = R17:16; \
	  R30 = #0; }

/*
 * Restore registers and thread_info.regs state. THREADINFO_REG
 * is assumed to still be sane, and R24 to have been correctly
 * preserved. Don't restore R29 (SP) until later.
 */

#define restore_pt_regs() \
	{ memw(THREADINFO_REG + #_THREAD_INFO_PT_REGS) = R24; \
	  R15:14 = memd(R0 + #_PT_PREDSUSR); } \
	{ R11:10 = memd(R0 + #_PT_M1M0); \
		R13:12 = memd(R0 + #_PT_LC1SA1); \
		p3:0 = R15; } \
	{ R15:14 = memd(R0 + #_PT_LC0SA0); \
		R3:2 = memd(R0 + #_PT_R0302); \
		usr = R14; } \
	{ R5:4 = memd(R0 + #_PT_R0504); \
		R7:6 = memd(R0 + #_PT_R0706); \
		C7:6 = R11:10; }\
	{ R9:8 = memd(R0 + #_PT_R0908); \
		R11:10 = memd(R0 + #_PT_R1110); \
    C3:2 = R13:12; }\
	{ R13:12 = memd(R0 + #_PT_R1312); \
	  R15:14 = memd(R0 + #_PT_R1514); \
		C1:0 = R15:14; }\
	{ R17:16 = memd(R0 + #_PT_R1716); \
	  R19:18 = memd(R0 + #_PT_R1918); } \
	{ R21:20 = memd(R0 + #_PT_R2120); \
	  R23:22 = memd(R0 + #_PT_R2322); } \
	{ R25:24 = memd(R0 + #_PT_R2524); \
	  R27:26 = memd(R0 + #_PT_R2726); } \
	R31:30 = memd(R0 + #_PT_CS1CS0); \
	{ C13:12 = R31:30; \
		R31:30 = memd(R0 + #_PT_GPUGP) ; \
		R28 = memw(R0 + #_PT_R2928); }\
	{ C11:10 = R31:30; \
		R31:30 = memd(R0 + #_PT_R3130); }

	/*
	 * Clears off enough space for the rest of pt_regs; evrec is a part
	 * of pt_regs in HVM mode.  Save R0/R1, set handler's address in R1.
	 * R0 is the address of pt_regs and is the parameter to save_pt_regs.
	 */

/*
 * Since the HVM isn't automagically pushing the EVREC onto the stack anymore,
 * we'll subract the entire size out and then fill it in ourselves.
 * Need to save off R0, R1, R2, R3 immediately.
 */

/* turn on I$ prefetch early */
/* the # ## # syntax inserts a literal ## */
#define	vm_event_entry(CHandler) \
	{ \
		R29 = add(R29, #-(_PT_REGS_SIZE)); \
		memd(R29 + #(_PT_R0100 + -_PT_REGS_SIZE)) = R1:0; \
		memd(R29 + #(_PT_R0302 + -_PT_REGS_SIZE)) = R3:2; \
		R0 = usr; \
	} \
	{ \
		memw(R29 + #_PT_PREDSUSR) = R0; \
		R0 = setbit(R0, #16); \
	} \
	usr = R0; \
	R1:0 = G1:0; \
	{ \
		memd(R29 + #_PT_ER_VMEL) = R1:0; \
		R1 = # ## #(CHandler); \
		R3:2 = G3:2; \
	} \
	{ \
		R0 = R29; \
		memd(R29 + #_PT_ER_VMPSP) = R3:2; \
		jump event_dispatch; \
	}

.text
	/*
	 * Do bulk save/restore in one place.
	 * Adds a jump to dispatch latency, but
	 * saves hundreds of bytes.
	 */

event_dispatch:
	save_pt_regs()
	/*
	 * Note:  every event handler needs to clear the IE cache bit,
	 * preferably early.
	 */
	{
		callr	r1
		R17.H = #HI(vmsetie_rte_disable);
	}

	/*
	 * Coming back from the C-world, our thread info pointer
	 * should be in the designated register (usually R19)
	 *
	 * If we were in kernel mode, we don't need to check scheduler
	 * or signals if CONFIG_PREEMPTION is not set.  If set, then it has
	 * to jump to a need_resched kind of block.
	 * BTW, CONFIG_PREEMPTION is not supported yet.
	 */

#ifdef CONFIG_PREEMPTION
	R16.L = #LO(vmsetie_disable);
	R16.H = #HI(vmsetie_disable);
	callr R16
#endif

	/*  "Nested control path" -- if the previous mode was kernel  */
	{
		R0 = memw(R29 + #_PT_ER_VMEST);
		R26.L = #LO(do_work_pending);
	}
	{
		R16.L = #LO(vmsetie_disable);
		R17.L = #LO(vmsetie_rte_disable);
	}
	{
		P0 = tstbit(R0, #HVM_VMEST_UM_SFT);
		if (!P0.new) jump:nt restore_all;
		R26.H = #HI(do_work_pending);
		R16.H = #HI(vmsetie_disable);
	}

	/*
	 * Check also the return from fork/system call, normally coming back from
	 * user mode
	 *
	 * R26 needs to have do_work_pending, and R16 should have vmsetie_disable
	 * And R17 might need to be reloaded with vmsetie_rte_disable in the fork
	 * case.
	 */

check_work_pending:
	/*  Disable interrupts while checking TIF  */
	callr R16

	{
		R0 = R29;  /*  regs should still be at top of stack  */
		R1 = memw(THREADINFO_REG + #_THREAD_INFO_FLAGS);
		callr R26;
		R17.H = #HI(vmsetie_rte_disable);
	}

	{
		P0 = cmp.eq(R0, #0); if (!P0.new) jump:nt check_work_pending;
		R17.L = #LO(vmsetie_rte_disable);
	}

restore_all:
	/*
	 * Disable interrupts, if they weren't already, before reg restore.
	 */
	callr R17
	/*  do the setregs here for VM 0.5  */
	/*  R29 here should already be pointing at pt_regs  */
	{
		R1:0 = memd(R29 + #_PT_ER_VMEL);
		R3:2 = memd(R29 + #_PT_ER_VMPSP);
	}

	G1:0 = R1:0;
	G3:2 = R3:2;

	R0 = R29
	restore_pt_regs()
	{
		R1:0 = memd(R29 + #_PT_R0100);
		R29 = add(R29, #_PT_REGS_SIZE);
	}
	trap1(#HVM_TRAP1_VMRTE)
	/* Notreached */


	.globl _K_enter_genex
_K_enter_genex:
	vm_event_entry(do_genex)

	.globl _K_enter_interrupt
_K_enter_interrupt:
	vm_event_entry(arch_do_IRQ)

	.globl _K_enter_trap0
_K_enter_trap0:
	vm_event_entry(do_trap0)

	.globl _K_enter_machcheck
_K_enter_machcheck:
	vm_event_entry(do_machcheck)

	.globl _K_enter_debug
_K_enter_debug:
	vm_event_entry(do_debug_exception)

	.globl ret_from_fork
ret_from_fork:
	{
		call schedule_tail
		R26.H = #HI(do_work_pending);
		R16.H = #HI(vmsetie_disable);
	}
	{
		P0 = cmp.eq(R24, #0);
		R26.L = #LO(do_work_pending);
		R16.L = #LO(vmsetie_disable);
	}
	if (P0) jump check_work_pending
	{
		R0 = R25;
		callr R24
	}
	{
		jump check_work_pending
	}
