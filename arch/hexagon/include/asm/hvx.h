#ifndef __ASM_HVX_H
#define __ASM_HVX_H

/*
 * thread refers which thread is actually hot in the registers
 */

#define HVX_CTXT_LOCK_MASK 0x1

struct hvx_ctxt_status {
	atomic_t flags;  /*  Only manipulate with atomic_cmpxchg  */
	struct thread_info *thread;
	unsigned long generation;
};

struct hvx_threadinfo {
	int cnum;	/*  Context # if thread is holding a ctxt reservation; -1 if not  */
	int prev_cnum;
	struct HVX_Vectors *vregs;
	unsigned long generation;
};

extern void restore_hvx_context(struct HVX_Vectors *);
extern void save_hvx_context(struct HVX_Vectors *);

#endif /* __ASM_HVX_H */
