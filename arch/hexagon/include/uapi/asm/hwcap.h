/*
 * Hexagon hardware capability definitions - User API
 *
 * Copyright Qualcomm Technologies, Inc. and its subsidiaries
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

#ifndef _UAPI__ASM_HEXAGON_HWCAP_H
#define _UAPI__ASM_HEXAGON_HWCAP_H

/*
 * HWCAP flags - for AT_HWCAP
 *
 * ISA Version Encoding:
 * Bits 0-6 encode the ISA version as enumeration values (7-bit field, supports 128 ISA variants)
 * This efficient encoding maps all released Hexagon ISA versions to sequential enum values.
 * Only essential features are included to keep the interface minimal.
 */

/* ISA version encoding in bits 0-6 (7 bits) */
#define HWCAP_HEXAGON_ISA_MASK	0x7F	/* ISA version mask */

/* ISA version enumeration values */
#define HWCAP_HEXAGON_ISA_V2	1	/* Hexagon V2 */
#define HWCAP_HEXAGON_ISA_V3	2	/* Hexagon V3 */
#define HWCAP_HEXAGON_ISA_V4	3	/* Hexagon V4 */
#define HWCAP_HEXAGON_ISA_V5	4	/* Hexagon V5 */
#define HWCAP_HEXAGON_ISA_V55	5	/* Hexagon V55 */
#define HWCAP_HEXAGON_ISA_V60	6	/* Hexagon V60 */
#define HWCAP_HEXAGON_ISA_V62	7	/* Hexagon V62 */
#define HWCAP_HEXAGON_ISA_V65	8	/* Hexagon V65 */
#define HWCAP_HEXAGON_ISA_V66	9	/* Hexagon V66 */
#define HWCAP_HEXAGON_ISA_V67	10	/* Hexagon V67 */
#define HWCAP_HEXAGON_ISA_V68	11	/* Hexagon V68 */
#define HWCAP_HEXAGON_ISA_V69	12	/* Hexagon V69 */
#define HWCAP_HEXAGON_ISA_V71	13	/* Hexagon V71 */
#define HWCAP_HEXAGON_ISA_V73	14	/* Hexagon V73 */
#define HWCAP_HEXAGON_ISA_V79	15	/* Hexagon V79 */
/* Future versions can be added as 16, 17, 18, etc. */

/* Essential feature flags */
#define HWCAP_HEXAGON_HVX		(1 << 7)	/* HVX (Hexagon Vector eXtensions) */
#define HWCAP_HEXAGON_CABAC		(1 << 8)	/* CABAC acceleration */
#define HWCAP_HEXAGON_HVX_LENGTH_128B	(1 << 9)	/* HVX 128-byte vector length */
#define HWCAP_HEXAGON_HVX_IEEE_FP	(1 << 10)	/* HVX IEEE floating point */
#define HWCAP_HEXAGON_AUDIO		(1 << 11)	/* Audio ISA extensions */

/* Bits 12-31 reserved for future use */

/* Utility macros for userspace applications */
#define HWCAP_HEXAGON_GET_ISA(hwcap)		((hwcap) & HWCAP_HEXAGON_ISA_MASK)
#define HWCAP_HEXAGON_IS_ISA(hwcap, version)	(HWCAP_HEXAGON_GET_ISA(hwcap) == (version))
#define HWCAP_HEXAGON_HAS_ISA(hwcap, version)	(HWCAP_HEXAGON_GET_ISA(hwcap) >= (version))

/*
 * HWCAP2 is not currently used by Hexagon.
 * Future hardware features can be added to the remaining HWCAP bits (8-31)
 * or to HWCAP2 if more space is needed.
 */

#endif /* _UAPI__ASM_HEXAGON_HWCAP_H */
