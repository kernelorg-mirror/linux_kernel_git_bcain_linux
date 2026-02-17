/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * xchg/cmpxchg operations for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_CMPXCHG_H
#define _ASM_CMPXCHG_H

#include <linux/build_bug.h>

/*
 * __xchg_word - atomically exchange a 32-bit register and memory location
 */
static inline unsigned long
__xchg_word(unsigned long x, volatile void *ptr)
{
	unsigned long retval;

	__asm__ __volatile__ (
	"1:	%0 = memw_locked(%1);\n"
	"	memw_locked(%1,P0) = %2;\n"
	"	if (!P0) jump 1b;\n"
	: "=&r" (retval)
	: "r" (ptr), "r" (x)
	: "memory", "p0"
	);
	return retval;
}

/*
 * __xchg_small - atomically exchange a sub-word (1 or 2 byte) value.
 *
 * Hexagon only has word-sized LL/SC (memw_locked), so we align down to
 * the containing word, load it with LL, replace the target byte(s),
 * and store back with SC.
 */
static inline unsigned long
__xchg_small(unsigned long x, volatile void *ptr, int size)
{
	unsigned long aligned, shift, mask, old32, new32;

	aligned = (unsigned long)ptr & ~3UL;
	shift = ((unsigned long)ptr & 3) * 8;
	mask = ((1UL << (size * 8)) - 1) << shift;
	x = (x << shift) & mask;

	__asm__ __volatile__ (
	"1:	%0 = memw_locked(%2);\n"
	"	%1 = and(%0, %4);\n"		/* clear target bits */
	"	%1 = or(%1, %3);\n"		/* insert new value */
	"	memw_locked(%2, P0) = %1;\n"
	"	if (!P0) jump 1b;\n"
	: "=&r" (old32), "=&r" (new32)
	: "r" (aligned), "r" (x), "r" (~mask)
	: "memory", "p0"
	);
	return (old32 & mask) >> shift;
}

static inline unsigned long
__arch_xchg(unsigned long x, volatile void *ptr, int size)
{
	switch (size) {
	case 1:
	case 2:
		return __xchg_small(x, ptr, size);
	case 4:
		return __xchg_word(x, ptr);
	default:
		BUILD_BUG();
	}
}

/*
 * Atomically swap the contents of a register with memory.  Should be atomic
 * between multiple CPU's and within interrupts on the same CPU.
 */
#define arch_xchg(ptr, v) ((__typeof__(*(ptr)))__arch_xchg((unsigned long)(v), (ptr), \
							   sizeof(*(ptr))))

/*
 * __cmpxchg_word - atomic compare-and-exchange for 32-bit values.
 */
static inline unsigned long
__cmpxchg_word(volatile void *ptr, unsigned long old, unsigned long new)
{
	unsigned long oldval;

	__asm__ __volatile__ (
	"1:	%0 = memw_locked(%1);\n"
	"	{ P0 = cmp.eq(%0,%2);\n"
	"	  if (!P0.new) jump:nt 2f; }\n"
	"	memw_locked(%1,p0) = %3;\n"
	"	if (!P0) jump 1b;\n"
	"2:\n"
	: "=&r" (oldval)
	: "r" (ptr), "r" (old), "r" (new)
	: "memory", "p0"
	);
	return oldval;
}

/*
 * __cmpxchg_small - atomic compare-and-exchange for sub-word (1/2 byte) values.
 *
 * Hexagon only has word-sized LL/SC (memw_locked), so we align down to
 * the containing word, load it with LL, compare/replace the target byte(s)
 * within the word, and store back with SC.
 */
static inline unsigned long
__cmpxchg_small(volatile void *ptr, unsigned long old, unsigned long new,
		int size)
{
	unsigned long aligned, shift, mask, old32;
	unsigned long old_shifted, new_shifted, tmp;

	aligned = (unsigned long)ptr & ~3UL;
	shift = ((unsigned long)ptr & 3) * 8;
	mask = ((1UL << (size * 8)) - 1) << shift;
	old_shifted = (old << shift) & mask;
	new_shifted = (new << shift) & mask;

	__asm__ __volatile__ (
	"1:	%0 = memw_locked(%2);\n"
	"	%1 = and(%0, %3);\n"		/* extract target bits */
	"	{ P0 = cmp.eq(%1,%4);\n"
	"	  if (!P0.new) jump:nt 2f; }\n"
	"	%1 = and(%0, %5);\n"		/* clear target bits */
	"	%1 = or(%1, %6);\n"		/* insert new value */
	"	memw_locked(%2, P0) = %1;\n"
	"	if (!P0) jump 1b;\n"
	"2:\n"
	: "=&r" (old32), "=&r" (tmp)
	: "r" (aligned), "r" (mask), "r" (old_shifted),
	  "r" (~mask), "r" (new_shifted)
	: "memory", "p0"
	);
	return (old32 & mask) >> shift;
}

static inline unsigned long
__arch_cmpxchg(volatile void *ptr, unsigned long old, unsigned long new,
	       int size)
{
	switch (size) {
	case 1:
	case 2:
		return __cmpxchg_small(ptr, old, new, size);
	case 4:
		return __cmpxchg_word(ptr, old, new);
	default:
		BUILD_BUG();
	}
}

/*
 *  see rt-mutex-design.txt; cmpxchg supposedly checks if *ptr == A and swaps.
 *  looks just like atomic_cmpxchg on our arch currently with a bunch of
 *  variable casting.
 */
#define arch_cmpxchg(ptr, old, new)					\
({									\
	(__typeof__(*(ptr)))__arch_cmpxchg((ptr),			\
					  (unsigned long)(old),		\
					  (unsigned long)(new),		\
					  sizeof(*(ptr)));		\
})

#endif /* _ASM_CMPXCHG_H */
