// SPDX-License-Identifier: GPL-2.0-only
/*
 * Memory subsystem initialization for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/sizes.h>
#include <asm/atomic.h>
#include <linux/highmem.h>
#include <asm/tlb.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/vm_mmu.h>

/*
 * Define a startpg just past the end of the kernel image and a lastpg
 * that corresponds to the end of real or simulated platform memory.
 */
#define bootmem_startpg (PFN_UP(((unsigned long) _end) - PAGE_OFFSET + PHYS_OFFSET))

unsigned long bootmem_lastpg;	/*  Should be set by platform code  */
unsigned long __phys_offset;	/*  physical kernel offset >> 12  */

/*  Set as variable to limit PMD copies  */
int max_kernel_seg = 0x303;

/*  indicate pfn's of high memory  */
unsigned long highstart_pfn, highend_pfn;

/* Default cache attribute for newly created page tables */
unsigned long _dflt_cache_att = CACHEDEF;

/*
 * The current "generation" of kernel map, which should not roll
 * over until Hell freezes over.  Actual bound in years needs to be
 * calculated to confirm.
 */
DEFINE_SPINLOCK(kmap_gen_lock);

/*  checkpatch says don't init this to 0.  */
unsigned long long kmap_generation;

void sync_icache_dcache(pte_t pte)
{
	unsigned long addr;
	struct page *page;

	page = pte_page(pte);
	addr = (unsigned long) page_address(page);

	__vmcache_idsync(addr, PAGE_SIZE);
}

void __init arch_zone_limits_init(unsigned long *max_zone_pfns)
{
	/*
	 *  This is not particularly well documented anywhere, but
	 *  give ZONE_NORMAL all the memory, including the big holes
	 *  left by the kernel+bootmem_map which are already left as reserved
	 *  in the bootmem_map; free_area_init should see those bits and
	 *  adjust accordingly.
	 */
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
}

static void __init paging_init(void)
{
	/*
	 * Set the init_mm descriptors "context" value to point to the
	 * initial kernel segment table's physical address.
	 */
	init_mm.context.ptbase = __pa(init_mm.pgd);
}

#ifndef DMA_RESERVE
#define DMA_RESERVE		0
#endif

#define DMA_CHUNKSIZE		SZ_4M
#define DMA_RESERVED_BYTES	(DMA_RESERVE * DMA_CHUNKSIZE)

/*
 * Pick out the memory size.  We look for mem=size,
 * where size is "size[KkMm]"
 */
static int __init early_mem(char *p)
{
	unsigned long size;
	char *endp;

	size = memparse(p, &endp);

	bootmem_lastpg = PFN_DOWN(size);

	return 0;
}
early_param("mem", early_mem);

size_t hexagon_coherent_pool_size = (size_t) (DMA_RESERVE << 22);

void __init setup_arch_memory(void)
{
	/*  XXX Todo: this probably should be cleaned up  */
	u32 *segtable = (u32 *) &swapper_pg_dir[0];
	u32 *segtable_end;

	/*
	 * Set up boot memory allocator
	 *
	 * The Gorman book also talks about these functions.
	 * This needs to change for highmem setups.
	 */

	/*  Prior to this, bootmem_lastpg is actually mem size  */
	bootmem_lastpg += ARCH_PFN_OFFSET;

#if DMA_RESERVE > 0
	/* Memory size needs to be a multiple of 16M */
	bootmem_lastpg = PFN_DOWN((bootmem_lastpg << PAGE_SHIFT) &
		~((BIG_KERNEL_PAGE_SIZE) - 1));
#endif

	memblock_add(PHYS_OFFSET,
		     (bootmem_lastpg - ARCH_PFN_OFFSET) << PAGE_SHIFT);

	/* Reserve kernel text/data/bss */
	memblock_reserve(PHYS_OFFSET,
			 (bootmem_startpg - ARCH_PFN_OFFSET) << PAGE_SHIFT);

	/*
	 * A bootloader-provided DTB sits in our RAM until it is
	 * unflattened; keep the allocator away from it.
	 */
	if (boot_dtb_phys && initial_boot_params)
		memblock_reserve(boot_dtb_phys,
				 fdt_totalsize(initial_boot_params));
	/*
	 * Reserve the top DMA_RESERVE bytes of RAM for DMA (uncached)
	 * memory allocation
	 */
	max_low_pfn = bootmem_lastpg - PFN_DOWN(DMA_RESERVED_BYTES);
	min_low_pfn = ARCH_PFN_OFFSET;
	memblock_reserve(PFN_PHYS(max_low_pfn), DMA_RESERVED_BYTES);

	printk(KERN_INFO "bootmem_startpg:  0x%08lx\n", bootmem_startpg);
	printk(KERN_INFO "bootmem_lastpg:  0x%08lx\n", bootmem_lastpg);
	printk(KERN_INFO "min_low_pfn:  0x%08lx\n", min_low_pfn);
	printk(KERN_INFO "max_low_pfn:  0x%08lx\n", max_low_pfn);

	/*
	 * The default VM page tables (will be) populated with
	 * VA=PA+PAGE_OFFSET mapping.  We go in and invalidate entries
	 * higher than what we have memory for.
	 */

	/*  this is pointer arithmetic; each entry covers 4MB  */
	segtable = segtable + (PAGE_OFFSET >> 22);
	segtable_end = segtable + (1<<(30-22));

	/*
	 * Move forward to the start of empty pages; take into account
	 * phys_offset shift.
	 */

	segtable += (bootmem_lastpg-ARCH_PFN_OFFSET)>>(22-PAGE_SHIFT);
	{
		int i;

		for (i = 1 ; i <= DMA_RESERVE ; i++)
			segtable[-i] = ((segtable[-i] & __HVM_PTE_PGMASK_4MB)
				| __HVM_PTE_R | __HVM_PTE_W | __HVM_PTE_X
				| __HEXAGON_C_UNC << 6
				| __HVM_PDE_S_4MB);
	}

	printk(KERN_INFO "clearing segtable from %p to %p\n", segtable,
		segtable_end);
	while (segtable < (segtable_end-8))
		*(segtable++) = __HVM_PDE_S_INVALID;
	/* stop the pointer at the device I/O 4MB page  */

	printk(KERN_INFO "segtable = %p (should be equal to _K_io_map)\n",
		segtable);

	/*
	 *  The bootmem allocator seemingly just lives to feed memory
	 *  to the paging system
	 */
	printk(KERN_INFO "PAGE_SIZE=%lu\n", PAGE_SIZE);
	paging_init();  /*  See Gorman Book, 2.3  */

	/*
	 *  At this point, the page allocator is kind of initialized, but
	 *  apparently no pages are available (just like with the bootmem
	 *  allocator), and need to be freed themselves via mem_init(),
	 *  which is called by start_kernel() later on in the process
	 */
}

static const pgprot_t protection_map[16] = {
	[VM_NONE]					= __pgprot(_NO_PERM | CACHEDEF),
	[VM_READ]					= __pgprot(_PAGE_USER | _PAGE_READ |
								   CACHEDEF),
	[VM_WRITE]					= __pgprot(_NO_PERM | CACHEDEF),
	[VM_WRITE | VM_READ]				= __pgprot(_PAGE_USER | _PAGE_READ |
								   CACHEDEF),
	[VM_EXEC]					= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_EXEC | VM_READ]				= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_EXEC | VM_WRITE]				= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_EXEC | VM_WRITE | VM_READ]			= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_SHARED]					= __pgprot(_NO_PERM | CACHEDEF),
	[VM_SHARED | VM_READ]				= __pgprot(_PAGE_USER | _PAGE_READ |
								   CACHEDEF),
	[VM_SHARED | VM_WRITE]				= __pgprot(_PAGE_USER | _PAGE_WRITE |
								   CACHEDEF),
	[VM_SHARED | VM_WRITE | VM_READ]		= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_WRITE | CACHEDEF),
	[VM_SHARED | VM_EXEC]				= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_SHARED | VM_EXEC | VM_READ]			= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_SHARED | VM_EXEC | VM_WRITE]		= __pgprot(_PAGE_USER | _PAGE_WRITE |
								   _PAGE_EXECUTE |
								   CACHEDEF),
	[VM_SHARED | VM_EXEC | VM_WRITE | VM_READ]	= __pgprot(_PAGE_USER | _PAGE_READ |
								   _PAGE_EXECUTE |
								   _PAGE_WRITE | CACHEDEF)
};
DECLARE_VM_GET_PAGE_PROT
