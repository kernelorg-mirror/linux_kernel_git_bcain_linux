// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon Angel semihosting misc functions
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#include <linux/init.h>
#include <asm/angel_console.h>
#include <asm/mem-layout.h>
#include <asm/page.h>

void __init angel_get_command_line(char *s, unsigned int n)
{
	unsigned long message[2];
	/*
	 * According to the Angel spec, this operation returns
	 * a success/failure (0/-1) code and a pointer to the
	 * string in the string buffer.  We don't seem to get
	 * either value in the Hexagon simulator environment,
	 * so while we'll try to obtain the values, we won't
	 * inspect them, and instead blindly assume success.
	 */
	unsigned long rv0;
	unsigned long rv1;

	/*
	 * Protocol says pass a pointer to a two-word block
	 * containing buffer address and max length.  Simulator
	 * sees through the TLB memory map, but pass the physical
	 * address just the same.
	 */

	message[0] = __pa(s);
	message[1] = n;

	asm volatile("R0=#0x15;"
		"R1=%2;"
		"trap0(#0);"
		"%0 = R0;"
		"%1 = R1;"
		: "=r" (rv0), "=r" (rv1)
		: "r" (message)
		: "r0", "r1", "r2", "r3", "r4", "r5");
}

