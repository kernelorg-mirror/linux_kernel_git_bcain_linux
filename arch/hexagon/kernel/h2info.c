// SPDX-License-Identifier: GPL-2.0
/*
 * What the hypervisor tells a Hexagon guest about itself and the core, and the
 * state of the performance monitoring unit, for debugging.
 *
 *   /sys/kernel/hexagon/info/<name>   one value of the hypervisor's info call
 *   /sys/kernel/hexagon/{arch,vm_version,hthreads,timer_freq_hz}
 *   /sys/kernel/hexagon/pmu/<reg>     the PMU's configuration registers
 *   /sys/kernel/hexagon/cpus/cpuN     the VCPU of each CPU
 *   debugfs hexagon/{info,pmu,tlb,cpus}   all of them, with the counters
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/percpu.h>
#include <linux/seq_file.h>
#include <linux/sysfs.h>
#include <asm/hexagon_vm.h>

DECLARE_PER_CPU(u32, vpid);

/* The codes of the hypervisor's info call, in the order of its enum. */
static const char * const info_names[] = {
	"build_id", "boot_flags", "stlb", "syscfg", "livelock", "rev",
	"ssbase", "tlb_free", "tlb_size", "physaddr", "tcm_base", "l2mem_size",
	"tcm_size", "h2k_pgsize", "h2k_npages", "l2vic_base", "timer_base",
	"timer_int", "error", "hthreads", "l2tag_size", "l2cfg_base",
	"clade_base", "cfgbase", "hvx_vlength", "coproc_contexts", "hvx_switch",
	"vtcm_base", "vtcm_size", "ecc_base", "l2_line_sz", "audio_ext",
	"vtcm_bank_width", "l1d_size", "max_cluster_coproc", "hmx_instances",
	"corecfg_base", "hlx_contexts", "unit_start",
	/* 39 is the unit entry: it indexes with arguments that are not checked */
	NULL,
	"core_id", "core_count", "shift", "tcm_offset", "noc_mbase",
	"noc_sbase", "boot_r0", "boot_r1",
};

/* The PMU registers of the hypervisor's PMU call. */
#define PMUCTRL_GETREG		2
static const struct {
	const char *name;
	long reg;
} pmu_regs[] = {
	{ "evtcfg", 8 }, { "evtcfg1", 9 }, { "cfg", 10 },
	{ "stid0", 11 }, { "stid1", 12 },
};
static const struct {
	const char *name;
	long reg;
} tlb_counters[] = {
	{ "tlb_miss_exec_lo", -2 }, { "tlb_miss_exec_hi", -3 },
	{ "tlb_miss_rw_lo", -4 }, { "tlb_miss_rw_hi", -5 },
	{ "stlb_miss_lo", -6 }, { "stlb_miss_hi", -7 },
};

static u32 pmu_get(long reg)
{
	return __vmpmucfg(PMUCTRL_GETREG, 0, reg, 0);
}

/* The counters are global registers, which a guest can read. */
static u32 pmu_counter(int idx)
{
	u32 val;

	switch (idx) {
	case 0:
		asm volatile("%0 = g26" : "=r"(val));
		break;
	case 1:
		asm volatile("%0 = g27" : "=r"(val));
		break;
	case 2:
		asm volatile("%0 = g28" : "=r"(val));
		break;
	default:
		asm volatile("%0 = g29" : "=r"(val));
		break;
	}

	return val;
}

static unsigned long h2_info(unsigned int code)
{
	return __vmgetinfo(code, 0, 0);
}

/* sysfs */

struct h2_attr {
	struct kobj_attribute attr;
	unsigned int code;
};

static ssize_t info_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	struct h2_attr *a = container_of(attr, struct h2_attr, attr);

	return sysfs_emit(buf, "%#lx\n", h2_info(a->code));
}

static ssize_t arch_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "v%lx\n", h2_info(5) & 0xff);
}

static ssize_t vm_version_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%#lx\n", (unsigned long)__vmversion());
}

static ssize_t hthreads_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	unsigned long mask = h2_info(19);

	return sysfs_emit(buf, "%d (mask %#lx)\n", hweight32(mask), mask);
}

static ssize_t timer_freq_hz_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%llu\n", __vmtimerop(getfreq, 0, 0));
}

static ssize_t pmu_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	struct h2_attr *a = container_of(attr, struct h2_attr, attr);

	return sysfs_emit(buf, "%#x\n", pmu_get(pmu_regs[a->code].reg));
}

static ssize_t cpu_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	struct h2_attr *a = container_of(attr, struct h2_attr, attr);

	return sysfs_emit(buf, "vpid %#x, %s\n", per_cpu(vpid, a->code),
			  cpu_online(a->code) ? "online" : "offline");
}

static struct kobj_attribute arch_attr = __ATTR_RO(arch);
static struct kobj_attribute vm_version_attr = __ATTR_RO(vm_version);
static struct kobj_attribute hthreads_attr = __ATTR_RO(hthreads);
static struct kobj_attribute timer_freq_hz_attr = __ATTR_RO(timer_freq_hz);

static struct attribute *top_attrs[] = {
	&arch_attr.attr,
	&vm_version_attr.attr,
	&hthreads_attr.attr,
	&timer_freq_hz_attr.attr,
	NULL,
};

static const struct attribute_group top_group = {
	.attrs = top_attrs,
};

/* Make a directory of read-only files, one show function for all of them. */
static int add_dir(struct kobject *parent, const char *dir,
		   struct h2_attr *attrs, const char * const *names, int n,
		   ssize_t (*show)(struct kobject *, struct kobj_attribute *,
				   char *))
{
	struct attribute **list;
	struct attribute_group *group;
	struct kobject *kobj;
	int i, j = 0;

	kobj = kobject_create_and_add(dir, parent);
	if (!kobj)
		return -ENOMEM;

	list = kcalloc(n + 1, sizeof(*list), GFP_KERNEL);
	group = kzalloc_obj(*group);
	if (!list || !group)
		return -ENOMEM;

	for (i = 0; i < n; i++) {
		if (!names[i])
			continue;
		sysfs_attr_init(&attrs[i].attr.attr);
		attrs[i].attr.attr.name = names[i];
		attrs[i].attr.attr.mode = 0444;
		attrs[i].attr.show = show;
		attrs[i].code = i;
		list[j++] = &attrs[i].attr.attr;
	}
	group->attrs = list;

	return sysfs_create_group(kobj, group);
}

/* debugfs */

static int info_debugfs_show(struct seq_file *m, void *unused)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(info_names); i++)
		if (info_names[i])
			seq_printf(m, "%2d %-20s = %#010lx\n", i, info_names[i],
				   h2_info(i));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(info_debugfs);

static int pmu_debugfs_show(struct seq_file *m, void *unused)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pmu_regs); i++)
		seq_printf(m, "%-8s = %#010x\n", pmu_regs[i].name,
			   pmu_get(pmu_regs[i].reg));

	for (i = 0; i < 4; i++)
		seq_printf(m, "pmucnt%d  = %u\n", i, pmu_counter(i));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(pmu_debugfs);

static int tlb_debugfs_show(struct seq_file *m, void *unused)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(tlb_counters); i++) {
		u32 v = pmu_get(tlb_counters[i].reg);

		if (v == (u32)-1)
			seq_printf(m, "%-18s n/a (not counted by this hypervisor)\n",
				   tlb_counters[i].name);
		else
			seq_printf(m, "%-18s %u\n", tlb_counters[i].name, v);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(tlb_debugfs);

static int cpus_debugfs_show(struct seq_file *m, void *unused)
{
	int cpu;

	for_each_possible_cpu(cpu)
		seq_printf(m, "cpu%-2d vpid %#x %s%s\n", cpu, per_cpu(vpid, cpu),
			   cpu_online(cpu) ? "online" : "offline",
			   cpu_present(cpu) ? "" : " (not present)");

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cpus_debugfs);

static struct h2_attr info_attrs[ARRAY_SIZE(info_names)];
static struct h2_attr pmu_attrs[ARRAY_SIZE(pmu_regs)];
static struct h2_attr cpu_attrs[NR_CPUS];

static int __init h2info_init(void)
{
	const char *pmu_names[ARRAY_SIZE(pmu_regs)];
	const char *cpu_names[NR_CPUS];
	static char cpu_name_buf[NR_CPUS][8];
	struct kobject *kobj;
	struct dentry *dir;
	int i;

	kobj = kobject_create_and_add("hexagon", kernel_kobj);
	if (!kobj)
		return -ENOMEM;

	sysfs_create_group(kobj, &top_group);
	add_dir(kobj, "info", info_attrs, info_names, ARRAY_SIZE(info_names),
		info_show);

	for (i = 0; i < ARRAY_SIZE(pmu_regs); i++)
		pmu_names[i] = pmu_regs[i].name;
	add_dir(kobj, "pmu", pmu_attrs, pmu_names, ARRAY_SIZE(pmu_regs),
		pmu_show);

	for (i = 0; i < nr_cpu_ids; i++) {
		snprintf(cpu_name_buf[i], sizeof(cpu_name_buf[i]), "cpu%d", i);
		cpu_names[i] = cpu_name_buf[i];
	}
	add_dir(kobj, "cpus", cpu_attrs, cpu_names, nr_cpu_ids, cpu_show);

	dir = debugfs_create_dir("hexagon", NULL);
	debugfs_create_file("info", 0444, dir, NULL, &info_debugfs_fops);
	debugfs_create_file("pmu", 0444, dir, NULL, &pmu_debugfs_fops);
	debugfs_create_file("tlb", 0444, dir, NULL, &tlb_debugfs_fops);
	debugfs_create_file("cpus", 0444, dir, NULL, &cpus_debugfs_fops);

	return 0;
}
late_initcall(h2info_init);
