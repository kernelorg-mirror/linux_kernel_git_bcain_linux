/*
 * Hexagon hardware capability definitions
 *
 * Copyright (c) 2010-2024, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_HEXAGON_HWCAP_H
#define __ASM_HEXAGON_HWCAP_H

#include <uapi/asm/hwcap.h>

/* Kernel-internal definitions: ISA versions are used directly from UAPI */

#ifndef __ASSEMBLER__
/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP		(elf_hwcap)

extern unsigned long elf_hwcap;
#endif

#endif /* __ASM_HEXAGON_HWCAP_H */
