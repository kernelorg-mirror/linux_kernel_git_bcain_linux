#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include "irq.h"
#include "tlmm.h"
#include "gpio.h"

#define MSM_NAND_PHYS		0x81600000
#define EBI2_REG_BASE		0x81400000

#ifdef NOTNOW
static struct resource resources_nand[] = {
	[0] = {
		.name   = "msm_nand_dmac",
		.start	= DMOV_NAND_CHAN,
		.end	= DMOV_NAND_CHAN,
		.flags	= IORESOURCE_DMA,
	},
	[1] = {
		.name   = "msm_nand_phys",
		.start  = MSM_NAND_PHYS,
		.end    = MSM_NAND_PHYS + 0x7FF,
		.flags  = IORESOURCE_MEM,
	},
	[3] = {
		.name   = "ebi2_reg_base",
		.start  = EBI2_REG_BASE,
		.end    = EBI2_REG_BASE + 0x60,
		.flags  = IORESOURCE_MEM,
	},
};

struct flash_platform_data msm_nand_data = {
	.parts		= NULL,
	.nr_parts	= 0,
	.interleave     = 0,
};

struct platform_device msm_device_nand = {
	.name		= "msm_nand",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_nand),
	.resource	= resources_nand,
	.dev		= {
		.platform_data	= &msm_nand_data,
	},
};
#endif

#ifdef NOTNOW
#define MSM_SDC1_BASE         0x80A00000
static struct resource resources_sdc1[] = {
	{
		.start	= MSM_SDC1_BASE,
		.end	= MSM_SDC1_BASE + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= INT_SDC1_0,
		.end	= INT_SDC1_1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= 8,
		.end	= 8,
		.flags	= IORESOURCE_DMA,
	},
};

struct platform_device msm_device_sdc1 = {
	.name		= "msm_sdcc",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(resources_sdc1),
	.resource	= resources_sdc1,
	.dev		= {
	.coherent_dma_mask	= 0xffffffff,
	},
};


static struct platform_device *msm_sdcc_devices[] __initdata = {
	&msm_device_sdc1,
};

int __init msm_add_sdcc(unsigned int controller, struct mmc_platform_data *plat)
{
	struct platform_device	*pdev;

	if (controller != 1)
		return -EINVAL;

	pdev = msm_sdcc_devices[controller-1];
	pdev->dev.platform_data = plat;
	return platform_device_register(pdev);
}

#endif /* NOTNOW */

#ifdef NOTNOW
static void __init msm_register_device(struct platform_device *pdev, void *data)
{
	int ret;

	pdev->dev.platform_data = data;

	ret = platform_device_register(pdev);
	if (ret)
		dev_err(&pdev->dev,
			  "%s: platform_device_register() failed = %d\n",
			  __func__, ret);
}
#endif


#define PIRANHA_GPIO_PLATFORM_DATA(ix, begin, end, irq)			\
	[ix] = {							\
		.gpio_base	= begin,				\
		.ngpio		= end - begin + 1,			\
		.irq_base	= MSM_GPIO_TO_INT(begin),		\
		.irq_summary	= irq,					\
		.regs = {						\
			.in		= GPIO_IN_ ## ix,		\
			.out		= GPIO_OUT_ ## ix,		\
			.oe		= GPIO_OE_ ## ix,		\
		},							\
	}


#define PIRANHA_GPIO_DEVICE(ix, pdata)			\
	{						\
		.name		= "piranha-gpio",	\
		.id		= ix,			\
		.num_resources	= 0,			\
		.dev = {				\
			.platform_data = &pdata[ix],	\
		},					\
	}

static struct piranha_gpio_platform_data gpio_platform_data[] = {
	PIRANHA_GPIO_PLATFORM_DATA(0,   0,  31, INT_SIRC_0),
	PIRANHA_GPIO_PLATFORM_DATA(1,  32,  63, INT_SIRC_0),
	PIRANHA_GPIO_PLATFORM_DATA(2,  64,  95, INT_SIRC_0),
	PIRANHA_GPIO_PLATFORM_DATA(3,  96, 127, INT_SIRC_0),
	PIRANHA_GPIO_PLATFORM_DATA(4, 128, 159, INT_SIRC_0),
	PIRANHA_GPIO_PLATFORM_DATA(5, 160, 167, INT_SIRC_0),
};

struct platform_device msm_gpio_devices[] = {
	PIRANHA_GPIO_DEVICE(0, gpio_platform_data),
	PIRANHA_GPIO_DEVICE(1, gpio_platform_data),
	PIRANHA_GPIO_DEVICE(2, gpio_platform_data),
	PIRANHA_GPIO_DEVICE(3, gpio_platform_data),
	PIRANHA_GPIO_DEVICE(4, gpio_platform_data),
	PIRANHA_GPIO_DEVICE(5, gpio_platform_data),
};

