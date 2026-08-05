/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KASAN shadow memory layout for Hexagon.
 *
 * Copyright (C) 2026 Qualcomm Innovation Center, Inc.
 */
#ifndef _ASM_HEXAGON_KASAN_H
#define _ASM_HEXAGON_KASAN_H

#ifdef CONFIG_KASAN

#include <asm/mem-layout.h>

/* One shadow byte per eight bytes of memory. */
#define KASAN_SHADOW_SCALE_SHIFT	3

/*
 * The shadow covers the whole kernel half of the address space, from
 * PAGE_OFFSET up to KASAN_KERNEL_END -- everything below the 4MB segments
 * that setup_arch_memory() leaves alone for device I/O and the hypervisor.
 * At one shadow byte per eight bytes that is an eighth of the range, and it
 * is carved off the top of the kernel map, above vmalloc:
 *
 *   PAGE_OFFSET ... high_memory      linear map
 *               ... KASAN_SHADOW_START  vmalloc
 *               ... KASAN_SHADOW_END    shadow (this region)
 *               ... 0xffffffff        device I/O, hypervisor
 *
 * The shadow is itself inside the shadowed range, so it has a shadow of its
 * own; kasan_init() leaves that pointing at the all-zero early region, which
 * reads as "addressable" and is never poisoned.
 *
 * Everything here is 4MB aligned because that is the granularity of a
 * Hexagon page directory entry, which is what the shadow is mapped with.
 */
#define KASAN_KERNEL_END	_AC(0xfe000000, UL)
#define KASAN_SHADOW_SIZE	((KASAN_KERNEL_END - PAGE_OFFSET) >> 3)
#define KASAN_SHADOW_END	KASAN_KERNEL_END
#define KASAN_SHADOW_START	(KASAN_SHADOW_END - KASAN_SHADOW_SIZE)

/*
 * shadow = (addr >> 3) + KASAN_SHADOW_OFFSET.  Must match the value handed
 * to the compiler as -mllvm -asan-mapping-offset= (see arch/hexagon/Kconfig).
 */
#define KASAN_SHADOW_OFFSET	_AC(CONFIG_KASAN_SHADOW_OFFSET, UL)

#ifndef __ASSEMBLY__
void kasan_init(void);
#endif

#else /* CONFIG_KASAN */

#ifndef __ASSEMBLY__
static inline void kasan_init(void) { }
#endif

#endif /* CONFIG_KASAN */

#endif /* _ASM_HEXAGON_KASAN_H */
