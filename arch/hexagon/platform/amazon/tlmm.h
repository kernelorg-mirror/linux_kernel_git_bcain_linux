#ifndef _PIRANHA_TLMM_H
#define _PIRANHA_TLMM_H

#define NR_MSM_GPIOS 168

#define MSM_TLMM_BASE 0x94040000

struct piranha_gpio_regs {
	void __iomem *in;
	void __iomem *out;
	void __iomem *oe;
};

struct piranha_gpio_platform_data {
	unsigned gpio_base;
	unsigned ngpio;
	unsigned irq_base;
	unsigned irq_summary;
	struct piranha_gpio_regs regs;
};

#define MSM_GPIO_TO_INT(n) (NR_MSM_IRQS + (n))

#define GPIO_OUT(gpio)    ((0x4 * (gpio)/32))
#define GPIO_IN(gpio)     (0x48 + (0x4 * (gpio)/32))
#define GPIO_PAGE         (0x40)
#define GPIO_CONFIG       (0x44)
#define GPIO_REG(off)     (MSM_TLMM_BASE + (off))


/* output value */
#define GPIO_OUT_0         GPIO_REG(0x00)   /* gpio  31-0   */
#define GPIO_OUT_1         GPIO_REG(0x04)   /* gpio  63-32  */
#define GPIO_OUT_2         GPIO_REG(0x08)   /* gpio  95-64  */
#define GPIO_OUT_3         GPIO_REG(0x0C)   /* gpio 127-96  */
#define GPIO_OUT_4         GPIO_REG(0x10)   /* gpio 159-128 */
#define GPIO_OUT_5         GPIO_REG(0x14)   /* gpio 167-160 */

/* same pin map as above, output enable */
#define GPIO_OE_0          GPIO_REG(0x20)
#define GPIO_OE_1          GPIO_REG(0x24)
#define GPIO_OE_2          GPIO_REG(0x28)
#define GPIO_OE_3          GPIO_REG(0x2C)
#define GPIO_OE_4          GPIO_REG(0x30)
#define GPIO_OE_5          GPIO_REG(0x34)

/* same pin map as above, input read */
#define GPIO_IN_0          GPIO_REG(0x48)
#define GPIO_IN_1          GPIO_REG(0x4C)
#define GPIO_IN_2          GPIO_REG(0x50)
#define GPIO_IN_3          GPIO_REG(0x54)
#define GPIO_IN_4          GPIO_REG(0x58)
#define GPIO_IN_5          GPIO_REG(0x5C)

#endif
