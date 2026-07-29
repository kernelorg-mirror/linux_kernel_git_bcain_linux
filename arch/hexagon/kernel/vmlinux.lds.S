/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linker script for Hexagon kernel
 *
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <asm-generic/vmlinux.lds.h>
#include <asm/asm-offsets.h>	/*  Most of the kernel defines are here  */
#include <asm/mem-layout.h>	/*  except for page_offset  */
#include <asm/cache.h>		/*  and now we're pulling cache line size  */
#include <asm/thread_info.h>	/*  and we need THREAD_SIZE too */

OUTPUT_ARCH(hexagon)
ENTRY(stext)

jiffies = jiffies_64;

/*
See asm-generic/vmlinux.lds.h for expansion of some of these macros.
See asm-generic/sections.h for seemingly required labels.
*/

#ifndef PAGE_SIZE
#define PAGE_SIZE _PAGE_SIZE
#endif

SECTIONS
{
	. = LOAD_OFFSET;

	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)

	__init_end = .;

        . = ALIGN(_PAGE_SIZE);
	_stext = .;
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		_text = .;
		TEXT_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		SCHED_TEXT
		LOCK_TEXT
		KPROBES_TEXT
		*(.fixup)
	}
	_etext = .;

	/*
	 * Capture orphan data sections before PERCPU to ensure
	 * PERCPU starts at a page-aligned address.
	 * The .modinfo section is normally discarded but needs explicit handling.
	 */
	.modinfo : AT(ADDR(.modinfo) - LOAD_OFFSET) {
		*(.modinfo)
	}

	/* PERCPU must be page-aligned for v5.2+ kernel */
	PERCPU_SECTION(PAGE_SIZE)

	INIT_DATA_SECTION(PAGE_SIZE)

	_sdata = .;
		RW_DATA(32,PAGE_SIZE,_THREAD_SIZE)
		RO_DATA(PAGE_SIZE)
	_edata = .;

	EXCEPTION_TABLE(16)

	BSS_SECTION(_PAGE_SIZE, _PAGE_SIZE, _PAGE_SIZE)

	_end = .;

	STABS_DEBUG
	DWARF_DEBUG
	MODINFO
	ELF_DETAILS
	.hexagon.attributes 0 : { *(.hexagon.attributes) }

	DISCARDS
}
