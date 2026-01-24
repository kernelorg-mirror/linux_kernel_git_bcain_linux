/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linker script for Hexagon kernel
 *
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <asm/mem-layout.h>

#define LOAD_OFFSET (PAGE_OFFSET+ELF_LOAD_ADDRESS)

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

/*
 * ELF file will sort of look like it's loaded at zero, although
 * it may not be.  Will be fixed up in the kernel "segtable".
 * I like "page directory" better.
 */

SECTIONS
{
	. = LOAD_OFFSET;

	__init_begin = .;
	HEAD_TEXT_SECTION
	INIT_TEXT_SECTION(PAGE_SIZE)
	PERCPU_SECTION(L1_CACHE_BYTES)

	. = ALIGN(4);
	.arch.info.init : AT(ADDR(.arch.info.init) - LOAD_OFFSET) {
	__arch_info_begin = .;
		*(.arch.info.init)
	__arch_info_end = .;
	}

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

	INIT_DATA_SECTION(PAGE_SIZE)

	_sdata = .;
		RW_DATA(32,PAGE_SIZE,_THREAD_SIZE)
		RO_DATA(PAGE_SIZE)
	_edata = .;

	EXCEPTION_TABLE(16)

	BSS_SECTION(_PAGE_SIZE, _PAGE_SIZE, _PAGE_SIZE)


#if defined(CONFIG_MINIVM)
	. = ALIGN(1<<16);
	_linux_end = .;
	. = 0xffff0000;
	vm_bootup = _linux_end - LOAD_OFFSET;
	.vm_data : AT(_linux_end - LOAD_OFFSET) {
		*(.vm_data)
	}
	_end = _linux_end + (1<<16);
#else
	_end = .;
#endif

	STABS_DEBUG
	DWARF_DEBUG
	MODINFO
	ELF_DETAILS
	.hexagon.attributes 0 : { *(.hexagon.attributes) }

	DISCARDS
}
