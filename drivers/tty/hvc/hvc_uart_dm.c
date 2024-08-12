/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 * Copyright 2012, Code Aurora Forum. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/io.h>

//  Might be time for some devtree action

#include "hvc_console.h"
#include "uart_dm.h"

static long hvcaddr_val;
char *console_buffer;
int  buffer_tail;

void __iomem *membase;

static unsigned int msm_boot_uart_dm_init_rx_transfer(void __iomem *uart_dm_base)
{
	writel(MSM_BOOT_UART_DM_DMRX_DEF_VALUE, MSM_BOOT_UART_DM_DMRX(uart_dm_base));

	return MSM_BOOT_UART_DM_E_SUCCESS;
}

u32 fifo_word;

static int hvc_uart_dm_get_chars(uint32_t vt, char *buf, int count)
{
	int i_can_has_read = 0;
	bool device_read = false;
	u32 rx_buffer_state = 0;
	int max_read = 256;

	//  check the leftover word first
	while (fifo_word) {
		*buf++ = fifo_word & 0xff;
		fifo_word >>= 8;
		i_can_has_read++;
	}

	while ((i_can_has_read < count) && (i_can_has_read < max_read)) {

//		if (readl(MSM_BOOT_UART_DM_SR(membase)) & MSM_BOOT_UART_DM_SR_UART_OVERRUN) {
//			writel(MSM_BOOT_UART_DM_CMD_RESET_ERR_STAT, MSM_BOOT_UART_DM_CR(membase));
//		} /* Check for Overrun error. We'll just reset Error Status */

		if (!(readl(MSM_BOOT_UART_DM_SR(membase)) & MSM_BOOT_UART_DM_SR_RXRDY)) {
			break;
		}

		fifo_word = readl(MSM_BOOT_UART_DM_RF(membase, 0));
		device_read = true;
		//  This is painful, but I don't trust the snap count, DMRX, or damn near anything really.
		//  And it's no worse than what we had before
		while (fifo_word && (i_can_has_read < max_read)) {
			*buf++ = fifo_word & 0xff;
			fifo_word >>= 8;
			i_can_has_read++;
		}

	}

	if (device_read) {
		/* Check if we've received stale event */
		if (readl(MSM_BOOT_UART_DM_MISR(membase)) & MSM_BOOT_UART_DM_RXSTALE) {
			/* Send command to reset stale interrupt */
			writel(MSM_BOOT_UART_DM_GCMD_DIS_STALE_EVT, MSM_BOOT_UART_DM_CR(membase));
			writel(MSM_BOOT_UART_DM_CMD_RES_STALE_INT, MSM_BOOT_UART_DM_CR(membase));
			writel(MSM_BOOT_UART_DM_GCMD_ENA_STALE_EVT, MSM_BOOT_UART_DM_CR(membase));
			//writel(MSM_BOOT_UART_DM_DMRX_DEF_VALUE, MSM_BOOT_UART_DM_DMRX(membase));
		}

		//  Technically supposed to check the RX bytes thing against DMRX and restart, but screw it.
		//  Made DMRX large so we should always just be hitting the stale event
		writel(MSM_BOOT_UART_DM_DMRX_DEF_VALUE, MSM_BOOT_UART_DM_DMRX(membase));
	}  //  go ahead and slap the device if we touched it

	//  Always check the packing buffer and flush it.
	rx_buffer_state = (readl(MSM_BOOT_UART_DM_RXFS(membase)) >> 7) & 7;
	if (rx_buffer_state > 0) {
		writel(MSM_BOOT_UART_DM_GCMD_SW_FORCE_STALE, MSM_BOOT_UART_DM_CR(membase));
	}

	return i_can_has_read;
}


void wait_for_empty(void __iomem *membase)
{
	int timeout = 0;
	while (!(readl(MSM_BOOT_UART_DM_SR(membase)) & MSM_BOOT_UART_DM_SR_TXEMT)) {
		if (++timeout > 1000000)
			break;
	}
}

void uart_dm_putc(void __iomem *membase, char c)
{
	wait_for_empty(membase);

	writel(1, MSM_BOOT_UART_DM_NO_CHARS_FOR_TX(membase));
	/*  Clear TX_READY interrupt  */
	writel(MSM_BOOT_UART_DM_GCMD_RES_TX_RDY_INT, MSM_BOOT_UART_DM_CR(membase));
//	asm volatile("syncht;");
	writel((unsigned long) ((unsigned char ) c), MSM_BOOT_UART_DM_TF(membase,0));
}


static int hvc_uart_dm_put_chars(uint32_t vt, const char *s, int n)
{
	int i;

	for (i=0; i<n; i++) {
		uart_dm_putc(membase, *s);
//		console_buffer[buffer_tail++] = *(s++);
//		if (buffer_tail >= PAGE_SIZE) {
//			buffer_tail = 0;
//		}
		s++;
	}

	return n;
}

static const struct hv_ops hvc_uart_dm_get_put_ops = {
	.get_chars = hvc_uart_dm_get_chars,
	.put_chars = hvc_uart_dm_put_chars,
};

static int __init hvc_uart_dm_console_init(void)
{
	membase = ioremap(hvcaddr_val, PAGE_SIZE);
	if (!membase) {
		printk("could not remap UART DM\n");
		return -EBUSY;
	}
#ifdef CONFIG_UART_DM_SLOWDOWN
	//  Maybe make this a config param
	writel(MSM_BOOT_UART_DM_CR_RX_DISABLE | MSM_BOOT_UART_DM_CR_TX_DISABLE, MSM_BOOT_UART_DM_CR(membase));
	//  77 -- 19200
	//  99 -- 38400
	//  cc -- 115200
	writel(0x99, MSM_BOOT_UART_DM_CSR(membase));

	writel(MSM_BOOT_UART_DM_CMD_RESET_RX, MSM_BOOT_UART_DM_CR(membase));
	writel(MSM_BOOT_UART_DM_CMD_RESET_TX, MSM_BOOT_UART_DM_CR(membase));
	writel(MSM_BOOT_UART_DM_CR_RX_ENABLE | MSM_BOOT_UART_DM_CR_TX_ENABLE, MSM_BOOT_UART_DM_CR(membase));
#endif

#ifdef CONFIG_HEXAGON_DINI
	writel(0x0, MSM_BOOT_UART_DM_MR1(membase));	// what?
	writel(0x34, MSM_BOOT_UART_DM_MR2(membase));	// 8-n-1
	writel(0xBB, MSM_BOOT_UART_DM_CSR(membase));	// maybe juice this later
	writel(0x20, MSM_BOOT_UART_DM_CR(membase));	// reset tx
#endif
	msm_boot_uart_dm_init_rx_transfer(membase);

	hvc_instantiate(0, 0, &hvc_uart_dm_get_put_ops);
	add_preferred_console("hvc", 0, NULL);
	return 0;
}
console_initcall(hvc_uart_dm_console_init);

static int __init hvcsetup(char *opt)
{
	kstrtol(opt, 16, &hvcaddr_val);
}
__setup("hvcaddr=", hvcsetup);

static int __init hvc_uart_dm_init(void)
{
	struct hvc_struct *s = hvc_alloc(0, 0, &hvc_uart_dm_get_put_ops, 128);
	printk("hvc addr is %lx\n", hvcaddr_val);

//	console_buffer = get_zeroed_page(GFP_KERNEL);

	return IS_ERR(s) ? PTR_ERR(s) : 0;
}
device_initcall(hvc_uart_dm_init);
