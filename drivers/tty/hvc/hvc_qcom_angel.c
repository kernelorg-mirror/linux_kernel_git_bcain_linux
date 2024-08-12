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

#include <asm/angel_console.h>
#include "hvc_console.h"

/* 
 * Angel is nice but it kills performance; sometimes 
 * we stop the angel servicing but still want to look
 * at output.
 */

char angel_buffer[4096];
static int  buffer_tail;

static int hvc_angel_put_chars(uint32_t vt, const char *s, int n)
{
	int i;

	angel_write(NULL, s, n);

	for (i=0; i<n; i++) {
		angel_buffer[buffer_tail++] = *(s++);
		if (buffer_tail >= 4096) {
			buffer_tail = 0;
		}
	}

	return n;
}

static int hvc_angel_get_chars(uint32_t vt, char *buf, int count)
{
	return 0;
}

static const struct hv_ops hvc_angel_get_put_ops = {
	.get_chars = hvc_angel_get_chars,
	.put_chars = hvc_angel_put_chars,
};

static int __init hvc_angel_console_init(void)
{
	hvc_instantiate(0, 0, &hvc_angel_get_put_ops);
	add_preferred_console("hvc", 0, NULL);
	return 0;
}
console_initcall(hvc_angel_console_init);

static int __init hvc_angel_init(void)
{
	struct hvc_struct *s = hvc_alloc(0, 0, &hvc_angel_get_put_ops, 128);
	return IS_ERR(s) ? PTR_ERR(s) : 0;
}
device_initcall(hvc_angel_init);
