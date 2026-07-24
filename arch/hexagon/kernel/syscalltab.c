// SPDX-License-Identifier: GPL-2.0-only
/*
 * System call table for Hexagon
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/syscalls.h>
#include <linux/signal.h>
#include <linux/unistd.h>

#include <asm/syscall.h>

#define __SYSCALL(nr, call) [nr] = (call),
#define __SYSCALL_WITH_COMPAT(nr, native, compat)        __SYSCALL(nr, native)

/*
 * mmap2(2)'s offset argument is always in 4096-byte units, independent of the
 * kernel PAGE_SIZE (this is what musl and glibc pass), while ksys_mmap_pgoff()
 * expects an offset already in PAGE_SIZE units. Routing sys_mmap2 straight to
 * it skipped that conversion, making vm_pgoff PAGE_SIZE/4096 times too large
 * for any non-zero file offset. On the 64KB-page port this is 16x: file
 * mappings past the first page (e.g. later PT_LOAD segments of shared
 * libraries larger than PAGE_SIZE) then pointed beyond EOF and took a spurious
 * SIGBUS on first access, breaking the dynamic loader for many binaries.
 */
SYSCALL_DEFINE6(hexagon_mmap2, unsigned long, addr, unsigned long, len,
		unsigned long, prot, unsigned long, flags,
		unsigned long, fd, unsigned long, pgoff)
{
	if (pgoff & ((1 << (PAGE_SHIFT - 12)) - 1))
		return -EINVAL;

	return ksys_mmap_pgoff(addr, len, prot, flags, fd,
			       pgoff >> (PAGE_SHIFT - 12));
}
#define sys_mmap2 sys_hexagon_mmap2

SYSCALL_DEFINE6(hexagon_fadvise64_64, int, fd, int, advice,
		SC_ARG64(offset), SC_ARG64(len))
{
	return ksys_fadvise64_64(fd, SC_VAL64(loff_t, offset), SC_VAL64(loff_t, len), advice);
}
#define sys_fadvise64_64 sys_hexagon_fadvise64_64

#define sys_sync_file_range sys_sync_file_range2

void *sys_call_table[__NR_syscalls] = {
#include <asm/syscall_table_32.h>
};
