/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef HEXAGON_ASM_USER_H
#define HEXAGON_ASM_USER_H

/*
 * Layout for registers passed in elf core dumps to userspace.
 *
 * Basically a rearranged subset of "pt_regs".
 *
 * Interested parties:  libc, gdb...
 */

struct user_regs_struct {
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long r14;
	unsigned long r15;
	unsigned long r16;
	unsigned long r17;
	unsigned long r18;
	unsigned long r19;
	unsigned long r20;
	unsigned long r21;
	unsigned long r22;
	unsigned long r23;
	unsigned long r24;
	unsigned long r25;
	unsigned long r26;
	unsigned long r27;
	unsigned long r28;
	unsigned long r29;
	unsigned long r30;
	unsigned long r31;
	unsigned long sa0;
	unsigned long lc0;
	unsigned long sa1;
	unsigned long lc1;
	unsigned long m0;
	unsigned long m1;
	unsigned long usr;
	unsigned long p3_0;
	unsigned long gp;
	unsigned long ugp;
	unsigned long pc;
	unsigned long cause;
	unsigned long badva;
	/* cs0 and cs1 are only available with HEXAGON_ARCH_VERSION >= 4 */
	unsigned long cs0;
	unsigned long cs1;
	unsigned long pad1;  /* pad out to 48 words total */
};

struct user_fpregs_struct {
	HVX_Vector v0;
	HVX_Vector v1;
	HVX_Vector v2;
	HVX_Vector v3;
	HVX_Vector v4;
	HVX_Vector v5;
	HVX_Vector v6;
	HVX_Vector v7;
	HVX_Vector v8;
	HVX_Vector v9;
	HVX_Vector v10;
	HVX_Vector v11;
	HVX_Vector v12;
	HVX_Vector v13;
	HVX_Vector v14;
	HVX_Vector v15;
	HVX_Vector v16;
	HVX_Vector v17;
	HVX_Vector v18;
	HVX_Vector v19;
	HVX_Vector v20;
	HVX_Vector v21;
	HVX_Vector v22;
	HVX_Vector v23;
	HVX_Vector v24;
	HVX_Vector v25;
	HVX_Vector v26;
	HVX_Vector v27;
	HVX_Vector v28;
	HVX_Vector v29;
	HVX_Vector v30;
	HVX_Vector v31;
	/* hvx vector predicate registers */
	HVX_Vector vecpredsave;
};

#endif
