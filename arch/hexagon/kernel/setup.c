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
#include <linux/initrd.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <asm/io.h>
#include <asm/fixmap.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/processor.h>
#include <asm/hexagon_vm.h>
#include <asm/vm_mmu.h>
#include <asm/platform.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/angel_console.h>
#include <asm/hwcap.h>
#include <linux/percpu.h>

#ifdef CONFIG_SMP
DECLARE_PER_CPU(u32, vpid);
#else
DEFINE_PER_CPU(u32, vpid);
#endif

unsigned long vmversion;
unsigned long elf_hwcap __read_mostly;

EXPORT_SYMBOL_GPL(elf_hwcap);

char cmd_line[COMMAND_LINE_SIZE];
static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;

u64 boot_dtb_phys;  /* DTB physical address, set by head.S from R1:0 */

void *boot_info;

const struct machine_desc *mdesc;

/*
 * populate hardware capabilities using vmgetinfo
 */
static void __init setup_hwcap(void)
{
	struct vm_rev rev;
	unsigned long hvx_vlength;
	unsigned int isa;

	elf_hwcap = 0;

	/* Get revision information for ISA version */
	rev.raw = __vmgetinfo(vm_info_rev);

	/*
	 * rev.isa encodes the version as two hex digits: 0x68 for V68, 0x73
	 * for V73, 0x81 for V81 and so on (this is the low byte of the DSP
	 * revision register).  Decode those digits into the plain decimal
	 * version number so the switch below and the printout show "v73"
	 * rather than the raw 0x73 (=115).
	 */
	isa = ((rev.isa >> 4) & 0xf) * 10 + (rev.isa & 0xf);

	/* Set ISA version using enumeration values in 7-bit field (bits 0-6) */
	switch (isa) {
	case 81:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V81;
		break;
	case 79:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V79;
		break;
	case 73:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V73;
		break;
	case 71:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V71;
		break;
	case 69:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V69;
		break;
	case 68:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V68;
		break;
	case 67:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V67;
		break;
	case 66:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V66;
		break;
	case 65:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V65;
		break;
	case 62:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V62;
		break;
	case 60:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V60;
		break;
	case 55:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V55;
		break;
	case 5:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V5;
		break;
	case 4:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V4;
		break;
	case 3:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V3;
		break;
	case 2:
		elf_hwcap |= HWCAP_HEXAGON_ISA_V2;
		break;
	default:
		/* Unknown ISA version - leave ISA field as 0 */
		break;
	}

	/* Check for HVX support */
	hvx_vlength = __vmgetinfo(vm_info_hvx_vlength);
	if (hvx_vlength) {
		elf_hwcap |= HWCAP_HEXAGON_HVX;

		if (hvx_vlength >= 128)
			elf_hwcap |= HWCAP_HEXAGON_HVX_LENGTH_128B;
	}

	printk(KERN_INFO "Hexagon hwcap: 0x%08lx (v%u%s%s%s%s%s)\n",
		elf_hwcap,
		isa,
		(elf_hwcap & HWCAP_HEXAGON_HVX) ? ", HVX" : "",
		(elf_hwcap & HWCAP_HEXAGON_HVX_LENGTH_128B) ? ", HVX-128B" : "",
		(elf_hwcap & HWCAP_HEXAGON_HVX_IEEE_FP) ? ", HVX-IEEE-FP" : "",
		(elf_hwcap & HWCAP_HEXAGON_AUDIO) ? ", Audio" : "",
		(elf_hwcap & HWCAP_HEXAGON_CABAC) ? ", CABAC" : "");
}

/*
 * setup_arch -  high level architectural setup routine
 * @cmdline_p: pointer to pointer to command-line arguments
 */

void __init setup_arch(char **cmdline_p)
{
	void *dtb;

	/*
	 * If bootloader passed a DTB address in R1:0, use it.
	 * Otherwise fall back to built-in DTB.
	 */
	if (boot_dtb_phys) {
		/*
		 * The linear map only covers RAM at/above PHYS_OFFSET; a
		 * DTB below it (e.g. from a bootloader that placed it
		 * outside our window) cannot be phys_to_virt()'d.
		 */
		if (boot_dtb_phys < PHYS_OFFSET) {
			pr_warn("DTB at phys 0x%llx below PHYS_OFFSET, using built-in\n",
				boot_dtb_phys);
			boot_dtb_phys = 0;
		}
	}

	if (boot_dtb_phys) {
		dtb = phys_to_virt((unsigned long)boot_dtb_phys);
		if (fdt_magic(dtb) != FDT_MAGIC) {
			pr_warn("Invalid DTB at phys 0x%llx, using built-in\n",
				boot_dtb_phys);
			boot_dtb_phys = 0;
			dtb = &__dtb_start;
		}
	} else {
		dtb = &__dtb_start;
	}

	/*
	 * Set up event bindings to handle exceptions and interrupts.
	 */
	__vmsetvec(_K_VM_event_vector);

	register_angel_console();

	printk(KERN_INFO "PHYS_OFFSET=0x%08lx\n", PHYS_OFFSET);
	/*  initial machine setup from flattened device tree  */
	mdesc = setup_machine_fdt(dtb);
	if (!mdesc)
		panic("setup_machine_fdt returned NULL\n");

	if (mdesc->setup_arch_platform)
		mdesc->setup_arch_platform();

	printk("vmversion=0x%08lx\n", vmversion);
	printk("vm build id=0x%08lx\n", __vmgetinfo(vm_info_build_id));
	printk("boot_dtb_phys=0x%llx\n", boot_dtb_phys);

	/* Setup hardware capabilities */
	setup_hwcap();

	if (!mdesc)
		panic("setup_machine_fdt returned NULL\n");

	if (mdesc->setup_arch_platform) {
		mdesc->setup_arch_platform();
	}

	printk("vmversion=0x%08lx\n", vmversion);
	printk("vm build id=0x%08lx\n", __vmgetinfo(vm_info_build_id));
	/*
	 * Command line precedence:
	 * - External DTB (boot_dtb_phys != 0): DTB /chosen/bootargs takes
	 *   precedence, with CONFIG_CMDLINE as fallback if bootargs is empty.
	 * - Built-in DTB (boot_dtb_phys == 0): always use CONFIG_CMDLINE.
	 *   The built-in DTB bootargs may be incomplete for the current
	 *   boot environment (e.g. missing mem=, initrd params).
	 */
	if (!boot_dtb_phys || !boot_command_line[0])
		strscpy(boot_command_line, default_command_line,
			COMMAND_LINE_SIZE);
	strscpy(cmd_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = cmd_line;

	early_fixmap_init();

	parse_early_param();

	setup_arch_memory();

	reserve_initrd_mem();
	/*  Now is time we unflatten devicetree  */
	unflatten_device_tree();

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
		//  Break this out elsewhere?
		build_id	= __vmgetinfo(vm_info_build_id);
		bootflags.raw	= __vmgetinfo(vm_info_boot_flags);
		stlb_info.raw	= __vmgetinfo(vm_info_stlb);
		syscfg.raw	= __vmgetinfo(vm_info_syscfg);
		rev.raw		= __vmgetinfo(vm_info_rev);
		l2size		= __vmgetinfo(vm_info_l2mem_size);
		l2tcm		= __vmgetinfo(vm_info_tcm_size);
		l2tcm_base	= __vmgetinfo(vm_info_tcm_base);
		l2cache		= __vmgetinfo(vm_info_l2tag_size);

		//  Macro might be nice
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
		seq_printf(m, "\n");
	}
#endif

	seq_printf(m, "processor\t\t: %d\n", cpu);
	seq_printf(m, "model name\t\t: Hexagon Virtual Machine\n");
#ifdef CONFIG_HEXAGON_MSS
	seq_printf(m, "subsystem\t\t: MSS\n");
#endif
#ifdef CONFIG_HEXAGON_FORCE_USR
	seq_printf(m, "Forced USR Value\t: 0x%08x\n", CONFIG_HEXAGON_USR_VAL);
#endif
	seq_printf(m, "VPID:  0x%08x\n", per_cpu(vpid, cpu));
	seq_printf(m, "\n");
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start  = &c_start,
	.next   = &c_next,
	.stop   = &c_stop,
	.show   = &show_cpuinfo,
};

static int __init stahp(char *str)
{
	asm volatile("brkpt;\n");
	return 0;
}

early_param("stahp", stahp);
