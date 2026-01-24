// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon device tree support
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/bootmem.h>
#include <linux/initrd.h>
#include <asm/platform.h>
#include <asm/prom.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	/*
	 * Hexagon memory layout is fixed at boot; the kernel maps all
	 * physical memory via its page tables in head.S.  Dynamic memory
	 * addition from the device tree is not supported.
	 */
	WARN_ONCE(1, "hexagon: unexpected memory node in device tree\n");
}

#ifdef CONFIG_BLK_DEV_INITRD
static void __init __maybe_unused early_init_dt_setup_initrd_arch(u64 start,
					    u64 end)
{
	initrd_start = (unsigned long)__va(start);
	initrd_end = (unsigned long)__va(end);
	initrd_below_start_ok = 1;
}
#endif

/*
 * Iterator for of_flat_dt_match_machine
 */
static const void * __init arch_get_next_mach(const char *const **match)
{
	static const struct machine_desc *mdesc = __arch_info_begin;
	const struct machine_desc *m = mdesc;

	if (m >= __arch_info_end)
		return NULL;

	mdesc++;
	*match = m->dt_compat;
	return m;
}

/*
 * setup_machine_fdt - set up machine based on dtb passed to kernel
 * @dt_phys: physical address of dtb
 *
 * Matches device tree to a machine descriptor by compatible string,
 * scans the root node properties, and retrieves the command line
 * from the /chosen node.
 */
const struct machine_desc * __init setup_machine_fdt(void *dt_phys)
{
	const struct machine_desc *mdesc_best = NULL;

#ifdef CONFIG_DTB_BUILTIN
	initial_boot_params = dt_phys;
#else
	initial_boot_params = phys_to_virt(dt_phys);
#endif

	if (fdt_magic(initial_boot_params) != OF_DT_HEADER)
		return NULL;

	mdesc_best = of_flat_dt_match_machine(NULL, arch_get_next_mach);

	if (!mdesc_best)
		panic("Unrecognized device tree\n");

	early_init_dt_scan_root();
	early_init_dt_scan_chosen(boot_command_line);

	return mdesc_best;
}
