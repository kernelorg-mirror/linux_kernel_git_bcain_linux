// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hexagon Virtual Machine TLB functions
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*
 * The Hexagon Virtual Machine conceals the real workings of
 * the TLB, but there are one or two functions that need to
 * be instantiated for it, differently from a native build.
 *
 * The VM caches translations per address space and __vmclrmap()
 * only purges the *calling* CPU's current address space, so flushes
 * for an mm that is active on another CPU must run there via IPI.
 * Flushes for an mm that is not running anywhere are deferred to its
 * next switch_mm() through context.need_invalidate.  The IPIs are
 * waited on, which also orders the flush against the VM's page table
 * walker on other CPUs: once a flush returns, freed page tables are
 * unreachable, as the generic mmu_gather code assumes.
 */
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <asm/page.h>
#include <asm/hexagon_vm.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

struct tlb_flush_info {
	struct mm_struct *mm;
	unsigned long start;
	unsigned long end;
};

static void ipi_flush_tlb_range(void *info)
{
	struct tlb_flush_info *fi = info;

	if (fi->mm->context.ptbase == current->active_mm->context.ptbase)
		__vmclrmap((void *)fi->start, fi->end - fi->start);
}

static void ipi_flush_tlb_mm(void *info)
{
	struct mm_struct *mm = info;

	if (mm->context.ptbase == current->active_mm->context.ptbase)
		tlb_flush_all();
}

static void ipi_flush_tlb_kernel_range(void *info)
{
	struct tlb_flush_info *fi = info;

	__vmclrmap((void *)fi->start, fi->end - fi->start);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	if (mm->context.ptbase == current->active_mm->context.ptbase)
		__vmclrmap((void *)start, end - start);
	else
		mm->context.need_invalidate = 1;

	if (num_online_cpus() > 1) {
		struct tlb_flush_info fi = {
			.mm = mm, .start = start, .end = end
		};

		smp_call_function(ipi_flush_tlb_range, &fi, 1);
	}
}

/*
 * Flush a page from the kernel virtual map - used by highmem
 */
void flush_tlb_one(unsigned long vaddr)
{
	__vmclrmap((void *)(vaddr & ~(PAGE_SIZE - 1)), PAGE_SIZE);
}

/*
 * Flush this CPU's current address space.
 * A single Hexagon core has 6 thread contexts but
 * only one TLB.
 */
void tlb_flush_all(void)
{
	/*  should probably use that fixaddr end or whateve label  */
	__vmclrmap(0, 0xffff0000);
}

/*
 * Flush TLB entries associated with a given mm_struct mapping.
 */
void flush_tlb_mm(struct mm_struct *mm)
{
	if (current->active_mm->context.ptbase == mm->context.ptbase)
		tlb_flush_all();
	else
		mm->context.need_invalidate = 1;

	if (num_online_cpus() > 1)
		smp_call_function(ipi_flush_tlb_mm, mm, 1);
}

/*
 * Flush TLB state associated with a page of a vma.
 */
void flush_tlb_page(struct vm_area_struct *vma, unsigned long vaddr)
{
	struct mm_struct *mm = vma->vm_mm;

	vaddr &= ~(PAGE_SIZE - 1);

	if (mm->context.ptbase == current->active_mm->context.ptbase)
		__vmclrmap((void *)vaddr, PAGE_SIZE);
	else
		mm->context.need_invalidate = 1;

	if (num_online_cpus() > 1) {
		struct tlb_flush_info fi = {
			.mm = mm, .start = vaddr, .end = vaddr + PAGE_SIZE
		};

		smp_call_function(ipi_flush_tlb_range, &fi, 1);
	}
}

/*
 * Flush TLB entries associated with a kernel address range.
 * Like flush range, but without the check on the vma->vm_mm.
 */
void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	/*
	 * The clrmaps below only purge currently-active address spaces.
	 * Bump the kernel map generation so every other address space
	 * invalidates its cached VM state when next switched in, as
	 * switch_mm() does when kernel mappings are created.
	 */
	spin_lock(&kmap_gen_lock);
	kmap_generation++;
	current->active_mm->context.generation = kmap_generation;
	spin_unlock(&kmap_gen_lock);

	__vmclrmap((void *)start, end - start);

	if (num_online_cpus() > 1) {
		struct tlb_flush_info fi = {
			.start = start, .end = end
		};

		smp_call_function(ipi_flush_tlb_kernel_range, &fi, 1);
	}
}
