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

ENTRY(__vmversion)
	trap1(#HVM_TRAP1_VMVERSION);
	jumpr	R31;

ENTRY(__vmrte)
	trap1(#HVM_TRAP1_VMRTE);
	jumpr	R31;

ENTRY(__vmsetvec)
	trap1(#HVM_TRAP1_VMSETVEC);
	jumpr	R31;

ENTRY(__vmsetie_cached)
	{
	  allocframe(#8);
	  memd(SP+#-16) = R17:16;
	  R17:16 = R1:0;
	}
retry:
	r2 = memw_locked(r17);
	{
	  p0 = cmp.eq(r2, r16);
	  if (p0.new) jump:t nochange;
	}
change:
	{
	  p0 = cmp.eq(r2, #0);
	  if (!p0.new) jump:t ints_were_enabled;
	}
ints_were_disabled:	/* 0->1 */
	/*  Interrupts currently off, so nobody should have interrupted us */
	/*  Except the hypervisor, which might make us lose our reservation. */
	memw_locked(r17, p0) = r16;	/* should always pass? */
	if (!p0) jump retry;
	trap1(#HVM_TRAP1_VMSETIE);
	jump out;	/* oldval READY 2 ROCK */
ints_were_enabled:	/* 1->0 */
	/* oldval is ready to go; but now all caller saves are potentially trampled,
	   dependent on VM_CLOBBERLIST */
	trap1(#HVM_TRAP1_VMSETIE);
	/*
	 * If something else interrupted us in the midst of all this,
	 * it doesn't matter -- by this point, we won.  We got the setie(0),
	 * and now we call the shots.  Break reservations and force the value.
	 */
force_change:				/* still doing 1->0 */
	memw_locked(r17, p0) = r16;	/* break other reservation */
	if (p0) jump out;		/* reservations can spontaneously be lost, so just retry. */
	r2 = memw_locked(r17);
	jump force_change;
nochange:
	/* Not totally sure if this is necessary.  */
	memw_locked(r17, p0) = r16;
	if (!p0) jump retry
out:
	{
	  R17:16 = memd(SP+#0);
	  dealloc_return;
	}

ENTRY(__vmgetinfo)
	trap1(#HVM_TRAP1_VMGETINFO);
	jumpr	R31;

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

/*  Less official-ish traps?  */

ENTRY(__vmpmucfg)
	trap1(#HVM_TRAP1_VMPMUCONFIG);
	jumpr R31;

/*  Probably not actually going to use these; see vm_entry.S  */

ENTRY(__vmsetregs)
	trap1(#HVM_TRAP1_VMSETREGS);
	jumpr	R31;

ENTRY(__vmgetregs)
	trap1(#HVM_TRAP1_VMGETREGS);
	jumpr	R31;

ENTRY(__vmtimerop)
	trap1(#HVM_TRAP1_VMTIMEROP);
	jumpr	R31;

ENTRY(__vmhwconfig)
	trap0(#31);
	jumpr	R31;
