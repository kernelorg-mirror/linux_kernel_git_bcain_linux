/* SPDX-License-Identifier: GPL-2.0 */
/*
 * syscall_wrapper.h - hexagon specific wrappers to syscall definitions
 *
 * Based on arch/riscv/include/asm/syscall_wrapper.h
 */

#ifndef _ASM_HEXAGON_SYSCALL_WRAPPER_H
#define _ASM_HEXAGON_SYSCALL_WRAPPER_H

#include <asm/ptrace.h>

asmlinkage long __hexagon_sys_ni_syscall(const struct pt_regs *);

/*
 * Hexagon passes the six syscall arguments in r0-r5, and 64-bit arguments in
 * even/odd register pairs.  Declaring the callee as six longs and aliasing it
 * to the real definition keeps that register assignment intact for syscalls
 * that take >word-size arguments, the same way 32-bit riscv does it.
 */
#define __SYSCALL_SE_DEFINEx(x, prefix, name, ...)				\
	__diag_push();								\
	__diag_ignore(GCC, 8, "-Wattribute-alias",				\
		      "Type aliasing is used to sanitize syscall arguments");	\
	__diag_ignore(clang, 23, "-Wunknown-warning-option",			\
		      "Avoid breaking versions without -Wattribute-alias");	\
	__diag_ignore(clang, 23, "-Wattribute-alias",				\
		      "Type aliasing is used to sanitize syscall arguments");	\
	static long __se_##prefix##name(ulong, ulong, ulong, ulong, ulong,	\
					ulong)					\
		__attribute__((alias(__stringify(___se_##prefix##name))));	\
	__diag_pop();								\
	static long noinline ___se_##prefix##name(__MAP(x,__SC_LONG,__VA_ARGS__))\
		__used;								\
	static long ___se_##prefix##name(__MAP(x,__SC_LONG,__VA_ARGS__))

/*
 * restart_r0 rather than r00: do_trap0() checkpoints the incoming r0 there
 * before dispatching, and r00 is overwritten with the return value, so this is
 * the copy that survives a restarted syscall.
 */
#define SC_HEXAGON_REGS_TO_ARGS(x, ...)						\
	regs->restart_r0,regs->r01,regs->r02,regs->r03,regs->r04,regs->r05

#define __SYSCALL_DEFINEx(x, name, ...)						\
	asmlinkage long __hexagon_sys##name(const struct pt_regs *regs);	\
	ALLOW_ERROR_INJECTION(__hexagon_sys##name, ERRNO);			\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__));	\
	__SYSCALL_SE_DEFINEx(x, sys, name, __VA_ARGS__)				\
	{									\
		long ret = __do_sys##name(__MAP(x,__SC_CAST,__VA_ARGS__));	\
		__MAP(x,__SC_TEST,__VA_ARGS__);					\
		__PROTECT(x, ret,__MAP(x,__SC_ARGS,__VA_ARGS__));		\
		return ret;							\
	}									\
	asmlinkage long __hexagon_sys##name(const struct pt_regs *regs)		\
	{									\
		return __se_sys##name(SC_HEXAGON_REGS_TO_ARGS(x,__VA_ARGS__));	\
	}									\
	static inline long __do_sys##name(__MAP(x,__SC_DECL,__VA_ARGS__))

#define SYSCALL_DEFINE0(sname)							\
	SYSCALL_METADATA(_##sname, 0);						\
	asmlinkage long __hexagon_sys_##sname(const struct pt_regs *__unused);	\
	ALLOW_ERROR_INJECTION(__hexagon_sys_##sname, ERRNO);			\
	asmlinkage long __hexagon_sys_##sname(const struct pt_regs *__unused)

#define COND_SYSCALL(name)							\
	asmlinkage long __weak __hexagon_sys_##name(const struct pt_regs *regs);\
	asmlinkage long __weak __hexagon_sys_##name(const struct pt_regs *regs)	\
	{									\
		return sys_ni_syscall();					\
	}

#endif /* _ASM_HEXAGON_SYSCALL_WRAPPER_H */
