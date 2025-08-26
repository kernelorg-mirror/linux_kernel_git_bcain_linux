#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/delay.h>

struct dentry *clock_lval_dentry;
struct dentry *clock_dump_dentry;

void *hexagon_pub_base;
u32 clock_lval;

//  The original boot loops_per_jiffy is calculated while running at 19.2MHz.
unsigned long tcxo_lpj;


struct hexagon_ss_register {
	const char *name;
	u32 offset;
};

enum reg_index {
	PLL_MODE,
	PLL_L_VAL,
	PLL_CAL_L_VAL,
	PLL_USER_CTL,
	PLL_USER_CTL_U,
	PLL_CONFIG_CTL,
	PLL_CONFIG_CTL_U,
	PLL_TEST_CTL,
	PLL_TEST_CTL_U,
	PLL_STATUS,
	PLL_FREQ_CTL,
	PLL_OPMODE,
	PLL_STATE,
	PLL_DROOP,
	PLL_SPARE,
	PLL_SSC_DELTA_ALPHA,
	PLL_SSC_UPDATE_RATE,
	PLL_SSC_NUM_STEPS,
	PLL_RCG_UPDATE_STATUS,
	PLL_RCG_UPDATE_CFG,
	PLL_RCG_UPDATE_DLYCTL,
	PLL_RCG_UPDATE_CMD,
	CORE_CFG_RCGR,
	CORE_CMD_RCGR,
	_REG_COUNT
};

struct hexagon_ss_register registers[] = {
	{ .name = "PLL_MODE",			.offset = 0x200 },
	{ .name = "PLL_L_VAL",			.offset = 0x204 },
	{ .name = "PLL_CAL_L_VAL",		.offset = 0x208 },
	{ .name = "PLL_USER_CTL",		.offset = 0x20c },
	{ .name = "PLL_USER_CTL_U",		.offset = 0x210 },
	{ .name = "PLL_CONFIG_CTL",		.offset = 0x214 },
	{ .name = "PLL_CONFIG_CTL_U",		.offset = 0x218 },
	{ .name = "PLL_TEST_CTL",		.offset = 0x21c },
	{ .name = "PLL_TEST_CTL_U",		.offset = 0x220 },
	{ .name = "PLL_STATUS",			.offset = 0x224 },
	{ .name = "PLL_FREQ_CTL",		.offset = 0x228 },
	{ .name = "PLL_OPMODE",			.offset = 0x238 },
	{ .name = "PLL_STATE",			.offset = 0x23C },
	{ .name = "PLL_DROOP",			.offset = 0x234 },
	{ .name = "PLL_SPARE",			.offset = 0x23c },
	{ .name = "PLL_SSC_DELTA_ALPHA",	.offset = 0x240 },
	{ .name = "PLL_SSC_UPDATE_RATE",	.offset = 0x244 },
	{ .name = "PLL_SSC_NUM_STEPS",		.offset = 0x248 },
	{ .name = "PLL_RCG_UPDATE_STATUS",	.offset = 0x250 },
	{ .name = "PLL_RCG_UPDATE_CFG",		.offset = 0x254 },
	{ .name = "PLL_RCG_UPDATE_DLYCTL",	.offset = 0x258 },
	{ .name = "PLL_RCG_UPDATE_CMD",		.offset = 0x25c },
	{ .name = "CORE_CFG_RCGR",		.offset = 0x02c },
	{ .name = "CORE_CMD_RCGR",		.offset = 0x028 },
};


#define ADDR(REG) (hexagon_pub_base + registers[REG].offset)
#define TIMEOUT 10

static void init_fabia_pll(u32 Lval)
{
/* 
 * XXX_SM: for real hardware this is a VITAL step as it allows the dsp to
 * full speed.  This is not needed on QQVP.
 * The original PLL is from Hana: TURING_QDSP6SS_PLL_MODE | 0x8300200
 * Lemans: TURINGNSP_0_TURING_CORE0_CORE0_CM_PLL_LUCID_EVO | 0x26000000
 * https://ipcatalog.qualcomm.com/swi/chip/434/version/11766/module/24917394#TURINGNSP_0_TURING_CORE0_PLL_MODE
 */
	int i;
	u32 val;

	val = readl(ADDR(PLL_MODE));
	if ((val & (1 << 2)) != 0) {
		printk("yo dawg, PLL ain't in reset so I don't know what u thinkin\n");
		return;
	}

	//  To-do:  Other sanity check:  make sure the PLL really is in the OFF state, or maybe do a check-and-exit?
	//  To-do:  disable or check FSM mode?
#define HAL_CLK_UPDATED_CONFIG_CTL_VAL    0x20485699
#define HAL_CLK_UPDATED_CONFIG_CTL_U_VAL  0x00002067
#define HAL_CLK_UPDATED_TEST_CTL_VAL      0x40000000
#define HAL_CLK_UPDATED_TEST_CTL_U_VAL    0x0
#define HAL_CLK_UPDATED_USER_CTL_U_VAL    0x4804

	writel(HAL_CLK_UPDATED_CONFIG_CTL_VAL, ADDR(PLL_CONFIG_CTL));
	writel(HAL_CLK_UPDATED_CONFIG_CTL_U_VAL, ADDR(PLL_CONFIG_CTL_U));
	writel(HAL_CLK_UPDATED_TEST_CTL_VAL, ADDR(PLL_TEST_CTL));
	writel(HAL_CLK_UPDATED_TEST_CTL_U_VAL, ADDR(PLL_TEST_CTL_U));
	writel(HAL_CLK_UPDATED_USER_CTL_U_VAL, ADDR(PLL_USER_CTL_U));

	writel(Lval, ADDR(PLL_L_VAL));
	writel(Lval, ADDR(PLL_CAL_L_VAL));

	val = readl(ADDR(PLL_MODE));
	val &= ~1;  //  clear the outctrl bit; turns off outputs
	writel(val, ADDR(PLL_MODE));

	//  set the opmode to standby
	writel(0, ADDR(PLL_OPMODE));

	//  resetn = 1
	val = readl(ADDR(PLL_MODE));
	val |= 1 << 2;  // "NO_RESET"
	writel(val, ADDR(PLL_MODE));

	//  "Command the PLL to begin running"
	writel(1, ADDR(PLL_OPMODE));  //  "RUN!!!!!!"

	i=0;
	while (i < TIMEOUT) {
		val = readl(ADDR(PLL_MODE));
		if (val >> 31) {
			break;
		}
		//msleep(1);
		i++;
	}
	if (i >= TIMEOUT) {
		printk("Timeout waiting for PLL lock detect; aborting\n");
		printk("***: Your target might be slow ***\n");
		return;
	}

	val = readl(ADDR(PLL_USER_CTL));
	val |= 1;  //  enable main output of PLL
	writel(val, ADDR(PLL_USER_CTL));

	val = readl(ADDR(PLL_MODE));
	val |= 1;  //  globally enable the outputs of the PLL
	writel(val, ADDR(PLL_MODE));

	//  Switch Hexagon over to use the PLL output
	writel(0x201, ADDR(CORE_CFG_RCGR));
	writel(0x1, ADDR(CORE_CMD_RCGR));

	i=0;
	while (i < TIMEOUT) {
		val = readl(ADDR(CORE_CMD_RCGR));
		if ((val & 1) == 0) {
			break;
		}
		//msleep(10);  // boy this cmd takes a long time
		i++;
	}
	if (i >= TIMEOUT) {
		printk("Timeout waiting for RCG CMD\n");
		return;
	}

	loops_per_jiffy = tcxo_lpj * Lval;
}

static void change_pll(u32 lval)
{
	u32 val;
	int i;

	//  Sanity check:  PLL_USER_CTL_U[10] must be zero; latches must not be bypassed
	if (readl(ADDR(PLL_USER_CTL_U)) & (1 << 10)) {
		printk("Latches are not bypassed\n");
		return;
	}

	//  Sanity check:  PLL_OPMODE[2:0] == 1
	if (readl(ADDR(PLL_OPMODE)) != 1) {
		printk("PLL not running\n");
		return;
	}

	//  Sanity check:  disable state write or something
	if (readl(ADDR(PLL_USER_CTL_U)) & (1 << 4)) {
		printk("State write not disabled\n");
		return;
	}

	//  Set the Lval

	writel(lval, ADDR(PLL_L_VAL));

	//  Latch input into PLL

	val = readl(ADDR(PLL_MODE));
	val |= (1<<22);  //  set update bit?
	writel(val, ADDR(PLL_MODE));

	//  Wait for pll_ack_latch
	i = 0;
	while (i < TIMEOUT) {
		val = readl(ADDR(PLL_MODE));
		if ((val & (1 << 29)) == 1) {
			break;
		}
		i++;
	}
	if (i >= TIMEOUT) {
		printk("Timeout waiting for latch ack\n");
	}

	//  Return latch bit to 0?
	val = readl(ADDR(PLL_MODE));
	val &= ~(1<<22);  //  clear update bit?
	writel(val, ADDR(PLL_MODE));

	loops_per_jiffy = tcxo_lpj * lval;
}

static ssize_t clock_lval_read(struct file *filp, char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	char buf[24];
	int r;

	clock_lval = readl(ADDR(PLL_L_VAL));
	r = snprintf(buf, sizeof(buf), "%d\n", clock_lval);
	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static ssize_t clock_lval_write(struct file *filp,
	const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	ssize_t ret = -EFAULT;
	size_t size;
	char buf[24];

	size = min(sizeof(buf) - 1, cnt);
	if (copy_from_user(buf, ubuf, size)) {
		printk("copy error\n");
		goto out;
	}

	buf[size] = '\0';

	ret = kstrtou32(buf, 0, &clock_lval);
	if (ret == 0) {
		ret = cnt;
	}
	else {
		printk("%s conversion error %d\n", buf, -ret);
	}

	change_pll(clock_lval);
out:
	return ret;
};

#define BUFLEN 1024

static ssize_t clock_dump_read(struct file *filp, char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	char *kbuf = kzalloc(BUFLEN, GFP_KERNEL);
	ssize_t ret;
	size_t total = 0;
	int i;

	for (i=0; i < _REG_COUNT; i++) {
		total += snprintf(kbuf+total, BUFLEN-total, "%s = 0x%08x\n", registers[i].name, readl(ADDR(i)));
	}
	ret = simple_read_from_buffer(ubuf, cnt, ppos, kbuf, total);
	kfree(kbuf);
	return ret;
}

//  sometimes use the write to this to trigger some debug action
static ssize_t clock_dump_write(struct file *filp, const char __user *ubuf,
	size_t cnt, loff_t *ppos)
{
	return cnt;
}

static const struct file_operations lval_fops = {
	.open = simple_open,
	.read = clock_lval_read,
	.write = clock_lval_write,
};

static const struct file_operations dump_fops = {
	.open = simple_open,
	.read = clock_dump_read,
	.write = clock_dump_write,
};

static int __init v66_debug_init(void)
{
	tcxo_lpj = loops_per_jiffy;

	hexagon_pub_base = ioremap(0x26300000, PAGE_SIZE);

	if (!hexagon_pub_base) {
		printk("ioremap error\n");
		goto out;
	}

	clock_lval_dentry = debugfs_create_file("clock_lval", 0755, NULL, NULL, &lval_fops);
	clock_dump_dentry = debugfs_create_file("clock_dump", 0755, NULL, NULL, &dump_fops);

	clock_lval = 62;  //  Todo:  May need to set some voltage rail
	init_fabia_pll(clock_lval);
out:

	return 0;
}

static void __exit v66_debug_exit(void)
{
	debugfs_remove(clock_lval_dentry);
	debugfs_remove(clock_dump_dentry);

	iounmap(hexagon_pub_base);

}

module_init(v66_debug_init);
module_exit(v66_debug_exit);
