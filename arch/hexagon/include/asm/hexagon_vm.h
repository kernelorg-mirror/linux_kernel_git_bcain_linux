/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Declarations for to Hexagon Virtal Machine.
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#ifndef ASM_HEXAGON_VM_H
#define ASM_HEXAGON_VM_H

/*
 * In principle, a Linux kernel for the VM could
 * selectively define the virtual instructions
 * as inline assembler macros, but for a first pass,
 * we'll use subroutines for both the VM and the native
 * kernels.  It's costing a subroutine call/return,
 * but it makes for a single set of entry points
 * for tracing/debugging.
 */

#define HVM_TRAP1_VMVERSION		0
#define HVM_TRAP1_VMRTE			1
#define HVM_TRAP1_VMSETVEC		2
#define HVM_TRAP1_VMSETIE		3
#define HVM_TRAP1_VMGETIE		4
#define HVM_TRAP1_VMINTOP		5
#define HVM_TRAP1_VMCLRMAP		10
#define HVM_TRAP1_VMNEWMAP		11
#define HVM_TRAP1_FORMERLY_VMWIRE	12
#define HVM_TRAP1_VMCACHE		13
#define HVM_TRAP1_VMGETTIME		14
#define HVM_TRAP1_VMSETTIME		15
#define HVM_TRAP1_VMWAIT		16
#define HVM_TRAP1_VMYIELD		17
#define HVM_TRAP1_VMSTART		18
#define HVM_TRAP1_VMSTOP		19
#define HVM_TRAP1_VMVPID		20
#define HVM_TRAP1_VMSETREGS		21
#define HVM_TRAP1_VMGETREGS		22
#define HVM_TRAP1_VMTIMEROP		24
#define HVM_TRAP1_VMPMUCONFIG		25
#define HVM_TRAP1_VMGETINFO		26

#define VM_CLOBBERLIST_ABIV2 "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",\
	"r9", "r10", "r11", "r12", "r13", "r14", "r15", "r28", "r31", "sa0",\
	"lc0", "sa1", "lc1", "m0", "m1", "usr", "p0", "p1", "p2", "p3"

#define VM_CLOBBERLIST_ABIV1 VM_CLOBBERLIST_ABIV2, "r16", "r17", "r18",\
	"r19", "r20", "r21", "r22", "r23"

#if __HEXAGON_ARCH__ >= 3
#define VM_CLOBBERLIST VM_CLOBBERLIST_ABIV2
#else
#define VM_CLOBBERLIST VM_CLOBBERLIST_ABIV1
#endif

#ifndef __ASSEMBLY__

enum VM_CACHE_OPS {
	hvmc_ickill,
	hvmc_dckill,
	hvmc_l2kill,
	hvmc_dccleaninva,
	hvmc_icinva,
	hvmc_idsync,
	hvmc_fetch_cfg
};

enum VM_INT_OPS {
	hvmi_nop,
	hvmi_globen,
	hvmi_globdis,
	hvmi_locen,
	hvmi_locdis,
	hvmi_affinity,
	hvmi_get,
	hvmi_peek,
	hvmi_status,
	hvmi_post,
	hvmi_clear
};

enum VM_TIMER_OPS {
	getfreq,
	getres,
	gettime,
	gettimeout,
	settimeout,
	deltatimeout
};

enum VM_STOP_STATUS {
	none,
	poweroff,
	halt,
	restart,
	machinecheck
};

enum VM_INFO_TYPE {
        vm_info_build_id,
        vm_info_boot_flags,
        vm_info_stlb,
        vm_info_syscfg,
	vm_info_livelock,
	vm_info_rev,
	vm_info_ssbase,
	vm_info_tlb_free,
	vm_info_tlb_size,
	vm_info_physaddr,
	vm_info_tcm_base,
	vm_info_l2mem_size,
	vm_info_tcm_size,
	vm_info_h2k_pgsize,
	vm_info_h2k_npages,
	vm_info_l2vic_base,
	vm_info_timer_base,
	vm_info_timer_int,
	vm_info_error,
	vm_info_hthreads,
	vm_info_l2tag_size,
	vm_info_l2cfg_base,
	vm_info_clade_base,
	vm_info_cfgbase,
	vm_info_hvx_vlength,
	vm_info_hvx_contexts,
	vm_info_hvx_switch,
        vm_info_max
};

typedef enum {
	HWCONFIG_L2CACHE,
	HWCONFIG_PARTITIONS,
	HWCONFIG_PREFETCH,
	HWCONFIG_EXTBITS,
	HWCONFIG_VLENGTH,
	HWCONFIG_EXTPOWER,
	HWCONFIG_GETL2REG,
	HWCONFIG_SETL2REG,
	HWCONFIG_L2LOCKA,
	HWCONFIG_L2UNLOCK,
	HWCONFIG_HWINTOP,
	HWCONFIG_RESERVED_00,
	HWCONFIG_RESERVED_01,
	HWCONFIG_HWTHREADS_NUM,
	HWCONFIG_HWTHREADS_MASK,
	HWCONFIG_RESERVED_02,
	HWCONFIG_HMXBITS,
	HWCONFIG_MAX
} hwconfig_type_t;

struct vm_syscfg {
	union {
		unsigned long raw;
		struct {
			unsigned long m:1;
			unsigned long i:1;
			unsigned long d:1;
			unsigned long t:1;
			unsigned long g:1;
			unsigned long r:1;
			unsigned long c:1;

			unsigned long unused4:1;

			unsigned long ida:1;
			unsigned long pm:1;

			unsigned long te:1;
			unsigned long tl:1;
			unsigned long kl:1;

			unsigned long bq:1;

			unsigned long unused3:1;

			unsigned long dmt:1;


			unsigned long l2cfg:3;

			unsigned long itcm:1;

			unsigned long unused2:1;

			unsigned long l2nwa:1;
			unsigned long l2nra:1;

			unsigned long l2wb:1;
			unsigned long l2p:1;

			unsigned long l1dp:2;
			unsigned long l1ip:2;
			unsigned long l2part:2;
			unsigned long unused:1;
		};
	};
};

struct vm_rev {
	union {
		unsigned long raw;
		struct {
			unsigned long isa:8;
			unsigned long l2tags:4;
			unsigned long l2size:4;
			unsigned long layer:4;
			unsigned long unused:4;
			unsigned long metal:8;
		};
	};
};



struct vm_boot_flags {
	union {
		unsigned long raw;
		struct {
			unsigned long use_tcm:1;
			unsigned long unused:31;
		};
	};
};

struct vm_stlb_info {
	union {
		unsigned long raw;
		struct {
			unsigned long max_sets_log2:8;
			unsigned long max_ways:8;
			unsigned long size:8;
			unsigned long unused:7;
			unsigned long enabled:1;
		};
	};
};

extern void _K_VM_event_vector(void);

void __vmrte(void);
long __vmsetvec(void *);

void load_ie_cache(void);
long vmgetie_cached(void);
long __vmsetie(long ie);
long vmsetie_cached(long ie);
long __vmsetie_cached(long ie, long *cache);
void clear_ie_cached(void);

long __vmgetie(void);
long __vmintop(enum VM_INT_OPS, long, long, long, long);
long __vmclrmap(void *, unsigned long);
long __vmnewmap(void *, unsigned long type, unsigned long tlb_flush_flag);
long __vmcache(enum VM_CACHE_OPS op, unsigned long addr, unsigned long len);
unsigned long long __vmgettime(void);
long __vmsettime(unsigned long long);
long __vmstart(void *, void *, int relprio);
void __vmstop(enum VM_STOP_STATUS);
long __vmwait(void);
void __vmyield(void);
long __vmvpid(void);
unsigned long __vmgetinfo(unsigned long);

/*  I think arg2 can be signed...  */
unsigned long __vmpmucfg(unsigned long operation, unsigned long r1, long r2, unsigned long r3);

/*  Totally dangerous HWCONFIG  */

long __vmhwconfig(unsigned long, unsigned long, unsigned long, unsigned long);

#ifdef CONFIG_H2
unsigned long long __vmtimerop(enum VM_TIMER_OPS op, unsigned long dummy, unsigned long long timeout);
#endif

unsigned long __vmversion(void);

static inline long __vmcache_ickill(void)
{
	return __vmcache(hvmc_ickill, 0, 0);
}

static inline long __vmcache_dckill(void)
{
	return __vmcache(hvmc_dckill, 0, 0);
}

static inline long __vmcache_l2kill(void)
{
	return __vmcache(hvmc_l2kill, 0, 0);
}

static inline long __vmcache_dccleaninva(unsigned long addr, unsigned long len)
{
	return __vmcache(hvmc_dccleaninva, addr, len);
}

static inline long __vmcache_icinva(unsigned long addr, unsigned long len)
{
	return __vmcache(hvmc_icinva, addr, len);
}

static inline long __vmcache_idsync(unsigned long addr,
					   unsigned long len)
{
#ifdef CONFIG_HEXAGON_NOCACHESYNC
		return 1;
#else
	return __vmcache(hvmc_idsync, addr, len);
#endif
}

static inline long __vmcache_fetch_cfg(unsigned long val)
{
	return __vmcache(hvmc_fetch_cfg, val, 0);
}

/* interrupt operations  */

static inline long __vmintop_nop(void)
{
	return __vmintop(hvmi_nop, 0, 0, 0, 0);
}

static inline long __vmintop_globen(long i)
{
	return __vmintop(hvmi_globen, i, 0, 0, 0);
}

static inline long __vmintop_globdis(long i)
{
	return __vmintop(hvmi_globdis, i, 0, 0, 0);
}

static inline long __vmintop_locen(long i)
{
	return __vmintop(hvmi_locen, i, 0, 0, 0);
}

static inline long __vmintop_locdis(long i)
{
	return __vmintop(hvmi_locdis, i, 0, 0, 0);
}

static inline long __vmintop_affinity(long i, long cpu)
{
	return __vmintop(hvmi_affinity, i, cpu, 0, 0);
}

static inline long __vmintop_get(void)
{
	return __vmintop(hvmi_get, 0, 0, 0, 0);
}

static inline long __vmintop_peek(void)
{
	return __vmintop(hvmi_peek, 0, 0, 0, 0);
}

static inline long __vmintop_status(long i)
{
	return __vmintop(hvmi_status, i, 0, 0, 0);
}

static inline long __vmintop_post(long i, long cpu)
{
	return __vmintop(hvmi_post, i, cpu, 0, 0);
}

static inline long __vmintop_clear(long i)
{
	return __vmintop(hvmi_clear, i, 0, 0, 0);
}

#else /* Only assembly code should reference these */

#endif /* __ASSEMBLY__ */

/*
 * Constants for virtual instruction parameters and return values
 */

/* vmnewmap arguments */

#define VM_TRANS_TYPE_LINEAR 0
#define VM_TRANS_TYPE_TABLE 1
#define VM_TLB_INVALIDATE_FALSE 0
#define VM_TLB_INVALIDATE_TRUE 1

/* vmsetie arguments */

#define VM_INT_DISABLE	0
#define VM_INT_ENABLE	1

/* vmsetimask arguments */

#define VM_INT_UNMASK	0
#define VM_INT_MASK	1

#define VM_NEWMAP_TYPE_LINEAR	0
#define VM_NEWMAP_TYPE_PGTABLES	1


/*
 * Event Record definitions useful to both C and Assembler
 */

/* VMEST Layout */

#define HVM_VMEST_UM_SFT	31
#define HVM_VMEST_UM_MSK	1
#define HVM_VMEST_IE_SFT	30
#define HVM_VMEST_IE_MSK	1
#define HVM_VMEST_SS_SFT	29
#define HVM_VMEST_SS_MSK	1
#define HVM_VMEST_EVENTNUM_SFT	16
#define HVM_VMEST_EVENTNUM_MSK	0xff
#define HVM_VMEST_CAUSE_SFT	0
#define HVM_VMEST_CAUSE_MSK	0xffff

/*
 * The initial program gets to find a system environment descriptor
 * on its stack when it begins execution. The first word is a version
 * code to indicate what is there.  Zero means nothing more.
 */

#define HEXAGON_VM_SED_NULL	0

/*
 * Event numbers for vector binding
 */

#define HVM_EV_RESET		0
#define HVM_EV_MACHCHECK	1
#define HVM_EV_GENEX		2
#define HVM_EV_TRAP		8
#define HVM_EV_INTR		15
/* These shoud be nuked as soon as we know the VM is up to spec v0.1.1 */
#define HVM_EV_INTR_0		16
#define HVM_MAX_INTR		240

/*
 * Cause values for General Exception
 */

#define HVM_GE_C_BUS	0x01
#define HVM_GE_C_XPROT	0x11
#define HVM_GE_C_XUSER	0x12
#define HVM_GE_C_INVI	0x15
#define HVM_GE_C_COPROC 0x16
#define HVM_GE_C_PRIVI	0x1B
#define HVM_GE_C_XMAL	0x1C
#define HVM_GE_C_WREG	0x1D
#define HVM_GE_C_PCAL	0x1E
#define HVM_GE_C_RMAL	0x20
#define HVM_GE_C_WMAL	0x21
#define HVM_GE_C_RPROT	0x22
#define HVM_GE_C_WPROT	0x23
#define HVM_GE_C_RUSER	0x24
#define HVM_GE_C_WUSER	0x25
#define HVM_GE_C_CACHE	0x28

/* Extended TLB miss cause codes; earlier ones might be deprecated */
#define HVM_GE_C_TLBMISSX_0		0x60
#define HVM_GE_C_TLBMISSX_1		0x61
#define HVM_GE_C_TLBMISSX_ICINVA	0x62
#define HVM_GE_C_TLBMISSR		0x70
#define HVM_GE_C_TLBMISSW		0x71

/*
 * Cause codes for Machine Check
 */

#define	HVM_MCHK_C_DOWN		0x00
#define	HVM_MCHK_C_BADSP	0x01
#define	HVM_MCHK_C_BADEX	0x02
#define	HVM_MCHK_C_BADPT	0x03
#define	HVM_MCHK_C_REGWR	0x29

#endif
