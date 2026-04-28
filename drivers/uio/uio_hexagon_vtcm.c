/*
 * SPDX-License-Identifier: GPL-2.0
 * Hexagon VTCM UIO driver
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/thread_info.h>

static int hexagon_vtcm_open(struct uio_info *info, struct inode *inode)
{
	/* Disallow access if HVX was never powered up */
	if (!current_thread_info()->hvx)
		return -EIO;

	return 0;
}

static int hexagon_vtcm_probe(struct platform_device *pdev)
{
	struct uio_info *info;
	struct device_node *node = pdev->dev.of_node;
	struct resource res;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->name = "hexagon_vtcm";
	info->version = "0.1";

	if (of_address_to_resource(node, 0, &res))
		return -ENODEV;

	info->mem[0].name = "vtcm";
	info->mem[0].addr = res.start;
	info->mem[0].size = resource_size(&res);
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->open = hexagon_vtcm_open;

	ret = uio_register_device(&pdev->dev, info);
	if (ret) {
		dev_err(&pdev->dev, "UIO registration failed\n");
		return ret;
	}
	platform_set_drvdata(pdev, info);

	return 0;
}

static void hexagon_vtcm_remove(struct platform_device *pdev)
{
	struct uio_info *info = platform_get_drvdata(pdev);

	uio_unregister_device(info);
}

static const struct of_device_id hexagon_vtcm_match[] = {
	{ .compatible = "qcom,hexagon_vtcm", },
	{}
};
MODULE_DEVICE_TABLE(of, hexagon_vtcm_match);

static struct platform_driver hexagon_vtcm_driver = {
	.driver = {
		.name = "hexagon_vtcm",
		.of_match_table = hexagon_vtcm_match,
	},
	.probe = hexagon_vtcm_probe,
	.remove = hexagon_vtcm_remove,
};
module_platform_driver(hexagon_vtcm_driver);

MODULE_DESCRIPTION("Hexagon VTCM UIO driver");
MODULE_LICENSE("GPL");
