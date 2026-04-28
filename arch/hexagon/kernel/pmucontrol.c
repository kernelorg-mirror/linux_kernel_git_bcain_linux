/*
 * SPDX-License-Identifier: GPL-2.0
 * Hexagon PMU (Performance Monitoring Unit) debugfs control interface
 *
 * Exposes PMU counter registers and event configuration via debugfs
 * entries under /sys/kernel/debug/pmu/.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <asm/hexagon_vm.h>

#define BUFLEN 1024

static struct dentry *pmu_dir;
static bool pmu_debug;

struct pmu_regs {
	unsigned long pmucnt[4];
	unsigned long tlbmissx[2];
	unsigned long tlbmissrw[2];
	unsigned long stlbmiss[2];
	unsigned long pmuevtcfg;
	unsigned long pmucfg;
};

static struct pmu_regs regs;

#define VMTRAP_GET_PCYCLES	"0xe"

#define PMUCONFIG_SETREG	1
#define PMUCONFIG_GETREG	2

static inline unsigned long pmu_reg_read(long regnum)
{
	return __vmpmucfg(PMUCONFIG_GETREG, 0x0, regnum, 0x0);
}

static inline void pmu_reg_write(long regnum, unsigned long val)
{
	__vmpmucfg(PMUCONFIG_SETREG, 0x0, regnum, val);
}

static void read_pmu_regs(struct pmu_regs *r)
{
	r->pmucnt[0] = pmu_reg_read(0);
	r->pmucnt[1] = pmu_reg_read(1);
	r->pmucnt[2] = pmu_reg_read(2);
	r->pmucnt[3] = pmu_reg_read(3);

	r->tlbmissx[0] = pmu_reg_read(-2);
	r->tlbmissx[1] = pmu_reg_read(-3);
	r->tlbmissrw[0] = pmu_reg_read(-4);
	r->tlbmissrw[1] = pmu_reg_read(-5);
	r->stlbmiss[0] = pmu_reg_read(-6);
	r->stlbmiss[1] = pmu_reg_read(-7);
	r->pmuevtcfg = pmu_reg_read(8);
	r->pmucfg = pmu_reg_read(10);

	if (pmu_debug) {
		pr_info("pmucnt[0] = %lu\n"
			"pmucnt[1] = %lu\n"
			"pmucnt[2] = %lu\n"
			"pmucnt[3] = %lu\n"
			"tlbmissx[0] = %lu\n"
			"tlbmissx[1] = %lu\n"
			"tlbmissrw[0] = %lu\n"
			"tlbmissrw[1] = %lu\n"
			"stlbmiss[0] = %lu\n"
			"stlbmiss[1] = %lu\n"
			"pmuevtcfg = 0x%lx\n"
			"pmucfg = 0x%08lx\n",
			r->pmucnt[0], r->pmucnt[1],
			r->pmucnt[2], r->pmucnt[3],
			r->tlbmissx[0], r->tlbmissx[1],
			r->tlbmissrw[0], r->tlbmissrw[1],
			r->stlbmiss[0], r->stlbmiss[1],
			r->pmuevtcfg, r->pmucfg);
	}
}

static void write_pmu_regs(struct pmu_regs *r)
{
	pmu_reg_write(0, r->pmucnt[0]);
	pmu_reg_write(1, r->pmucnt[1]);
	pmu_reg_write(2, r->pmucnt[2]);
	pmu_reg_write(3, r->pmucnt[3]);

	pmu_reg_write(-2, r->tlbmissx[0]);
	pmu_reg_write(-3, r->tlbmissx[1]);
	pmu_reg_write(-4, r->tlbmissrw[0]);
	pmu_reg_write(-5, r->tlbmissrw[1]);
	pmu_reg_write(-6, r->stlbmiss[0]);
	pmu_reg_write(-7, r->stlbmiss[1]);
}

static void reset_pmu(void)
{
	pmu_reg_write(8, 0x0);
	memset(&regs, 0, sizeof(regs) - 2 * sizeof(unsigned long));
	write_pmu_regs(&regs);
	pmu_reg_write(8, regs.pmuevtcfg);
}

static ssize_t enable_read_file(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	char buf[32];
	int len;

	regs.pmucfg = pmu_reg_read(10);
	len = snprintf(buf, sizeof(buf), "%lu\n", regs.pmucfg);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t enable_write_file(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	char mybuf[32];
	ssize_t ret;

	if (count >= sizeof(mybuf))
		return -EINVAL;

	if (copy_from_user(mybuf, buf, count))
		return -EFAULT;

	mybuf[count] = '\0';
	ret = kstrtoul(mybuf, 0, &regs.pmucfg);
	if (ret)
		return ret;

	if ((regs.pmucfg & 0xf) == 0)
		pmu_reg_write(8, 0);
	else
		reset_pmu();

	return count;
}

static ssize_t pmuregs_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char *mybuf;
	ssize_t ret;
	int len;

	mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	if (!mybuf)
		return -ENOMEM;

	read_pmu_regs(&regs);

	len = snprintf(mybuf, BUFLEN,
		       "PMU counters:\n\t%lu\n\t%lu\n\t%lu\n\t%lu\n\n"
		       "TLB missx:\n\t%lu\n\t%lu\n\n"
		       "TLB missrw:\n\t%lu\n\t%lu\n\n"
		       "STLB miss:\n\t%lu\n\t%lu\n\n"
		       "PMU event config:\n\t0x%08lx\n\n",
		       regs.pmucnt[0], regs.pmucnt[1],
		       regs.pmucnt[2], regs.pmucnt[3],
		       regs.tlbmissx[0], regs.tlbmissx[1],
		       regs.tlbmissrw[0], regs.tlbmissrw[1],
		       regs.stlbmiss[0], regs.stlbmiss[1],
		       regs.pmuevtcfg);
	ret = simple_read_from_buffer(userbuf, count, ppos, mybuf, len);
	kfree(mybuf);
	return ret;
}

static ssize_t pcycle_read_file(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	char buf[32];
	u64 cpu_cycles;
	u64 hw_cycles;
	int reg = (int)(long)file->private_data;
	int len;

	asm volatile("trap1(#" VMTRAP_GET_PCYCLES ")\n"
		     "%0 = r1:0\n"
		     "%1 = r3:2\n"
		     : "=r" (cpu_cycles), "=r" (hw_cycles)
		     :
		     : "r0", "r1", "r2", "r3");

	len = snprintf(buf, sizeof(buf), "%llu\n", reg ? hw_cycles : cpu_cycles);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t debug_write_file(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char mybuf[4];

	if (count == 0 || count > sizeof(mybuf))
		return -EINVAL;

	if (copy_from_user(mybuf, buf, count))
		return -EFAULT;

	pmu_debug = (mybuf[0] != '0');
	return count;
}

static ssize_t debug_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char buf[4];
	int len;

	len = snprintf(buf, sizeof(buf), "%u\n", pmu_debug);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t pmuevtcfg_read_file(struct file *file, char __user *userbuf,
				   size_t count, loff_t *ppos)
{
	char buf[32];
	int len;

	regs.pmuevtcfg = pmu_reg_read(8);
	len = snprintf(buf, sizeof(buf), "0x%lx\n", regs.pmuevtcfg);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static ssize_t pmuevtcfg_write_file(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	char mybuf[32];
	ssize_t ret;

	if (count >= sizeof(mybuf))
		return -EINVAL;

	if (copy_from_user(mybuf, buf, count))
		return -EFAULT;

	mybuf[count] = '\0';
	ret = kstrtoul(mybuf, 0, &regs.pmuevtcfg);
	if (ret)
		return ret;

	pmu_reg_write(8, regs.pmuevtcfg);
	return count;
}

static ssize_t pmucnt_read_file(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	char buf[32];
	int reg = (int)(long)file->private_data;
	int len;

	len = snprintf(buf, sizeof(buf), "%lu\n", pmu_reg_read(reg));
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static int pmucnt_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return nonseekable_open(inode, file);
}

static const struct file_operations enable_fops = {
	.read = enable_read_file,
	.write = enable_write_file,
};

static const struct file_operations debug_fops = {
	.read = debug_read_file,
	.write = debug_write_file,
};

static const struct file_operations pmuevtcfg_fops = {
	.read = pmuevtcfg_read_file,
	.write = pmuevtcfg_write_file,
};

static const struct file_operations pmucnt_fops = {
	.open = pmucnt_open,
	.read = pmucnt_read_file,
};

static const struct file_operations pcycle_fops = {
	.open = pmucnt_open,
	.read = pcycle_read_file,
};

static const struct file_operations pmuregs_fops = {
	.read = pmuregs_read_file,
};

static int __init debugpmu_init(void)
{
	struct vm_rev rev;

	pmu_dir = debugfs_create_dir("pmu", NULL);

	debugfs_create_file("debug", 0644, pmu_dir, NULL, &debug_fops);
	debugfs_create_file("enable", 0644, pmu_dir, NULL, &enable_fops);
	debugfs_create_file("pmuevtcfg", 0644, pmu_dir, NULL, &pmuevtcfg_fops);
	debugfs_create_file("pcycle_cpu", 0444, pmu_dir, (void *)0, &pcycle_fops);
	debugfs_create_file("pcycle_hw", 0444, pmu_dir, (void *)1, &pcycle_fops);

	debugfs_create_file("pmucnt0", 0444, pmu_dir, (void *)0, &pmucnt_fops);
	debugfs_create_file("pmucnt1", 0444, pmu_dir, (void *)1, &pmucnt_fops);
	debugfs_create_file("pmucnt2", 0444, pmu_dir, (void *)2, &pmucnt_fops);
	debugfs_create_file("pmucnt3", 0444, pmu_dir, (void *)3, &pmucnt_fops);

	debugfs_create_file("pmuregs", 0444, pmu_dir, NULL, &pmuregs_fops);

	rev.raw = __vmgetinfo(vm_info_rev);
	if (rev.raw >= 0x65)
		pmu_reg_write(10, 0x0);
	else
		pmu_reg_write(10, 0xf);

	read_pmu_regs(&regs);

	return 0;
}

static void __exit debugpmu_exit(void)
{
	debugfs_remove_recursive(pmu_dir);
}

module_init(debugpmu_init);
module_exit(debugpmu_exit);

MODULE_DESCRIPTION("Hexagon PMU debugfs control interface");
MODULE_LICENSE("GPL");
