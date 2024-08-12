#define DEBUG

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#define DRIVER_NAME "hexagon-irqtest"

#define IPC_IRQS 4

int irq[IPC_IRQS];

static irqreturn_t irqtest_handler(int irq, void *data)
{
	printk("%s %d\n", __func__, irq);
	return IRQ_HANDLED;
}


static int irqtest_probe(struct platform_device *pdev)
{
	int i;
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	for (i=0; i<IPC_IRQS; i++) {
		// 0 is fail!
		irq[i] = platform_get_irq(pdev, i);
		dev_dbg(&pdev->dev, "irq %d\n", irq[i]);
		// 0 is pass!
		// last arg is cookie!  day I got cookie!
		ret = devm_request_irq(&pdev->dev, irq[i], irqtest_handler, IRQF_SHARED, DRIVER_NAME, &pdev->dev);
		if (ret) {
			dev_dbg(&pdev->dev, "request for irq %d failed\n", irq[i]);
		}
	}

	return 0;
}

static int irqtest_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id irqtest_dt_match[] = {
        {
                .compatible = "qcom,hexagon-irqtest",
        },
        { }
};
MODULE_DEVICE_TABLE(of, irqtest_dt_match);

static struct platform_driver irqtest_driver = {
	.probe = irqtest_probe,
	.remove = irqtest_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = irqtest_dt_match,
        },
};
module_platform_driver(irqtest_driver);

