// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon platform initialization
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pfn.h>

#include <asm/platform.h>
#include <asm/angel_console.h>
#include <asm/irq.h>
#include <asm/page.h>

/* 256MB default */
__initdata unsigned long bootmem_lastpg = PFN_DOWN(1 << 28);
