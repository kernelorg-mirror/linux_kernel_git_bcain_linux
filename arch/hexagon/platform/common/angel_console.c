/*
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/init.h>
#include <linux/console.h>
#include <linux/kernel.h>  /*  printk  */
#include <asm/angel_console.h>
#include <asm/platform.h>
/*
 * In the intermediate transition from simulator to hardware,
 * we can't use the extended Angel API, and need to use the
 * basic, pointer-based console output. Unfortunately, the
 * simulator implementation of this bypasses the TLB while
 * the hardware platform version plays through the map.
 * So we use a different method, depending on where we're
 * running...
 */

static char	cr = '\r';

unsigned int on_simulator;  /*  fixme  */

void angel_write(struct console *c, const char *s, unsigned n)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (!on_simulator) {
			/*
			 * On hardware development platform, cr isn't automatic
			 * with nl
			 */

			if (*s == '\n') {
				asm volatile("R0=#0x43;"
					"R1=%0;"
					"trap0(#0);"
					:
					: "r" (cr)
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
		} else {
			/* On the sim, do call-by-value with no cr */
			asm volatile("R0=#0x43;"
				"R1=%0;"
				"trap0(#0);"
				:
				: "r" (*s)
				: "r0", "r1", "r2", "r3", "r4", "r5"
			);
		}
		s++;
	}

}

static struct console angel_cons_info = {
	.name	= "angel",
	.write	= angel_write,
	.flags	= CON_PRINTBUFFER | CON_BOOT,
	.index	= -1,
};

void __init register_angel_console(void)
{
	register_console(&angel_cons_info);
}


