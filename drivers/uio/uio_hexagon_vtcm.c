#include <linux/device.h>
#include <linux/module.h>
#include <linux/uio_driver.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

static int hexagon_vtcm_open(struct uio_info *info, struct inode *inode)
{
	//  Disallow access if HVX was never powered up
	if (!current_thread_info()->hvx) {
		return -EIO;
	}
	return 0;
}

static int hexagon_vtcm_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct uio_info *info;
	struct device_node *node = pdev->dev.of_node;
	struct resource res;

	printk("%s\n", __func__);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto out;
	}

	info->name = "hexagon_vtcm";
	info->version = "0.0";

	/*  Should be queried from hypervisor instead of picked from devtree  */
	if (of_address_to_resource(node, 0 , &res)) {
		goto out_free;
	}
	info->mem[0].name = "vtcm";
	info->mem[0].addr = res.start;
	info->mem[0].size = resource_size(&res);
	//  pgprot_noncached is not working right now; it's all cached
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->open = hexagon_vtcm_open;

	if (uio_register_device(&pdev->dev, info) != 0) {
		dev_err(&pdev->dev, "UIO registration failed\n");
		ret = -ENODEV;
		goto out_free;
	}
	platform_set_drvdata(pdev, info);

	return 0;
out_free:
	kfree(info);

out:
	return ret;
}

static int hexagon_vtcm_remove(struct platform_device *pdev)
{
	return 0;
}

// Todo:  use huge pages.

static const struct of_device_id hexagon_vtcm_match[] = {
	{ .compatible = "qcom,hexagon_vtcm", },
	{}
};

static struct platform_driver hexagon_vtcm_driver = {
	.driver = {
		.name = "hexagon_vtcm",
		.owner = THIS_MODULE,
		.of_match_table = hexagon_vtcm_match,
	},
	.probe = hexagon_vtcm_probe,
	.remove = hexagon_vtcm_remove,
};

static int __init hexagon_vtcm_init(void)
{
	printk("%s\n", __func__);
	return platform_driver_register(&hexagon_vtcm_driver);
}

static void __init hexagon_vtcm_exit(void)
{
	platform_driver_unregister(&hexagon_vtcm_driver);
}

module_init(hexagon_vtcm_init)
module_exit(hexagon_vtcm_exit)
