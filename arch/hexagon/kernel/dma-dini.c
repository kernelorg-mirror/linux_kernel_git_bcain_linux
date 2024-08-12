/*
 * DMA implementation for Hexagon
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <linux/genalloc.h>
#include <linux/export.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <asm/dma-mapping.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/page.h>

#define BRAM_PA			0xD0080000UL
#define BRAM_SZ			SZ_32K
#define BRAM_COHERENT_SZ	SZ_4K  //  this is LOCKED at 4k currently as that's the page mapping size
#define BRAM_INCOHERENT_SZ	(BRAM_SZ-BRAM_COHERENT_SZ)
#define BRAM_DMA_ADDR_START	0x00080000UL
#define INCOHERENT_ALLOC_SHIFT	5
#define COHERENT_ALLOC_SHIFT	6

void	*coherent_va_start;
void	*incoherent_va_start;

struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

int bad_dma_address;  /*  globals are automatically initialized to zero  */

void *dma_addr_to_virt(dma_addr_t dma_addr)
{
	return incoherent_va_start+(dma_addr-(BRAM_DMA_ADDR_START+BRAM_COHERENT_SZ));
}

int dma_supported(struct device *dev, u64 mask)
{
	if (mask == DMA_BIT_MASK(32))
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(dma_supported);

int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

struct gen_pool *coherent_pool;
struct gen_pool *incoherent_pool;

//  For DINI:
//
//  Shared RAM accessible by everyone starts at 0xd0080000
//  This is both "coherent" memory as well as well as scatter-gather-able memory.
//  So the coherent block maybe should be as small as possible (iomap as uncached).  Drivers seem to grab one coherent page and keep it.
//  And the rest can be cached and behave like normal...?
//  Also, self-referential pointers (bus address?) should be 0x080000-0x8ffff
//
//  So:  create 1 pool for alloc coherent
//       create a second pool for DMA stuff:
//       * ioremap
//       * add to pool
//       * pull addresses out of pool at map time; dma address is actually VA-VA(pool_start) + 0x80000
//       * at unmap time, copy out from pool to main memory and return to pool
//
//  Last thing:  create a faux page table to go from VA to dma mappings, since there is no association made at the map calls, and the VA
//               of the destination for "from" can be anywhere.  "To" doesn't matter.

//  Make the coherent pointers look like 0x0008xxxx

static void *hexagon_dma_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t flag,
				 struct dma_attrs *attrs)
{
	void *ret;

	ret = (void *) gen_pool_alloc(coherent_pool, size);


	if (ret) {
		memset(ret, 0, size);
		*dma_addr = (dma_addr_t) (ret - coherent_va_start + BRAM_DMA_ADDR_START);
	} else
		*dma_addr = ~0;

	return ret;
}

static void hexagon_free_coherent(struct device *dev, size_t size, void *vaddr,
				  dma_addr_t dma_addr, struct dma_attrs *attrs)
{
	gen_pool_free(coherent_pool, (unsigned long) vaddr, size);
}

static int check_addr(const char *name, struct device *hwdev,
		      dma_addr_t bus, size_t size)
{
	if (hwdev && hwdev->dma_mask && !dma_capable(hwdev, bus, size)) {
		if (*hwdev->dma_mask >= DMA_BIT_MASK(32))
			printk(KERN_ERR
				"%s: overflow %Lx+%zu of device mask %Lx\n",
				name, (long long)bus, size,
				(long long)*hwdev->dma_mask);
		return 0;
	}
	return 1;
}

static int hexagon_map_sg(struct device *hwdev, struct scatterlist *sg,
			  int nents, enum dma_data_direction dir,
			  struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(hwdev);
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		s->dma_address = ops->map_page(hwdev, sg_page(s), s->offset, s->length, dir, attrs);
		s->dma_length = s->length;

		//if (!check_addr("map_sg", hwdev, s->dma_address, s->length))
		//	return 0;
	}
	return nents;
}

void hexagon_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
                enum dma_data_direction dir, struct dma_attrs *attrs)
{
	struct dma_map_ops *ops = get_dma_ops(dev);
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
                ops->unmap_page(dev, s->dma_address, s->dma_length, dir, attrs);
	}
}



/*
 * address is virtual
 */

static inline void dma_sync(void *addr, size_t size,
			    enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		hexagon_clean_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	case DMA_FROM_DEVICE:
		hexagon_inv_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	case DMA_BIDIRECTIONAL:
		flush_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	default:
		BUG();
	}
}

/**
 * hexagon_map_page() - maps an address for device DMA
 * @dev:	pointer to DMA device
 * @page:	pointer to page struct of DMA memory
 * @offset:	offset within page
 * @size:	size of memory to map
 * @dir:	transfer direction
 * @attrs:	pointer to DMA attrs (not used)
 *
 * Called to map a memory address to a DMA address prior
 * to accesses to/from device.
 *
 * We don't particularly have many hoops to jump through
 * so far.  Straight translation between phys and virtual.
 *
 * DMA is not cache coherent so sync is necessary; this
 * seems to be a convenient place to do it.
 *
 */
static dma_addr_t hexagon_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   struct dma_attrs *attrs)
{
	dma_addr_t bus = (dma_addr_t) gen_pool_alloc(incoherent_pool, size);

	//printk("%s va=0x%08x sz=%d dir=%d -> 0x%08x", __func__, page_to_virt(page)+offset, size, dir, bus);

	BUG_ON(!bus);

	bus = bus - (dma_addr_t) incoherent_va_start + BRAM_DMA_ADDR_START + BRAM_COHERENT_SZ;

	//printk(" (0x%08x)\n", bus);

	WARN_ON(size == 0);

	// 'bus' now has the device address of the incoherent memory

	if (dir == DMA_TO_DEVICE) {
		//  Copy from 
		memcpy(dma_addr_to_virt(bus), page_to_virt(page)+offset, size);
	}

	dma_sync(dma_addr_to_virt(bus), size, dir);
	return bus;
}

static void hexagon_unmap_page(struct device *dev, dma_addr_t handle,
                size_t size, enum dma_data_direction dir,
                struct dma_attrs *attrs)
{
	void *dst;

	//printk("%s bus 0x%08x sz=%d dir=%d -> va=0x%08x\n", __func__, handle, size, dir, dst);

	if (dir == DMA_TO_DEVICE) {
		goto out;
	}  //  was already copied over, so just let go of the page

	dma_sync(dma_addr_to_virt(handle), size, DMA_BIDIRECTIONAL);  //  invalidate first, then copy
	//  copy out is not our problem since we don't have the original skb pointer
	//  Shit, it is our problem though because we can't free it until the copy out is done.
	return;

out:
	gen_pool_free(incoherent_pool, dma_addr_to_virt(handle), size);
}

static void hexagon_sync_single_for_cpu(struct device *dev,
					dma_addr_t dma_handle, size_t size,
					enum dma_data_direction dir)
{
	dma_sync(dma_addr_to_virt(dma_handle), size, dir);
}

static void hexagon_sync_single_for_device(struct device *dev,
					dma_addr_t dma_handle, size_t size,
					enum dma_data_direction dir)
{
	dma_sync(dma_addr_to_virt(dma_handle), size, dir);
}

struct dma_map_ops hexagon_dma_ops = {
	.alloc		= hexagon_dma_alloc_coherent,
	.free		= hexagon_free_coherent,
	.map_sg		= hexagon_map_sg,
	.unmap_sg	= hexagon_unmap_sg,
	.map_page	= hexagon_map_page,
	.unmap_page	= hexagon_unmap_page,
	.sync_single_for_cpu = hexagon_sync_single_for_cpu,
	.sync_single_for_device = hexagon_sync_single_for_device,
	.is_phys	= 1,
};



void __init hexagon_dma_init(void)
{
	printk("%s\n", __func__);

	BUG_ON(dma_ops);  //  wat?

	//coherent_va_start = ioremap_nocache(BRAM_PA, BRAM_COHERENT_SZ);
	coherent_va_start = 0xFE000000;
	incoherent_va_start = 0xFE000000 + BRAM_COHERENT_SZ;  // need define...

	printk("%s coherent_va_start 0x%08x\n",  __func__, coherent_va_start);
	printk("%s incoherent_va_start 0x%08x\n", __func__, incoherent_va_start);

	BUG_ON(!coherent_va_start || !incoherent_va_start);

	//  I think the Xilinx DMA driver requests coherent buffer descriptors in chunks which it manages itself.
	coherent_pool = gen_pool_create(COHERENT_ALLOC_SHIFT, -1);
	BUG_ON(!coherent_pool);
	gen_pool_add(coherent_pool, coherent_va_start, BRAM_COHERENT_SZ, -1);

	//  The incoherent chunks come from everywhere else and can be anything...
	incoherent_pool = gen_pool_create(INCOHERENT_ALLOC_SHIFT, -1);
	BUG_ON(!incoherent_pool);
	BUG_ON(gen_pool_add(incoherent_pool, incoherent_va_start, BRAM_INCOHERENT_SZ, -1));

	dma_ops = &hexagon_dma_ops;  //  probably should associate this with the device, however that's done
}

