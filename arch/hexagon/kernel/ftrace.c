// SPDX-License-Identifier: GPL-2.0-only
/* Function tracing for the Hexagon profiling ABI. */
#include <linux/ftrace.h>
#include <linux/kprobes.h>

extern ftrace_func_t ftrace_trace_function;

void notrace ftrace_stub(unsigned long ip, unsigned long parent_ip,
			 struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
}

void notrace hexagon_ftrace_caller(unsigned long ip, unsigned long parent_ip,
				   unsigned long *parent)
{
	ftrace_func_t func = READ_ONCE(ftrace_trace_function);

	if (func != ftrace_stub)
		func(ip, parent_ip, READ_ONCE(function_trace_op), NULL);
}
NOKPROBE_SYMBOL(hexagon_ftrace_caller);
NOKPROBE_SYMBOL(ftrace_stub);
NOKPROBE_SYMBOL(mcount);
