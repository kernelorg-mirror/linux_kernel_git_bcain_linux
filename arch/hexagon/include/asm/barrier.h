/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Memory barrier definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_HEXAGON_BARRIER_H
#define _ASM_HEXAGON_BARRIER_H

/*
 * Hexagon is strongly ordered within a single hardware thread, but for SMP
 * configurations explicit barrier instructions are required to ensure memory
 * operations are globally visible in the correct order.
 *
 * The "syncht" instruction drains all posted memory transactions, ensuring
 * prior stores are visible to other threads before subsequent memory
 * operations proceed.
 */
#define __mb()		({ asm volatile("syncht" ::: "memory"); })
#define __rmb()		__mb()
#define __wmb()		__mb()

#include <asm-generic/barrier.h>

#endif /* _ASM_HEXAGON_BARRIER_H */
