// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device tree setup for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <asm/prom.h>

/*  called via early_init_dt_scan_memory?  */
void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	/*
	 * RAM discovery comes from bootmem_lastpg (mem=) rather than the
	 * DTB; ignore /memory nodes instead of tripping over them when a
	 * bootloader-provided tree carries one.
	 */
	pr_info("Ignoring DT memory node: base 0x%llx size 0x%llx\n",
		base, size);
}

void __init early_init_devtree(void *dtb)
{
	initial_boot_params = dtb;

	if (fdt_magic(initial_boot_params) != OF_DT_HEADER)
		panic("Invalid device tree\n");

	of_get_flat_dt_root();

	/*
	 * Sets the top level address and size cells which are stored
	 * in globals by the devtree infrastructure.
	 */
	early_init_dt_scan_root();

	/* Retrieve various information from the /chosen node */
	early_init_dt_scan_chosen(boot_command_line);
}
