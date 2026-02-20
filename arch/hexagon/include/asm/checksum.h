/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_CHECKSUM_H
#define _ASM_CHECKSUM_H

#define do_csum	do_csum
unsigned int do_csum(const void *voidptr, int len);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
#define csum_tcpudp_nofold csum_tcpudp_nofold
__wsum csum_tcpudp_nofold(__be32 saddr, __be32 daddr,
			  __u32 len, __u8 proto, __wsum sum);

#define csum_tcpudp_magic csum_tcpudp_magic
__sum16 csum_tcpudp_magic(__be32 saddr, __be32 daddr,
			  __u32 len, __u8 proto, __wsum sum);

/*
 * Hexagon does not support unaligned loads, so provide an
 * arch-specific implementation that handles potentially
 * misaligned in6_addr pointers safely.
 */
struct in6_addr;
#define _HAVE_ARCH_IPV6_CSUM
__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, __u8 proto, __wsum csum);

#include <asm-generic/checksum.h>

#endif
