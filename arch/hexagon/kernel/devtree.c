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

//  basically a copy of dt_scan_chosen, but doesn't do the initrd scan.
static int __init early_init_dt_scan_chosen_noinitrd(unsigned long node, const char *uname,
				     int depth, void *data)
{
	unsigned long l;
	char *p;

	pr_debug("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

	if (depth != 1 || !data ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

	//early_init_dt_check_for_initrd(node);

	/* Retrieve command line */
	p = of_get_flat_dt_prop(node, "bootargs", &l);
	if (p != NULL && l > 0)
		strscpy(data, p, min((int)l, COMMAND_LINE_SIZE));

	/*
	 * CONFIG_CMDLINE is meant to be a default in case nothing else
	 * managed to set the command line, unless CONFIG_CMDLINE_FORCE
	 * is set in which case we override whatever was found earlier.
	 */
#ifdef CONFIG_CMDLINE
#ifndef CONFIG_CMDLINE_FORCE
	if (!((char *)data)[0])
#endif
		strscpy(data, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif /* CONFIG_CMDLINE */

	pr_debug("Command line is: %s\n", (char*)data);

	/* break now */
	return 1;
}



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
 * Sets up some of the earliest stuff -- setting the machine type,
 * pulling the command line options, and finding the memory.
 *
 * Since we're calling this from setup_arch, this is going to happen
 * very, very early.  Prior to even the bootmem being set up in the
 * old scheme I think.
 *
 * Also since we're always being fired up by the hypervisor, then
 * we are already running with the MMU on with an init segtable.
 */
const struct machine_desc * __init setup_machine_fdt(void *dt_phys)
{
	const struct machine_desc *mdesc_best = NULL;
	unsigned long dt_root;

#ifdef CONFIG_HEXAGON_MSM8974_FLUID
	//  Fixme:  use this for...  everybody if possible
	//  The "tags" struct or whatever provided by LK; currently stuffing it in the "external buffer" space.
	initial_boot_params = (void *) &external_buffer;
	dt_root = of_get_flat_dt_root();
	of_scan_flat_dt(early_init_dt_scan_chosen_noinitrd, boot_command_line);
#endif

	/*  reset global pointer that devtree uses to the proper blob  */
#ifdef CONFIG_DTB_BUILTIN
	/*  This is the label placed on the assembly blob; will already be in virtual space  */
	initial_boot_params = dt_phys;
#else
        initial_boot_params = phys_to_virt(dt_phys);
#endif

        /* check device tree validity */
        if (fdt_magic(initial_boot_params) != OF_DT_HEADER) {
                return NULL;
	}

	dt_root = of_get_flat_dt_root();

	mdesc_best = of_flat_dt_match_machine(NULL, arch_get_next_mach);

	if (!mdesc_best) {
		panic("Unrecognized device tree\n");
	}

        /*
	 * Sets the top level address and size cells which are stored
	 * in globals by the devtree infrastructure.
	 */
        early_init_dt_scan_root();

        /*  Retrieve various information from the /chosen node  */
#ifndef CONFIG_HEXAGON_MSM8974_FLUID
	//  Fixme:  do this consistently for platforms
	early_init_dt_scan_chosen(boot_command_line);
#endif
        /*  Setup memory  */
	//of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	return mdesc_best;
}


/*
 * Other notes:
 *
 * of_have_populated_dt - returns whether or not devicetree is "populated"
 *
 */


