/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hexagon eBPF JIT compiler -- instruction encoding helpers
 *
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _HEXAGON_BPF_JIT_H
#define _HEXAGON_BPF_JIT_H

#include <linux/bpf.h>
#include <linux/filter.h>

/* Hexagon register numbers */
enum {
	HEX_REG_R0  =  0,
	HEX_REG_R1  =  1,
	HEX_REG_R2  =  2,
	HEX_REG_R3  =  3,
	HEX_REG_R4  =  4,
	HEX_REG_R5  =  5,
	HEX_REG_R6  =  6,
	HEX_REG_R7  =  7,
	HEX_REG_R8  =  8,
	HEX_REG_R9  =  9,
	HEX_REG_R10 = 10,
	HEX_REG_R11 = 11,
	HEX_REG_R12 = 12,
	HEX_REG_R13 = 13,
	HEX_REG_R14 = 14,
	HEX_REG_R15 = 15,
	HEX_REG_R16 = 16,
	HEX_REG_R17 = 17,
	HEX_REG_R18 = 18,
	/* R19 is reserved for thread_info (never touch) */
	HEX_REG_R20 = 20,
	HEX_REG_R21 = 21,
	HEX_REG_R22 = 22,
	HEX_REG_R23 = 23,
	HEX_REG_R24 = 24,
	HEX_REG_R25 = 25,
	HEX_REG_R26 = 26,
	HEX_REG_R27 = 27,
	HEX_REG_R28 = 28,
	HEX_REG_SP  = 29,
	HEX_REG_FP  = 30,
	HEX_REG_LR  = 31,
};

/* Predicate registers */
enum {
	HEX_REG_P0 = 0,
	HEX_REG_P1 = 1,
	HEX_REG_P2 = 2,
	HEX_REG_P3 = 3,
};

/*
 * Every emitted instruction forms a single-instruction packet.
 * Parse bits [15:14] = 0b11 marks end-of-packet.
 */
#define PACKET_END	(0x3u << 14)

/* JIT context */
struct hexagon_jit_context {
	struct bpf_prog *prog;
	u32 *insns;		/* emitted Hexagon instructions (RW image) */
	u32 *ro_insns;		/* read-only image base for offset calc */
	int ninsns;		/* number of instructions emitted */
	int prologue_len;	/* prologue length in instructions */
	int epilogue_offset;	/* epilogue start in instructions */
	int *offset;		/* BPF insn index -> Hexagon insn offset */
	int stack_size;		/* total stack frame size */
};

struct hexagon_jit_data {
	struct bpf_binary_header *header;
	struct bpf_binary_header *ro_header;
	u8 *image;
	u8 *ro_image;
	struct hexagon_jit_context ctx;
};

/* Convert instruction count to byte offset */
static inline int ninsns_rvoff(int ninsns)
{
	return ninsns << 2;  /* 4 bytes per instruction */
}

static inline void bpf_fill_ill_insns(void *area, unsigned int size)
{
	memset(area, 0, size);
}

static inline void bpf_flush_icache(void *start, void *end)
{
	flush_icache_range((unsigned long)start, (unsigned long)end);
}

/* Emit one 32-bit Hexagon instruction as a single-instruction packet. */
static inline void emit(u32 insn, struct hexagon_jit_context *ctx)
{
	/* Clear PP field and set to end-of-packet */
	insn = (insn & ~(0x3u << 14)) | PACKET_END;
	if (ctx->insns)
		ctx->insns[ctx->ninsns] = insn;
	ctx->ninsns++;
}

static inline int epilogue_offset(struct hexagon_jit_context *ctx)
{
	int to = ctx->epilogue_offset, from = ctx->ninsns;

	return ninsns_rvoff(to - from);
}

static inline int hex_offset(int insn, int off, struct hexagon_jit_context *ctx)
{
	int from, to;

	off++;  /* BPF branch is from PC+1, Hex is from PC */
	from = (insn > 0) ? ctx->offset[insn - 1] : ctx->prologue_len;
	to = (insn + off > 0) ? ctx->offset[insn + off - 1] : ctx->prologue_len;
	return ninsns_rvoff(to - from);
}

/* Return -1 or inverted cond. */
static inline int invert_bpf_cond(u8 cond)
{
	switch (cond) {
	case BPF_JEQ:  return BPF_JNE;
	case BPF_JGT:  return BPF_JLE;
	case BPF_JLT:  return BPF_JGE;
	case BPF_JGE:  return BPF_JLT;
	case BPF_JLE:  return BPF_JGT;
	case BPF_JNE:  return BPF_JEQ;
	case BPF_JSGT: return BPF_JSLE;
	case BPF_JSLT: return BPF_JSGE;
	case BPF_JSGE: return BPF_JSLT;
	case BPF_JSLE: return BPF_JSGT;
	}
	return -1;
}

/*
 * Immediate range checks.
 * Hexagon uses various immediate widths; these helpers verify fit.
 */
static inline bool is_s8(s32 val) { return val >= -128 && val <= 127; }
static inline bool is_s10(s32 val) { return val >= -512 && val <= 511; }
static inline bool is_s11(s32 val) { return val >= -1024 && val <= 1023; }
static inline bool is_s16(s32 val) { return val >= -32768 && val <= 32767; }
static inline bool is_u5(s32 val)  { return val >= 0 && val <= 31; }
static inline bool is_u16(s32 val) { return val >= 0 && val <= 65535; }

/*
 * =====================================================================
 * Hexagon instruction encoding functions
 *
 * Each function returns a 32-bit instruction word WITHOUT the PP bits set.
 * The emit() function will set PP=0b11 (end-of-packet) when emitting.
 *
 * Encoding templates from Hexagon Programmer's Reference Manual and
 * qemu/target/hexagon/imported/encode_pp.def
 * =====================================================================
 */

/* Rd = add(Rs, Rt) : 11110011000sssss PP-ttttt---ddddd */
static inline u32 hex_a2_add(u8 rd, u8 rs, u8 rt)
{
	return 0xf3000000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rd = sub(Rt, Rs) : 11110011001sssss PP-ttttt---ddddd */
static inline u32 hex_a2_sub(u8 rd, u8 rt, u8 rs)
{
	return 0xf3200000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rd = and(Rs, Rt) : 11110001000sssss PP-ttttt---ddddd */
static inline u32 hex_a2_and(u8 rd, u8 rs, u8 rt)
{
	return 0xf1000000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rd = or(Rs, Rt) : 11110001001sssss PP-ttttt---ddddd */
static inline u32 hex_a2_or(u8 rd, u8 rs, u8 rt)
{
	return 0xf1200000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rd = xor(Rs, Rt) : 11110001011sssss PP-ttttt---ddddd */
static inline u32 hex_a2_xor(u8 rd, u8 rs, u8 rt)
{
	return 0xf1600000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rd = Rs (transfer) : 01110000011sssss PP0--------ddddd */
static inline u32 hex_a2_tfr(u8 rd, u8 rs)
{
	return 0x70600000 | ((rs & 0x1f) << 16) | (rd & 0x1f);
}

/*
 * Rd = #s16 (transfer immediate signed 16-bit)
 * Encoding: 01111000ii-iiiii PPiiiiiiiiiddddd
 * Bits [27:24] = 1000, [23:21] = imm[15:13], [20:16] = imm[12:8],
 * [13:5] = imm[7:0]<<5? -- Let's encode carefully.
 *
 * Actual encoding from the manual:
 *   0111 1000 iiii iiii PPii iiii iiid dddd
 * Where s16 is split as: [27:21] = s16[15:9], [13:5] = s16[8:0]
 */
static inline u32 hex_a2_tfrsi(u8 rd, s16 imm)
{
	u32 u = (u16)imm;

	return 0x78000000 |
	       ((u & 0xc000) << (22 - 14)) |  /* imm[15:14] -> bits [23:22] */
	       ((u & 0x3e00) << (16 - 9))  |  /* imm[13:9]  -> bits [20:16] */
	       ((u & 0x01ff) << 5)          |  /* imm[8:0]   -> bits [13:5]  */
	       (rd & 0x1f);
}

/*
 * Rd = add(Rs, #s16)
 * Encoding: 1011 iiii iiis ssss PPii iiii iiid dddd
 */
static inline u32 hex_a2_addi(u8 rd, u8 rs, s16 imm)
{
	u32 u = (u16)imm;

	return 0xb0000000 |
	       ((u & 0xfe00) << (21 - 9))  |  /* imm[15:9] -> bits [27:21] */
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |  /* imm[8:0]  -> bits [13:5]  */
	       (rd & 0x1f);
}

/*
 * Rd = sub(#s10, Rs)
 * Encoding: 0111 0110 01is ssss PPii iiii iiid dddd
 */
static inline u32 hex_a2_subri(u8 rd, s16 imm, u8 rs)
{
	u32 u = (u16)(imm & 0x3ff);  /* 10-bit */

	return 0x76400000 |
	       ((u & 0x0200) << (21 - 9))  |  /* imm[9] -> bit [21] */
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |  /* imm[8:0] -> bits [13:5] */
	       (rd & 0x1f);
}

/*
 * Rx.H = #u16 (set high halfword)
 * Encoding: 0111 0010 ii1x xxxx PPii iiii iiii iiii
 */
static inline u32 hex_a2_tfrih(u8 rx, u16 imm)
{
	u32 u = imm;

	return 0x72200000 |
	       ((u & 0xc000) << (22 - 14))  |  /* imm[15:14] -> bits [23:22] */
	       ((rx & 0x1f) << 16)          |
	       (u & 0x3fff);                    /* imm[13:0] -> bits [13:0] */
}

/*
 * Rx.L = #u16 (set low halfword)
 * Encoding: 0111 0001 ii1x xxxx PPii iiii iiii iiii
 */
static inline u32 hex_a2_tfril(u8 rx, u16 imm)
{
	u32 u = imm;

	return 0x71200000 |
	       ((u & 0xc000) << (22 - 14))  |  /* imm[15:14] -> bits [23:22] */
	       ((rx & 0x1f) << 16)          |
	       (u & 0x3fff);                    /* imm[13:0] -> bits [13:0] */
}

/*
 * Rd = cmp.eq(Rs,Rt) -- returns 0 or 1 in Rd
 * Encoding: 11110011010sssss PP-ttttt---ddddd
 */
static inline u32 hex_a4_rcmpeq(u8 rd, u8 rs, u8 rt)
{
	return 0xf3400000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rd = mpyi(Rs, Rt) : 11101101000sssss PP0ttttt000ddddd */
static inline u32 hex_m2_mpyi(u8 rd, u8 rs, u8 rt)
{
	return 0xed000000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/* Rdd = mpyu(Rs, Rt) 32x32->64 unsigned : 11100101010sssss PP0ttttt000ddddd */
static inline u32 hex_m2_dpmpyuu_s0(u8 rdd, u8 rs, u8 rt)
{
	return 0xe5400000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rdd & 0x1f);
}

/*
 * Rd = asl(Rs, #u5) -- shift left immediate
 * Encoding: 10001100 000sssss PP0iiiii 010ddddd
 */
static inline u32 hex_s2_asl_i_r(u8 rd, u8 rs, u8 imm)
{
	return 0x8c000000 | ((rs & 0x1f) << 16) | ((imm & 0x1f) << 8) |
	       0x00000040 | (rd & 0x1f);
}

/*
 * Rd = lsr(Rs, #u5) -- logical shift right immediate
 * Encoding: 10001100 000sssss PP0iiiii 001ddddd
 */
static inline u32 hex_s2_lsr_i_r(u8 rd, u8 rs, u8 imm)
{
	return 0x8c000000 | ((rs & 0x1f) << 16) | ((imm & 0x1f) << 8) |
	       0x00000020 | (rd & 0x1f);
}

/*
 * Rd = asr(Rs, #u5) -- arithmetic shift right immediate
 * Encoding: 10001100 000sssss PP0iiiii 000ddddd
 */
static inline u32 hex_s2_asr_i_r(u8 rd, u8 rs, u8 imm)
{
	return 0x8c000000 | ((rs & 0x1f) << 16) | ((imm & 0x1f) << 8) |
	       (rd & 0x1f);
}

/*
 * Rd = asl(Rs, Rt) -- shift left by register
 * Encoding: 1100 0110 01-sssss PP-ttttt 10-ddddd
 */
static inline u32 hex_s2_asl_r_r(u8 rd, u8 rs, u8 rt)
{
	return 0xc6400000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       0x00000080 | (rd & 0x1f);
}

/*
 * Rd = lsr(Rs, Rt) -- logical shift right by register
 * Encoding: 1100 0110 01-sssss PP-ttttt 01-ddddd
 */
static inline u32 hex_s2_lsr_r_r(u8 rd, u8 rs, u8 rt)
{
	return 0xc6400000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       0x00000040 | (rd & 0x1f);
}

/*
 * Rd = asr(Rs, Rt) -- arithmetic shift right by register
 * Encoding: 1100 0110 01-sssss PP-ttttt 00-ddddd
 */
static inline u32 hex_s2_asr_r_r(u8 rd, u8 rs, u8 rt)
{
	return 0xc6400000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (rd & 0x1f);
}

/*
 * Rd = memub(Rs + #s11) -- load unsigned byte
 * Encoding: 10010ii1 001sssss PPiiiiii iiiddddd
 */
static inline u32 hex_l2_loadrub_io(u8 rd, u8 rs, s16 imm)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0x91200000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |
	       (rd & 0x1f);
}

/*
 * Rd = memuh(Rs + #s11:1) -- load unsigned halfword
 * Encoding: 10010ii1 011sssss PPiiiiii iiiddddd
 */
static inline u32 hex_l2_loadruh_io(u8 rd, u8 rs, s16 imm)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0x91600000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |
	       (rd & 0x1f);
}

/*
 * Rd = memw(Rs + #s11:2) -- load word
 * Encoding: 10010ii1 100sssss PPiiiiii iiiddddd
 */
static inline u32 hex_l2_loadri_io(u8 rd, u8 rs, s16 imm)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0x91800000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |
	       (rd & 0x1f);
}

/*
 * Rdd = memd(Rs + #s11:3) -- load doubleword (into register pair)
 * Encoding: 10010ii1 110sssss PPiiiiii iiiddddd
 * Note: imm is a pre-scaled s13, but the encoding stores s11 = imm >> 3.
 *       Caller must pass imm >> 3 as the offset.
 */
static inline u32 hex_l2_loadrd_io(u8 rdd, u8 rs, s16 imm)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0x91c00000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |
	       (rdd & 0x1f);
}

/*
 * memb(Rs + #s11) = Rt -- store byte
 * Encoding: 10100ii1 000sssss PPittttt iiiiiiii
 */
static inline u32 hex_s2_storerb_io(u8 rs, s16 imm, u8 rt)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0xa1000000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x0100) << (13 - 8))  |
	       ((rt & 0x1f) << 8)          |
	       (u & 0xff);
}

/*
 * memh(Rs + #s11:1) = Rt -- store halfword
 * Encoding: 10100ii1 010sssss PPittttt iiiiiiii
 */
static inline u32 hex_s2_storerh_io(u8 rs, s16 imm, u8 rt)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0xa1400000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x0100) << (13 - 8))  |
	       ((rt & 0x1f) << 8)          |
	       (u & 0xff);
}

/*
 * memw(Rs + #s11:2) = Rt -- store word
 * Encoding: 10100ii1 100sssss PPittttt iiiiiiii
 */
static inline u32 hex_s2_storeri_io(u8 rs, s16 imm, u8 rt)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0xa1800000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x0100) << (13 - 8))  |
	       ((rt & 0x1f) << 8)          |
	       (u & 0xff);
}

/*
 * memd(Rs + #s11:3) = Rtt -- store doubleword
 * Encoding: 10100ii1 110sssss PPittttt iiiiiiii
 */
static inline u32 hex_s2_storerd_io(u8 rs, s16 imm, u8 rtt)
{
	u32 u = (u16)(imm & 0x7ff);

	return 0xa1c00000 |
	       ((u & 0x0600) << (25 - 9))  |
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x0100) << (13 - 8))  |
	       ((rtt & 0x1f) << 8)         |
	       (u & 0xff);
}

/*
 * Pd = cmp.eq(Rs, Rt)
 * Encoding: 11110010 -00sssss PP-ttttt ---000dd
 */
static inline u32 hex_c2_cmpeq(u8 pd, u8 rs, u8 rt)
{
	return 0xf2000000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (pd & 0x3);
}

/*
 * Pd = cmp.gt(Rs, Rt) -- signed greater-than
 * Encoding: 11110010 -10sssss PP-ttttt ---000dd
 */
static inline u32 hex_c2_cmpgt(u8 pd, u8 rs, u8 rt)
{
	return 0xf2400000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (pd & 0x3);
}

/*
 * Pd = cmp.gtu(Rs, Rt) -- unsigned greater-than
 * Encoding: 11110010 -11sssss PP-ttttt ---000dd
 */
static inline u32 hex_c2_cmpgtu(u8 pd, u8 rs, u8 rt)
{
	return 0xf2600000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (pd & 0x3);
}

/*
 * jump #r22:2  -- PC-relative unconditional jump
 * Encoding: 0101 100i iiii iiii PPii iiii iiii iii-
 * The immediate is a signed 22-bit value, in units of 2 bytes (word-aligned).
 * Caller passes the byte offset; we encode offset >> 2.
 */
static inline u32 hex_j2_jump(s32 offset)
{
	u32 imm = ((u32)offset >> 2) & 0x3fffff;  /* 22-bit field */

	return 0x58000000 |
	       ((imm & 0x3fe000) << (15 - 13))  |  /* imm[21:13] -> [24:16] */
	       ((imm & 0x001fff) << 1);             /* imm[12:0]  -> [13:1]  */
}

/*
 * call #r22:2 -- PC-relative function call
 * Encoding: 0101 101i iiii iiii PPii iiii iiii iii0
 */
static inline u32 hex_j2_call(s32 offset)
{
	u32 imm = ((u32)offset >> 2) & 0x3fffff;

	return 0x5a000000 |
	       ((imm & 0x3fe000) << (15 - 13))  |
	       ((imm & 0x001fff) << 1);
}

/*
 * jumpr Rs -- indirect jump
 * Encoding: 0101 0010 100sssss PP--------------
 */
static inline u32 hex_j2_jumpr(u8 rs)
{
	return 0x52800000 | ((rs & 0x1f) << 16);
}

/*
 * callr Rs -- indirect call
 * Encoding: 0101 0000 101sssss PP--------------
 */
static inline u32 hex_j2_callr(u8 rs)
{
	return 0x50a00000 | ((rs & 0x1f) << 16);
}

/*
 * if (Pu) jump:nt #r15:2 -- conditional jump (taken)
 * Encoding: 0101 1100 ii0i iiii PPi 00-uu iiiii ii-
 * Caller passes byte offset; we encode offset >> 2.
 */
static inline u32 hex_j2_jumpt(u8 pu, s32 offset)
{
	u32 imm = ((u32)offset >> 2) & 0x7fff;  /* 15-bit field */

	return 0x5c000000 |
	       ((imm & 0x6000) << (22 - 13))  |  /* imm[14:13] -> [23:22] */
	       ((imm & 0x1f00) << (16 - 8))   |  /* imm[12:8]  -> [20:16] */
	       ((imm & 0x0080) << (13 - 7))   |  /* imm[7]     -> [13]    */
	       ((pu & 0x3) << 8)               |  /* Pu -> [9:8]           */
	       ((imm & 0x007f) << 1);             /* imm[6:0]   -> [7:1]  */
}

/*
 * if (!Pu) jump:nt #r15:2 -- conditional jump (not taken)
 * Encoding: 0101 1100 ii1i iiii PPi 00-uu iiiii ii-
 */
static inline u32 hex_j2_jumpf(u8 pu, s32 offset)
{
	u32 imm = ((u32)offset >> 2) & 0x7fff;

	return 0x5c000000 |
	       ((imm & 0x6000) << (22 - 13))  |
	       ((imm & 0x1f00) << (16 - 8))   |
	       (1 << 21)                       |  /* sense bit = 1 (false) */
	       ((imm & 0x0080) << (13 - 7))   |
	       ((pu & 0x3) << 8)               |
	       ((imm & 0x007f) << 1);
}

/* nop : 0111 1111 -------- PP-------------- */
static inline u32 hex_a2_nop(void)
{
	return 0x7f000000;
}

/*
 * Rd = swiz(Rs) -- byte-swap a word (endian reverse)
 * Encoding: 1000 1100 100sssss PP------111ddddd
 */
static inline u32 hex_a2_swiz(u8 rd, u8 rs)
{
	return 0x8c800000 | ((rs & 0x1f) << 16) | 0x000000e0 | (rd & 0x1f);
}

/*
 * Rd = memw_locked(Rs) -- load word locked (LL)
 * Encoding: 1001 0010 000sssss PP000---000ddddd
 */
static inline u32 hex_l2_loadw_locked(u8 rd, u8 rs)
{
	return 0x92000000 | ((rs & 0x1f) << 16) | (rd & 0x1f);
}

/*
 * memw_locked(Rs, Pd) = Rt -- store word locked (SC)
 * Encoding: 1010 0000 101sssss PP-ttttt----00dd
 */
static inline u32 hex_s2_storew_locked(u8 rs, u8 pd, u8 rt)
{
	return 0xa0a00000 | ((rs & 0x1f) << 16) | ((rt & 0x1f) << 8) |
	       (pd & 0x3);
}

/*
 * Pd = cmp.eq(Rs, #s10) -- compare register with signed 10-bit immediate
 * Encoding: 01110101 00isssssPPiiiiiiii000dd
 */
static inline u32 hex_c4_cmpeqi(u8 pd, u8 rs, s16 imm)
{
	u32 u = (u16)(imm & 0x3ff);

	return 0x75000000 |
	       ((u & 0x0200) << (21 - 9))  |  /* imm[9] -> bit [21] */
	       ((rs & 0x1f) << 16)         |
	       ((u & 0x01ff) << 5)         |  /* imm[8:0] -> bits [13:5] */
	       ((pd & 0x3));
}

/* Function declarations for the JIT compiler */
void bpf_jit_build_prologue(struct hexagon_jit_context *ctx, bool is_subprog);
void bpf_jit_build_epilogue(struct hexagon_jit_context *ctx);
int bpf_jit_emit_insn(const struct bpf_insn *insn,
		       struct hexagon_jit_context *ctx, bool extra_pass);

#endif /* _HEXAGON_BPF_JIT_H */
