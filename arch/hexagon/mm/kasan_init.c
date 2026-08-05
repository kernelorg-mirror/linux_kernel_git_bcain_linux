// SPDX-License-Identifier: GPL-2.0
/*
 * KASAN shadow memory initialisation for Hexagon.
 *
 * Copyright (C) 2026 Qualcomm Innovation Center, Inc.
 */

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/memblock.h>
#include <linux/sizes.h>
#include <linux/sched/task.h>
#include <linux/string.h>

#include <asm/hexagon_vm.h>
#include <asm/kasan.h>
#include <asm/mem-layout.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/vm_mmu.h>

/*
 * The whole shadow window is mapped by head.S with 4MB page directory
 * entries that all point here, so it reads as zero -- "addressable, not
 * poisoned" -- from the first instrumented instruction in start_kernel().
 * kasan_init() below replaces the entries covering the linear map with real
 * memory; the rest keep aliasing this, which is what makes accesses to
 * vmalloc and to the shadow itself go unreported.
 *
 * It is 4MB and 4MB aligned because that is what a Hexagon PDE maps.
 */
char kasan_early_shadow_area[SZ_4M] __aligned(SZ_4M);

#define KASAN_PDE_BITS	(__HVM_PTE_R | __HVM_PTE_W |			\
			 (__HEXAGON_C_WB_L2 << 6) | __HVM_PDE_S_4MB)

void __init kasan_init(void)
{
	u32 *segtable = (u32 *)&swapper_pg_dir[0];
	unsigned long lowmem_end, shadow, end;

	lowmem_end = PAGE_OFFSET +
		     ((max_low_pfn - ARCH_PFN_OFFSET) << PAGE_SHIFT);

	shadow = ALIGN_DOWN((unsigned long)kasan_mem_to_shadow((void *)PAGE_OFFSET),
			    SZ_4M);
	end = ALIGN((unsigned long)kasan_mem_to_shadow((void *)lowmem_end),
		    SZ_4M);

	for (; shadow < end; shadow += SZ_4M) {
		phys_addr_t pa = memblock_phys_alloc(SZ_4M, SZ_4M);

		if (!pa)
			panic("kasan: no memory for shadow of 0x%08lx\n",
			      shadow);
		/*
		 * __memset(), not memset(): the instrumented one would check
		 * the shadow of the block we are about to install, which is
		 * still whatever the allocator left there.
		 */
		__memset(__va(pa), 0, SZ_4M);
		segtable[shadow >> 22] = pa | KASAN_PDE_BITS;
	}

	/* Reinstall the segment table so the new entries take effect. */
	__vmnewmap((void *)__pa(swapper_pg_dir), VM_TRANS_TYPE_TABLE,
		   VM_TLB_INVALIDATE_TRUE);

	/*
	 * Reports are suppressed while current->kasan_depth is non-zero, and
	 * init_task starts with it set to 1 so that nothing complains before
	 * the shadow exists.  Everything forked from it inherits the value, so
	 * without this no report would ever be printed.
	 */
	init_task.kasan_depth = 0;

	kasan_init_generic();
}

