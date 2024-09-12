#include <linux/device.h>
#include <linux/module.h>
#include <linux/uio_driver.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>

static const struct platform_driver hexagon_shbuf_driver;

static ssize_t cachectl_show(struct uio_mem *mem, char *buf)
{
	return sprintf(buf, "format:  [hex op number] [hex address] [hex size]\n");
}


static int valid_mem_address(struct uio_mem *mem, void *addr, size_t size)
{	
	int memidx;
	struct vm_area_struct *vma;
	struct uio_device *idev;

	vma = find_vma(current->mm, (unsigned long)addr);
	if (!vma) {
		pr_debug("fail vma\n");
		return 0;
	}
	idev = vma->vm_private_data;
	if (idev != mem->idev) {
		pr_debug("fail wrong vma backing\n");
		return 0;
	}
	memidx = uio_find_mem_index(vma);
	if (&idev->info->mem[memidx] != mem) {
		pr_debug("fail wrong mem backing\n");
		return 0;
	}
	if (((addr + size) - vma->vm_start) > mem->size) {
		pr_debug("fail wrong mem size\n");
		return 0;
	}
	return 1;
}

static ssize_t cachectl_store(struct uio_mem *mem, const char *buf,
			     size_t count)
{
	u32 op = 0;
	void *addr = 0;
	size_t size = 0;
	ssize_t ret = -EINVAL;

	pr_debug("cachectl_store()\n");

	if (sscanf(buf, "%x %x %x", &op, &addr, &size) != 3) {
		pr_debug("- Bad parameters\n");
		goto out;
	}

	pr_debug("op 0x%x addr 0x%x size 0x%x\n", op, addr, size);

	get_task_struct(current);
	down_read(&current->mm->mmap_sem);

	if (!valid_mem_address(mem, addr, size)) {
		goto unlock;
	}

	switch (op) {
		case 0:
			hexagon_cache_sync(addr, size, DMA_FROM_DEVICE);
			break;
		case 1:
			hexagon_cache_sync(addr, size, DMA_TO_DEVICE);
			break;
		default:
			goto unlock;
	}

	ret = count;
unlock:
	up_read(&current->mm->mmap_sem);
	put_task_struct(current);
out:
	return ret;
}

static struct map_sysfs_entry cachectl_attribute =
	__ATTR(cachectl, (S_IRUSR | S_IWUSR), cachectl_show, cachectl_store);


static int hexagon_shbuf_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct uio_info *info;
	struct device_node *node = NULL;
	struct resource res;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		ret = -ENOMEM;
		goto out;
	}

	info->name = "hexagon_shbuf";
	info->version = "0.1";

#if 0
	node = pdev->dev.of_node;
#endif
	if (node) {
		if (of_address_to_resource(node, 0 , &res)) {
			goto out_free;
		}
		info->mem[0].name = "shbuf";
		info->mem[0].addr = res.start;
		info->mem[0].size = resource_size(&res);
		/*  btw our pgprot_noncached is kind of broken right now.  it's all cached.  */
		info->mem[0].memtype = UIO_MEM_PHYS;
	}
	else {
		u32 i;
		struct resource *r;

		for (i=0; i < pdev->num_resources; i++) {
			r = platform_get_resource(pdev, IORESOURCE_MEM, i);
			if (!r) {
				dev_err(&pdev->dev, "bad resources\n");
				goto out_free;
			}
			info->mem[i].name = "shbuf";
			info->mem[i].addr = r->start;
			info->mem[i].size = resource_size(r);
			info->mem[i].memtype = UIO_MEM_PHYS;
		}
	}

	if (uio_register_device(&pdev->dev, info) != 0) {
		dev_err(&pdev->dev, "UIO registration failed\n");
		ret = -ENODEV;
		goto out_free;
	}
	platform_set_drvdata(pdev, info);

	sysfs_create_file(&info->mem[0].map->kobj, &cachectl_attribute);

	return 0;
out_free:
	kfree(info);

out:
	return ret;
}

/*  Todo:  (pin) hugepages?  */
static int hexagon_shbuf_mmap(struct uio_info *info, struct vm_area_struct *vma)
{
	return 0;
}

static int hexagon_shbuf_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id hexagon_shbuf_match[] = {
	{ .compatible = "qcom,hexagon_shbuf", },
	{}
};

static const struct platform_driver hexagon_shbuf_driver = {
	.driver = {
		.name = "hexagon_shbuf",
		.owner = THIS_MODULE,
		.of_match_table = hexagon_shbuf_match,
	},
	.probe = hexagon_shbuf_probe,
	.remove = hexagon_shbuf_remove,
};

static int del_device(struct device_driver *dev)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int new_device(struct device_driver *drv, const char *buf, size_t len)
{
	char devname[64];
	char *p=buf;
	struct resource res_array[MAX_UIO_MAPS];
	struct platform_device *dev;
	unsigned long val;
	int rescount = 0;
	int ret = -1;
	int id = -1;

	if (len == 0) {
		return len;
	}

	memset(res_array, 0, sizeof(struct resource) * MAX_UIO_MAPS);

	do {
		if (sscanf(p, "0x%lx", &val)) {
			if (rescount % 2 == 0) {
				res_array[rescount/2].start = val;
				res_array[rescount/2].flags = IORESOURCE_MEM;
			}
			if (rescount % 2 == 1) {
				res_array[rescount/2].end = res_array[rescount/2].start + val - 1;
			}
			rescount++;
		}
		strsep(&p, " ");
	} while(p);

	if (rescount && rescount % 2 == 1) {
		return len;
	}

	snprintf(devname, 64, "hexagon_shbuf");

	//  or use platform_device_register?
	dev = platform_device_alloc(devname, PLATFORM_DEVID_AUTO);
	if (platform_device_add_resources(dev, res_array, rescount/2)) {
		goto out_free;
	}

	ret = platform_device_add(dev);

	if (!ret)
		return len;
out_free:
	platform_device_put(dev);
	return ret;
}

static struct uio_driver hexagon_shbuf_uio_driver = {
	.driver = &hexagon_shbuf_driver.driver,
	.new_device = new_device,
	.del_device = del_device,
};

static int __init hexagon_shbuf_init(void)
{
	uio_register_driver(&hexagon_shbuf_uio_driver);
	return platform_driver_register(&hexagon_shbuf_driver);

out_err:
	return -ENOENT;

}

static void __init hexagon_shbuf_exit(void)
{
	/*  Todo:  uio_unregister_driver  */
	platform_driver_unregister(&hexagon_shbuf_driver);
}

module_init(hexagon_shbuf_init)
module_exit(hexagon_shbuf_exit)
