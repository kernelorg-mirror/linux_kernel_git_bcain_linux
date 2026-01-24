// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon system calls
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#include <linux/types.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <asm/mman.h>
#include <asm/registers.h>

asmlinkage int sys_mmap(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, off_t off)
{
	int retval = -EINVAL;

	if (off & ~PAGE_MASK)
		goto out;

	retval = sys_mmap_pgoff(addr, len, prot, flags, fd, off >> PAGE_SHIFT);
out:
	return retval;
}

static bool straight_mmap2;

asmlinkage long sys_mmap2(unsigned long addr, size_t len,
			unsigned long prot, unsigned long flags,
			unsigned long fd, unsigned long pgoff)
{
	long ret = -EINVAL;

	if (!straight_mmap2)
		pgoff >>= PAGE_SHIFT - 12;

	ret = sys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);

	return ret;
}

static int __init use_straight_mmap2(char *str)
{
	straight_mmap2 = true;
	return 0;
}

early_param("use_straight_mmap2", use_straight_mmap2);
