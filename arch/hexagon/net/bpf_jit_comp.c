// SPDX-License-Identifier: GPL-2.0
/*
 * Hexagon eBPF JIT compiler
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Based on the RV32 BPF JIT by Luke Nelson and Xi Wang, and the ARM32 BPF JIT
 * by Shubham Bansal and Mircea Gherzan.
 *
 * Hexagon is a 32-bit VLIW architecture; BPF is 64-bit.  Each BPF register
 * maps to a pair of Hexagon 32-bit registers {hi, lo}.  Every emitted
 * instruction is a single-instruction packet (PP=0b11).
 */

#include <linux/bpf.h>
#include <linux/filter.h>
#include <linux/math64.h>
#include "bpf_jit.h"

#define NR_JIT_ITERATIONS	32

/*
 * BPF-to-Hexagon register mapping -- {hi, lo} pairs.
 *
 * R14  = tail call counter (TCC), caller-saved -- must save around calls
 * R19  = reserved for thread_info (-ffixed-r19), never touched
 * R28  = JIT scratch register
 * R29  = SP, R30 = FP, R31 = LR
 */
enum {
	BPF_FP_HI,
	BPF_FP_LO,
	BPF_JIT_SCRATCH_REGS,
};

#define NR_SAVED_REGISTERS	14
#define STACK_OFFSET(k) (-(int)(4 + (4 * NR_SAVED_REGISTERS) + (4 * (k))))

#define TMP_REG_1  (MAX_BPF_JIT_REG + 0)
#define TMP_REG_2  (MAX_BPF_JIT_REG + 1)

#define HEX_REG_TCC  HEX_REG_R14

static const s8 bpf2hex[][2] = {
	[BPF_REG_0]  = {HEX_REG_R17, HEX_REG_R16},
	[BPF_REG_1]  = {HEX_REG_R1,  HEX_REG_R0},
	[BPF_REG_2]  = {HEX_REG_R3,  HEX_REG_R2},
	[BPF_REG_3]  = {HEX_REG_R5,  HEX_REG_R4},
	[BPF_REG_4]  = {HEX_REG_R7,  HEX_REG_R6},
	[BPF_REG_5]  = {HEX_REG_R9,  HEX_REG_R8},
	[BPF_REG_6]  = {HEX_REG_R21, HEX_REG_R20},
	[BPF_REG_7]  = {HEX_REG_R23, HEX_REG_R22},
	[BPF_REG_8]  = {HEX_REG_R25, HEX_REG_R24},
	[BPF_REG_9]  = {HEX_REG_R27, HEX_REG_R26},
	[BPF_REG_FP] = {STACK_OFFSET(BPF_FP_HI), STACK_OFFSET(BPF_FP_LO)},
	[BPF_REG_AX] = {HEX_REG_R11, HEX_REG_R10},
	[TMP_REG_1]  = {HEX_REG_R13, HEX_REG_R12},
	[TMP_REG_2]  = {HEX_REG_R15, HEX_REG_R14},
};

static s8 hi(const s8 *r) { return r[0]; }
static s8 lo(const s8 *r) { return r[1]; }
static bool is_stacked(s8 reg) { return reg < 0; }

/* ------------------------------------------------------------------ */
/* 64-bit div/mod helpers (called from JIT code)                       */
/* ------------------------------------------------------------------ */

static u64 jit_udiv64(u64 dividend, u64 divisor)
{
	return div64_u64(dividend, divisor);
}

static u64 jit_mod64(u64 dividend, u64 divisor)
{
	u64 rem;

	div64_u64_rem(dividend, divisor, &rem);
	return rem;
}

static s64 jit_sdiv64(s64 dividend, s64 divisor)
{
	return div64_s64(dividend, divisor);
}

static s64 jit_smod64(s64 dividend, s64 divisor)
{
	return dividend - div64_s64(dividend, divisor) * divisor;
}

/* ------------------------------------------------------------------ */
/* 32-bit div/mod helpers (called from JIT code)                       */
/* ------------------------------------------------------------------ */

static u32 jit_udiv32(u32 dividend, u32 divisor)
{
	return dividend / divisor;
}

static u32 jit_mod32(u32 dividend, u32 divisor)
{
	return dividend % divisor;
}

static s32 jit_sdiv32(s32 dividend, s32 divisor)
{
	return dividend / divisor;
}

static s32 jit_smod32(s32 dividend, s32 divisor)
{
	return dividend % divisor;
}

/* ------------------------------------------------------------------ */
/* Register spill / fill                                               */
/* ------------------------------------------------------------------ */

static const s8 *bpf_get_reg64(const s8 *reg, const s8 *tmp,
				struct hexagon_jit_context *ctx)
{
	if (is_stacked(hi(reg))) {
		emit(hex_l2_loadri_io(hi(tmp), HEX_REG_FP, hi(reg) / 4), ctx);
		emit(hex_l2_loadri_io(lo(tmp), HEX_REG_FP, lo(reg) / 4), ctx);
		reg = tmp;
	}
	return reg;
}

static void bpf_put_reg64(const s8 *reg, const s8 *src,
			   struct hexagon_jit_context *ctx)
{
	if (is_stacked(hi(reg))) {
		emit(hex_s2_storeri_io(HEX_REG_FP, hi(reg) / 4, hi(src)), ctx);
		emit(hex_s2_storeri_io(HEX_REG_FP, lo(reg) / 4, lo(src)), ctx);
	}
}

static const s8 *bpf_get_reg32(const s8 *reg, const s8 *tmp,
				struct hexagon_jit_context *ctx)
{
	if (is_stacked(lo(reg))) {
		emit(hex_l2_loadri_io(lo(tmp), HEX_REG_FP, lo(reg) / 4), ctx);
		reg = tmp;
	}
	return reg;
}

static void bpf_put_reg32(const s8 *reg, const s8 *src,
			   struct hexagon_jit_context *ctx)
{
	if (is_stacked(lo(reg))) {
		emit(hex_s2_storeri_io(HEX_REG_FP, lo(reg) / 4, lo(src)), ctx);
		if (!ctx->prog->aux->verifier_zext) {
			emit(hex_a2_tfrsi(HEX_REG_R28, 0), ctx);
			emit(hex_s2_storeri_io(HEX_REG_FP, hi(reg) / 4,
					       HEX_REG_R28), ctx);
		}
	} else if (!ctx->prog->aux->verifier_zext) {
		emit(hex_a2_tfrsi(hi(reg), 0), ctx);
	}
}

/* ------------------------------------------------------------------ */
/* Immediate materialization                                           */
/* ------------------------------------------------------------------ */

static void emit_imm(u8 rd, s32 imm, struct hexagon_jit_context *ctx)
{
	if (is_s16(imm)) {
		emit(hex_a2_tfrsi(rd, (s16)imm), ctx);
	} else {
		emit(hex_a2_tfril(rd, (u16)(imm & 0xffff)), ctx);
		emit(hex_a2_tfrih(rd, (u16)((imm >> 16) & 0xffff)), ctx);
	}
}

static void emit_imm32(const s8 *rd, s32 imm, struct hexagon_jit_context *ctx)
{
	emit_imm(lo(rd), imm, ctx);
	emit(hex_a2_tfrsi(hi(rd), (imm >= 0) ? 0 : -1), ctx);
}

static void emit_imm64(const s8 *rd, s32 imm_hi, s32 imm_lo,
		       struct hexagon_jit_context *ctx)
{
	emit_imm(lo(rd), imm_lo, ctx);
	emit_imm(hi(rd), imm_hi, ctx);
}

/* ------------------------------------------------------------------ */
/* 64-bit div/mod emission                                             */
/* ------------------------------------------------------------------ */

/*
 * Emit a call to a 64-bit div/mod helper.
 *
 * The helpers take (u64 dividend, u64 divisor) in R1:R0, R3:R2
 * and return a 64-bit result in R1:R0.
 *
 * We must save/restore R0-R5 since they overlap BPF_REG_1 through BPF_REG_3.
 * Also save TCC (R14, caller-saved) so it survives the call.
 */
static void emit_divmod64(const s8 *dst, const s8 *src, const s8 *tmp,
			   struct hexagon_jit_context *ctx,
			   bool is_mod, bool is_signed)
{
	const s8 *rd = bpf_get_reg64(dst, tmp, ctx);
	u32 addr;

	/* Save R0-R5 and TCC on the stack */
	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, -32), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 0, HEX_REG_R0), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 1, HEX_REG_R1), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 2, HEX_REG_R2), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 3, HEX_REG_R3), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 4, HEX_REG_R4), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 5, HEX_REG_R5), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 6, HEX_REG_TCC), ctx);

	/*
	 * Move dividend (dst) into R1:R0, divisor (src) into R3:R2.
	 * If a source register is in R0-R5, it was just saved to the stack
	 * and may be clobbered by a preceding move -- load from save area.
	 */
#define MOVE_OR_LOAD(to, from) do {					\
	if ((u8)(from) <= HEX_REG_R5)					\
		emit(hex_l2_loadri_io((to), HEX_REG_SP, (from)), ctx);	\
	else								\
		emit(hex_a2_tfr((to), (from)), ctx);			\
} while (0)

	MOVE_OR_LOAD(HEX_REG_R0, lo(rd));
	MOVE_OR_LOAD(HEX_REG_R1, hi(rd));
	MOVE_OR_LOAD(HEX_REG_R2, lo(src));
	MOVE_OR_LOAD(HEX_REG_R3, hi(src));

#undef MOVE_OR_LOAD

	/* Select helper */
	if (is_mod)
		addr = is_signed ? (u32)(uintptr_t)jit_smod64
				 : (u32)(uintptr_t)jit_mod64;
	else
		addr = is_signed ? (u32)(uintptr_t)jit_sdiv64
				 : (u32)(uintptr_t)jit_udiv64;

	emit_imm(HEX_REG_R28, addr, ctx);
	emit(hex_j2_callr(HEX_REG_R28), ctx);

	/* Move result R1:R0 into dst */
	emit(hex_a2_tfr(lo(rd), HEX_REG_R0), ctx);
	emit(hex_a2_tfr(hi(rd), HEX_REG_R1), ctx);

	/* Restore R0-R5 and TCC */
	emit(hex_l2_loadri_io(HEX_REG_R0, HEX_REG_SP, 0), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R1, HEX_REG_SP, 1), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R2, HEX_REG_SP, 2), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R3, HEX_REG_SP, 3), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R4, HEX_REG_SP, 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R5, HEX_REG_SP, 5), ctx);
	emit(hex_l2_loadri_io(HEX_REG_TCC, HEX_REG_SP, 6), ctx);
	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, 32), ctx);

	bpf_put_reg64(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* 32-bit div/mod emission                                             */
/* ------------------------------------------------------------------ */

/*
 * Emit a call to a 32-bit div/mod helper.
 *
 * The helpers take (u32 dividend, u32 divisor) in R0, R1
 * and return a 32-bit result in R0.
 */
static void emit_divmod32(const s8 *dst, const s8 *src, const s8 *tmp,
			   struct hexagon_jit_context *ctx,
			   bool is_mod, bool is_signed)
{
	const s8 *rd = bpf_get_reg32(dst, tmp, ctx);
	u32 addr;

	/* Save R0-R1 and TCC on the stack */
	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, -16), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 0, HEX_REG_R0), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 1, HEX_REG_R1), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 2, HEX_REG_TCC), ctx);

	/*
	 * Move dividend (dst) into R0, divisor (src) into R1.
	 * If a source register is R0 or R1, it was just saved to the stack
	 * and may be clobbered by the preceding move -- load from save area.
	 */
#define MOVE_OR_LOAD(to, from) do {					\
	if ((u8)(from) <= HEX_REG_R1)					\
		emit(hex_l2_loadri_io((to), HEX_REG_SP, (from)), ctx);	\
	else								\
		emit(hex_a2_tfr((to), (from)), ctx);			\
} while (0)

	MOVE_OR_LOAD(HEX_REG_R0, lo(rd));
	MOVE_OR_LOAD(HEX_REG_R1, lo(src));

#undef MOVE_OR_LOAD

	/* Select helper */
	if (is_mod)
		addr = is_signed ? (u32)(uintptr_t)jit_smod32
				 : (u32)(uintptr_t)jit_mod32;
	else
		addr = is_signed ? (u32)(uintptr_t)jit_sdiv32
				 : (u32)(uintptr_t)jit_udiv32;

	emit_imm(HEX_REG_R28, addr, ctx);
	emit(hex_j2_callr(HEX_REG_R28), ctx);

	/* Move result R0 into dst */
	emit(hex_a2_tfr(lo(rd), HEX_REG_R0), ctx);

	/* Restore R0-R1 and TCC */
	emit(hex_l2_loadri_io(HEX_REG_R0, HEX_REG_SP, 0), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R1, HEX_REG_SP, 1), ctx);
	emit(hex_l2_loadri_io(HEX_REG_TCC, HEX_REG_SP, 2), ctx);
	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, 16), ctx);

	bpf_put_reg32(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* Prologue / Epilogue                                                 */
/* ------------------------------------------------------------------ */

/*
 * Stack frame (grows downward):
 *
 *            +------------------------+  <- old SP = new FP
 *   FP-4    | saved LR  (R31)       |
 *   FP-8    | saved old FP (R30)    |
 *   FP-12   | saved R27             |
 *   FP-16   | saved R26             |
 *   FP-20   | saved R25             |
 *   FP-24   | saved R24             |
 *   FP-28   | saved R23             |
 *   FP-32   | saved R22             |
 *   FP-36   | saved R21             |
 *   FP-40   | saved R20             |
 *   FP-44   | saved R18             |
 *   FP-48   | saved R17             |
 *   FP-52   | saved R16             |
 *   FP-56   | saved R28             |
 *            +------------------------+  <- FP - 4*NR_SAVED
 *            | BPF_FP scratch        |
 *            +------------------------+  <- BPF_REG_FP value
 *            | BPF program stack     |
 *            +------------------------+  <- SP
 */

static void __build_epilogue(bool is_tail_call,
			     struct hexagon_jit_context *ctx)
{
	const s8 *r0 = bpf2hex[BPF_REG_0];

	if (!is_tail_call) {
		emit(hex_a2_tfr(HEX_REG_R0, lo(r0)), ctx);
		emit(hex_a2_tfr(HEX_REG_R1, hi(r0)), ctx);
	}

	/* Restore callee-saved registers */
	emit(hex_l2_loadri_io(HEX_REG_R28, HEX_REG_FP, -56 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R16, HEX_REG_FP, -52 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R17, HEX_REG_FP, -48 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R18, HEX_REG_FP, -44 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R20, HEX_REG_FP, -40 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R21, HEX_REG_FP, -36 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R22, HEX_REG_FP, -32 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R23, HEX_REG_FP, -28 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R24, HEX_REG_FP, -24 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R25, HEX_REG_FP, -20 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R26, HEX_REG_FP, -16 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R27, HEX_REG_FP, -12 / 4), ctx);
	emit(hex_l2_loadri_io(HEX_REG_LR, HEX_REG_FP, -4 / 4), ctx);

	/* Restore SP = FP, then restore FP */
	emit(hex_a2_tfr(HEX_REG_SP, HEX_REG_FP), ctx);
	emit(hex_l2_loadri_io(HEX_REG_FP, HEX_REG_FP, -8 / 4), ctx);

	if (is_tail_call) {
		/*
		 * R12 holds the target bpf_func.
		 * Jump to target + 4 to skip TCC init in target's prologue.
		 */
		emit(hex_a2_addi(HEX_REG_R12, HEX_REG_R12, 4), ctx);
		emit(hex_j2_jumpr(HEX_REG_R12), ctx);
	} else {
		emit(hex_j2_jumpr(HEX_REG_LR), ctx);
	}
}

void bpf_jit_build_prologue(struct hexagon_jit_context *ctx, bool is_subprog)
{
	const s8 *fp = bpf2hex[BPF_REG_FP];
	const s8 *r1 = bpf2hex[BPF_REG_1];
	int stack_adjust = 0;
	int bpf_stack_adjust = round_up(ctx->prog->aux->stack_depth, 8);

	stack_adjust += NR_SAVED_REGISTERS * 4;
	stack_adjust += BPF_JIT_SCRATCH_REGS * 4;
	stack_adjust += bpf_stack_adjust;
	stack_adjust = round_up(stack_adjust, 8);

	/* First instruction: init TCC (skipped by tail calls) */
	emit(hex_a2_tfrsi(HEX_REG_TCC, MAX_TAIL_CALL_CNT), ctx);

	/* Set up frame pointer */
	emit(hex_a2_tfr(HEX_REG_R28, HEX_REG_FP), ctx);
	emit(hex_a2_tfr(HEX_REG_FP, HEX_REG_SP), ctx);
	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, -stack_adjust), ctx);

	/* Save callee-saved registers */
	emit(hex_s2_storeri_io(HEX_REG_FP, -4 / 4, HEX_REG_LR), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -8 / 4, HEX_REG_R28), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -12 / 4, HEX_REG_R27), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -16 / 4, HEX_REG_R26), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -20 / 4, HEX_REG_R25), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -24 / 4, HEX_REG_R24), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -28 / 4, HEX_REG_R23), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -32 / 4, HEX_REG_R22), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -36 / 4, HEX_REG_R21), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -40 / 4, HEX_REG_R20), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -44 / 4, HEX_REG_R18), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -48 / 4, HEX_REG_R17), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -52 / 4, HEX_REG_R16), ctx);
	emit(hex_s2_storeri_io(HEX_REG_FP, -56 / 4, HEX_REG_R28), ctx);

	/* Set up BPF frame pointer (store in scratch area) */
	if (is_stacked(lo(fp))) {
		const s8 *t = bpf2hex[TMP_REG_1];

		emit(hex_a2_tfr(lo(t), HEX_REG_SP), ctx);
		emit(hex_a2_tfrsi(hi(t), 0), ctx);
		bpf_put_reg64(fp, t, ctx);
	}

	/* Zero-extend BPF_REG_1 (context pointer is 32-bit) */
	emit(hex_a2_tfrsi(hi(r1), 0), ctx);

	ctx->stack_size = stack_adjust;
}

void bpf_jit_build_epilogue(struct hexagon_jit_context *ctx)
{
	__build_epilogue(false, ctx);
}

/* ------------------------------------------------------------------ */
/* Sign-extending move (MOVSX)                                         */
/* ------------------------------------------------------------------ */

static void emit_movsx32(const s8 *dst, const s8 *src, s16 off,
			  struct hexagon_jit_context *ctx)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg32(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg32(src, tmp2, ctx);

	switch (off) {
	case 8:
		/* sign-extend byte: asl 24, asr 24 */
		emit(hex_s2_asl_i_r(lo(rd), lo(rs), 24), ctx);
		emit(hex_s2_asr_i_r(lo(rd), lo(rd), 24), ctx);
		break;
	case 16:
		/* sign-extend halfword: asl 16, asr 16 */
		emit(hex_s2_asl_i_r(lo(rd), lo(rs), 16), ctx);
		emit(hex_s2_asr_i_r(lo(rd), lo(rd), 16), ctx);
		break;
	}

	bpf_put_reg32(dst, rd, ctx);
}

static void emit_movsx64(const s8 *dst, const s8 *src, s16 off,
			  struct hexagon_jit_context *ctx)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);

	switch (off) {
	case 8:
		emit(hex_s2_asl_i_r(lo(rd), lo(rs), 24), ctx);
		emit(hex_s2_asr_i_r(lo(rd), lo(rd), 24), ctx);
		emit(hex_s2_asr_i_r(hi(rd), lo(rd), 31), ctx);
		break;
	case 16:
		emit(hex_s2_asl_i_r(lo(rd), lo(rs), 16), ctx);
		emit(hex_s2_asr_i_r(lo(rd), lo(rd), 16), ctx);
		emit(hex_s2_asr_i_r(hi(rd), lo(rd), 31), ctx);
		break;
	case 32:
		if (lo(rd) != lo(rs))
			emit(hex_a2_tfr(lo(rd), lo(rs)), ctx);
		emit(hex_s2_asr_i_r(hi(rd), lo(rd), 31), ctx);
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* 64-bit ALU -- register operand                                      */
/* ------------------------------------------------------------------ */

static void emit_alu_r64(const s8 *dst, const s8 *src,
			  struct hexagon_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);

	switch (op) {
	case BPF_MOV:
		emit(hex_a2_tfr(lo(rd), lo(rs)), ctx);
		emit(hex_a2_tfr(hi(rd), hi(rs)), ctx);
		break;
	case BPF_ADD:
		if (rd == rs) {
			/* dst += dst -> shift left by 1 */
			emit(hex_s2_lsr_i_r(HEX_REG_R28, lo(rd), 31), ctx);
			emit(hex_s2_asl_i_r(hi(rd), hi(rd), 1), ctx);
			emit(hex_a2_or(hi(rd), hi(rd), HEX_REG_R28), ctx);
			emit(hex_s2_asl_i_r(lo(rd), lo(rd), 1), ctx);
		} else {
			emit(hex_a2_tfr(HEX_REG_R28, lo(rd)), ctx);
			emit(hex_a2_add(lo(rd), lo(rd), lo(rs)), ctx);
			/* carry = (lo(rd) < old_lo) unsigned */
			emit(hex_c2_cmpgtu(HEX_REG_P0, HEX_REG_R28, lo(rd)), ctx);
			emit(hex_a2_add(hi(rd), hi(rd), hi(rs)), ctx);
			/* if carry, hi += 1 */
			emit(hex_a2_tfrsi(HEX_REG_R28, 1), ctx);
			emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
			emit(hex_a2_add(hi(rd), hi(rd), HEX_REG_R28), ctx);
		}
		break;
	case BPF_SUB:
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs), lo(rd)), ctx);
		emit(hex_a2_sub(hi(rd), hi(rd), hi(rs)), ctx);
		emit(hex_a2_sub(lo(rd), lo(rd), lo(rs)), ctx);
		/* if borrow, hi -= 1 */
		emit(hex_a2_tfrsi(HEX_REG_R28, 1), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
		emit(hex_a2_sub(hi(rd), hi(rd), HEX_REG_R28), ctx);
		break;
	case BPF_AND:
		emit(hex_a2_and(lo(rd), lo(rd), lo(rs)), ctx);
		emit(hex_a2_and(hi(rd), hi(rd), hi(rs)), ctx);
		break;
	case BPF_OR:
		emit(hex_a2_or(lo(rd), lo(rd), lo(rs)), ctx);
		emit(hex_a2_or(hi(rd), hi(rd), hi(rs)), ctx);
		break;
	case BPF_XOR:
		emit(hex_a2_xor(lo(rd), lo(rd), lo(rs)), ctx);
		emit(hex_a2_xor(hi(rd), hi(rd), hi(rs)), ctx);
		break;
	case BPF_MUL:
		/*
		 * 64x64->64:
		 *   result_lo = lo_d * lo_s  (low 32 bits)
		 *   result_hi = hi_d*lo_s + hi_s*lo_d + mulhu(lo_d, lo_s)
		 *
		 * Use R15:R14 = mpyu(lo_d, lo_s) for 32x32->64 unsigned.
		 */
		emit(hex_m2_mpyi(HEX_REG_R28, hi(rs), lo(rd)), ctx);
		emit(hex_m2_mpyi(hi(rd), hi(rd), lo(rs)), ctx);
		emit(hex_m2_dpmpyuu_s0(HEX_REG_R14, lo(rd), lo(rs)), ctx);
		emit(hex_a2_add(hi(rd), hi(rd), HEX_REG_R28), ctx);
		emit(hex_a2_tfr(lo(rd), HEX_REG_R14), ctx);
		emit(hex_a2_add(hi(rd), hi(rd), HEX_REG_R15), ctx);
		break;
	case BPF_NEG:
		emit(hex_a2_subri(lo(rd), 0, lo(rd)), ctx);
		/* borrow = (lo != 0) ? 1 : 0 */
		emit(hex_c4_cmpeqi(HEX_REG_P0, lo(rd), 0), ctx);
		emit(hex_a2_subri(hi(rd), 0, hi(rd)), ctx);
		/* if lo was nonzero (P0 false), hi -= 1 */
		emit(hex_a2_tfrsi(HEX_REG_R28, 1), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		emit(hex_a2_sub(hi(rd), hi(rd), HEX_REG_R28), ctx);
		break;
	case BPF_LSH:
		/*
		 * 64-bit left shift by register amount.
		 *
		 * R28 = lo(rs) - 32
		 * if (R28 >= 0) {        // shift >= 32
		 *     hi = asl(lo_d, R28)
		 *     lo = 0
		 * } else {               // shift < 32
		 *     tmp = lsr(lo_d, 1)
		 *     R28 = 31 - lo(rs)  // = -(R28) - 1 = ~R28
		 *     tmp = lsr(tmp, R28)
		 *     hi = asl(hi_d, lo(rs))
		 *     hi = or(hi, tmp)
		 *     lo = asl(lo_d, lo(rs))
		 * }
		 */
		emit(hex_a2_addi(HEX_REG_R28, lo(rs), -32), ctx);
		/* P0 = (R28 >= 0), i.e. shift >= 32 -- use cmp.gt(R28, -1) */
		emit(hex_c4_cmpeqi(HEX_REG_P0, HEX_REG_R28, -1), ctx);
		/* if shift==32 exactly, P0 is true (eq -1) -> skip to else.
		 * We need: if R28 >= 0 jump to big path.
		 * Use: P0 = cmp.gt(R28, -1) -- but we don't have cmp.gti.
		 * Instead: R28 >= 0 <-> !(R28 < 0) <-> cmp.gt(-1, R28) is false.
		 * Actually simpler: just use a sub and check sign.
		 * Let's use the approach: if (lo(rs) u>= 32) big path.
		 */
		/* Redo: use cmpgtu to check lo(rs) >= 32 as unsigned */
		emit(hex_a2_tfrsi(HEX_REG_R28, 31), ctx);
		/* P0 = cmp.gtu(lo(rs), 31) -- true if shift >= 32 */
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs), HEX_REG_R28), ctx);
		/* if (!P0) jump past big path to small path */
		emit(hex_j2_jumpf(HEX_REG_P0, 5 * 4), ctx);
		/* --- big path: shift >= 32 (4 insns) --- */
		emit(hex_a2_addi(HEX_REG_R28, lo(rs), -32), ctx);
		emit(hex_s2_asl_r_r(hi(rd), lo(rd), HEX_REG_R28), ctx);
		emit(hex_a2_tfrsi(lo(rd), 0), ctx);
		emit(hex_j2_jump(7 * 4), ctx);  /* skip small path */
		/* --- small path: shift < 32 --- (7 insns) */
		emit(hex_s2_lsr_i_r(HEX_REG_R28, lo(rd), 1), ctx);
		emit(hex_a2_subri(hi(tmp2), 31, lo(rs)), ctx);
		emit(hex_s2_lsr_r_r(HEX_REG_R28, HEX_REG_R28, hi(tmp2)),
		     ctx);
		emit(hex_s2_asl_r_r(hi(rd), hi(rd), lo(rs)), ctx);
		emit(hex_a2_or(hi(rd), hi(rd), HEX_REG_R28), ctx);
		emit(hex_s2_asl_r_r(lo(rd), lo(rd), lo(rs)), ctx);
		emit(hex_a2_nop(), ctx);  /* landing pad */
		break;
	case BPF_RSH:
		/*
		 * 64-bit logical right shift by register amount.
		 */
		emit(hex_a2_tfrsi(HEX_REG_R28, 31), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs), HEX_REG_R28), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 5 * 4), ctx);
		/* --- big path: shift >= 32 --- */
		emit(hex_a2_addi(HEX_REG_R28, lo(rs), -32), ctx);
		emit(hex_s2_lsr_r_r(lo(rd), hi(rd), HEX_REG_R28), ctx);
		emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		emit(hex_j2_jump(7 * 4), ctx);
		/* --- small path: shift < 32 --- (7 insns) */
		emit(hex_s2_asl_i_r(HEX_REG_R28, hi(rd), 1), ctx);
		emit(hex_a2_subri(hi(tmp2), 31, lo(rs)), ctx);
		emit(hex_s2_asl_r_r(HEX_REG_R28, HEX_REG_R28, hi(tmp2)),
		     ctx);
		emit(hex_s2_lsr_r_r(lo(rd), lo(rd), lo(rs)), ctx);
		emit(hex_a2_or(lo(rd), lo(rd), HEX_REG_R28), ctx);
		emit(hex_s2_lsr_r_r(hi(rd), hi(rd), lo(rs)), ctx);
		emit(hex_a2_nop(), ctx);
		break;
	case BPF_ARSH:
		/*
		 * 64-bit arithmetic right shift by register amount.
		 */
		emit(hex_a2_tfrsi(HEX_REG_R28, 31), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs), HEX_REG_R28), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 5 * 4), ctx);
		/* --- big path: shift >= 32 --- */
		emit(hex_a2_addi(HEX_REG_R28, lo(rs), -32), ctx);
		emit(hex_s2_asr_r_r(lo(rd), hi(rd), HEX_REG_R28), ctx);
		emit(hex_s2_asr_i_r(hi(rd), hi(rd), 31), ctx);
		emit(hex_j2_jump(7 * 4), ctx);
		/* --- small path: shift < 32 --- (7 insns) */
		emit(hex_s2_asl_i_r(HEX_REG_R28, hi(rd), 1), ctx);
		emit(hex_a2_subri(hi(tmp2), 31, lo(rs)), ctx);
		emit(hex_s2_asl_r_r(HEX_REG_R28, HEX_REG_R28, hi(tmp2)),
		     ctx);
		emit(hex_s2_lsr_r_r(lo(rd), lo(rd), lo(rs)), ctx);
		emit(hex_a2_or(lo(rd), lo(rd), HEX_REG_R28), ctx);
		emit(hex_s2_asr_r_r(hi(rd), hi(rd), lo(rs)), ctx);
		emit(hex_a2_nop(), ctx);
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* 64-bit ALU -- immediate operand                                     */
/* ------------------------------------------------------------------ */

static void emit_alu_i64(const s8 *dst, s32 imm,
			  struct hexagon_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

	switch (op) {
	case BPF_MOV:
		emit_imm32(rd, imm, ctx);
		break;
	case BPF_AND:
		emit_imm(HEX_REG_R28, imm, ctx);
		emit(hex_a2_and(lo(rd), lo(rd), HEX_REG_R28), ctx);
		if (imm >= 0)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case BPF_OR:
		emit_imm(HEX_REG_R28, imm, ctx);
		emit(hex_a2_or(lo(rd), lo(rd), HEX_REG_R28), ctx);
		if (imm < 0)
			emit(hex_a2_tfrsi(hi(rd), -1), ctx);
		break;
	case BPF_XOR:
		emit_imm(HEX_REG_R28, imm, ctx);
		emit(hex_a2_xor(lo(rd), lo(rd), HEX_REG_R28), ctx);
		if (imm < 0) {
			emit(hex_a2_tfrsi(HEX_REG_R28, -1), ctx);
			emit(hex_a2_xor(hi(rd), hi(rd), HEX_REG_R28), ctx);
		}
		break;
	case BPF_LSH:
		if (imm >= 32) {
			emit(hex_s2_asl_i_r(hi(rd), lo(rd), imm - 32), ctx);
			emit(hex_a2_tfrsi(lo(rd), 0), ctx);
		} else if (imm > 0) {
			emit(hex_s2_lsr_i_r(HEX_REG_R28, lo(rd), 32 - imm),
			     ctx);
			emit(hex_s2_asl_i_r(hi(rd), hi(rd), imm), ctx);
			emit(hex_a2_or(hi(rd), hi(rd), HEX_REG_R28), ctx);
			emit(hex_s2_asl_i_r(lo(rd), lo(rd), imm), ctx);
		}
		break;
	case BPF_RSH:
		if (imm >= 32) {
			emit(hex_s2_lsr_i_r(lo(rd), hi(rd), imm - 32), ctx);
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		} else if (imm > 0) {
			emit(hex_s2_asl_i_r(HEX_REG_R28, hi(rd), 32 - imm),
			     ctx);
			emit(hex_s2_lsr_i_r(lo(rd), lo(rd), imm), ctx);
			emit(hex_a2_or(lo(rd), lo(rd), HEX_REG_R28), ctx);
			emit(hex_s2_lsr_i_r(hi(rd), hi(rd), imm), ctx);
		}
		break;
	case BPF_ARSH:
		if (imm >= 32) {
			emit(hex_s2_asr_i_r(lo(rd), hi(rd), imm - 32), ctx);
			emit(hex_s2_asr_i_r(hi(rd), hi(rd), 31), ctx);
		} else if (imm > 0) {
			emit(hex_s2_asl_i_r(HEX_REG_R28, hi(rd), 32 - imm),
			     ctx);
			emit(hex_s2_lsr_i_r(lo(rd), lo(rd), imm), ctx);
			emit(hex_a2_or(lo(rd), lo(rd), HEX_REG_R28), ctx);
			emit(hex_s2_asr_i_r(hi(rd), hi(rd), imm), ctx);
		}
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* 32-bit ALU                                                          */
/* ------------------------------------------------------------------ */

static void emit_alu_r32(const s8 *dst, const s8 *src,
			  struct hexagon_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg32(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg32(src, tmp2, ctx);

	switch (op) {
	case BPF_MOV:
		emit(hex_a2_tfr(lo(rd), lo(rs)), ctx);
		break;
	case BPF_ADD:
		emit(hex_a2_add(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_SUB:
		emit(hex_a2_sub(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_AND:
		emit(hex_a2_and(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_OR:
		emit(hex_a2_or(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_XOR:
		emit(hex_a2_xor(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_MUL:
		emit(hex_m2_mpyi(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_LSH:
		emit(hex_s2_asl_r_r(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_RSH:
		emit(hex_s2_lsr_r_r(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_ARSH:
		emit(hex_s2_asr_r_r(lo(rd), lo(rd), lo(rs)), ctx);
		break;
	case BPF_NEG:
		emit(hex_a2_subri(lo(rd), 0, lo(rd)), ctx);
		break;
	}

	bpf_put_reg32(dst, rd, ctx);
}

static void emit_alu_i32(const s8 *dst, s32 imm,
			  struct hexagon_jit_context *ctx, const u8 op)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *rd = bpf_get_reg32(dst, tmp1, ctx);

	switch (op) {
	case BPF_MOV:
		emit_imm(lo(rd), imm, ctx);
		break;
	case BPF_ADD:
		if (is_s16(imm)) {
			emit(hex_a2_addi(lo(rd), lo(rd), (s16)imm), ctx);
		} else {
			emit_imm(HEX_REG_R28, imm, ctx);
			emit(hex_a2_add(lo(rd), lo(rd), HEX_REG_R28), ctx);
		}
		break;
	case BPF_SUB:
		if (is_s16(-imm)) {
			emit(hex_a2_addi(lo(rd), lo(rd), (s16)(-imm)), ctx);
		} else {
			emit_imm(HEX_REG_R28, imm, ctx);
			emit(hex_a2_sub(lo(rd), lo(rd), HEX_REG_R28), ctx);
		}
		break;
	case BPF_AND:
		emit_imm(HEX_REG_R28, imm, ctx);
		emit(hex_a2_and(lo(rd), lo(rd), HEX_REG_R28), ctx);
		break;
	case BPF_OR:
		emit_imm(HEX_REG_R28, imm, ctx);
		emit(hex_a2_or(lo(rd), lo(rd), HEX_REG_R28), ctx);
		break;
	case BPF_XOR:
		emit_imm(HEX_REG_R28, imm, ctx);
		emit(hex_a2_xor(lo(rd), lo(rd), HEX_REG_R28), ctx);
		break;
	case BPF_LSH:
		if (is_u5(imm))
			emit(hex_s2_asl_i_r(lo(rd), lo(rd), imm), ctx);
		break;
	case BPF_RSH:
		if (is_u5(imm))
			emit(hex_s2_lsr_i_r(lo(rd), lo(rd), imm), ctx);
		break;
	case BPF_ARSH:
		if (is_u5(imm))
			emit(hex_s2_asr_i_r(lo(rd), lo(rd), imm), ctx);
		break;
	}

	bpf_put_reg32(dst, rd, ctx);
}

static void emit_zext64(const s8 *dst, struct hexagon_jit_context *ctx)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

	emit(hex_a2_tfrsi(hi(rd), 0), ctx);
	bpf_put_reg64(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* 64-bit conditional branch                                           */
/*                                                                     */
/* Block layout for 3-pair conditions (JGT, JGE, JLT, JLE, JS*):      */
/*   [0] cmp P0, hi1, hi2                                             */
/*   [1] if (P0) jump TAKEN/SKIP                                      */
/*   [2] cmp P0, hi2, hi1                                             */
/*   [3] if (P0) jump SKIP                                            */
/*   [4] cmp P0, lo1, lo2                                             */
/*   [5] if (!P0) jump SKIP                                           */
/*   [6] jump #target                                                  */
/*   [SKIP]: next insn after block                                     */
/*                                                                     */
/* Block layout for 2-pair conditions (JEQ, JNE):                      */
/*   [0] cmp P0, hi1, hi2                                             */
/*   [1] if (!P0) jump SKIP / jump TAKEN                              */
/*   [2] cmp P0, lo1, lo2                                             */
/*   [3] if (!P0) jump SKIP / if (P0) jump SKIP                      */
/*   [4] jump #target                                                  */
/*   [SKIP]: next insn after block                                     */
/*                                                                     */
/* SKIP(idx) = offset from insn[idx] to [SKIP] = (total - idx) * 4    */
/* TAKEN(idx) = offset from insn[idx] to insn[total-1] = ...          */
/* ------------------------------------------------------------------ */

static int emit_branch_r64(const s8 *src1, const s8 *src2, s32 rvoff,
			    struct hexagon_jit_context *ctx, const u8 op)
{
	int s = ctx->ninsns, e;
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rs1 = bpf_get_reg64(src1, tmp1, ctx);
	const s8 *rs2 = bpf_get_reg64(src2, tmp2, ctx);

	/*
	 * SKIP(idx, total): jump past the entire block (branch not taken).
	 * Offset from insn at index `idx` to byte after last insn.
	 * = (total - idx) * 4
	 *
	 * TAKEN(idx, total): jump to the final "jump #target" insn.
	 * = (total - 1 - idx) * 4
	 */
#define SKIP(idx, tot)  (((tot) - (idx)) * 4)
#define TAKEN(idx, tot) (((tot) - 1 - (idx)) * 4)

	switch (op) {
	case BPF_JEQ:
		/* 5 insns: cmp hi, skip, cmp lo, skip, jump */
		emit(hex_c2_cmpeq(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, SKIP(1, 5)), ctx);
		emit(hex_c2_cmpeq(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, SKIP(3, 5)), ctx);
		break;
	case BPF_JNE:
		/* 5 insns */
		emit(hex_c2_cmpeq(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, TAKEN(1, 5)), ctx);
		emit(hex_c2_cmpeq(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 5)), ctx);
		break;
	case BPF_JGT:
		/* 7 insns */
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JGE:
		/* 7 insns */
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		/* lo1 >= lo2 <-> !(lo2 > lo1) */
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JLT:
		/* 7 insns */
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JLE:
		/* 7 insns */
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JSGT:
		/* 7 insns -- signed hi, unsigned lo */
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JSGE:
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JSLT:
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JSLE:
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs2), hi(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, TAKEN(1, 7)), ctx);
		emit(hex_c2_cmpgt(HEX_REG_P0, hi(rs1), hi(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(3, 7)), ctx);
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	case BPF_JSET:
		/* 7 insns: AND hi, test, skip-to-taken, AND lo, test, skip, jump */
		emit(hex_a2_and(HEX_REG_R28, hi(rs1), hi(rs2)), ctx);
		emit(hex_c4_cmpeqi(HEX_REG_P0, HEX_REG_R28, 0), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, TAKEN(2, 7)), ctx);
		emit(hex_a2_and(HEX_REG_R28, lo(rs1), lo(rs2)), ctx);
		emit(hex_c4_cmpeqi(HEX_REG_P0, HEX_REG_R28, 0), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, SKIP(5, 7)), ctx);
		break;
	}

#undef SKIP
#undef TAKEN

	e = ctx->ninsns;
	rvoff -= ninsns_rvoff(e - s);
	emit(hex_j2_jump(rvoff), ctx);

	return 0;
}

/* ------------------------------------------------------------------ */
/* 32-bit conditional branch                                           */
/* ------------------------------------------------------------------ */

static int emit_branch_r32(const s8 *src1, const s8 *src2, s32 rvoff,
			    struct hexagon_jit_context *ctx, const u8 op)
{
	int s = ctx->ninsns, e;
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rs1 = bpf_get_reg32(src1, tmp1, ctx);
	const s8 *rs2 = bpf_get_reg32(src2, tmp2, ctx);

	/*
	 * All 32-bit branches: 3 insns (cmp, cond_jump, jump).
	 * SKIP from cond_jump (idx 1) = (3-1)*4 = 8.
	 */
	switch (op) {
	case BPF_JEQ:
		emit(hex_c2_cmpeq(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JNE:
		emit(hex_c2_cmpeq(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JGT:
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JGE:
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JLT:
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JLE:
		emit(hex_c2_cmpgtu(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JSGT:
		emit(hex_c2_cmpgt(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JSGE:
		emit(hex_c2_cmpgt(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JSLT:
		emit(hex_c2_cmpgt(HEX_REG_P0, lo(rs2), lo(rs1)), ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JSLE:
		emit(hex_c2_cmpgt(HEX_REG_P0, lo(rs1), lo(rs2)), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		break;
	case BPF_JSET:
		/* 4 insns: AND, cmp with 0, cond_jump, jump */
		emit(hex_a2_and(HEX_REG_R28, lo(rs1), lo(rs2)), ctx);
		emit(hex_c4_cmpeqi(HEX_REG_P0, HEX_REG_R28, 0), ctx);
		emit(hex_j2_jumpt(HEX_REG_P0, 8), ctx);
		break;
	}

	e = ctx->ninsns;
	rvoff -= ninsns_rvoff(e - s);
	emit(hex_j2_jump(rvoff), ctx);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Function calls                                                      */
/* ------------------------------------------------------------------ */

static void emit_call(bool fixed, u64 addr, struct hexagon_jit_context *ctx)
{
	const s8 *r0 = bpf2hex[BPF_REG_0];
	const s8 *r4 = bpf2hex[BPF_REG_4];
	const s8 *r5 = bpf2hex[BPF_REG_5];

	/*
	 * BPF_REG_1-3 are in R0-R5 (native ABI positions).
	 * BPF_REG_4 (R7:R6) and BPF_REG_5 (R9:R8) go on the stack
	 * per the Hexagon calling convention.
	 * Also save TCC (R14, caller-saved) so it survives the call.
	 */
	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, -24), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 0, lo(r4)), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 1, hi(r4)), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 2, lo(r5)), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 3, hi(r5)), ctx);
	emit(hex_s2_storeri_io(HEX_REG_SP, 4, HEX_REG_TCC), ctx);

	/* Load target address and call */
	emit_imm(HEX_REG_R28, (u32)addr, ctx);
	emit(hex_j2_callr(HEX_REG_R28), ctx);

	/* Restore TCC */
	emit(hex_l2_loadri_io(HEX_REG_TCC, HEX_REG_SP, 4), ctx);

	/* Move return value R1:R0 -> BPF_REG_0 (R17:R16) */
	emit(hex_a2_tfr(lo(r0), HEX_REG_R0), ctx);
	emit(hex_a2_tfr(hi(r0), HEX_REG_R1), ctx);

	emit(hex_a2_addi(HEX_REG_SP, HEX_REG_SP, 24), ctx);
}

/* ------------------------------------------------------------------ */
/* Tail call                                                           */
/* ------------------------------------------------------------------ */

static int emit_bpf_tail_call(int insn, struct hexagon_jit_context *ctx)
{
	int off, tc_ninsn, start_insn = ctx->ninsns;
	const s8 *arr_reg = bpf2hex[BPF_REG_2];
	const s8 *idx_reg = bpf2hex[BPF_REG_3];

	tc_ninsn = insn ? ctx->offset[insn] - ctx->offset[insn - 1] :
		ctx->offset[0];

	/* R28 = array->map.max_entries */
	off = offsetof(struct bpf_array, map.max_entries);
	emit_imm(HEX_REG_R28, off, ctx);
	emit(hex_a2_add(HEX_REG_R28, HEX_REG_R28, lo(arr_reg)), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R28, HEX_REG_R28, 0), ctx);

	/* if (index >= max_entries) goto out */
	/* P0 = cmp.gtu(R28, lo(idx))  i.e. max > idx means idx < max */
	emit(hex_c2_cmpgtu(HEX_REG_P0, HEX_REG_R28, lo(idx_reg)), ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit(hex_j2_jumpf(HEX_REG_P0, off), ctx);

	/* if (--TCC < 0) goto out */
	emit(hex_a2_addi(HEX_REG_TCC, HEX_REG_TCC, -1), ctx);
	emit(hex_c4_cmpeqi(HEX_REG_P0, HEX_REG_TCC, -1), ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit(hex_j2_jumpt(HEX_REG_P0, off), ctx);

	/* R28 = &array->ptrs[index] */
	emit(hex_s2_asl_i_r(HEX_REG_R28, lo(idx_reg), 2), ctx);
	emit(hex_a2_add(HEX_REG_R28, HEX_REG_R28, lo(arr_reg)), ctx);
	off = offsetof(struct bpf_array, ptrs);
	emit_imm(HEX_REG_R12, off, ctx);
	emit(hex_a2_add(HEX_REG_R28, HEX_REG_R28, HEX_REG_R12), ctx);
	emit(hex_l2_loadri_io(HEX_REG_R28, HEX_REG_R28, 0), ctx);

	/* if (!prog) goto out */
	emit(hex_c4_cmpeqi(HEX_REG_P0, HEX_REG_R28, 0), ctx);
	off = ninsns_rvoff(tc_ninsn - (ctx->ninsns - start_insn));
	emit(hex_j2_jumpt(HEX_REG_P0, off), ctx);

	/* R12 = prog->bpf_func */
	off = offsetof(struct bpf_prog, bpf_func);
	emit(hex_l2_loadri_io(HEX_REG_R12, HEX_REG_R28, off / 4), ctx);

	/* Epilogue: restore frame, jump to *(R12 + 4) */
	__build_epilogue(true, ctx);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Load / Store                                                        */
/* ------------------------------------------------------------------ */

static int emit_load_r64(const s8 *dst, const s8 *src, s16 off,
			  struct hexagon_jit_context *ctx, const u8 size)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);

	if (off) {
		emit_imm(HEX_REG_R28, off, ctx);
		emit(hex_a2_add(HEX_REG_R28, HEX_REG_R28, lo(rs)), ctx);
	} else {
		emit(hex_a2_tfr(HEX_REG_R28, lo(rs)), ctx);
	}

	switch (size) {
	case BPF_B:
		emit(hex_l2_loadrub_io(lo(rd), HEX_REG_R28, 0), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case BPF_H:
		emit(hex_l2_loadruh_io(lo(rd), HEX_REG_R28, 0), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case BPF_W:
		emit(hex_l2_loadri_io(lo(rd), HEX_REG_R28, 0), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case BPF_DW:
		emit(hex_l2_loadri_io(lo(rd), HEX_REG_R28, 0), ctx);
		emit(hex_l2_loadri_io(hi(rd), HEX_REG_R28, 1), ctx);
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
	return 0;
}

static int emit_store_r64(const s8 *dst, const s8 *src, s16 off,
			   struct hexagon_jit_context *ctx, const u8 size,
			   const u8 mode)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);

	if (off) {
		emit_imm(HEX_REG_R28, off, ctx);
		emit(hex_a2_add(HEX_REG_R28, HEX_REG_R28, lo(rd)), ctx);
	} else {
		emit(hex_a2_tfr(HEX_REG_R28, lo(rd)), ctx);
	}

	switch (size) {
	case BPF_B:
		emit(hex_s2_storerb_io(HEX_REG_R28, 0, lo(rs)), ctx);
		break;
	case BPF_H:
		emit(hex_s2_storerh_io(HEX_REG_R28, 0, lo(rs)), ctx);
		break;
	case BPF_W:
		emit(hex_s2_storeri_io(HEX_REG_R28, 0, lo(rs)), ctx);
		break;
	case BPF_DW:
		emit(hex_s2_storeri_io(HEX_REG_R28, 0, lo(rs)), ctx);
		emit(hex_s2_storeri_io(HEX_REG_R28, 1, hi(rs)), ctx);
		break;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Byte swap (endian conversion)                                       */
/* ------------------------------------------------------------------ */

static void emit_bswap_le(const s8 *dst, s32 imm,
			   struct hexagon_jit_context *ctx)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

	switch (imm) {
	case 16:
		emit(hex_s2_asl_i_r(lo(rd), lo(rd), 16), ctx);
		emit(hex_s2_lsr_i_r(lo(rd), lo(rd), 16), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case 32:
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case 64:
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
}

static void emit_bswap_be(const s8 *dst, s32 imm,
			   struct hexagon_jit_context *ctx)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

	switch (imm) {
	case 16:
		emit(hex_a2_swiz(lo(rd), lo(rd)), ctx);
		emit(hex_s2_lsr_i_r(lo(rd), lo(rd), 16), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case 32:
		emit(hex_a2_swiz(lo(rd), lo(rd)), ctx);
		if (!ctx->prog->aux->verifier_zext)
			emit(hex_a2_tfrsi(hi(rd), 0), ctx);
		break;
	case 64:
		emit(hex_a2_tfr(HEX_REG_R28, lo(rd)), ctx);
		emit(hex_a2_swiz(lo(rd), hi(rd)), ctx);
		emit(hex_a2_swiz(hi(rd), HEX_REG_R28), ctx);
		break;
	}

	bpf_put_reg64(dst, rd, ctx);
}

/* ------------------------------------------------------------------ */
/* 32-bit atomic operations (LL/SC loop)                               */
/* ------------------------------------------------------------------ */

static int emit_atomic(const s8 *dst, const s8 *src, s16 off,
		       struct hexagon_jit_context *ctx, const s32 imm)
{
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];
	const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);
	const s8 *rs = bpf_get_reg64(src, tmp2, ctx);
	u8 atomic_op = imm & ~BPF_FETCH;
	bool fetch = imm & BPF_FETCH;
	int loop_start;

	/* Compute address into R28 */
	if (off) {
		emit_imm(HEX_REG_R28, off, ctx);
		emit(hex_a2_add(HEX_REG_R28, HEX_REG_R28, lo(rd)), ctx);
	} else {
		emit(hex_a2_tfr(HEX_REG_R28, lo(rd)), ctx);
	}

	/* R28 = address, lo(rs) = source value */

	if (atomic_op == BPF_CMPXCHG) {
		/*
		 * CMPXCHG: compare *addr with BPF_REG_0.lo (R16),
		 * if equal store lo(rs), always return old in BPF_REG_0.lo.
		 *
		 * loop: R12 = memw_locked(R28)
		 *       P0 = cmp.eq(R12, R16)
		 *       if (!P0) jump done
		 *       memw_locked(R28, P0) = lo(rs)
		 *       if (!P0) jump loop
		 * done: R16 = R12   (old value -> BPF_REG_0.lo)
		 */
		loop_start = ctx->ninsns;
		emit(hex_l2_loadw_locked(HEX_REG_R12, HEX_REG_R28), ctx);
		emit(hex_c2_cmpeq(HEX_REG_P0, HEX_REG_R12, HEX_REG_R16),
		     ctx);
		emit(hex_j2_jumpf(HEX_REG_P0, 3 * 4), ctx);
		emit(hex_s2_storew_locked(HEX_REG_R28, HEX_REG_P0, lo(rs)),
		     ctx);
		emit(hex_j2_jumpf(HEX_REG_P0,
		     ninsns_rvoff(loop_start - (int)ctx->ninsns)), ctx);
		/* done: write old value back to R16 (BPF_REG_0 lo) */
		emit(hex_a2_tfr(HEX_REG_R16, HEX_REG_R12), ctx);
		return 0;
	}

	if (atomic_op == BPF_XCHG) {
		/*
		 * XCHG: atomically swap *addr with lo(rs).
		 *
		 * loop: R12 = memw_locked(R28)
		 *       memw_locked(R28, P0) = lo(rs)
		 *       if (!P0) jump loop
		 *
		 * lo(rs) = R12 (old value, BPF_FETCH is implicit)
		 */
		loop_start = ctx->ninsns;
		emit(hex_l2_loadw_locked(HEX_REG_R12, HEX_REG_R28), ctx);
		emit(hex_s2_storew_locked(HEX_REG_R28, HEX_REG_P0, lo(rs)),
		     ctx);
		emit(hex_j2_jumpf(HEX_REG_P0,
		     ninsns_rvoff(loop_start - (int)ctx->ninsns)), ctx);
		/* XCHG always fetches the old value */
		emit(hex_a2_tfr(lo(rs), HEX_REG_R12), ctx);
		bpf_put_reg64(src, rs, ctx);
		return 0;
	}

	/*
	 * ADD, AND, OR, XOR -- with optional FETCH.
	 *
	 * loop: R12 = memw_locked(R28)
	 *       R13 = op(R12, lo(rs))
	 *       memw_locked(R28, P0) = R13
	 *       if (!P0) jump loop
	 * if (fetch) lo(rs) = R12 (old value)
	 */
	loop_start = ctx->ninsns;
	emit(hex_l2_loadw_locked(HEX_REG_R12, HEX_REG_R28), ctx);

	switch (atomic_op) {
	case BPF_ADD:
		emit(hex_a2_add(HEX_REG_R13, HEX_REG_R12, lo(rs)), ctx);
		break;
	case BPF_AND:
		emit(hex_a2_and(HEX_REG_R13, HEX_REG_R12, lo(rs)), ctx);
		break;
	case BPF_OR:
		emit(hex_a2_or(HEX_REG_R13, HEX_REG_R12, lo(rs)), ctx);
		break;
	case BPF_XOR:
		emit(hex_a2_xor(HEX_REG_R13, HEX_REG_R12, lo(rs)), ctx);
		break;
	default:
		pr_err("bpf-jit: unknown atomic op %02x\n", atomic_op);
		return -EINVAL;
	}

	emit(hex_s2_storew_locked(HEX_REG_R28, HEX_REG_P0, HEX_REG_R13),
	     ctx);
	emit(hex_j2_jumpf(HEX_REG_P0,
	     ninsns_rvoff(loop_start - (int)ctx->ninsns)), ctx);

	if (fetch) {
		emit(hex_a2_tfr(lo(rs), HEX_REG_R12), ctx);
		bpf_put_reg64(src, rs, ctx);
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Main instruction emitter                                            */
/* ------------------------------------------------------------------ */

int bpf_jit_emit_insn(const struct bpf_insn *insn,
		       struct hexagon_jit_context *ctx, bool extra_pass)
{
	bool is64 = BPF_CLASS(insn->code) == BPF_ALU64 ||
		    BPF_CLASS(insn->code) == BPF_JMP;
	int s, e, i = insn - ctx->prog->insnsi;
	s32 rvoff;
	u8 code = insn->code;
	s16 off = insn->off;
	s32 imm = insn->imm;

	const s8 *dst = bpf2hex[insn->dst_reg];
	const s8 *src = bpf2hex[insn->src_reg];
	const s8 *tmp1 = bpf2hex[TMP_REG_1];
	const s8 *tmp2 = bpf2hex[TMP_REG_2];

	switch (code) {
	/* --- 64-bit ALU reg --- */
	case BPF_ALU64 | BPF_MOV | BPF_X:
		if (off != 0) {
			emit_movsx64(dst, src, off, ctx);
			break;
		}
		emit_alu_r64(dst, src, ctx, BPF_MOV);
		break;
	case BPF_ALU64 | BPF_ADD | BPF_X:
	case BPF_ALU64 | BPF_SUB | BPF_X:
	case BPF_ALU64 | BPF_AND | BPF_X:
	case BPF_ALU64 | BPF_OR  | BPF_X:
	case BPF_ALU64 | BPF_XOR | BPF_X:
	case BPF_ALU64 | BPF_MUL | BPF_X:
		emit_alu_r64(dst, src, ctx, BPF_OP(code));
		break;

	case BPF_ALU64 | BPF_ADD | BPF_K:
	case BPF_ALU64 | BPF_SUB | BPF_K:
	case BPF_ALU64 | BPF_MUL | BPF_K:
		emit_imm32(tmp2, imm, ctx);
		emit_alu_r64(dst, tmp2, ctx, BPF_OP(code));
		break;

	case BPF_ALU64 | BPF_NEG:
		emit_alu_r64(dst, tmp2, ctx, BPF_NEG);
		break;

	/* 64-bit shifts by register */
	case BPF_ALU64 | BPF_LSH | BPF_X:
	case BPF_ALU64 | BPF_RSH | BPF_X:
	case BPF_ALU64 | BPF_ARSH | BPF_X:
		emit_alu_r64(dst, src, ctx, BPF_OP(code));
		break;

	/* 64-bit div/mod */
	case BPF_ALU64 | BPF_DIV | BPF_X:
	case BPF_ALU64 | BPF_MOD | BPF_X:
		emit_divmod64(dst, src, tmp1, ctx,
			      BPF_OP(code) == BPF_MOD, off == 1);
		break;
	case BPF_ALU64 | BPF_DIV | BPF_K:
	case BPF_ALU64 | BPF_MOD | BPF_K:
		emit_imm32(tmp2, imm, ctx);
		emit_divmod64(dst, tmp2, tmp1, ctx,
			      BPF_OP(code) == BPF_MOD, off == 1);
		break;

	/* --- 64-bit ALU imm --- */
	case BPF_ALU64 | BPF_MOV  | BPF_K:
	case BPF_ALU64 | BPF_AND  | BPF_K:
	case BPF_ALU64 | BPF_OR   | BPF_K:
	case BPF_ALU64 | BPF_XOR  | BPF_K:
	case BPF_ALU64 | BPF_LSH  | BPF_K:
	case BPF_ALU64 | BPF_RSH  | BPF_K:
	case BPF_ALU64 | BPF_ARSH | BPF_K:
		emit_alu_i64(dst, imm, ctx, BPF_OP(code));
		break;

	/* --- 32-bit ALU reg --- */
	case BPF_ALU | BPF_MOV | BPF_X:
		if (imm == 1) {
			emit_zext64(dst, ctx);
			break;
		}
		if (off != 0) {
			emit_movsx32(dst, src, off, ctx);
			break;
		}
		fallthrough;
	case BPF_ALU | BPF_ADD  | BPF_X:
	case BPF_ALU | BPF_SUB  | BPF_X:
	case BPF_ALU | BPF_AND  | BPF_X:
	case BPF_ALU | BPF_OR   | BPF_X:
	case BPF_ALU | BPF_XOR  | BPF_X:
	case BPF_ALU | BPF_MUL  | BPF_X:
	case BPF_ALU | BPF_MUL  | BPF_K:
	case BPF_ALU | BPF_LSH  | BPF_X:
	case BPF_ALU | BPF_RSH  | BPF_X:
	case BPF_ALU | BPF_ARSH | BPF_X:
		if (BPF_SRC(code) == BPF_K) {
			emit_imm32(tmp2, imm, ctx);
			src = tmp2;
		}
		emit_alu_r32(dst, src, ctx, BPF_OP(code));
		break;

	case BPF_ALU | BPF_MOV  | BPF_K:
	case BPF_ALU | BPF_ADD  | BPF_K:
	case BPF_ALU | BPF_SUB  | BPF_K:
	case BPF_ALU | BPF_AND  | BPF_K:
	case BPF_ALU | BPF_OR   | BPF_K:
	case BPF_ALU | BPF_XOR  | BPF_K:
	case BPF_ALU | BPF_LSH  | BPF_K:
	case BPF_ALU | BPF_RSH  | BPF_K:
	case BPF_ALU | BPF_ARSH | BPF_K:
		emit_alu_i32(dst, imm, ctx, BPF_OP(code));
		break;

	case BPF_ALU | BPF_DIV | BPF_X:
	case BPF_ALU | BPF_MOD | BPF_X:
		emit_divmod32(dst, src, tmp1, ctx,
			      BPF_OP(code) == BPF_MOD, off == 1);
		break;
	case BPF_ALU | BPF_DIV | BPF_K:
	case BPF_ALU | BPF_MOD | BPF_K:
		emit_imm32(tmp2, imm, ctx);
		emit_divmod32(dst, tmp2, tmp1, ctx,
			      BPF_OP(code) == BPF_MOD, off == 1);
		break;

	case BPF_ALU | BPF_NEG:
		emit_alu_r32(dst, tmp2, ctx, BPF_NEG);
		break;

	/* --- Endian --- */
	case BPF_ALU | BPF_END | BPF_FROM_LE:
		emit_bswap_le(dst, imm, ctx);
		break;
	case BPF_ALU | BPF_END | BPF_FROM_BE:
		emit_bswap_be(dst, imm, ctx);
		break;
	case BPF_ALU64 | BPF_END | BPF_FROM_LE:
		/* BSWAP: unconditional byte swap (same as BE on LE host) */
		emit_bswap_be(dst, imm, ctx);
		break;

	/* --- Jumps --- */
	case BPF_JMP | BPF_JA:
		rvoff = hex_offset(i, off, ctx);
		emit(hex_j2_jump(rvoff), ctx);
		break;

	case BPF_JMP | BPF_CALL:
	{
		bool fixed;
		int ret;
		u64 addr;

		ret = bpf_jit_get_func_addr(ctx->prog, insn, extra_pass,
					     &addr, &fixed);
		if (ret < 0)
			return ret;
		emit_call(fixed, addr, ctx);
		break;
	}

	case BPF_JMP | BPF_TAIL_CALL:
		if (emit_bpf_tail_call(i, ctx))
			return -1;
		break;

	/* --- Conditional branches --- */
	case BPF_JMP   | BPF_JEQ  | BPF_X:
	case BPF_JMP   | BPF_JEQ  | BPF_K:
	case BPF_JMP32 | BPF_JEQ  | BPF_X:
	case BPF_JMP32 | BPF_JEQ  | BPF_K:
	case BPF_JMP   | BPF_JNE  | BPF_X:
	case BPF_JMP   | BPF_JNE  | BPF_K:
	case BPF_JMP32 | BPF_JNE  | BPF_X:
	case BPF_JMP32 | BPF_JNE  | BPF_K:
	case BPF_JMP   | BPF_JGT  | BPF_X:
	case BPF_JMP   | BPF_JGT  | BPF_K:
	case BPF_JMP32 | BPF_JGT  | BPF_X:
	case BPF_JMP32 | BPF_JGT  | BPF_K:
	case BPF_JMP   | BPF_JGE  | BPF_X:
	case BPF_JMP   | BPF_JGE  | BPF_K:
	case BPF_JMP32 | BPF_JGE  | BPF_X:
	case BPF_JMP32 | BPF_JGE  | BPF_K:
	case BPF_JMP   | BPF_JLT  | BPF_X:
	case BPF_JMP   | BPF_JLT  | BPF_K:
	case BPF_JMP32 | BPF_JLT  | BPF_X:
	case BPF_JMP32 | BPF_JLT  | BPF_K:
	case BPF_JMP   | BPF_JLE  | BPF_X:
	case BPF_JMP   | BPF_JLE  | BPF_K:
	case BPF_JMP32 | BPF_JLE  | BPF_X:
	case BPF_JMP32 | BPF_JLE  | BPF_K:
	case BPF_JMP   | BPF_JSGT | BPF_X:
	case BPF_JMP   | BPF_JSGT | BPF_K:
	case BPF_JMP32 | BPF_JSGT | BPF_X:
	case BPF_JMP32 | BPF_JSGT | BPF_K:
	case BPF_JMP   | BPF_JSGE | BPF_X:
	case BPF_JMP   | BPF_JSGE | BPF_K:
	case BPF_JMP32 | BPF_JSGE | BPF_X:
	case BPF_JMP32 | BPF_JSGE | BPF_K:
	case BPF_JMP   | BPF_JSLT | BPF_X:
	case BPF_JMP   | BPF_JSLT | BPF_K:
	case BPF_JMP32 | BPF_JSLT | BPF_X:
	case BPF_JMP32 | BPF_JSLT | BPF_K:
	case BPF_JMP   | BPF_JSLE | BPF_X:
	case BPF_JMP   | BPF_JSLE | BPF_K:
	case BPF_JMP32 | BPF_JSLE | BPF_X:
	case BPF_JMP32 | BPF_JSLE | BPF_K:
	case BPF_JMP   | BPF_JSET | BPF_X:
	case BPF_JMP   | BPF_JSET | BPF_K:
	case BPF_JMP32 | BPF_JSET | BPF_X:
	case BPF_JMP32 | BPF_JSET | BPF_K:
		rvoff = hex_offset(i, off, ctx);
		if (BPF_SRC(code) == BPF_K) {
			s = ctx->ninsns;
			emit_imm32(tmp2, imm, ctx);
			src = tmp2;
			e = ctx->ninsns;
			rvoff -= ninsns_rvoff(e - s);
		}
		if (is64)
			emit_branch_r64(dst, src, rvoff, ctx, BPF_OP(code));
		else
			emit_branch_r32(dst, src, rvoff, ctx, BPF_OP(code));
		break;

	/* --- Exit --- */
	case BPF_JMP | BPF_EXIT:
		if (i == ctx->prog->len - 1)
			break;
		rvoff = epilogue_offset(ctx);
		emit(hex_j2_jump(rvoff), ctx);
		break;

	/* --- 64-bit immediate load --- */
	case BPF_LD | BPF_IMM | BPF_DW:
	{
		struct bpf_insn insn1 = insn[1];
		s32 imm_lo = imm;
		s32 imm_hi = insn1.imm;
		const s8 *rd = bpf_get_reg64(dst, tmp1, ctx);

		emit_imm64(rd, imm_hi, imm_lo, ctx);
		bpf_put_reg64(dst, rd, ctx);
		return 1;
	}

	/* --- Memory loads --- */
	case BPF_LDX | BPF_MEM | BPF_B:
	case BPF_LDX | BPF_MEM | BPF_H:
	case BPF_LDX | BPF_MEM | BPF_W:
	case BPF_LDX | BPF_MEM | BPF_DW:
		if (emit_load_r64(dst, src, off, ctx, BPF_SIZE(code)))
			return -1;
		break;

	case BPF_ST | BPF_NOSPEC:
		break;

	/* --- Memory stores (immediate value) --- */
	case BPF_ST | BPF_MEM | BPF_B:
	case BPF_ST | BPF_MEM | BPF_H:
	case BPF_ST | BPF_MEM | BPF_W:
	case BPF_ST | BPF_MEM | BPF_DW:
		emit_imm32(tmp2, imm, ctx);
		src = tmp2;
		if (emit_store_r64(dst, src, off, ctx, BPF_SIZE(code),
				   BPF_MODE(code)))
			return -1;
		break;

	/* --- Memory stores (register value) --- */
	case BPF_STX | BPF_MEM | BPF_B:
	case BPF_STX | BPF_MEM | BPF_H:
	case BPF_STX | BPF_MEM | BPF_W:
	case BPF_STX | BPF_MEM | BPF_DW:
		if (emit_store_r64(dst, src, off, ctx, BPF_SIZE(code),
				   BPF_MODE(code)))
			return -1;
		break;

	case BPF_STX | BPF_ATOMIC | BPF_W:
		if (emit_atomic(dst, src, off, ctx, imm))
			return -1;
		break;

	/* 64-bit atomics not supported on 32-bit architecture */
	case BPF_STX | BPF_ATOMIC | BPF_DW:
		goto notsupported;

notsupported:
		pr_info_once("bpf-jit: not supported: opcode %02x ***\n", code);
		return -EFAULT;

	default:
		pr_err("bpf-jit: unknown opcode %02x\n", code);
		return -EINVAL;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Multi-pass compilation framework                                    */
/* ------------------------------------------------------------------ */

static int build_body(struct hexagon_jit_context *ctx, bool extra_pass,
		      int *offset)
{
	const struct bpf_prog *prog = ctx->prog;
	int i;

	for (i = 0; i < prog->len; i++) {
		const struct bpf_insn *insn = &prog->insnsi[i];
		int ret;

		ret = bpf_jit_emit_insn(insn, ctx, extra_pass);
		if (ret > 0)
			i++;
		if (offset)
			offset[i] = ctx->ninsns;
		if (ret < 0)
			return ret;
	}
	return 0;
}

bool bpf_jit_needs_zext(void)
{
	return true;
}

struct bpf_prog *bpf_int_jit_compile(struct bpf_verifier_env *env,
				     struct bpf_prog *prog)
{
	unsigned int prog_size = 0;
	bool extra_pass = false;
	struct bpf_prog *orig_prog = prog;
	int pass = 0, prev_ninsns = 0, i;
	struct hexagon_jit_data *jit_data;
	struct hexagon_jit_context *ctx;

	if (!prog->jit_requested)
		return orig_prog;

	/*
	 * Constant blinding is performed by the core (bpf_jit_blind_constants()
	 * in kernel/bpf/core.c) before this is called, so prog is already the
	 * blinded program here.
	 */

	jit_data = prog->aux->jit_data;
	if (!jit_data) {
		jit_data = kzalloc(sizeof(*jit_data), GFP_KERNEL);
		if (!jit_data) {
			prog = orig_prog;
			goto out;
		}
		prog->aux->jit_data = jit_data;
	}

	ctx = &jit_data->ctx;

	if (ctx->offset) {
		extra_pass = true;
		prog_size = sizeof(*ctx->insns) * ctx->ninsns;
		goto skip_init_ctx;
	}

	ctx->prog = prog;
	ctx->offset = kcalloc(prog->len, sizeof(int), GFP_KERNEL);
	if (!ctx->offset) {
		prog = orig_prog;
		goto out_offset;
	}

	if (build_body(ctx, extra_pass, NULL)) {
		prog = orig_prog;
		goto out_offset;
	}

	for (i = 0; i < prog->len; i++) {
		prev_ninsns += 32;
		ctx->offset[i] = prev_ninsns;
	}

	for (i = 0; i < NR_JIT_ITERATIONS; i++) {
		pass++;
		ctx->ninsns = 0;

		bpf_jit_build_prologue(ctx, bpf_is_subprog(prog));
		ctx->prologue_len = ctx->ninsns;

		if (build_body(ctx, extra_pass, ctx->offset)) {
			prog = orig_prog;
			goto out_offset;
		}

		ctx->epilogue_offset = ctx->ninsns;
		bpf_jit_build_epilogue(ctx);

		if (ctx->ninsns == prev_ninsns) {
			if (jit_data->header)
				break;

			prog_size = sizeof(*ctx->insns) * ctx->ninsns;
			jit_data->header =
				bpf_jit_binary_alloc(prog_size,
						     &jit_data->image,
						     sizeof(u32),
						     bpf_fill_ill_insns);
			if (!jit_data->header) {
				prog = orig_prog;
				goto out_offset;
			}
			ctx->insns = (u32 *)jit_data->image;
		}
		prev_ninsns = ctx->ninsns;
	}

	if (i == NR_JIT_ITERATIONS) {
		pr_err("bpf-jit: image did not converge in %d passes!\n", i);
		prog = orig_prog;
		goto out_free_hdr;
	}

skip_init_ctx:
	pass++;
	ctx->ninsns = 0;

	bpf_jit_build_prologue(ctx, bpf_is_subprog(prog));
	if (build_body(ctx, extra_pass, NULL)) {
		prog = orig_prog;
		goto out_free_hdr;
	}
	bpf_jit_build_epilogue(ctx);

	if (bpf_jit_enable > 1)
		bpf_jit_dump(prog->len, prog_size, pass, ctx->insns);

	prog->bpf_func = (void *)ctx->insns;
	prog->jited = 1;
	prog->jited_len = prog_size;

	if (!prog->is_func || extra_pass) {
		if (WARN_ON(bpf_jit_binary_lock_ro(jit_data->header)))
			goto out_free_hdr;
		bpf_flush_icache(jit_data->header,
				 (u8 *)jit_data->header +
				 jit_data->header->size);
		for (i = 0; i < prog->len; i++)
			ctx->offset[i] = ninsns_rvoff(ctx->offset[i]);
		bpf_prog_fill_jited_linfo(prog, ctx->offset);
out_offset:
		kfree(ctx->offset);
		kfree(jit_data);
		prog->aux->jit_data = NULL;
	}
out:
	return prog;

out_free_hdr:
	bpf_jit_binary_free(jit_data->header);
	goto out_offset;
}

