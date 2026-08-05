// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Qualcomm Innovation Center, Inc.
 *
 * Hexagon has hand-written memcpy()/memset() but used to take memmove() from
 * lib/string.c.  KASAN needs an arch memmove(): the compiler turns memmove()
 * in instrumented code into __asan_memmove(), and mm/kasan/shadow.c only
 * provides that wrapper -- which calls __memmove() -- when the architecture
 * declares __HAVE_ARCH_MEMMOVE.
 *
 * This file is built without instrumentation (see the Makefile), which is
 * what __memmove() has to be.
 */

#include <linux/export.h>
#include <linux/string.h>
#include <linux/types.h>

void *__memmove(void *dest, const void *src, size_t count)
{
	char *d = dest;
	const char *s = src;

	if (d == s || !count)
		return dest;

	if (d < s) {
		while (count--)
			*d++ = *s++;
	} else {
		d += count;
		s += count;
		while (count--)
			*--d = *--s;
	}

	return dest;
}
EXPORT_SYMBOL(__memmove);

/* <asm/string.h> redirects memmove() to __memmove() for uninstrumented code. */
#undef memmove
void *memmove(void *dest, const void *src, size_t count) __alias(__memmove);
EXPORT_SYMBOL(memmove);
