#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/delay.h>

struct dentry *clock_lval_dentry;
struct dentry *clock_debug_dentry;

void *lpass_pub_base;
u32 clock_lval;

#define QDSP6SS_GFMUX_CTL	0x020
#define QDSP6SS_GFMUX_STATUS	0x024
#define QDSP6SS_CORE_CMD_RCGR	0x028
#define QDSP6SS_CORE_CFG_RCGR	0x02c
#define QDSP6SS_PLL_MODE	0x200
#define QDSP6SS_PLL_L_VAL	0x204
#define QDSP6SS_PLL_USER_CTL	0x210
#define QDSP6SS_PLL_USER_CTL_U	0x214
#define QDSP6SS_PLL_CONFIG_CTL	0x218

ssize_t clock_lval_read(struct file *filp, char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	char buf[24];
	int r;

	clock_lval = readl(lpass_pub_base + QDSP6SS_PLL_L_VAL);
	r = snprintf(buf, sizeof(buf), "%d\n", clock_lval);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

//  Per spec Lval should produce a speed between 500 and 1GHz.  Don't look at me.

void init_spark_pll(u32 Lval)
{
	u32 val;

	val = Lval;  //  (Ref/R) * [L+ALPHA]/P
	writel(val, lpass_pub_base + QDSP6SS_PLL_L_VAL);

	//  take the early out and go gfmux B
	val = (0x2 << 20);	//  VCO mode 2
	val |= (1 << 8);	//  post-div 1
	val |= (1 << 3);	//  pllout_lv_early enable the early-out
	val |= (0 << 0);	//  pllout_lv_main "clear only when PLL-GFMUX path is used and RCG is bypassed"
	writel(val, lpass_pub_base + QDSP6SS_PLL_USER_CTL);

	val = (1 << 2);		//  enable lock detection
	writel(val, lpass_pub_base + QDSP6SS_PLL_USER_CTL_U);

	val = 0x4001051b;	//  sprinkle some magic dust while we're at it
	writel(val, lpass_pub_base + QDSP6SS_PLL_CONFIG_CTL);

	// take PLL out of bypass mode, wait for 5us
	val = 0x2;  //  PLL_HW_UPDATE_LOGIC_BYPASS is not set
	writel(val, lpass_pub_base + QDSP6SS_PLL_MODE);
	udelay(5);  //  vm gettime based; should be safe

	// take PLL out of reset mode
	val = 0x6;
	writel(val, lpass_pub_base + QDSP6SS_PLL_MODE);

	// Wait until PLL locked
	// poll QDSP6SS_PLL_MODE until PLL_LOCK_DET[31] bit is set (1)

	val = readl(lpass_pub_base + QDSP6SS_PLL_MODE);
	while ((val & (1<<31)) == 0)
		val = readl(lpass_pub_base + QDSP6SS_PLL_MODE);

	// STEP 5: Enable the PLL output
	val = 0x7;
	writel(val, lpass_pub_base + QDSP6SS_PLL_MODE);

}

void change_spark_pll(u32 lval)
{
	u32 val;

	//  Set new lval
	val = lval;  //  (Ref/R) * [L+ALPHA]/P
	writel(val, lpass_pub_base + QDSP6SS_PLL_L_VAL);


	//  Set PLL_UPDATE
	val = readl(lpass_pub_base + QDSP6SS_PLL_MODE);
	val |= (1 << 22);
	writel(val, lpass_pub_base + QDSP6SS_PLL_MODE);
	//  wait for ACK?

	val = readl(lpass_pub_base + QDSP6SS_PLL_MODE);
	while (val & (1 <<29))
		val = readl(lpass_pub_base + QDSP6SS_PLL_MODE);

	//  OK, when PLL_HW_UPDATE_LOGIC_BYPASS is zero, that means PLL_UPDATE is auto-cleared on the ACK.
}


//  source should be 0-3.  Really 0-1, but whatevs.
void switch_gfmux(unsigned long source)
{
	u32 val;

	val = readl(lpass_pub_base + QDSP6SS_GFMUX_CTL);
	val &= ~(0x3 << 2);
	val |= (source << 2);
	writel(val, lpass_pub_base + QDSP6SS_GFMUX_CTL);

	val = readl(lpass_pub_base + QDSP6SS_GFMUX_STATUS);
	while ((val & 1) != 0)
		val = readl(lpass_pub_base + QDSP6SS_GFMUX_STATUS);

//  uh...  not necessary?
#if 0
	// Configure RCG to select the PLL output and set divider accordingly
	val = 0x101;
	writel(val, lpass_pub_base + QDSP6SS_CORE_CFG_RCGR);
	val = 0x1;
	writel(val, lpass_pub_base + QDSP6SS_CORE_CMD_RCGR);

	// poll QDSP6SS_CORE_CMD_RCGR until update[0] is clear (0)
	val = readl(lpass_pub_base + QDSP6SS_CORE_CMD_RCGR);
	while ((val & 0) == 1) {
		val = readl(lpass_pub_base + QDSP6SS_CORE_CMD_RCGR);
	}
#endif
	//  TODO:  add calibrate_delay?

}

ssize_t clock_lval_write(struct file *filp,
	const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int i;
	ssize_t ret = -EFAULT;
	size_t size;
	char buf[24];

	size = min(sizeof(buf) - 1, cnt);
	if (copy_from_user(buf, ubuf, size)) {
		printk("copy error\n");
		goto out;
	}

	buf[size] = '\0';

	ret = kstrtoul(buf, 0, &clock_lval);
	if (ret == 0) {
		ret = cnt;
	}
	else {
		printk("%s conversion error %d\n", buf, -ret);
	}

	//switch_gfmux(0);
	//  This is supposed to be glitch free, dawg
	change_spark_pll(clock_lval);
	//switch_gfmux(1);

out:
	return ret;
};

#define BUFLEN 1024

ssize_t clock_debug_read(struct file *filp, char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	char *kbuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret;
	size_t len_avail;

	len_avail = snprintf(kbuf, BUFLEN,
			"GFMUX_CTL	= 0x%08x\n"
			"GFMUX_STATUS	= 0x%08x\n"
			"PLL_MODE	= 0x%08x\n"
			"PLL_L_VAL	= 0x%08x\n"
			"PLL_USER_CTL	= 0x%08x\n"
			"PLL_USER_CTL_U	= 0x%08x\n"
			"PLL_CONFIG_CTL	= 0x%08x\n",
			readl(lpass_pub_base + QDSP6SS_GFMUX_CTL),
			readl(lpass_pub_base + QDSP6SS_GFMUX_STATUS),
			readl(lpass_pub_base + QDSP6SS_PLL_MODE),
			readl(lpass_pub_base + QDSP6SS_PLL_L_VAL),
			readl(lpass_pub_base + QDSP6SS_PLL_USER_CTL),
			readl(lpass_pub_base + QDSP6SS_PLL_USER_CTL_U),
			readl(lpass_pub_base + QDSP6SS_PLL_CONFIG_CTL));

	ret = simple_read_from_buffer(ubuf, cnt, ppos, kbuf, len_avail);
	kfree(kbuf);
	return ret;
}

//  prlly should have just used the simple_attr stuff but whatevs

static const struct file_operations enable_fops = {
	.open = simple_open,
	.read = clock_lval_read,
	.write = clock_lval_write,
};

static const struct file_operations debug_fops = {
	.open = simple_open,
	.read = clock_debug_read,
};

static int __init v60_debug_init(void)
{
	printk("%s called\n", __FUNCTION__);

	lpass_pub_base = ioremap_nocache(0x09300000, PAGE_SIZE);

	if (!lpass_pub_base) {
		printk("ioremap error\n");
		goto out;
	}

	clock_lval = 43;  // 19.2MHz * 43
	init_spark_pll(clock_lval);
	switch_gfmux(1);

	clock_lval_dentry = debugfs_create_file("clock_lval", 0755, NULL, NULL, &enable_fops);
	clock_debug_dentry = debugfs_create_file("clock_debug", 0755, NULL, NULL, &debug_fops);

out:

	return 0;
}

static void __exit v60_debug_exit(void)
{
	printk("%s called\n", __FUNCTION__);
	debugfs_remove(clock_lval_dentry);

	iounmap(lpass_pub_base);

}

module_init(v60_debug_init);
module_exit(v60_debug_exit);


