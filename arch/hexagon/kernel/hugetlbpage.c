#include <linux/init.h>
#include <linux/hugetlb.h>
#include <linux/mm.h>
#include <linux/bug.h>
#include <asm/pgtable.h>

int pud_huge(pud_t pud)
{
	return 0;
}

int pmd_huge(pmd_t pmd)
{
	pte_t *pte = (pte_t *) &pmd;

	return pte_huge(*pte);
}

/*
 * The huge page size (4 and 16MB) PTE entries actually sit in the first level
 * of the page tables.
 */

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	addr = addr & HPAGE_MASK;  /*  return the lowest entry  */
	pgd = pgd_offset(mm, addr);

	return (pte_t *) pgd;
}

/*
 * In the 16MB case, check all four possible PTE positions that it can land in
 * to make sure they're all invalid
 */

pte_t *huge_pte_alloc(struct mm_struct *mm,
	unsigned long addr, unsigned long sz)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;

	addr = addr & HPAGE_MASK;
	pgd = pgd_offset(mm, addr);

	pte = (pte_t *) pgd;
	pmd = (pmd_t *) pgd;

	/*  check the 4MB sibling entries */
	BUG_ON(!pmd_none(*pmd) && !pte_huge(*pte));
	BUG_ON(!pmd_none(*(pmd+1)) && !pte_huge(*(pte+1)));
	BUG_ON(!pmd_none(*(pmd+2)) && !pte_huge(*(pte+2)));
	BUG_ON(!pmd_none(*(pmd+3)) && !pte_huge(*(pte+3)));

	return pte;
}


//  Todo:  restrict hugepagesz if we're only supporting 1 size
static __init int set_default_hugepagesz(void)
{
	return 0;
}

