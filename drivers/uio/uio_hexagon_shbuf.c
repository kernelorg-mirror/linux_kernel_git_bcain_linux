/*
 * SPDX-License-Identifier: GPL-2.0
 * Hexagon Shared Buffer UIO driver
 *
 * Provides userspace access to shared memory regions for Hexagon DSP
 * communication. Memory regions are specified via platform device resources.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>
#include <linux/of.h>
#include <linux/of_address.h>

static int hexagon_shbuf_probe(struct platform_device *pdev)
{
	struct uio_info *info;
	struct device_node *node = pdev->dev.of_node;
	struct resource res;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->name = "hexagon_shbuf";
	info->version = "0.1";

	if (node && !of_address_to_resource(node, 0, &res)) {
		info->mem[0].name = "shbuf";
		info->mem[0].addr = res.start;
		info->mem[0].size = resource_size(&res);
		info->mem[0].memtype = UIO_MEM_PHYS;
	} else {
		u32 i;
		struct resource *r;

		for (i = 0; i < pdev->num_resources; i++) {
			r = platform_get_resource(pdev, IORESOURCE_MEM, i);
			if (!r) {
				dev_err(&pdev->dev, "bad resources\n");
				return -ENODEV;
			}
			info->mem[i].name = "shbuf";
			info->mem[i].addr = r->start;
			info->mem[i].size = resource_size(r);
			info->mem[i].memtype = UIO_MEM_PHYS;
		}
	}

	ret = uio_register_device(&pdev->dev, info);
	if (ret) {
		dev_err(&pdev->dev, "UIO registration failed\n");
		return ret;
	}
	platform_set_drvdata(pdev, info);

	return 0;
}

static void hexagon_shbuf_remove(struct platform_device *pdev)
{
	struct uio_info *info = platform_get_drvdata(pdev);

	uio_unregister_device(info);
}

static const struct of_device_id hexagon_shbuf_match[] = {
	{ .compatible = "qcom,hexagon_shbuf", },
	{}
};
MODULE_DEVICE_TABLE(of, hexagon_shbuf_match);

static struct platform_driver hexagon_shbuf_driver = {
	.driver = {
		.name = "hexagon_shbuf",
		.of_match_table = hexagon_shbuf_match,
	},
	.probe = hexagon_shbuf_probe,
	.remove = hexagon_shbuf_remove,
};
module_platform_driver(hexagon_shbuf_driver);

MODULE_DESCRIPTION("Hexagon Shared Buffer UIO driver");
MODULE_LICENSE("GPL");
