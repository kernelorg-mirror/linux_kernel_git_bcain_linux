/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Page table support for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_PGTABLE_H
#define _ASM_PGTABLE_H

/*
 * Page table definitions for Qualcomm Hexagon processor.
 */
#include <asm/page.h>
#include <asm-generic/pgtable-nopmd.h>

/* A handy thing to have if one has the RAM. Declared in head.S */

extern pmd_t _K_io_map;
extern pte_t _K_init_devicetable;

/*
 * The PTE model described here is that of the Hexagon Virtual Machine,
 * which autonomously walks 2-level page tables.  At a lower level, we
 * also describe the RISCish software-loaded TLB entry structure of
 * the underlying Hexagon processor. A kernel built to run on the
 * virtual machine has no need to know about the underlying hardware.
 */
#include <asm/vm_mmu.h>

/*
 * To maximize the comfort level for the PTE manipulation macros,
 * define the "well known" architecture-specific bits.
 */
#define _PAGE_READ	__HVM_PTE_R
#define _PAGE_WRITE	__HVM_PTE_W
#define _PAGE_EXECUTE	__HVM_PTE_X
#define _PAGE_USER	__HVM_PTE_U
#define _PAGE_PRESENT	0	/* Implicit on hexagon; page size field = valid */

#define _PAGE_PERM_MASK	(_PAGE_READ | _PAGE_WRITE | _PAGE_EXECUTE | _PAGE_USER)
#define _SWAP_PERM	(_PAGE_USER)
#define _NO_PERM	(_PAGE_READ)
/*
 * The lone software bit we have is used to track dirty.
 */
#define _PAGE_DIRTY	(1<<3)

/*
 * We're using _PAGE_EXECUTE as our "accessed" bit because we don't
 * have any more bits to spare.  protection_map only cares about the
 * X bit in the case of PROT_NONE, for which we can just turn off the
 * U (user) bit to signify.
 * If we take an X fault on that page, the fault code runs a mkyoung
 * on it anyways, which will re-enable it and signify accessed
 * at the same time.
 */

#define _PAGE_ACCESSED	_PAGE_EXECUTE

/*
 * We're not defining _PAGE_GLOBAL here, since there's no concept
 * of global pages or ASIDs exposed to the Hexagon Virtual Machine,
 * and we want to use the same page table structures and macros in
 * the native kernel as we do in the virtual machine kernel.
 * So we'll put up with a bit of inefficiency for now...
 */

/* We borrow bit 6 to store the exclusive marker in swap PTEs. */
#define _PAGE_SWP_EXCLUSIVE	(1<<6)

/*
 * Top "FOURTH" level (pgd), which for the Hexagon VM is really
 * only the second from the bottom, pgd and pud both being collapsed.
 * Each entry represents 4MB of virtual address space, 4K of table
 * thus maps the full 4GB.
 */
#define PGDIR_SHIFT 22
#define PTRS_PER_PGD 1024

#define PGDIR_SIZE (1UL << PGDIR_SHIFT)
#define PGDIR_MASK (~(PGDIR_SIZE-1))

#ifdef CONFIG_PAGE_SIZE_4KB
#define PTRS_PER_PTE 1024
#endif

#ifdef CONFIG_PAGE_SIZE_16KB
#define PTRS_PER_PTE 256
#endif

#ifdef CONFIG_PAGE_SIZE_64KB
#define PTRS_PER_PTE 64
#endif

#ifdef CONFIG_PAGE_SIZE_256KB
#define PTRS_PER_PTE 16
#endif

#ifdef CONFIG_PAGE_SIZE_1MB
#define PTRS_PER_PTE 4
#endif

/*  Any bigger and the PTE disappears.  */
#define pgd_ERROR(e) \
	printk(KERN_WARNING "%s:%d: bad pgd %08lx.\n", __FILE__, __LINE__,\
		pgd_val(e))

/*
 * Page Protection Constants. Includes (in this variant) cache attributes.
 */
extern unsigned long _dflt_cache_att;

/*  PROT_NONE still should show up as present but !pte_none */
#define PAGE_NONE	__pgprot(_NO_PERM | _dflt_cache_att)
#define PAGE_KERNEL	__pgprot(_PAGE_READ | \
				_PAGE_WRITE | _PAGE_EXECUTE | _dflt_cache_att)

/*
 * Aliases for mapping mmap() protection bits to page protections.
 * These get used for static initialization, so using the _dflt_cache_att
 * variable for the default cache attribute isn't workable. If the
 * default gets changed at boot time, the boot option code has to
 * update data structures like the protaction_map[] array.
 */
#define CACHEDEF	(CACHE_DEFAULT << 6)

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];  /* located in head.S */

/* Seems to be zero even in architectures where the zero page is firewalled? */
#define FIRST_USER_ADDRESS	0UL

#ifdef CONFIG_HUGETLB_PAGE
#define pte_huge(pte)	((pte_val(pte) & 0x7) == HVM_HUGEPAGE_SIZE)
#define pte_mkhuge(pte)	__pte((pte_val(pte) & ~0x7) | HVM_HUGEPAGE_SIZE)
#endif

/*
 * For now, assume that higher-level code will do TLB/MMU invalidations
 * and don't insert that overhead into this low-level function.
 */
extern void sync_icache_dcache(pte_t pte);

static inline void set_pte(pte_t *ptep, pte_t pteval)
{
	*ptep = pteval;
}

/*
 * For the Hexagon Virtual Machine MMU (or its emulation), a null/invalid
 * L1 PTE (PMD/PGD) has 7 in the least significant bits.
 */

/*
 * Hugetlb implementation currently relies on these being the same.
 */
#define _NULL_PMD	0x7
#define _NULL_PTE	_NULL_PMD

static inline void pmd_clear(pmd_t *pmd_entry_ptr)
{
	 pmd_val(*pmd_entry_ptr) = _NULL_PMD;
}

/*
 * Conveniently, a null PTE value is invalid.
 */
static inline void pte_clear(struct mm_struct *mm, unsigned long addr,
				pte_t *ptep)
{
	pte_val(*ptep) = _NULL_PTE;
}

/**
 * pmd_none - check if pmd_entry is mapped
 * @pmd_entry:  pmd entry
 *
 * MIPS checks it against that "invalid pte table" thing.
 */
static inline int pmd_none(pmd_t pmd)
{
	return pmd_val(pmd) == _NULL_PMD;
}

/**
 * pmd_present - is there a page table behind this?
 * Essentially the inverse of pmd_none.  We maybe
 * save an inline instruction by defining it this
 * way, instead of simply "!pmd_none".
 */
static inline int pmd_present(pmd_t pmd)
{
	return pmd_val(pmd) != (unsigned long)_NULL_PMD;
}

/**
 * pmd_bad - check if a PMD entry is "bad". That might mean swapped out.
 * As we have no known cause of badness, it's null, as it is for many
 * architectures.
 */
static inline int pmd_bad(pmd_t pmd)
{
#ifdef CONFIG_HUGETLB_PAGE
	pte_t *pte = (pte_t *) &pmd;

	//  total hack:  if it's a hugetlb pmd, call it "bad" so free_pgd_range will wipe it out.  I hopez.
	if (pte_huge(*pte)) {
		return 1;
	}
#endif
	return 0;
}

/*
 * pmd_pfn - converts a PMD entry to a page frame number
 */
#define pmd_pfn(pmd)  (pmd_val(pmd) >> PAGE_SHIFT)

/*
 * pmd_page - converts a PMD entry to a page pointer
 */
#define pmd_page(pmd)  (pfn_to_page(pmd_val(pmd) >> PAGE_SHIFT))

/**
 * pte_none - check if pte is mapped
 * @pte: pte_t entry
 */
static inline int pte_none(pte_t pte)
{
	return pte_val(pte) == _NULL_PTE;
};

#define pte_match_perm(pte, perm)	((pte_val(pte) & _PAGE_PERM_MASK) == perm)

/*
 * pte_present - check if page is present
 */
static inline int pte_present(pte_t pte)
{
	int swap = pte_match_perm(pte, _SWAP_PERM);

	/*
	 * since pte_none isn't a subset of !pte_present like it used to be,
	 * we seem to need to check for that here as well; see change_pte_range
	 */

	return !swap && !pte_none(pte);
}

/* pte_page - returns a page (frame pointer/descriptor?) based on a PTE */
#define pte_page(x) pfn_to_page(pte_pfn(x))

/* pte_mkold - mark PTE as not recently accessed */
static inline pte_t pte_mkold(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_ACCESSED;
	return pte;
}

/* pte_mkyoung - mark PTE as recently accessed */
static inline pte_t pte_mkyoung(pte_t pte)
{
	pte_val(pte) |= _PAGE_ACCESSED;
	return pte;
}

/* pte_mkclean - mark page as in sync with backing store */
static inline pte_t pte_mkclean(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_DIRTY;
	return pte;
}

/* pte_mkdirty - mark page as modified */
static inline pte_t pte_mkdirty(pte_t pte)
{
	pte_val(pte) |= _PAGE_DIRTY;
	return pte;
}

/* pte_young - "is PTE marked as accessed"? */
static inline int pte_young(pte_t pte)
{
	return pte_val(pte) & _PAGE_ACCESSED;
}

/* pte_dirty - "is PTE dirty?" */
static inline int pte_dirty(pte_t pte)
{
	return pte_val(pte) & _PAGE_DIRTY;
}

/* pte_modify - set protection bits on PTE */
static inline pte_t pte_modify(pte_t pte, pgprot_t prot)
{
	pte_val(pte) &= PAGE_MASK;
	pte_val(pte) |= pgprot_val(prot);
	return pte;
}

/* pte_wrprotect - mark page as not writable */
static inline pte_t pte_wrprotect(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_WRITE;

	if (pte_match_perm(pte, _SWAP_PERM)) {
		pte_val(pte) &= ~_PAGE_PERM_MASK;
		pte_val(pte) |= _NO_PERM;
	}	/*  This would have made it look like swap  */

	return pte;
}

/* pte_mkwrite - mark page as writable */
static inline pte_t pte_mkwrite_novma(pte_t pte)
{
	if (pte_match_perm(pte, _NO_PERM)) {
		/*  Essentially clear the read and set the user  */
		pte_val(pte) &= ~_PAGE_PERM_MASK;
		pte_val(pte) |= _PAGE_USER;
	}
	pte_val(pte) |= _PAGE_WRITE;
	return pte;
}

/* pte_read - "is PTE marked as readable?" */
static inline int pte_read(pte_t pte)
{
	if (pte_match_perm(pte, _NO_PERM)) {
		return 0;
	}
	return pte_val(pte) & _PAGE_READ;
}

/* pte_write - "is PTE marked as writable?" */
static inline int pte_write(pte_t pte)
{
	if (pte_match_perm(pte, _NO_PERM)) {
		return 0;
	}
	return pte_val(pte) & _PAGE_WRITE;
}

/* pte_exec - "is PTE marked as executable?" */
static inline int pte_exec(pte_t pte)
{
	if (pte_match_perm(pte, _NO_PERM)) {
		return 0;
	}
	return pte_val(pte) & _PAGE_EXECUTE;
}

/* __pte_to_swp_entry - extract swap entry from PTE */
#define __pte_to_swp_entry(pte) ((swp_entry_t) { pte_val(pte) })

/* __swp_entry_to_pte - extract PTE from swap entry */
#define __swp_entry_to_pte(x) ((pte_t) { (x).val })

#define PFN_PTE_SHIFT	PAGE_SHIFT
/* pfn_pte - convert page number and protection value to page table entry */
#define pfn_pte(pfn, pgprot) __pte((pfn << PAGE_SHIFT) | pgprot_val(pgprot))

/* pte_pfn - convert pte to page frame number */
#define pte_pfn(pte) (pte_val(pte) >> PAGE_SHIFT)
#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return (unsigned long)__va(pmd_val(pmd) & PAGE_MASK);
}

/*
 * Swap/file PTE definitions.  If the page is marked with _SWAP_PERM, the rest
 * of the PTE is interpreted as swap information.  The remaining free bits are
 * interpreted as swap type/offset tuple.
 *
 * Format of swap PTE:
 *
 *      bits	[4-0]:		swap type
 * 	bit	5:		PAGE_USER - must be 1 (_SWAP_PERM)
 *      bit	6:		exclusive marker (_PAGE_SWP_EXCLUSIVE)
 *      bit	7:		reserved
 *      bit	8:		reserved
 *  	bit	9:		PAGE_READ - must be 0 (_SWAP_PERM)
 *  	bit	10:		PAGE_WRITE - must be 0 (_SWAP_PERM)
 *  	bit	11:		PAGE_EXECUTE - must be 0 (_SWAP_PERM)
 *      bits	[31-12]:	swap offset (22 bits)
 *
 */

/* Used for swap PTEs */
#define __swp_type(swp_pte)		((swp_pte).val & 0x1f)


#define __swp_offset(swp_pte) \
	((swp_pte).val >> 12)


#define __swp_entry(type, offset) \
	((swp_entry_t)	{ \
		((type & 0x1f) | (_PAGE_USER) | \
		 (offset << 12)) })

static inline bool pte_swp_exclusive(pte_t pte)
{
	return pte_val(pte) & _PAGE_SWP_EXCLUSIVE;
}

static inline pte_t pte_swp_mkexclusive(pte_t pte)
{
	pte_val(pte) |= _PAGE_SWP_EXCLUSIVE;
	return pte;
}

static inline pte_t pte_swp_clear_exclusive(pte_t pte)
{
	pte_val(pte) &= ~_PAGE_SWP_EXCLUSIVE;
	return pte;
}

#endif
