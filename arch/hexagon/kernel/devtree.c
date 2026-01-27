#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/memblock.h>
#include <linux/initrd.h>
#include <asm/platform.h>
#include <asm/prom.h>

/*  Apparently called to "allocate" memory for the devicetree itself?  */
static void * __init __maybe_unused early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return memblock_alloc(size, align);
}

/*  called via early_init_dt_scan_memory?  */
void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	//  Cheesy solution is to just twiddle bootmem_lastpg
	BUG();  ///  XXX Todo fixme
}

#ifdef CONFIG_BLK_DEV_INITRD
/*  Seems to be the implementation used by most archs  */
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
 * @dtb: virtual address of DTB
 *
 * Sets up some of the earliest stuff -- setting the machine type,
 * pulling the command line options, and finding the memory.
 *
 * Called from setup_arch very early, before bootmem is set up.
 * The DTB is either the built-in blob (__dtb_start) or one passed
 * by the bootloader via R1:0.
 */
const struct machine_desc * __init setup_machine_fdt(void *dtb)
{
	const struct machine_desc *mdesc_best = NULL;

	initial_boot_params = dtb;

	/* check device tree validity */
	if (fdt_magic(initial_boot_params) != OF_DT_HEADER)
		return NULL;

	of_get_flat_dt_root();

	mdesc_best = of_flat_dt_match_machine(NULL, arch_get_next_mach);
	if (!mdesc_best)
		panic("Unrecognized device tree\n");

	/*
	 * Sets the top level address and size cells which are stored
	 * in globals by the devtree infrastructure.
	 */
	early_init_dt_scan_root();

	/* Retrieve various information from the /chosen node */
	early_init_dt_scan_chosen(boot_command_line);

	return mdesc_best;
}


/*
 * Other notes:
 *
 * of_have_populated_dt - returns whether or not devicetree is "populated"
 *
 */


