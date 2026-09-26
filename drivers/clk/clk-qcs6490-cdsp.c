// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core clock of the QCS6490 CDSP (Kodiak's Turing QDSP6 subsystem).
 *
 * The core comes out of reset on the 19.2 MHz reference clock.  This driver
 * starts the subsystem's PLL and moves the core onto it, for a Linux that
 * runs on the DSP itself, and provides the resulting clock.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/sysfs.h>

#define PLL_MODE		0x00
#define  PLL_MODE_OUTCTRL	BIT(0)
#define  PLL_MODE_RESET_N	BIT(2)
#define  PLL_MODE_LOCK_DET	BIT(31)
#define PLL_L_VAL		0x04
#define PLL_CAL_L_VAL		0x08
#define PLL_USER_CTL		0x0c
#define PLL_USER_CTL_U		0x10
#define PLL_USER_CTL_U1		0x14
#define PLL_CONFIG_CTL		0x18
#define PLL_CONFIG_CTL_U	0x1c
#define PLL_CONFIG_CTL_U1	0x20
#define PLL_TEST_CTL		0x24
#define PLL_TEST_CTL_U		0x28
#define PLL_TEST_CTL_U1		0x2c
#define PLL_OPMODE		0x38
#define  PLL_OPMODE_RUN		BIT(0)
#define  PLL_OPMODE_MASK	GENMASK(2, 0)
#define PLL_ALPHA_VAL		0x40

#define CORE_CMD_RCGR		0x1020
#define  CMD_UPDATE		BIT(0)
#define CORE_CFG_RCGR		0x1024
#define  CFG_SRC_SEL_MASK	GENMASK(10, 8)
#define  CFG_SRC_DIV_MASK	GENMASK(4, 0)

#define XO_HZ			19200000UL
#define PLL_SRC_SEL		2
#define PLL_SRC_DIV		1	/* 2 * divider - 1: divide by one */

#define PLL_LOCK_TIMEOUT_US	100000

#define PLL_STATUS		0x30
#define PLL_STATE		0x3c
#define CORE_CBCR		0x1040

struct q6cc {
	struct device *dev;
	void __iomem *base;
	struct clk_hw hw;
};

#define to_q6cc(_hw) container_of(_hw, struct q6cc, hw)

static void q6cc_update(void __iomem *base, u32 off, u32 mask, u32 val)
{
	writel((readl(base + off) & ~mask) | val, base + off);
}

static int q6cc_start_pll(struct device *dev, void __iomem *base, u32 l, u32 cal_l)
{
	u32 val;
	int ret;

	writel(0x20485699, base + PLL_CONFIG_CTL);
	writel(0x00002261, base + PLL_CONFIG_CTL_U);
	writel(0x329a299c, base + PLL_CONFIG_CTL_U1);
	writel(0x0, base + PLL_USER_CTL);
	writel(0x805, base + PLL_USER_CTL_U);
	writel(0x0, base + PLL_USER_CTL_U1);
	writel(0x0, base + PLL_TEST_CTL);
	writel(0x0, base + PLL_TEST_CTL_U);
	writel(0x0, base + PLL_TEST_CTL_U1);

	writel(l, base + PLL_L_VAL);
	writel(cal_l, base + PLL_CAL_L_VAL);
	writel(0x0, base + PLL_ALPHA_VAL);

	/* Take the PLL out of reset with its output off, then run it. */
	q6cc_update(base, PLL_MODE, PLL_MODE_OUTCTRL, 0);
	q6cc_update(base, PLL_OPMODE, PLL_OPMODE_MASK, 0);
	q6cc_update(base, PLL_MODE, PLL_MODE_RESET_N, PLL_MODE_RESET_N);
	q6cc_update(base, PLL_OPMODE, PLL_OPMODE_RUN, PLL_OPMODE_RUN);

	ret = readl_poll_timeout(base + PLL_MODE, val, val & PLL_MODE_LOCK_DET,
				 100, PLL_LOCK_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "PLL did not lock (PLL_MODE %#x)\n", val);
		return ret;
	}

	/* Enable the PLL's output, then the main output. */
	q6cc_update(base, PLL_USER_CTL, BIT(0), BIT(0));
	q6cc_update(base, PLL_MODE, PLL_MODE_OUTCTRL, PLL_MODE_OUTCTRL);

	return 0;
}

static int q6cc_select_pll(struct device *dev, void __iomem *base)
{
	u32 val;
	int ret;

	q6cc_update(base, CORE_CFG_RCGR,
		    CFG_SRC_SEL_MASK | CFG_SRC_DIV_MASK,
		    FIELD_PREP(CFG_SRC_SEL_MASK, PLL_SRC_SEL) |
		    FIELD_PREP(CFG_SRC_DIV_MASK, PLL_SRC_DIV));
	q6cc_update(base, CORE_CMD_RCGR, CMD_UPDATE, CMD_UPDATE);

	/* The update bit clears when the new configuration is in use. */
	ret = readl_poll_timeout(base + CORE_CMD_RCGR, val, !(val & CMD_UPDATE),
				 10, 1000);
	if (ret)
		dev_err(dev, "core clock did not switch over\n");

	return ret;
}

/* What the hardware says the PLL and the core clock are doing right now. */
static bool q6cc_pll_locked(struct q6cc *cc)
{
	return readl(cc->base + PLL_MODE) & PLL_MODE_LOCK_DET;
}

static unsigned long q6cc_pll_rate(struct q6cc *cc)
{
	u64 rate;

	if (!q6cc_pll_locked(cc))
		return 0;

	/* The alpha of this PLL has 16 bits. */
	rate = (u64)XO_HZ * readl(cc->base + PLL_L_VAL);
	rate += ((u64)XO_HZ * readl(cc->base + PLL_ALPHA_VAL)) >> 16;

	return rate;
}

static const char *q6cc_core_source(struct q6cc *cc)
{
	switch (FIELD_GET(CFG_SRC_SEL_MASK, readl(cc->base + CORE_CFG_RCGR))) {
	case 0:
		return "xo";
	case PLL_SRC_SEL:
		return "pll";
	default:
		return "other";
	}
}

static unsigned long q6cc_core_rate(struct q6cc *cc)
{
	u32 cfg = readl(cc->base + CORE_CFG_RCGR);
	u32 div = FIELD_GET(CFG_SRC_DIV_MASK, cfg);
	unsigned long rate;

	switch (FIELD_GET(CFG_SRC_SEL_MASK, cfg)) {
	case 0:
		rate = XO_HZ;
		break;
	case PLL_SRC_SEL:
		rate = q6cc_pll_rate(cc);
		break;
	default:
		return 0;
	}

	/* The divider is 2 * divide - 1, in half steps; 0 is not dividing. */
	if (div > 1)
		rate = rate * 2 / (div + 1);

	return rate;
}

static unsigned long q6cc_recalc_rate(struct clk_hw *hw, unsigned long parent)
{
	return q6cc_core_rate(to_q6cc(hw));
}

static const struct clk_ops q6cc_core_ops = {
	.recalc_rate = q6cc_recalc_rate,
};

#define Q6CC_ATTR(_name, _fmt, _val)					\
static ssize_t _name##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct q6cc *cc = dev_get_drvdata(dev);				\
									\
	return sysfs_emit(buf, _fmt "\n", _val);			\
}									\
static DEVICE_ATTR_RO(_name)

Q6CC_ATTR(core_rate_hz, "%lu", q6cc_core_rate(cc));
Q6CC_ATTR(core_source, "%s", q6cc_core_source(cc));
Q6CC_ATTR(pll_rate_hz, "%lu", q6cc_pll_rate(cc));
Q6CC_ATTR(pll_locked, "%d", q6cc_pll_locked(cc));
Q6CC_ATTR(pll_mode, "%#x", readl(cc->base + PLL_MODE));
Q6CC_ATTR(pll_l_val, "%u", readl(cc->base + PLL_L_VAL));
Q6CC_ATTR(pll_cal_l_val, "%u", readl(cc->base + PLL_CAL_L_VAL));
Q6CC_ATTR(pll_alpha_val, "%u", readl(cc->base + PLL_ALPHA_VAL));

static struct attribute *q6cc_attrs[] = {
	&dev_attr_core_rate_hz.attr,
	&dev_attr_core_source.attr,
	&dev_attr_pll_rate_hz.attr,
	&dev_attr_pll_locked.attr,
	&dev_attr_pll_mode.attr,
	&dev_attr_pll_l_val.attr,
	&dev_attr_pll_cal_l_val.attr,
	&dev_attr_pll_alpha_val.attr,
	NULL,
};
ATTRIBUTE_GROUPS(q6cc);

static const struct debugfs_reg32 q6cc_regs[] = {
	{ "PLL_MODE", PLL_MODE },
	{ "PLL_L_VAL", PLL_L_VAL },
	{ "PLL_CAL_L_VAL", PLL_CAL_L_VAL },
	{ "PLL_USER_CTL", PLL_USER_CTL },
	{ "PLL_USER_CTL_U", PLL_USER_CTL_U },
	{ "PLL_USER_CTL_U1", PLL_USER_CTL_U1 },
	{ "PLL_CONFIG_CTL", PLL_CONFIG_CTL },
	{ "PLL_CONFIG_CTL_U", PLL_CONFIG_CTL_U },
	{ "PLL_CONFIG_CTL_U1", PLL_CONFIG_CTL_U1 },
	{ "PLL_TEST_CTL", PLL_TEST_CTL },
	{ "PLL_TEST_CTL_U", PLL_TEST_CTL_U },
	{ "PLL_TEST_CTL_U1", PLL_TEST_CTL_U1 },
	{ "PLL_STATUS", PLL_STATUS },
	{ "PLL_OPMODE", PLL_OPMODE },
	{ "PLL_STATE", PLL_STATE },
	{ "PLL_ALPHA_VAL", PLL_ALPHA_VAL },
	{ "CORE_CMD_RCGR", CORE_CMD_RCGR },
	{ "CORE_CFG_RCGR", CORE_CFG_RCGR },
	{ "CORE_CBCR", CORE_CBCR },
};

static int q6cc_state_show(struct seq_file *m, void *unused)
{
	struct q6cc *cc = m->private;
	u32 mode = readl(cc->base + PLL_MODE);
	u32 cfg = readl(cc->base + CORE_CFG_RCGR);

	seq_printf(m, "xo:            %lu Hz\n", XO_HZ);
	seq_printf(m, "pll mode:      %#010x (out %s, reset %s, lock %s)\n", mode,
		   mode & PLL_MODE_OUTCTRL ? "enabled" : "disabled",
		   mode & PLL_MODE_RESET_N ? "released" : "held",
		   mode & PLL_MODE_LOCK_DET ? "detected" : "no");
	seq_printf(m, "pll opmode:    %#x (%s)\n", readl(cc->base + PLL_OPMODE),
		   (readl(cc->base + PLL_OPMODE) & PLL_OPMODE_MASK) ==
		   PLL_OPMODE_RUN ? "run" : "standby");
	seq_printf(m, "pll L/CAL_L:   %u / %u, alpha %u\n",
		   readl(cc->base + PLL_L_VAL), readl(cc->base + PLL_CAL_L_VAL),
		   readl(cc->base + PLL_ALPHA_VAL));
	seq_printf(m, "pll rate:      %lu Hz\n", q6cc_pll_rate(cc));
	seq_printf(m, "core cfg:      %#x (source %s, divider field %lu)\n", cfg,
		   q6cc_core_source(cc), FIELD_GET(CFG_SRC_DIV_MASK, cfg));
	seq_printf(m, "core update:   %s\n",
		   readl(cc->base + CORE_CMD_RCGR) & CMD_UPDATE ? "pending" : "done");
	seq_printf(m, "core clock:    %lu Hz\n", q6cc_core_rate(cc));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(q6cc_state);

static void q6cc_debugfs_remove(void *dir)
{
	debugfs_remove_recursive(dir);
}

static void q6cc_debugfs_init(struct q6cc *cc)
{
	struct debugfs_regset32 *regset;
	struct dentry *dir = debugfs_create_dir(dev_name(cc->dev), NULL);

	regset = devm_kzalloc(cc->dev, sizeof(*regset), GFP_KERNEL);
	if (!regset)
		return;

	regset->regs = q6cc_regs;
	regset->nregs = ARRAY_SIZE(q6cc_regs);
	regset->base = cc->base;
	regset->dev = cc->dev;

	debugfs_create_regset32("regs", 0444, dir, regset);
	debugfs_create_file("state", 0444, dir, cc, &q6cc_state_fops);
	devm_add_action_or_reset(cc->dev, q6cc_debugfs_remove, dir);
}

static int q6cc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_init_data init = {
		.name = "cdsp_core",
		.ops = &q6cc_core_ops,
		/* Whatever is in the registers is the rate. */
		.flags = CLK_GET_RATE_NOCACHE,
	};
	struct q6cc *cc;
	void __iomem *base;
	unsigned long rate;
	u32 l, cal_l;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	cc = devm_kzalloc(dev, sizeof(*cc), GFP_KERNEL);
	if (!cc)
		return -ENOMEM;
	cc->dev = dev;
	cc->base = base;
	platform_set_drvdata(pdev, cc);

	ret = of_property_read_u32(dev->of_node, "qcom,pll-l-val", &l);
	if (ret)
		return ret;
	ret = of_property_read_u32(dev->of_node, "qcom,pll-cal-l-val", &cal_l);
	if (ret)
		return ret;

	/* Whoever started us first (a firmware, or a previous boot) wins. */
	if (FIELD_GET(CFG_SRC_SEL_MASK, readl(base + CORE_CFG_RCGR)) !=
	    PLL_SRC_SEL) {
		ret = q6cc_start_pll(dev, base, l, cal_l);
		if (!ret)
			ret = q6cc_select_pll(dev, base);
		if (ret)
			return ret;
	}

	cc->hw.init = &init;
	ret = devm_clk_hw_register(dev, &cc->hw);
	if (ret)
		return ret;

	rate = q6cc_core_rate(cc);
	dev_info(dev, "core clock at %lu MHz\n", rate / 1000000);

	q6cc_debugfs_init(cc);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, &cc->hw);
}

static const struct of_device_id q6cc_of_match[] = {
	{ .compatible = "qcom,qcs6490-cdsp-q6cc" },
	{}
};
MODULE_DEVICE_TABLE(of, q6cc_of_match);

static struct platform_driver q6cc_driver = {
	.probe = q6cc_probe,
	.driver = {
		.name = "qcs6490-cdsp-q6cc",
		.of_match_table = q6cc_of_match,
		.dev_groups = q6cc_groups,
	},
};

/* Early, so that what runs after it runs on the fast clock. */
static int __init q6cc_init(void)
{
	return platform_driver_register(&q6cc_driver);
}
postcore_initcall(q6cc_init);

MODULE_DESCRIPTION("QCS6490 CDSP core clock");
MODULE_LICENSE("GPL");
