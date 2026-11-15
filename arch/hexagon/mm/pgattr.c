// SPDX-License-Identifier: GPL-2.0-only
/* Kernel permissions and a temporary, non-executable alias for text writes. */
#include <linux/export.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/kprobes.h>
#include <linux/set_memory.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/unaligned.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/hexagon_vm.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/text-patching.h>
#include <asm/tlbflush.h>

static pte_t poke_ptes[PTRS_PER_PTE] __aligned(PAGE_SIZE);
static DEFINE_RAW_SPINLOCK(poke_lock);
static DEFINE_MUTEX(permissions_lock);

void __init hexagon_init_kernel_mapping(void)
{
	unsigned long addr, end;
	int i;

	if (IS_ENABLED(CONFIG_STRICT_KERNEL_RWX)) {
		end = PAGE_OFFSET + ((max_low_pfn - ARCH_PFN_OFFSET) << PAGE_SHIFT);
		for (addr = PAGE_OFFSET; addr < end; addr += PGDIR_SIZE) {
			pte_t *ptes = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			unsigned long va;

			for (i = 0; i < PTRS_PER_PTE; i++) {
				pgprot_t prot = PAGE_KERNEL;

				va = addr + i * PAGE_SIZE;
				if (va >= end) {
					ptes[i] = __pte(_NULL_PTE);
					continue;
				}
				if ((va >= (unsigned long)__init_begin &&
				     va < PAGE_ALIGN((unsigned long)__init_end)) ||
				    (va >= (unsigned long)_stext &&
				     va < PAGE_ALIGN((unsigned long)_etext)))
					prot = rodata_enabled ? PAGE_KERNEL_ROX : PAGE_KERNEL_EXEC;
				ptes[i] = pfn_pte(__pa(va) >> PAGE_SHIFT, prot);
			}
			swapper_pg_dir[pgd_index(addr)] =
				__pgd(__pa(ptes) | HEXAGON_L1_PTE_SIZE);
		}
	}

	for (i = 0; i < PTRS_PER_PTE; i++)
		poke_ptes[i] = __pte(_NULL_PTE);
	addr = HEXAGON_TEXT_POKE_ADDR;
	swapper_pg_dir[pgd_index(addr)] =
		__pgd(__pa(poke_ptes) | HEXAGON_L1_PTE_SIZE);
	max_kernel_seg = max_t(int, max_kernel_seg, pgd_index(addr));
	/* The monitor must see completed tables before switching roots. */
	wmb();
	__vmnewmap((void *)__pa(swapper_pg_dir), VM_TRANS_TYPE_TABLE,
		   VM_TLB_INVALIDATE_TRUE);
}

static pte_t *kernel_pte(unsigned long addr)
{
	pmd_t *pmd = (pmd_t *)&init_mm.pgd[pgd_index(addr)];

	if ((pmd_val(*pmd) & __HVM_PDE_S) >= __HVM_PDE_S_4MB)
		return NULL;
	return pte_offset_kernel(pmd, addr);
}

static int change_memory(unsigned long addr, int numpages,
			 unsigned long clear, unsigned long set)
{
	unsigned long end, va;
	pte_t *pte;
	int ret = 0;

	if (numpages < 0 || addr < PAGE_OFFSET || offset_in_page(addr) ||
	    (unsigned long)numpages > (ULONG_MAX - addr) / PAGE_SIZE)
		return -EINVAL;
	if (!numpages)
		return 0;
	end = addr + (unsigned long)numpages * PAGE_SIZE;
	mutex_lock(&permissions_lock);
	/* Validate everything before making any changes. */
	for (va = addr; va < end; va += PAGE_SIZE) {
		pte = kernel_pte(va);
		if (!pte || !pte_present(*pte)) {
			ret = -EINVAL;
			goto out;
		}
	}
	if (set & _PAGE_EXECUTE)
		flush_icache_range(addr, end);
	for (va = addr; va < end; va += PAGE_SIZE) {
		pte = kernel_pte(va);
		set_pte(pte, __pte((pte_val(*pte) & ~clear) | set));
	}
	/* Publish permission changes before invalidating cached translations. */
	wmb();
	flush_tlb_kernel_range(addr, end);
out:
	mutex_unlock(&permissions_lock);
	return ret;
}

int set_memory_ro(unsigned long addr, int numpages)
{
	return change_memory(addr, numpages, _PAGE_WRITE, 0);
}
EXPORT_SYMBOL(set_memory_ro);

int set_memory_rw(unsigned long addr, int numpages)
{
	return change_memory(addr, numpages, 0, _PAGE_WRITE);
}
EXPORT_SYMBOL(set_memory_rw);

int set_memory_x(unsigned long addr, int numpages)
{
	return change_memory(addr, numpages, 0, _PAGE_EXECUTE);
}
EXPORT_SYMBOL(set_memory_x);

int set_memory_nx(unsigned long addr, int numpages)
{
	return change_memory(addr, numpages, _PAGE_EXECUTE, 0);
}
EXPORT_SYMBOL(set_memory_nx);

int set_memory_rox(unsigned long addr, int numpages)
{
	return change_memory(addr, numpages, _PAGE_WRITE, _PAGE_EXECUTE);
}
EXPORT_SYMBOL(set_memory_rox);

void mark_rodata_ro(void)
{
	unsigned long start = (unsigned long)__start_rodata;
	unsigned long end = (unsigned long)__hexagon_rodata_end;

	if (set_memory_ro(start, (end - start) >> PAGE_SHIFT))
		panic("Hexagon: cannot protect rodata");
	pr_info("Hexagon: kernel text RX, rodata RO, data NX\n");
}

void free_initmem(void)
{
	unsigned long start = (unsigned long)__init_begin;
	unsigned long end = PAGE_ALIGN((unsigned long)__init_end);

	if (IS_ENABLED(CONFIG_STRICT_KERNEL_RWX)) {
		if (set_memory_nx(start, (end - start) >> PAGE_SHIFT) ||
		    set_memory_rw(start, (end - start) >> PAGE_SHIFT))
			panic("Hexagon: cannot release init text");
	}
	free_initmem_default(POISON_FREE_INITMEM);
}

/* No allocation or kernel IPI shootdown: usable inside stop_machine(). */
int notrace hexagon_patch_text(void *addr, const void *src, size_t size)
{
	unsigned long flags, va = (unsigned long)addr;
	unsigned long alias = HEXAGON_TEXT_POKE_ADDR;
	pte_t *pte = &poke_ptes[pte_index(alias)];

	raw_spin_lock_irqsave(&poke_lock, flags);
	while (size) {
		struct page *page = is_vmalloc_addr((void *)va) ?
			vmalloc_to_page((void *)va) : virt_to_page((void *)va);
		size_t len = min(size, PAGE_SIZE - offset_in_page(va));

		if (!page) {
			raw_spin_unlock_irqrestore(&poke_lock, flags);
			return -EFAULT;
		}
		set_pte(pte, pfn_pte(page_to_pfn(page),
				     __pgprot(_PAGE_READ | _PAGE_WRITE | _dflt_cache_att)));
		/* Install the alias before asking the monitor to translate it. */
		wmb();
		__vmclrmap((void *)alias, PAGE_SIZE);
		/* Kprobes replace one word while other CPUs may execute the packet. */
		if (IS_ALIGNED(va, sizeof(u32)) && len == sizeof(u32))
			WRITE_ONCE(*(u32 *)(alias + offset_in_page(va)),
				   get_unaligned((const u32 *)src));
		else
			memcpy((void *)(alias + offset_in_page(va)), src, len);
		flush_icache_range(alias + offset_in_page(va),
				   alias + offset_in_page(va) + len);
		set_pte(pte, __pte(_NULL_PTE));
		/* Revoke the alias before reusing the slot on another CPU. */
		wmb();
		__vmclrmap((void *)alias, PAGE_SIZE);
		flush_icache_range(va, va + len);
		va += len;
		src += len;
		size -= len;
	}
	raw_spin_unlock_irqrestore(&poke_lock, flags);
	return 0;
}
NOKPROBE_SYMBOL(hexagon_patch_text);
