// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fixmap implementation for Hexagon
 *
 * Copyright (c) 2024, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/vm_mmu.h>
#include <asm/hexagon_vm.h>

/*
 * PTE table for the fixmap region.
 * This covers one PGD entry (4MB) which is enough for the fixmap region.
 * Aligned to the PTE table size requirement for the page size.
 */
pte_t fixmap_pte[PTRS_PER_PTE] __page_aligned_bss;

/*
 * early_fixmap_init - Initialize the fixmap page table
 *
 * This must be called early in boot, before any fixmap entries are used.
 * It sets up the PGD entry to point to our fixmap_pte table.
 */
void __init early_fixmap_init(void)
{
	unsigned long addr = FIXADDR_START;
	pgd_t *pgd = swapper_pg_dir + pgd_index(addr);
	unsigned long pte_phys;
	unsigned long pgd_val;
	int i;

	/* Initialize all PTEs to invalid */
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte_val(fixmap_pte[i]) = _NULL_PTE;

	/*
	 * Set up the PGD entry to point to our PTE table.
	 * The PGD entry format is:
	 *   bits [31:N] = physical address of PTE table (masked)
	 *   bits [2:0]  = page size selector
	 *
	 * For 64KB pages, use __HVM_PDE_S_64KB and __HVM_PDE_PTMASK_64KB
	 */
	pte_phys = __pa(fixmap_pte);

#if defined(CONFIG_PAGE_SIZE_4KB)
	pgd_val = (pte_phys & __HVM_PDE_PTMASK_4KB) | __HVM_PDE_S_4KB;
#elif defined(CONFIG_PAGE_SIZE_16KB)
	pgd_val = (pte_phys & __HVM_PDE_PTMASK_16KB) | __HVM_PDE_S_16KB;
#elif defined(CONFIG_PAGE_SIZE_64KB)
	pgd_val = (pte_phys & __HVM_PDE_PTMASK_64KB) | __HVM_PDE_S_64KB;
#elif defined(CONFIG_PAGE_SIZE_256KB)
	pgd_val = (pte_phys & __HVM_PDE_PTMASK_256KB) | __HVM_PDE_S_256KB;
#elif defined(CONFIG_PAGE_SIZE_1MB)
	pgd_val = (pte_phys & __HVM_PDE_PTMASK_1MB) | __HVM_PDE_S_1MB;
#else
#error "Unsupported page size for fixmap"
#endif

	set_pgd(pgd, __pgd(pgd_val));

	pr_debug("fixmap: PGD at %p, PTE table at %p (phys 0x%lx)\n",
		 pgd, fixmap_pte, pte_phys);
}

/*
 * __set_fixmap - Set a fixmap entry
 * @idx: fixmap index
 * @phys: physical address to map (page-aligned)
 * @prot: page protection flags
 *
 * Maps a physical page at the fixmap virtual address corresponding to idx.
 * If prot is zero (FIXMAP_PAGE_CLEAR), the mapping is removed.
 */
void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *ptep;
	pte_t pte;

	BUG_ON(idx >= __end_of_fixed_addresses);

	ptep = &fixmap_pte[pte_index(addr)];

	if (pgprot_val(prot)) {
		/* Create a valid PTE with the physical address and protection */
		pte = pfn_pte(phys >> PAGE_SHIFT, prot);
		set_pte(ptep, pte);
	} else {
		/* Clear the mapping */
		pte_clear(&init_mm, addr, ptep);
	}

	/*
	 * Flush TLB for this address.
	 * On Hexagon VM, we use the tlb invalidation mechanism.
	 */
	flush_tlb_one(addr);
}
