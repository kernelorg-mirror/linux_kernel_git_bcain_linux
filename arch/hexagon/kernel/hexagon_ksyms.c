/*
 * Export of symbols defined in assembly files and/or libgcc.
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <asm/hexagon_vm.h>
#include <asm/io.h>
#include <asm/notify.h>
#include <linux/uaccess.h>

/* Additional functions */
EXPORT_SYMBOL(__clear_user_hexagon);
EXPORT_SYMBOL(raw_copy_from_user);
EXPORT_SYMBOL(raw_copy_to_user);
EXPORT_SYMBOL(__iounmap);
EXPORT_SYMBOL(__strnlen_user);
EXPORT_SYMBOL(vmgetie_cached);
EXPORT_SYMBOL(vmsetie_cached);
EXPORT_SYMBOL(__vmyield);
EXPORT_SYMBOL(__vmhwconfig);  //  yikes
EXPORT_SYMBOL(empty_zero_page);
EXPORT_SYMBOL(ioremap_nocache);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(blocking_thread_register_notify);
EXPORT_SYMBOL(atomic_thread_register_notify);
/* Additional variables */
EXPORT_SYMBOL(__phys_offset);
EXPORT_SYMBOL(_dflt_cache_att);
EXPORT_SYMBOL(bad_dma_address);

#define DECLARE_EXPORT(name)     \
	extern void name(void); EXPORT_SYMBOL(name)

/* Symbols found in libgcc that assorted kernel modules need */
DECLARE_EXPORT(__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes);

/* Additional functions */
DECLARE_EXPORT(csum_tcpudp_magic);

/* libgcc symbol names changes */
#if (defined(CONFIG_HEXAGON_LLVM) || (3 == __GNUC__) || (4 == __GNUC__ && 4 == __GNUC_MINOR__)) || (__clang__ == 1)
DECLARE_EXPORT(__hexagon_divsi3);
DECLARE_EXPORT(__hexagon_modsi3);
DECLARE_EXPORT(__hexagon_udivsi3);
DECLARE_EXPORT(__hexagon_umodsi3);
#else
DECLARE_EXPORT(__divsi3);
DECLARE_EXPORT(__modsi3);
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__umodsi3);
#endif
