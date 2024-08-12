#ifndef _PIRANHA_BOARD_H
#define _PIRANHA_BOARD_H

extern struct platform_device msm_gpio_devices[];

#define UART1_NS_REG				0x78
#define UART2_NS_REG				0x7c
#define UART3_NS_REG				0x80

#define UART_NS_REG__UART1_SRC_SEL__TCX		(0x0 << 0)
#define UART_NS_REG__UART1_SRC_SEL__TCX_DIV4	(0x1 << 0)
#define UART_NS_REG__UART1_SRC_SEL__EXT_CLK_1	(0x2 << 0)
#define UART_NS_REG__UART1_SRC_SEL__GND_TIE	(0x3 << 0)
#define UART_NS_REG__UART1_CLK_BRANCH_ENA___M	(1 << 2)
#define UART_NS_REG__UART1_CLK_INV		(1 << 3)
#define UART_NS_REG__UART1_ROOT_ENA___M		(1 << 4)

#define UART_NS_REG__UART2_SRC_SEL__TCX		(0x0 << 0)
#define UART_NS_REG__UART2_SRC_SEL__TCX_DIV4	(0x1 << 0)
#define UART_NS_REG__UART2_SRC_SEL__EXT_CLK_1	(0x2 << 0)
#define UART_NS_REG__UART2_SRC_SEL__GND_TIE	(0x3 << 0)
#define UART_NS_REG__UART2_CLK_BRANCH_ENA___M	(1 << 2)
#define UART_NS_REG__UART2_ROOT_ENA___M		(1 << 4)

#define UART_NS_REG__UART3_SRC_SEL__TCX04	0x1
#define UART_NS_REG__UART3_SRC_SEL___S		12
#define UART_NS_REG__UART3_ROOT_ENA___M		0x10
#define UART_NS_REG__UART3_CLK_BRANCH_ENA___M	0x4

#endif
