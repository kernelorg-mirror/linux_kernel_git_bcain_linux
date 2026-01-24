/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Platform support for the Hexagon architecture
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#ifndef _ASM_HEXAGON_PLATFORM_H
#define _ASM_HEXAGON_PLATFORM_H

#include <asm/mach_desc.h>

extern unsigned long bootmem_lastpg;
extern u32 dt_blob_start;

extern void early_memtest(unsigned long start, unsigned long end);

#endif
