// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon Angel semihosting console
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#include <linux/init.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <asm/angel_console.h>
#include <asm/platform.h>

static char cr = '\r';

/*
 * angel_write - output a string via Angel semihosting traps
 *
 * Angel trap0(#0) ABI:
 *   R0 = 0x03: write one character, R1 = pointer to the character
 *   R0 = 0x43: write one character (alternate), R1 = pointer to the character
 *
 * For newlines, emit a preceding \r for serial terminals.
 */
void angel_write(struct console *c, const char *s, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (*s == '\n') {
			asm volatile("R0=#0x43;"
				"R1=%0;"
				"trap0(#0);"
				:
				: "r" (&cr)
				: "r0", "r1", "r2", "r3", "r4", "r5"
			);
		}
		asm volatile("R0=#0x03;"
			"R1=%0;"
			"trap0(#0);"
			:
			: "r" ((long)(s))
			: "r0", "r1", "r2", "r3", "r4", "r5"
		);
		s++;
	}
}

static struct console angel_cons_info = {
	.name	= "angel",
	.write	= angel_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

void __init register_angel_console(void)
{
	register_console(&angel_cons_info);
}
