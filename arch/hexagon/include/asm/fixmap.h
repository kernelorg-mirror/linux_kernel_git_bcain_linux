/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Fixmap support for Hexagon - enough to support highmem features
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_FIXMAP_H
#define _ASM_FIXMAP_H

/*
 * A lot of the fixmap info is already in mem-layout.h
 */
#include <linux/types.h>
#include <asm/page.h>
#include <asm/mem-layout.h>

#define __early_set_fixmap __set_fixmap
#define __late_set_fixmap __set_fixmap
#define __late_clear_fixmap(idx) __set_fixmap((idx), 0, FIXMAP_PAGE_CLEAR)

extern void __set_fixmap(enum fixed_addresses idx, phys_addr_t phys, pgprot_t prot);

#include <asm-generic/fixmap.h>

extern void __init early_fixmap_init(void);

#endif
