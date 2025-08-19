/*
 * SPDX-License-Identifier: GPL-2.0
 * Hexagon HugeTLB page table support
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Hexagon huge pages (16MB, HPAGE_SHIFT=24) span 4 consecutive PGD entries
 * (PGDIR_SHIFT=22, so each PGD entry covers 4MB).  The huge PTE is stored
 * directly in these PGD entries rather than in a separate PTE table.
 */

#include <linux/mm.h>
#include <linux/hugetlb.h>

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;

	pgd = pgd_offset(mm, addr);

	/* Huge PTEs are stored directly in PGD entries; PGD is pre-allocated */
	return (pte_t *)pgd;
}

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr,
		       unsigned long sz)
{
	pgd_t *pgd;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		return NULL;

	return (pte_t *)pgd;
}
