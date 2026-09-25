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
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

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

static int q6cc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_hw *hw;
	void __iomem *base;
	unsigned long rate;
	u32 l, cal_l;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

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

	rate = readl(base + PLL_L_VAL) * XO_HZ;
	hw = devm_clk_hw_register_fixed_rate(dev, "cdsp_core", NULL, 0, rate);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	dev_info(dev, "core clock at %lu MHz\n", rate / 1000000);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
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
