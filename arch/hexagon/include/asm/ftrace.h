/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_HEXAGON_FTRACE_H
#define _ASM_HEXAGON_FTRACE_H

#ifndef __ASSEMBLY__
#include <linux/string.h>

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
