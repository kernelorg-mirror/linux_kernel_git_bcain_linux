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

/*
 * Two levels of indirection so that the argument is macro-expanded before it
 * is pasted: the redirects below (sys_fadvise64_64, sys_sync_file_range) are
 * plain #defines, and an operand of ## is not expanded.
 */
#define __SC_ENTRY(call)	__SC_ENTRY_(call)
#define __SC_ENTRY_(call)	__hexagon_##call

#define __SYSCALL_WITH_COMPAT(nr, native, compat)        __SYSCALL(nr, native)

/*
 * mmap2(2)'s offset argument is always in 4096-byte units, independent of the
 * kernel PAGE_SIZE. Routing sys_mmap2 straight to sys_mmap_pgoff (whose offset
 * is in PAGE_SIZE units) skipped that conversion, making vm_pgoff PAGE_SIZE/4096
 * times too large for any non-zero file offset. On the 64KB-page port this is
 * 16x: file mappings past the first page (e.g. later PT_LOAD segments of shared
 * libraries larger than PAGE_SIZE) then pointed beyond EOF and took a spurious
 * SIGBUS on first access, breaking the dynamic loader for many binaries. Use
 * the real sys_mmap2() (in syscall.c), which converts via >>(PAGE_SHIFT-12).
 */

SYSCALL_DEFINE6(hexagon_fadvise64_64, int, fd, int, advice,
		SC_ARG64(offset), SC_ARG64(len))
{
	return ksys_fadvise64_64(fd, SC_VAL64(loff_t, offset), SC_VAL64(loff_t, len), advice);
}
#define sys_fadvise64_64 sys_hexagon_fadvise64_64

#define sys_sync_file_range sys_sync_file_range2

/* Not defined using SYSCALL_DEFINE0 to avoid error injection */
asmlinkage long __hexagon_sys_ni_syscall(const struct pt_regs *__unused);
asmlinkage long __hexagon_sys_ni_syscall(const struct pt_regs *__unused)
{
	return -ENOSYS;
}

#define __SYSCALL(nr, call) asmlinkage long __SC_ENTRY(call)(const struct pt_regs *);
#include <asm/syscall_table_32.h>
#undef __SYSCALL

#define __SYSCALL(nr, call) [nr] = __SC_ENTRY(call),

syscall_fn sys_call_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = __hexagon_sys_ni_syscall,
#include <asm/syscall_table_32.h>
};
