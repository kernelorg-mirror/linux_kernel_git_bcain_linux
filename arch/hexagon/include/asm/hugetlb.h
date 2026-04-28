/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_HEXAGON_HUGETLB_H
#define _ASM_HEXAGON_HUGETLB_H

/*
 * Hexagon 16MB huge pages (HPAGE_SHIFT=24) span 4 consecutive PTE slots
 * in the two-level page table.  All hugetlb PTE operations must mirror
 * writes across all 4 slots to keep them in sync.
 */

#define __HAVE_ARCH_HUGE_SET_HUGE_PTE_AT
#define __HAVE_ARCH_HUGE_PTEP_GET_AND_CLEAR
#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
#define __HAVE_ARCH_HUGE_PTEP_SET_WRPROTECT
#define __HAVE_ARCH_HUGE_PTEP_SET_ACCESS_FLAGS
#define __HAVE_ARCH_HUGE_PTE_CLEAR

#include <asm-generic/hugetlb.h>

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
				   pte_t *ptep, pte_t pte, unsigned long sz)
{
	set_pte_at(mm, addr, ptep + 3, pte);
	set_pte_at(mm, addr, ptep + 2, pte);
	set_pte_at(mm, addr, ptep + 1, pte);
	set_pte_at(mm, addr, ptep, pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
					    unsigned long addr, pte_t *ptep,
					    unsigned long sz)
{
	ptep_get_and_clear(mm, addr, ptep + 3);
	ptep_get_and_clear(mm, addr, ptep + 2);
	ptep_get_and_clear(mm, addr, ptep + 1);
	return ptep_get_and_clear(mm, addr, ptep);
}

static inline pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
					  unsigned long addr, pte_t *ptep)
{
	ptep_clear_flush(vma, addr, ptep + 3);
	ptep_clear_flush(vma, addr, ptep + 2);
	ptep_clear_flush(vma, addr, ptep + 1);
	return ptep_clear_flush(vma, addr, ptep);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
					   unsigned long addr, pte_t *ptep)
{
	ptep_set_wrprotect(mm, addr, ptep + 3);
	ptep_set_wrprotect(mm, addr, ptep + 2);
	ptep_set_wrprotect(mm, addr, ptep + 1);
	ptep_set_wrprotect(mm, addr, ptep);
}

static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
					     unsigned long addr, pte_t *ptep,
					     pte_t pte, int dirty)
{
	ptep_set_access_flags(vma, addr, ptep + 3, pte, dirty);
	ptep_set_access_flags(vma, addr, ptep + 2, pte, dirty);
	ptep_set_access_flags(vma, addr, ptep + 1, pte, dirty);
	return ptep_set_access_flags(vma, addr, ptep, pte, dirty);
}

static inline void huge_pte_clear(struct mm_struct *mm, unsigned long addr,
				  pte_t *ptep, unsigned long sz)
{
	pte_clear(mm, addr, ptep + 3);
	pte_clear(mm, addr, ptep + 2);
	pte_clear(mm, addr, ptep + 1);
	pte_clear(mm, addr, ptep);
}

#endif /* _ASM_HEXAGON_HUGETLB_H */
