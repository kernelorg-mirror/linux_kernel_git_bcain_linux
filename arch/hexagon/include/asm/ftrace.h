/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_HEXAGON_FTRACE_H
#define _ASM_HEXAGON_FTRACE_H

/* -mlong-calls emits an extender followed by the call instruction. */
#define MCOUNT_INSN_SIZE 8
#define ARCH_SUPPORTS_FTRACE_OPS 1

#ifndef __ASSEMBLY__
#include <linux/string.h>

#ifdef CONFIG_FUNCTION_TRACER
struct ftrace_ops;
extern struct ftrace_ops *function_trace_op;
void mcount(void);
void hexagon_ftrace_caller(unsigned long ip, unsigned long parent_ip,
			  unsigned long *parent);
#endif

#ifdef CONFIG_FTRACE_SYSCALLS
/*
 * The syscall functions are named __hexagon_sys_<name>, whereas the syscall
 * tracepoints are named after <name>.
 */
#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym,
					       const char *name)
{
	return !strcmp(sym + strlen("__hexagon_"), name);
}
#endif /* CONFIG_FTRACE_SYSCALLS */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_HEXAGON_FTRACE_H */
