/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hexagon VM instruction support
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>
#include <asm/hexagon_vm.h>

/*
 * C wrappers for virtual machine "instructions".  These
 * could be, and perhaps some day will be, handled as in-line
 * macros, but for tracing/debugging it's handy to have
 * a single point of invocation for each of them.
 * Conveniently, they take parameters and return values
 * consistent with the ABI calling convention.
 */

ENTRY(__vmrte)
	trap1(#HVM_TRAP1_VMRTE);
	jumpr	R31;

ENTRY(__vmsetvec)
	trap1(#HVM_TRAP1_VMSETVEC);
	jumpr	R31;

/*
 * long __vmsetie_cached(long newval, long *cache)
 *
 * Set the VM's interrupt-enable state and this CPU's cached copy of it
 * together, and return the previous state.  The two must not be allowed to
 * disagree, so the cache is updated under a memw_locked reservation: an
 * interrupt taken in the middle of the sequence breaks the reservation and
 * the update is retried.
 *
 * R16 holds the new value and R17 the address of the cache throughout; both
 * are callee-saved, so they survive the hypercall.
 */
ENTRY(__vmsetie_cached)
	{
	  allocframe(#8);
	  memd(SP+#-16) = R17:16;
	  R17:16 = R1:0;
	}
.Lretry:
	r2 = memw_locked(r17);
	{
	  p0 = cmp.eq(r2, r16);
	  if (p0.new) jump:t .Lnochange;
	}
	{
	  p0 = cmp.eq(r2, #0);
	  if (!p0.new) jump:t .Lints_were_enabled;
	}
.Lints_were_disabled:	/* 0->1 */
	/*
	 * Interrupts are currently off, so nothing but the hypervisor can
	 * have interrupted us -- and that only costs us the reservation.
	 */
	memw_locked(r17, p0) = r16;
	if (!p0) jump .Lretry;
	trap1(#HVM_TRAP1_VMSETIE);
	jump .Lout;			/* R0 already holds the old value */
.Lints_were_enabled:	/* 1->0 */
	/*
	 * The old value is already in R0, which is just as well: the hypercall
	 * is free to trample every caller-saved register.
	 */
	trap1(#HVM_TRAP1_VMSETIE);
	/*
	 * Interrupts are off from here on, so whatever raced with us has
	 * already run.  Force the cache to the value we just installed,
	 * retrying only for a spontaneously lost reservation.
	 */
.Lforce_change:
	memw_locked(r17, p0) = r16;
	if (p0) jump .Lout;
	r2 = memw_locked(r17);
	jump .Lforce_change;
.Lnochange:
	/*
	 * The cache already reads back as the requested value, but a racing
	 * update may still be holding a reservation on it; take the store
	 * anyway so that it loses and retries against the settled value.
	 */
	memw_locked(r17, p0) = r16;
	if (!p0) jump .Lretry
.Lout:
	{
	  R17:16 = memd(SP+#0);
	  dealloc_return;
	}

ENTRY(__vmgetie)
	trap1(#HVM_TRAP1_VMGETIE);
	jumpr	R31;

ENTRY(__vmintop)
	trap1(#HVM_TRAP1_VMINTOP);
	jumpr	R31;

ENTRY(__vmclrmap)
	trap1(#HVM_TRAP1_VMCLRMAP);
	jumpr	R31;

ENTRY(__vmnewmap)
	r1 = #VM_NEWMAP_TYPE_PGTABLES;
	trap1(#HVM_TRAP1_VMNEWMAP);
	jumpr	R31;

ENTRY(__vmcache)
	trap1(#HVM_TRAP1_VMCACHE);
	jumpr	R31;

ENTRY(__vmgettime)
	trap1(#HVM_TRAP1_VMGETTIME);
	jumpr	R31;

ENTRY(__vmsettime)
	trap1(#HVM_TRAP1_VMSETTIME);
	jumpr	R31;

ENTRY(__vmwait)
	trap1(#HVM_TRAP1_VMWAIT);
	jumpr	R31;

ENTRY(__vmyield)
	trap1(#HVM_TRAP1_VMYIELD);
	jumpr	R31;

ENTRY(__vmstart)
	trap1(#HVM_TRAP1_VMSTART);
	jumpr	R31;

ENTRY(__vmstop)
	trap1(#HVM_TRAP1_VMSTOP);
	jumpr	R31;

ENTRY(__vmvpid)
	trap1(#HVM_TRAP1_VMVPID);
	jumpr	R31;

/*  Probably not actually going to use these; see vm_entry.S  */

ENTRY(__vmsetregs)
	trap1(#HVM_TRAP1_VMSETREGS);
	jumpr	R31;

ENTRY(__vmgetregs)
	trap1(#HVM_TRAP1_VMGETREGS);
	jumpr	R31;
