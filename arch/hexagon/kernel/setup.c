// SPDX-License-Identifier: GPL-2.0-only
/*
 * Arch related setup for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/hexagon_vm.h>
#include <asm/vm_mmu.h>
#include <asm/platform.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <linux/clk-provider.h>
#include <asm/angel_console.h>
#include <linux/percpu.h>

#ifdef CONFIG_SMP
DECLARE_PER_CPU(u32, vpid);
#else
DEFINE_PER_CPU(u32, vpid);
#endif

unsigned long vmversion;

char cmd_line[COMMAND_LINE_SIZE];

u64 boot_info;

const struct machine_desc *mdesc;

/*
 * setup_arch -  high level architectural setup routine
 * @cmdline_p: pointer to pointer to command-line arguments
 */

void __init setup_arch(char **cmdline_p)
{
	char *p = (char *)&external_buffer;
	void *dtb = &__dtb_start;

	/*
	 * Prefer bootloader-provided FDT if valid.  The boot stub passes
	 * the FDT physical address in R1:R0, which head.S saves as
	 * boot_info (64-bit).  If it points to a valid FDT within our
	 * mapped memory range, use it instead of the built-in DTB.
	 */
	if (boot_info) {
		void *boot_dtb = phys_to_virt((phys_addr_t)boot_info);

		if (fdt_magic(boot_dtb) == FDT_MAGIC)
			dtb = boot_dtb;
	}

	/*
	 * Set up event bindings to handle exceptions and interrupts.
	 */
	__vmsetvec(_K_VM_event_vector);

	register_angel_console();

	pr_info("PHYS_OFFSET=0x%08lx\n", PHYS_OFFSET);
	/* Initial machine setup from flattened device tree */
	mdesc = setup_machine_fdt(dtb);

	if (!mdesc)
		panic("setup_machine_fdt returned NULL\n");

	if (mdesc->setup_arch_platform)
		mdesc->setup_arch_platform();

	pr_info("vmversion=0x%08lx\n", vmversion);
	pr_info("vm build id=0x%08lx\n", __vmgetinfo(vm_info_build_id));
	pr_info("boot_info=%08llx\n", boot_info);
	/*
	 * If external_buffer has content (e.g. from a bootloader), use it.
	 * Otherwise trust early_init_dt_scan_chosen() which already handled:
	 *   DTB bootargs present -> use them
	 *   DTB bootargs empty   -> use CONFIG_CMDLINE
	 */
	if (p && (p[0] != '\0'))
		strscpy(boot_command_line, p, COMMAND_LINE_SIZE);
	/*
	 * boot_command_line and the value set up by setup_arch
	 * are both picked up by the init code. If no reason to
	 * make them different, pass the same pointer back.
	 */
	strscpy(cmd_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = cmd_line;

	parse_early_param();

	setup_arch_memory();

	/* Unflatten device tree */
	unflatten_device_tree();

	/* Initialize early clock providers from DT (e.g. fixed-clock) */
	of_clk_init(NULL);

#ifdef CONFIG_SMP
	smp_start_cpus();
#endif

#if defined(CONFIG_DUMMY_CONSOLE)
	if (!conswitchp)
		conswitchp = &dummy_con;
#endif

}

/*
 * Functions for dumping CPU info via /proc
 * Probably should move to kernel/proc.c or something.
 */
static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < nr_cpu_ids ? (void *)((unsigned long) *pos + 1) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

/*
 * Eventually this will dump information about
 * CPU properties like ISA level, TLB size, etc.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	int cpu = (unsigned long) v - 1;

#ifdef CONFIG_H2
	unsigned long		build_id;
	struct vm_boot_flags	bootflags;
	struct vm_stlb_info	stlb_info;
	struct vm_syscfg	syscfg;
	struct vm_rev		rev;
	unsigned long		l2size;
	unsigned long		l2cache;
	unsigned long		l2tcm;
	unsigned long		l2tcm_base;
#endif

#ifdef CONFIG_SMP
	if (!cpu_online(cpu))
		return 0;
#endif


#ifdef CONFIG_H2
	if (cpu == 0) {
		build_id	= __vmgetinfo(vm_info_build_id);
		bootflags.raw	= __vmgetinfo(vm_info_boot_flags);
		stlb_info.raw	= __vmgetinfo(vm_info_stlb);
		syscfg.raw	= __vmgetinfo(vm_info_syscfg);
		rev.raw		= __vmgetinfo(vm_info_rev);
		l2size		= __vmgetinfo(vm_info_l2mem_size);
		l2tcm		= __vmgetinfo(vm_info_tcm_size);
		l2tcm_base	= __vmgetinfo(vm_info_tcm_base);
		l2cache		= __vmgetinfo(vm_info_l2tag_size);

		/* Display VM info */
		seq_printf(m, "VM Build ID\t\t: 0x%08lx\n", build_id);
		seq_printf(m, "VM Boot flags\t\t: 0x%08lx\n", bootflags.raw);
		seq_printf(m, "   use_tcm\t\t: %d\n", bootflags.use_tcm);
		seq_printf(m, "VM STLB Info\t\t: 0x%08lx\n", stlb_info.raw);
		seq_printf(m, "   enabled\t\t: %d\n", stlb_info.enabled);
		seq_printf(m, "   max_sets_log2\t: %d\n", stlb_info.max_sets_log2);
		seq_printf(m, "   max_ways\t\t: %d\n", stlb_info.max_ways);
		seq_printf(m, "   size\t\t\t: %d\n", stlb_info.size);
		seq_printf(m, "VM REV\t\t\t: 0x%08lx\n", rev.raw);
		seq_printf(m, "   isa\t\t\t: %d\n", rev.isa);
		seq_printf(m, "   l2tags\t\t: %d\n", rev.l2tags);
		seq_printf(m, "   l2size\t\t: %d\n", rev.l2size);
		seq_printf(m, "   layer\t\t: %d\n", rev.layer);
		seq_printf(m, "   metal\t\t: %d\n", rev.metal);

		seq_printf(m, "VM SYSCFG\t\t: 0x%08lx\n", syscfg.raw);
		seq_printf(m, "   L2 Mem\t\t: %lu\n", l2size);
		seq_printf(m, "   L2 Cache\t\t: %lu\n", l2cache);
		seq_printf(m, "   TCM\t\t\t: %lu\n", l2tcm);
		seq_printf(m, "   TCM Base\t\t: 0x%lx\n", l2tcm_base);
		seq_puts(m, "\n");
	}
#endif

	seq_printf(m, "processor\t\t: %d\n", cpu);
	seq_puts(m, "model name\t\t: Hexagon Virtual Machine\n");
#ifdef CONFIG_HEXAGON_MSS
	seq_puts(m, "subsystem\t\t: MSS\n");
#endif
#ifdef CONFIG_HEXAGON_FORCE_USR
	seq_printf(m, "Forced USR Value\t: 0x%08x\n", CONFIG_HEXAGON_USR_VAL);
#endif
	seq_printf(m, "VPID:  0x%08x\n", per_cpu(vpid, cpu));
	seq_puts(m, "\n");
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start  = &c_start,
	.next   = &c_next,
	.stop   = &c_stop,
	.show   = &show_cpuinfo,
};
