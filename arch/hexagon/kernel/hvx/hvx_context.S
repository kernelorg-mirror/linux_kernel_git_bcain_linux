/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HVX vector context save/restore for Hexagon
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <asm/asm-offsets.h>

.text
/* void save_hvx_context(struct HVX_Vectors *); */
	.p2align 2
	.globl save_hvx_context
	.type	save_hvx_context, @function
#define ZEROS  V0
#define SELECT V1
#define MUXREG V2
#define PREDS  V3
save_hvx_context:
	{
		R1 = add(R0, #(_PT_V0));
		R6 = #00000000;
		R7 = ##0x01010101;
	}
	{
		vmem(R1++#1) = V0;
		ZEROS = vsplat(R6);
		R6 = #1;
	}
	{
		vmem(R1++#1) = V1;
		SELECT = vsplat(R7);
	}
	{
		vmem(R1++#1) = V2;
		MUXREG = vmux(Q0, SELECT, ZEROS);
		SELECT.w = vasl(SELECT.w, R6);  /* 0x02020202  */
	}
	{
		vmem(R1++#1) = V3;
		PREDS = vmux(Q1, SELECT, ZEROS);
		SELECT.w = vasl(SELECT.w, R6);  /* 0x04040404  */
	}
	{
		vmem(R1++#1) = V4;
		MUXREG = vmux(Q2, SELECT, ZEROS);
		PREDS = vor(PREDS, MUXREG);
		SELECT.w = vasl(SELECT.w, R6);  /* 0x08080808  */
	}
	{
		vmem(R1++#1) = V5;
		MUXREG = vmux(Q3, SELECT, ZEROS);
		PREDS = vor(PREDS, MUXREG);
		R6 = add(R0, #(_PT_V0));
	}
	{
		V0 = vmem(R6++#1); /* start undoing the damage */
		vmem(R1++#1) = V6;
		PREDS = vor(PREDS, MUXREG);  /* PREDS has all predicates now */
		R7 = add(R0, #(_PT_VECPREDSAVE));
	}
	{
		V1 = vmem(R6++#1);
		vmem(R1++#1) = V7
	}
	{
		V2 = vmem(R6++#1);
		vmem(R1++#1) = V8
	}
	{
		V3 = vmem(R6++#1);
		vmem(R7) = PREDS;
	}
#undef ZEROS
#undef SELECT
#undef MUXREG
#undef PREDS
	vmem(R1++#1) = V9
	vmem(R1++#1) = V10
	vmem(R1++#1) = V11
	vmem(R1++#1) = V12
	vmem(R1++#1) = V13
	vmem(R1++#1) = V14
	vmem(R1++#1) = V15
	vmem(R1++#1) = V16
	vmem(R1++#1) = V17
	vmem(R1++#1) = V18
	vmem(R1++#1) = V19
	vmem(R1++#1) = V20
	vmem(R1++#1) = V21
	vmem(R1++#1) = V22
	vmem(R1++#1) = V23
	vmem(R1++#1) = V24
	vmem(R1++#1) = V25
	vmem(R1++#1) = V26
	vmem(R1++#1) = V27
	vmem(R1++#1) = V28
	vmem(R1++#1) = V29
	vmem(R1++#1) = V30
	vmem(R1++#1) = V31

	jumpr R31
	.size	save_hvx_context, .-save_hvx_context

/* void restore_hvx_context(struct HVX_Vectors *); */
	.p2align 2
	.globl restore_hvx_context
	.type	restore_hvx_context, @function
#define PREDS  V30
#define MASK   V31
#define CMPREG V29
restore_hvx_context:
	{
		R0 = add(R0, #(_PT_V0));
		R6 = add(R0, #(_PT_VECPREDSAVE));
		R8 = ##0x01010101;
	}
	{
		PREDS = vmem(R6);
		MASK = vsplat(R8);
		R7 = #1;
	}
	{
		CMPREG = vand(PREDS, MASK);
		V0 = vmem(R0++#1);
	}
	{
		Q0 = vcmp.eq(CMPREG.b, MASK.b);
		V1 = vmem(R0++#1);
		MASK.w = vasl(MASK.w, R7);  /* 0x02020202 */
	}
	{
		CMPREG = vand(PREDS, MASK);
		V2 = vmem(R0++#1);
	}
	{
		Q1 = vcmp.eq(CMPREG.b, MASK.b);
		V3 = vmem(R0++#1);
		MASK.w = vasl(MASK.w, R7);  /* 0x04040404 */
	}
	{
		CMPREG = vand(PREDS, MASK);
		V4 = vmem(R0++#1)
	}
	{
		Q2 = vcmp.eq(CMPREG.b, MASK.b);
		V5 = vmem(R0++#1)
		MASK.w = vasl(MASK.w, R7);  /* 0x04040404 */
	}
	{
		CMPREG = vand(PREDS, MASK);
		V6 = vmem(R0++#1)
	}
	{
		Q3 = vcmp.eq(CMPREG.b, MASK.b);
		V7 = vmem(R0++#1)
	}
#undef PREDS
#undef MASK
#undef CMPREG
	V8 = vmem(R0++#1)
	V9 = vmem(R0++#1)
	V10 = vmem(R0++#1)
	V11 = vmem(R0++#1)
	V12 = vmem(R0++#1)
	V13 = vmem(R0++#1)
	V14 = vmem(R0++#1)
	V15 = vmem(R0++#1)
	V16 = vmem(R0++#1)
	V17 = vmem(R0++#1)
	V18 = vmem(R0++#1)
	V19 = vmem(R0++#1)
	V20 = vmem(R0++#1)
	V21 = vmem(R0++#1)
	V22 = vmem(R0++#1)
	V23 = vmem(R0++#1)
	V24 = vmem(R0++#1)
	V25 = vmem(R0++#1)
	V26 = vmem(R0++#1)
	V27 = vmem(R0++#1)
	V28 = vmem(R0++#1)
	V29 = vmem(R0++#1)
	V30 = vmem(R0++#1)
	V31 = vmem(R0++#1)

	jumpr R31
	.size	restore_hvx_context, .-restore_hvx_context
