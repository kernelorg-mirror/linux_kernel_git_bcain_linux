#ifndef _AMAZON_GPIO_H_
#define _AMAZON_GPIO_H_

extern void __iomem *gpio_base;

#define NR_GPIO_IRQS	168
#define NR_MSM_IRQS	49

#define MSM_GPIO_BASE		0x94000000
#define MSM_GPIO_INT_BASE	0x94130000

#define GPIO_PAGE_OFFSET	0x40
#define GPIO_CFG_OFFSET		0x44

#define GPIO_MAC_RST_N		37
#define GPIO_MAC_TXD_3		119
#define GPIO_MAC_TXD_2		120
#define GPIO_MAC_TXD_1		121
#define GPIO_MAC_TXD_0		122
#define GPIO_MAC_TX_EN		123
#define GPIO_MAC_TX_CLK		133

/* UART GPIOs */
#define UART1_RFR_GPIO	136
#define UART1_CTS_GPIO	137
#define UART1_RXD_GPIO	138
#define UART1_TXD_GPIO	139

#define UART2_RXD_GPIO	142
#define UART2_TXD_GPIO	143
#define UART2_CTS_GPIO	144
#define UART2_RFR_GPIO	145

#endif
