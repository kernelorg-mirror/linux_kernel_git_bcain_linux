// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ptrace support for Hexagon
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/regset.h>
#include <linux/user.h>
#include <linux/elf.h>

#include <asm/user.h>
#include <asm/hvx.h>

struct pt_regs_offset {
	const char *name;
	int offset;
};

#define REG_OFFSET_NAME(r, member) \
	{ .name = #r, .offset = offsetof(struct pt_regs, member) }
#define GPR_OFFSET_NAME(n) REG_OFFSET_NAME(r##n, r##n)
#define REG_OFFSET_END { .name = NULL, .offset = 0 }

static const struct pt_regs_offset regoffset_table[] = {
	GPR_OFFSET_NAME(00), GPR_OFFSET_NAME(01), GPR_OFFSET_NAME(02),
	GPR_OFFSET_NAME(03), GPR_OFFSET_NAME(04), GPR_OFFSET_NAME(05),
	GPR_OFFSET_NAME(06), GPR_OFFSET_NAME(07), GPR_OFFSET_NAME(08),
	GPR_OFFSET_NAME(09), GPR_OFFSET_NAME(10), GPR_OFFSET_NAME(11),
	GPR_OFFSET_NAME(12), GPR_OFFSET_NAME(13), GPR_OFFSET_NAME(14),
	GPR_OFFSET_NAME(15), GPR_OFFSET_NAME(16), GPR_OFFSET_NAME(17),
	GPR_OFFSET_NAME(18), GPR_OFFSET_NAME(19), GPR_OFFSET_NAME(20),
	GPR_OFFSET_NAME(21), GPR_OFFSET_NAME(22), GPR_OFFSET_NAME(23),
	GPR_OFFSET_NAME(24), GPR_OFFSET_NAME(25), GPR_OFFSET_NAME(26),
	GPR_OFFSET_NAME(27), GPR_OFFSET_NAME(28), GPR_OFFSET_NAME(29),
	GPR_OFFSET_NAME(30), GPR_OFFSET_NAME(31),
	REG_OFFSET_NAME(sp, r29),
	REG_OFFSET_NAME(fp, r30),
	REG_OFFSET_NAME(lr, r31),
	REG_OFFSET_NAME(pc, hvmer.vmel),
	REG_OFFSET_NAME(usr, usr),
	REG_OFFSET_NAME(p3_0, preds),
	REG_OFFSET_NAME(m0, m0),
	REG_OFFSET_NAME(m1, m1),
	REG_OFFSET_NAME(sa0, sa0),
	REG_OFFSET_NAME(lc0, lc0),
	REG_OFFSET_NAME(sa1, sa1),
	REG_OFFSET_NAME(lc1, lc1),
	REG_OFFSET_NAME(gp, gp),
	REG_OFFSET_NAME(ugp, ugp),
	REG_OFFSET_NAME(cs0, cs0),
	REG_OFFSET_NAME(cs1, cs1),
	REG_OFFSET_END,
};

/* Offset of the register of this name in pt_regs, or -EINVAL. */
int regs_query_register_offset(const char *name)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name; roff++)
		if (!strcmp(roff->name, name))
			return roff->offset;
	return -EINVAL;
}

/* Name of the register at this offset in pt_regs, or NULL. */
const char *regs_query_register_name(unsigned int offset)
{
	const struct pt_regs_offset *roff;

	for (roff = regoffset_table; roff->name; roff++)
		if (roff->offset == offset)
			return roff->name;
	return NULL;
}

#if arch_has_single_step()
/*  Both called from ptrace_resume  */
void user_enable_single_step(struct task_struct *child)
{
	pt_set_singlestep(task_pt_regs(child));
	set_tsk_thread_flag(child, TIF_SINGLESTEP);
}

void user_disable_single_step(struct task_struct *child)
{
	pt_clr_singlestep(task_pt_regs(child));
	clear_tsk_thread_flag(child, TIF_SINGLESTEP);
}
#endif

static int genregs_get(struct task_struct *target,
		   const struct user_regset *regset,
		   struct membuf to)
{
	struct pt_regs *regs = task_pt_regs(target);

	/* The general idea here is that the copyout must happen in
	 * exactly the same order in which the userspace expects these
	 * regs. Now, the sequence in userspace does not match the
	 * sequence in the kernel, so everything past the 32 gprs
	 * happens one at a time.
	 */
	membuf_write(&to, &regs->r00, 32*sizeof(unsigned long));
	/* Must be exactly same sequence as struct user_regs_struct */
	membuf_store(&to, regs->sa0);
	membuf_store(&to, regs->lc0);
	membuf_store(&to, regs->sa1);
	membuf_store(&to, regs->lc1);
	membuf_store(&to, regs->m0);
	membuf_store(&to, regs->m1);
	membuf_store(&to, regs->usr);
	membuf_store(&to, regs->preds);
	membuf_store(&to, regs->gp);
	membuf_store(&to, regs->ugp);
	membuf_store(&to, pt_elr(regs)); // pc
	membuf_store(&to, (unsigned long)pt_cause(regs)); // cause
	membuf_store(&to, pt_badva(regs)); // badva
#if CONFIG_HEXAGON_ARCH_VERSION >=4
	membuf_store(&to, regs->cs0);
	membuf_store(&to, regs->cs1);
	return membuf_zero(&to, sizeof(unsigned long));
#else
	return membuf_zero(&to, 3 * sizeof(unsigned long));
#endif
}

static int genregs_set(struct task_struct *target,
		   const struct user_regset *regset,
		   unsigned int pos, unsigned int count,
		   const void *kbuf, const void __user *ubuf)
{
	int ret, ignore_offset;
	unsigned long bucket;
	struct pt_regs *regs = task_pt_regs(target);

	if (!regs)
		return -EIO;

	ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				 &regs->r00, 0, 32*sizeof(unsigned long));

#define INEXT(KPT_REG, USR_REG) \
	if (!ret) \
		ret = user_regset_copyin(&pos, &count, &kbuf, &ubuf, \
			KPT_REG, offsetof(struct user_regs_struct, USR_REG), \
			offsetof(struct user_regs_struct, USR_REG) + \
				sizeof(unsigned long));

	/* Must be exactly same sequence as struct user_regs_struct */
	INEXT(&regs->sa0, sa0);
	INEXT(&regs->lc0, lc0);
	INEXT(&regs->sa1, sa1);
	INEXT(&regs->lc1, lc1);
	INEXT(&regs->m0, m0);
	INEXT(&regs->m1, m1);
	INEXT(&regs->usr, usr);
	INEXT(&regs->preds, p3_0);
	INEXT(&regs->gp, gp);
	INEXT(&regs->ugp, ugp);
	INEXT(&pt_elr(regs), pc);

	/* CAUSE and BADVA aren't writeable. */
	INEXT(&bucket, cause);
	INEXT(&bucket, badva);

#if CONFIG_HEXAGON_ARCH_VERSION >=4
	INEXT(&regs->cs0, cs0);
	INEXT(&regs->cs1, cs1);
	ignore_offset = offsetof(struct user_regs_struct, pad1);
#else
	ignore_offset = offsetof(struct user_regs_struct, cs0);
#endif

	/* Ignore the rest, if needed */
	if (!ret)
		user_regset_copyin_ignore(&pos, &count, &kbuf, &ubuf,
					  ignore_offset, -1);
	else
		return ret;

	/*
	 * This is special; SP is actually restored by the VM via the
	 * special event record which is set by the special trap.
	 */
	regs->hvmer.vmpsp = regs->r29;
	return 0;
}

/*
 * The HVX registers of a thread that has used them: 32 vector registers and
 * the vector predicates.
 */
static int fpregs_get(struct task_struct *target,
		      const struct user_regset *regset,
		      struct membuf to)
{
	struct thread_info *ti = task_thread_info(target);

	if (!ti->extensions.hvx)
		return -EIO;

	return membuf_write(&to, ti->extensions.hvx->vregs,
			    sizeof(struct user_fpregs_struct));
}

static int fpregs_set(struct task_struct *target,
		      const struct user_regset *regset,
		      unsigned int pos, unsigned int count,
		      const void *kbuf, const void __user *ubuf)
{
	struct thread_info *ti = task_thread_info(target);

	if (!ti->extensions.hvx)
		return -EIO;

	return user_regset_copyin(&pos, &count, &kbuf, &ubuf,
				  ti->extensions.hvx->vregs, 0,
				  sizeof(struct user_fpregs_struct));
}

static int fpregs_active(struct task_struct *target,
			 const struct user_regset *regset)
{
	return task_thread_info(target)->extensions.hvx != NULL;
}

enum hexagon_regset {
	REGSET_GENERAL,
	REGSET_FPU,
};

static const struct user_regset hexagon_regsets[] = {
	[REGSET_GENERAL] = {
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n = ELF_NGREG,
		.size = sizeof(unsigned long),
		.align = sizeof(unsigned long),
		.regset_get = genregs_get,
		.set = genregs_set,
	},
	[REGSET_FPU] = {
		USER_REGSET_NOTE_TYPE(PRFPREG),
		.n = sizeof(struct user_fpregs_struct) / sizeof(HVX_Vector),
		.size = sizeof(HVX_Vector),
		.align = sizeof(HVX_Vector),
		.regset_get = fpregs_get,
		.set = fpregs_set,
		.active = fpregs_active,
	},
};

static const struct user_regset_view hexagon_user_view = {
	.name = "hexagon",
	.e_machine = ELF_ARCH,
	.ei_osabi = ELF_OSABI,
	.regsets = hexagon_regsets,
	.e_flags = ELF_CORE_EFLAGS,
	.n = ARRAY_SIZE(hexagon_regsets)
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &hexagon_user_view;
}

void ptrace_disable(struct task_struct *child)
{
	/* Boilerplate - resolves to null inline if no HW single-step */
	user_disable_single_step(child);
}

long arch_ptrace(struct task_struct *child, long request,
		 unsigned long addr, unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}
