// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/*
 * Support for user memory access from kernel.  This will
 * probably be inlined for performance at some point, but
 * for ease of debug, and to a lesser degree for code size,
 * we implement here as subroutines.
 */
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/pgtable.h>
#include <linux/mm.h>

/*
 * For clear_user(), exploit previously defined copy_to_user function
 * and the fact that we've got a handy zero page defined in kernel/head.S
 *
 * The Hexagon H2 hypervisor autonomously walks the kernel's page tables
 * for TLB misses.  When the hypervisor encounters an INVALID L1 (PGD)
 * entry during a kernel-mode write to a user address, some hypervisor
 * implementations (notably QEMU) may not properly generate a TLB miss
 * exception and instead hang.  Pre-fault each page via get_user_pages_fast
 * to ensure valid page table entries exist before the raw copy.
 */
__kernel_size_t __clear_user_hexagon(void __user *dest, unsigned long count)
{
	long uncleared;
	unsigned long addr = (unsigned long)dest;

	while (count > 0) {
		unsigned long offset = offset_in_page(addr);
		unsigned long this_len = PAGE_SIZE - offset;
		struct page *page;
		int ret;

		if (this_len > count)
			this_len = count;

		/* Pre-fault the page via software page table walk */
		ret = get_user_pages_fast(addr, 1, FOLL_WRITE, &page);
		if (ret <= 0)
			return count;

		uncleared = raw_copy_to_user((void __user *)addr,
					     &empty_zero_page, this_len);
		put_page(page);

		if (uncleared)
			return count - this_len + uncleared;

		addr += this_len;
		count -= this_len;
	}

	return 0;
}
