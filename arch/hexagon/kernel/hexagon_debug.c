#include <linux/module.h>
#include <linux/debugfs.h>

/*  Skeleton for stuffing stuff in debugfs  */
u32 sample_u32;
u32 kernel_strace;
u32 sig_debug;
u32 zebu_pmu_dump;
u32 memory_debug;


//extern u32 xfrm_drop_packets;
//extern atomic_t xfrm_packets_dropped;
//struct dentry * xfrm_drop_packets_dentry;
//struct dentry * xfrm_packets_dropped_dentry;

#ifdef CONFIG_HEXAGON_TMR_LAT
extern unsigned long long lat_min;
extern unsigned long long lat_max;
extern unsigned long long lat_avg;
extern unsigned long lat_tracking_enable;
#endif

static int zebu_dump_set(void *data, u64 val)
{
	asm volatile("r0=%0;"
		"trap0(#0);"
		:
		: "r" ((unsigned long) val)
		: "r0", "r1", "r2", "r3", "r4", "r5"
	);

        return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(zebu_pmu_dump_fops, NULL, zebu_dump_set, "%llu\n");


static int __init hexagon_debugfs_init(void)
{
	printk("%s called\n",__FUNCTION__);
	debugfs_create_u32("sample_u32", 0700, NULL, &sample_u32);
	debugfs_create_u32("kernel_strace", 0700, NULL, &kernel_strace);
	/*  should really not be w by everyone, but...  should be only internal anyways.  */
	debugfs_create_u32("sig_debug", 0666, NULL, &sig_debug);
	debugfs_create_file("zebu_pmu_dump", 0700, NULL, &zebu_pmu_dump, &zebu_pmu_dump_fops);
	debugfs_create_u32("memory_debug", 0700, NULL, &memory_debug);


#ifdef CONFIG_HEXAGON_TMR_LAT
	debugfs_create_u64("lat_min", 0700, NULL, &lat_min);
	debugfs_create_u64("lat_max", 0700, NULL, &lat_max);
	debugfs_create_u64("lat_avg", 0700, NULL, &lat_avg);
	debugfs_create_u32("lat_tracking_enable", 0700, NULL, &lat_tracking_enable);
#endif
	return 0;
}

static void __exit hexagon_debugfs_exit(void)
{
	/* debugfs entries cleaned up automatically on module unload */
}

module_init(hexagon_debugfs_init);
module_exit(hexagon_debugfs_exit);

