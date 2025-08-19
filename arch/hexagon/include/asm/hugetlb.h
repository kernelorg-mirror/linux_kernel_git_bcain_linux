#include <asm-generic/hugetlb.h>

static inline int is_hugepage_only_range(struct mm_struct *mm,
	unsigned long addr, unsigned long len)
{
	return 0;
}

static inline int huge_pte_none(pte_t pte)
{
	//  this would probably be better as pmd_none with the current implementation, and then the _NULL_PTE can go back to null for faster allocation
	//  bunch of macros probably gotta change though
	return pte_none(pte);
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

static inline pte_t huge_ptep_get(pte_t *ptep)
{
	return *ptep;
}

static inline int prepare_hugepage_range(struct file *file,
	unsigned long addr, unsigned long len)
{
	struct hstate *h = hstate_file(file);

	if (len & ~huge_page_mask(h))
		return -EINVAL;
	if (addr & ~huge_page_mask(h))
		return -EINVAL;

	return 0;
}


//  all hugetlb modifications need to be copied out to mirror entries.

static inline int huge_ptep_set_access_flags(struct vm_area_struct *vma,
	unsigned long addr, pte_t *ptep, pte_t pte, int dirty)
{
	ptep_set_access_flags(vma, addr, ptep+3, pte, dirty);
	ptep_set_access_flags(vma, addr, ptep+2, pte, dirty);
	ptep_set_access_flags(vma, addr, ptep+1, pte, dirty);
	return ptep_set_access_flags(vma, addr, ptep, pte, dirty);
}

static inline void huge_ptep_clear_flush(struct vm_area_struct *vma,
	unsigned long addr, pte_t *ptep)
{
	ptep_clear_flush(vma, addr, ptep+3);
	ptep_clear_flush(vma, addr, ptep+2);
	ptep_clear_flush(vma, addr, ptep+1);
	ptep_clear_flush(vma, addr, ptep);
}

static inline void set_huge_pte_at(struct mm_struct *mm, unsigned long addr,
	pte_t *ptep, pte_t pte)
{
	set_pte_at(mm, addr, ptep+3, pte);
	set_pte_at(mm, addr, ptep+2, pte);
	set_pte_at(mm, addr, ptep+1, pte);
	set_pte_at(mm, addr, ptep, pte);
}

static inline void huge_ptep_set_wrprotect(struct mm_struct *mm,
	unsigned long addr, pte_t *ptep)
{
	ptep_set_wrprotect(mm, addr, ptep+3);
	ptep_set_wrprotect(mm, addr, ptep+2);
	ptep_set_wrprotect(mm, addr, ptep+1);
	ptep_set_wrprotect(mm, addr, ptep);
}

static inline pte_t huge_pte_wrprotect(pte_t pte)
{
	return pte_wrprotect(pte);
}

static inline pte_t huge_ptep_get_and_clear(struct mm_struct *mm,
	unsigned long addr, pte_t *ptep)
{
	ptep_get_and_clear(mm, addr, ptep+3);
	ptep_get_and_clear(mm, addr, ptep+2);
	ptep_get_and_clear(mm, addr, ptep+1);
	return ptep_get_and_clear(mm, addr, ptep);
}

static inline void hugetlb_free_pgd_range(struct mmu_gather *tlb,
	unsigned long addr, unsigned long end, unsigned long floor,
	unsigned long ceiling)
{
	addr &= HPAGE_MASK;

	free_pgd_range(tlb, addr, end, floor, ceiling);
}
