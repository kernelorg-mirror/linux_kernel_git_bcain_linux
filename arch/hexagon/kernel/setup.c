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
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/hexagon_vm.h>
#include <asm/vm_mmu.h>
#include <asm/prom.h>
#include <asm/time.h>

char cmd_line[COMMAND_LINE_SIZE];
static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;

u64 boot_dtb_phys;  /* DTB physical address, set by head.S from R1:0 */

/*
 * setup_arch -  high level architectural setup routine
 * @cmdline_p: pointer to pointer to command-line arguments
 */

void __init setup_arch(char **cmdline_p)
{
	/*
	 * The bootloader passes the DTB address in R1:0; the linear map
	 * only covers RAM at/above PHYS_OFFSET, so a DTB placed below it
	 * cannot be phys_to_virt()'d.  RAM size is not known this early, so
	 * also reject a DTB whose linear virtual address would fall outside
	 * the kernel linear window (PAGE_OFFSET .. top of the address space);
	 * such an address would otherwise fault when dereferenced.
	 */
	if (!boot_dtb_phys || boot_dtb_phys < PHYS_OFFSET ||
	    boot_dtb_phys - PHYS_OFFSET > (u64)(-PAGE_OFFSET))
		panic("No usable device tree from bootloader\n");

	/*
	 * Set up event bindings to handle exceptions and interrupts.
	 */
	__vmsetvec(_K_VM_event_vector);

	/*  initial machine setup from flattened device tree  */
	early_init_devtree(phys_to_virt((unsigned long)boot_dtb_phys));

	/*  DTB /chosen/bootargs wins; CONFIG_CMDLINE is the fallback.  */
	if (!boot_command_line[0])
		strscpy(boot_command_line, default_command_line,
			COMMAND_LINE_SIZE);

	strscpy(cmd_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = cmd_line;

	parse_early_param();

	setup_arch_memory();

	/*  Now is time we unflatten devicetree  */
	unflatten_device_tree();

#ifdef CONFIG_SMP
	smp_start_cpus();
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

#ifdef CONFIG_SMP
	if (!cpu_online(cpu))
		return 0;
#endif

	seq_printf(m, "processor\t: %d\n", cpu);
	seq_printf(m, "model name\t: Hexagon Virtual Machine\n");
	seq_printf(m, "BogoMips\t: %lu.%02lu\n",
		(loops_per_jiffy * HZ) / 500000,
		((loops_per_jiffy * HZ) / 5000) % 100);
	seq_printf(m, "\n");
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start  = &c_start,
	.next   = &c_next,
	.stop   = &c_stop,
	.show   = &show_cpuinfo,
};
