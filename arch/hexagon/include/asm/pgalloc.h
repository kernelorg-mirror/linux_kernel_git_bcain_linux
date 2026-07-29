/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Page table support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <asm/mem-layout.h>
#include <asm/atomic.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE
#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL

#include <asm-generic/pgalloc.h>

extern unsigned long kmap_generation;

/*
 * Page table creation interface
 */
static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	pgd = __pgd_alloc(mm, 0);

	/*
	 * There may be better ways to do this, but to ensure
	 * that new address spaces always contain the kernel
	 * base mapping, and to ensure that the user area is
	 * initially marked invalid, initialize the new map
	 * map with a copy of the kernel's persistent map.
	 */

	memcpy(pgd, swapper_pg_dir, PTRS_PER_PGD*sizeof(pgd_t));
	mm->context.generation = kmap_generation;

	/* Physical version is what is passed to virtual machine on switch */
	mm->context.ptbase = __pa(pgd);

	/*
	 * The VM keys ASIDs by page table base and keeps per-ASID
	 * TLB state cached across map switches.  If this pgd page was
	 * previously another mm's pgd, the VM would resume that stale
	 * ASID state, so the first switch to this mm must invalidate.
	 */
	mm->context.need_invalidate = 1;

	return pgd;
}

/*
 * Hexagon needs PTEs initialized to _NULL_PTE (0x7), not zero.
 * Override the generic pte_alloc_one functions.
 */
static inline pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	struct page *pte;
	pte_t *ptep;
	int i;

	pte = alloc_page(GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_ACCOUNT);
	if (!pte)
		return NULL;
	if (!pagetable_pte_ctor(mm, page_ptdesc(pte))) {
		__free_page(pte);
		return NULL;
	}
	/* Initialize PTEs to _NULL_PTE */
	ptep = (pte_t *)page_address(pte);
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte_val(ptep[i]) = _NULL_PTE;

	return pte;
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte;
	int i;

	pte = (pte_t *)__get_free_page(GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!pte)
		return NULL;

	/* Initialize PTEs to _NULL_PTE */
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte_val(pte[i]) = _NULL_PTE;

	return pte;
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte)
{
	/*
	 * Conveniently, zero in 3 LSB means indirect 4K page table.
	 * Not so convenient when you're trying to vary the page size.
	 */
	set_pmd(pmd, __pmd(((unsigned long)page_to_pfn(pte) << PAGE_SHIFT) |
		HEXAGON_L1_PTE_SIZE));
}

/*
 * Other architectures seem to have ways of making all processes
 * share the same pmd's for their kernel mappings, but the v0.3
 * Hexagon VM spec has a "monolithic" L1 table for user and kernel
 * segments.  We track "generations" of the kernel map to minimize
 * overhead, and update the "slave" copies of the kernel mappings
 * as part of switch_mm.  However, we still need to update the
 * kernel map of the active thread who's calling pmd_populate_kernel...
 */
static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	extern spinlock_t kmap_gen_lock;
	pmd_t *ppmd;
	int pmdindex;

	spin_lock(&kmap_gen_lock);
	kmap_generation++;
	mm->context.generation = kmap_generation;
	current->active_mm->context.generation = kmap_generation;
	spin_unlock(&kmap_gen_lock);

	set_pmd(pmd, __pmd(((unsigned long)__pa(pte)) | HEXAGON_L1_PTE_SIZE));

	/*
	 * Now the "slave" copy of the current thread.
	 * This is pointer arithmetic, not byte addresses!
	 */
	pmdindex = (pgd_t *)pmd - mm->pgd;
	ppmd = (pmd_t *)current->active_mm->pgd + pmdindex;
	set_pmd(ppmd, __pmd(((unsigned long)__pa(pte)) | HEXAGON_L1_PTE_SIZE));
	if (pmdindex > max_kernel_seg)
		max_kernel_seg = pmdindex;
}

#define __pte_free_tlb(tlb, pte, addr)	\
	tlb_remove_ptdesc((tlb), page_ptdesc(pte))

#endif
