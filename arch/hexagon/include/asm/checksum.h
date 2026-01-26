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

#define ip_fast_csum ip_fast_csum
__sum16 ip_fast_csum(const void *iph, unsigned int ihl);

#define ip_compute_csum ip_compute_csum
__sum16 ip_compute_csum(const void *buff, int len);

#define _HAVE_ARCH_CSUM_AND_COPY
__wsum csum_partial_copy_nocheck(const void *src, void *dst, int len);

#include <asm-generic/checksum.h>

#endif
