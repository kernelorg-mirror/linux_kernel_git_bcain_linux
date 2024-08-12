#include <linux/kgdb.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <asm/hexagon_vm.h>

/*  Might need SMP locking or something  */

#define BUFLEN 1024

DECLARE_PER_CPU(u32, vpid);

struct	dentry  *dir, *file[10];
bool	debug = false;

struct pmu_regs {
	unsigned long pmucnt[4];
	unsigned long tlbmissx[2];
	unsigned long tlbmissrw[2];
	unsigned long stlbmiss[2];
	unsigned long pmuevtcfg;
	unsigned long pmucfg;
};

struct pmu_regs regs;

int filecnt;

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

//  Todo:  probably only use this to initialize
void read_pmu_regs(struct pmu_regs* regs)
{
	regs->pmucnt[0] = pmu_reg_read(0);
	regs->pmucnt[1] = pmu_reg_read(1);
	regs->pmucnt[2] = pmu_reg_read(2);
	regs->pmucnt[3] = pmu_reg_read(3);

	regs->tlbmissx[0] = pmu_reg_read(-2);
	regs->tlbmissx[1] = pmu_reg_read(-3);
	regs->tlbmissrw[0] = pmu_reg_read(-4);
	regs->tlbmissrw[1] = pmu_reg_read(-5);
	regs->stlbmiss[0] = pmu_reg_read(-6);
	regs->stlbmiss[1] = pmu_reg_read(-7);
	regs->pmuevtcfg = pmu_reg_read(8);
	regs->pmucfg = pmu_reg_read(10);

	//  Replace with a dump function
	if(debug) {
		printk(	"pmucnt[0] = %u\n"
			"pmucnt[1] = %u\n"
			"pmucnt[2] = %u\n"
			"pmucnt[3] = %u\n"
			"pmucnt[-2] = %u\n"
			"pmucnt[-3] = %u\n"
			"pmucnt[-4] = %u\n"
			"pmucnt[-5] = %u\n"
			"pmucnt[-6] = %u\n"
			"pmucnt[-7] = %u\n"
			"pmuevtcfg = 0x%x\n"
			"pmucfg = 0x%08x\n",
			regs->pmucnt[0], regs->pmucnt[1],
			regs->pmucnt[2], regs->pmucnt[3],
			regs->tlbmissx[0], regs->tlbmissx[1],
			regs->tlbmissrw[0], regs->tlbmissrw[1],
			regs->stlbmiss[0], regs->stlbmiss[1],
			regs->pmuevtcfg, regs->pmucfg
		);
	}
}

//  Only does the counters, not the event or config
void write_pmu_regs(struct pmu_regs* regs)
{
	pmu_reg_write(0, regs->pmucnt[0]);
	pmu_reg_write(1, regs->pmucnt[1]);
	pmu_reg_write(2, regs->pmucnt[2]);
	pmu_reg_write(3, regs->pmucnt[3]);

	pmu_reg_write(-2, regs->tlbmissx[0]);
	pmu_reg_write(-3, regs->tlbmissx[1]);
	pmu_reg_write(-4, regs->tlbmissrw[0]);
	pmu_reg_write(-5, regs->tlbmissrw[1]);
	pmu_reg_write(-6, regs->stlbmiss[0]);
	pmu_reg_write(-7, regs->stlbmiss[1]);
}

//  Reset clears the registers and restores the previous pmucfg, which might have been enabled or disabled
void reset_pmu(void)
{
	//  Disable the PMU
	pmu_reg_write(8, 0x0);
	//  Clear counters
	memset(&regs, 0, sizeof(regs) - 2*sizeof(unsigned long));
	//  Write only the counters
	write_pmu_regs(&regs);
	//  re-set the event
	pmu_reg_write(8, regs.pmuevtcfg);
}

static ssize_t enable_read_file(struct file *file, char __user *userbuf,
				size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = 0;
	int len;

	regs.pmucfg = pmu_reg_read(10);
	len = snprintf(mybuf, BUFLEN, "%u\n", regs.pmucfg);
	ret = simple_read_from_buffer(userbuf, count, ppos, mybuf, len);

	kfree(mybuf);
	return ret;
}

//  REMOVE
static ssize_t pmuregs_read_file(struct file *file, char __user *userbuf,
                                size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = 0;
	int len;

	read_pmu_regs(&regs);

	len = snprintf(mybuf, BUFLEN,
					 "PMU counters:\n\t%u\n\t%u\n\t%u\n\t%u\n\nTLB missx:\n\t%u\n\t%u\n\nTLB missrw:\n\t%u\n\t%u\n\nSTLB miss:\n\t%u\n\t%u\n\nPMU event config:\n\t0x%08x\n\n",
					 regs.pmucnt[0], regs.pmucnt[1],regs.pmucnt[2], regs.pmucnt[3],
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
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = 0;
	u64 cpu_cycles;
	u64 hw_cycles;
	int reg = file->private_data;
	int len;

	//  Uh, already had a trap for this.  The HW_CYCLES is kind of an...  unofficial feature?
	asm volatile (" trap1(#" VMTRAP_GET_PCYCLES ") \n"
								" %0 = r1:0 \n"
								" %1 = r3:2 \n"
								: "=r" (cpu_cycles), "=r" (hw_cycles)
								:
								: "r0", "r1", "r2", "r3");

	len = snprintf(mybuf, BUFLEN, "%llu\n", reg ? hw_cycles : cpu_cycles);
	ret = simple_read_from_buffer(userbuf, count, ppos, mybuf, len);
	kfree(mybuf);
	return ret;
}

//  The whole tmask thing is unreliable, so we just hit PMUEVTCFG instead.
//  This just becomes our implict reset.

static ssize_t enable_write_file(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = -EINVAL;

	if(count >= BUFLEN) {
		goto out_err;
	}

	if (copy_from_user(mybuf, buf, count)) {
		ret = -EFAULT;
		goto out_err;
	}

	mybuf[count] = '\0';
	if (kstrtoul(mybuf, 0, &regs.pmucfg)) {
		ret = -EINVAL;
		goto out_err;
	}

	if ((regs.pmucfg & 0xf) == 0) {
		pmu_reg_write(8, 0);
	}  //  this means disable
	else {
		reset_pmu();  // what a mess
	}  //  this means enable (so reset)
	ret = count;

out_err:
	kfree(mybuf);
	return ret;
}


static ssize_t debug_write_file(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = -EINVAL;

	if(count > BUFLEN) {
		goto out_err;
	}

	if (copy_from_user(mybuf, buf, count)) {
		ret = -EFAULT;
		goto out_err;
	}
	debug = ((mybuf[0] == '0') ?  0 : 1);
	ret = count;

out_err:
	kfree(mybuf);
	return ret;
}

static ssize_t debug_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = 0;
	int len;

	len = snprintf(mybuf, BUFLEN, "%u\n", debug);
	ret = simple_read_from_buffer(userbuf, count, ppos, mybuf, len);
	kfree(mybuf);
	return ret;
}

static ssize_t pmuevtcfg_read_file(struct file *file, char __user *userbuf,
				   size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = 0;
	int len;

	regs.pmuevtcfg = pmu_reg_read(8);
	len = snprintf(mybuf, BUFLEN, "0x%x\n", regs.pmuevtcfg);
	ret = simple_read_from_buffer(userbuf, count, ppos, mybuf, len);
	kfree(mybuf);
	return ret;
}

//  This is designed to implicitly "reset" the PMU
static ssize_t pmuevtcfg_write_file(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = -EINVAL;

	if(count >= BUFLEN) {
		goto out_err;
	}

	ret = -EFAULT;
	if (copy_from_user(mybuf, buf, count)) {
		goto out_err;
	}
	mybuf[count] = '\0';
	if (kstrtoul(mybuf, 0, &regs.pmuevtcfg)) {
		goto out_err;
	}

	pmu_reg_write(8, regs.pmuevtcfg);
	ret = count;

out_err:
	kfree(mybuf);
	return ret;
}

static ssize_t pmucnt_read_file(struct file *file, char __user *userbuf,
				   size_t count, loff_t *ppos)
{
	char *mybuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret = 0;
	int reg = file->private_data;
	int len;

	len = snprintf(mybuf, BUFLEN, "%lu\n", pmu_reg_read(reg));
	ret = simple_read_from_buffer(userbuf, count, ppos, mybuf, len);
	kfree(mybuf);
	return ret;
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

//  REMOVE
static const struct file_operations pmuregs_fops = {
	.read = pmuregs_read_file,
};

static int __init debugpmu_module_init(void)
{
	dir = debugfs_create_dir("pmu", NULL);

	file[filecnt++] = debugfs_create_file("debug", 0644, dir, NULL, &debug_fops);
	file[filecnt++] = debugfs_create_file("enable", 0666, dir, NULL, &enable_fops);
	file[filecnt++] = debugfs_create_file("pmuevtcfg", 0666, dir, NULL, &pmuevtcfg_fops);
	file[filecnt++] = debugfs_create_file("pcycle_cpu", 0644, dir, 0, &pcycle_fops);
	file[filecnt++] = debugfs_create_file("pcycle_hw", 0644, dir, 1, &pcycle_fops);

	file[filecnt++] = debugfs_create_file("pmucnt0", 0644, dir, 0, &pmucnt_fops);
	file[filecnt++] = debugfs_create_file("pmucnt1", 0644, dir, 1, &pmucnt_fops);
	file[filecnt++] = debugfs_create_file("pmucnt2", 0644, dir, 2, &pmucnt_fops);
	file[filecnt++] = debugfs_create_file("pmucnt3", 0644, dir, 3, &pmucnt_fops);

	//  todo:  add TLB info
	//  todo:  remove
	//  todo:  add pmucfg as its own register because we now have extended events.
	file[filecnt++] = debugfs_create_file("pmuregs", 0600, dir, NULL, &pmuregs_fops);

#if CONFIG_HEXAGON_ARCH_VERSION >= 65
        pmu_reg_write(10, 0x0);  // "legacy mode" event configuration for v65+
#else
        pmu_reg_write(10, 0xf);  // Thread mask on v60
#endif
	read_pmu_regs(&regs);  // sync up initial values

	return 0;
}

static void __exit debugpmu_module_exit(void)
{
	while (filecnt) {
		debugfs_remove(file[--filecnt]);
	}
	debugfs_remove(dir);
}

module_init(debugpmu_module_init);
module_exit(debugpmu_module_exit);
MODULE_LICENSE("GPL");
