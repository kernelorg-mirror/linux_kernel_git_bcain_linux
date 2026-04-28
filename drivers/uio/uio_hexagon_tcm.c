/*
 * SPDX-License-Identifier: GPL-2.0
 * Hexagon TCM UIO driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/of.h>
#include <linux/of_address.h>

static int hexagon_tcm_probe(struct platform_device *pdev)
{
	struct uio_info *info;
	struct device_node *node = pdev->dev.of_node;
	struct resource res;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->name = "hexagon_tcm";
	info->version = "0.1";

	if (of_address_to_resource(node, 0, &res))
		return -ENODEV;

	info->mem[0].name = "tcm";
	info->mem[0].internal_addr = devm_ioremap_resource(&pdev->dev, &res);
	if (IS_ERR(info->mem[0].internal_addr))
		return PTR_ERR(info->mem[0].internal_addr);

	info->mem[0].addr = res.start;
	info->mem[0].size = resource_size(&res);
	info->mem[0].memtype = UIO_MEM_PHYS;

	ret = uio_register_device(&pdev->dev, info);
	if (ret) {
		dev_err(&pdev->dev, "UIO registration failed\n");
		return ret;
	}
	platform_set_drvdata(pdev, info);

	return 0;
}

static void hexagon_tcm_remove(struct platform_device *pdev)
{
	struct uio_info *info = platform_get_drvdata(pdev);

	uio_unregister_device(info);
}

static const struct of_device_id hexagon_tcm_match[] = {
	{ .compatible = "qcom,hexagon_tcm", },
	{}
};
MODULE_DEVICE_TABLE(of, hexagon_tcm_match);

static struct platform_driver hexagon_tcm_driver = {
	.driver = {
		.name = "hexagon_tcm",
		.of_match_table = hexagon_tcm_match,
	},
	.probe = hexagon_tcm_probe,
	.remove = hexagon_tcm_remove,
};
module_platform_driver(hexagon_tcm_driver);

MODULE_DESCRIPTION("Hexagon TCM UIO driver");
MODULE_LICENSE("GPL");
