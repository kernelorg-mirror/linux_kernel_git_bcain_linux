#include <linux/device.h>
#include <linux/module.h>
#include <linux/uio_driver.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

/*  Might make this module dependent on a generic HVX module, not sure  */
/*  Right now this is looking more like a housekeeping driver to manage the TCM mapping...  like locking?  */
/*  Need to figure out the process to dynamically resize/partition L2 into TCM only when needed.  */
/*  Also need pinned mappings :(  */

static int hexagon_tcm_probe(struct platform_device *pdev)
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

	info->name = "hexagon_tcm";
	info->version = "0.1";

	/*  Fill out uio_mem  */
	/*  Should be queried from hypervisor instead of picked from devtree  */
	if (of_address_to_resource(node, 0 , &res)) {
		goto out_free;
	}
	info->mem[0].name = "tcm";
	info->mem[0].internal_addr = devm_ioremap_resource(&pdev->dev, &res);
	info->mem[0].addr = res.start;
	info->mem[0].size = resource_size(&res);
	info->mem[0].memtype = UIO_MEM_PHYS;

	/*  private parts?  */
	/*  Fill out mmap? */
	/*  Fill out open? */
	/*  Fill out release? */

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

static int hexagon_tcm_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id hexagon_tcm_match[] = {
	{ .compatible = "qcom,hexagon_tcm", },
	{}
};

static struct platform_driver hexagon_tcm_driver = {
	.driver = {
		.name = "hexagon_tcm",
		.owner = THIS_MODULE,
		.of_match_table = hexagon_tcm_match,
	},
	.probe = hexagon_tcm_probe,
	.remove = hexagon_tcm_remove,
};

static int __init hexagon_tcm_init(void)
{
	printk("%s\n", __func__);
	return platform_driver_register(&hexagon_tcm_driver);
}

static void __init hexagon_tcm_exit(void)
{
	platform_driver_unregister(&hexagon_tcm_driver);
}

module_init(hexagon_tcm_init)
module_exit(hexagon_tcm_exit)
