// SPDX-License-Identifier: GPL-2.0-only
/* Function tracing for the Hexagon profiling ABI. */
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <asm/text-patching.h>

extern ftrace_func_t ftrace_trace_function;

#ifdef CONFIG_DYNAMIC_FTRACE
static ftrace_func_t trace_func = ftrace_stub;

static void make_call(u32 insns[2], unsigned long ip, unsigned long addr)
{
	u32 offset = addr - ip;

	insns[0] = 0x00004000 | ((offset >> 6) & 0x3fff) |
		   ((offset >> 4) & 0x0fff0000);
	insns[1] = 0x5a00c000 | ((offset & 0x3f) << 1);
}

static const u32 nops[2] = { 0x7f004000, 0x7f00c000 };

static int replace_call(unsigned long ip, const u32 old[2], const u32 new[2])
{
	u32 actual[2];

	if (copy_from_kernel_nofault(actual, (void *)ip, sizeof(actual)))
		return -EFAULT;
	if (memcmp(actual, old, sizeof(actual)))
		return -EINVAL;
	return hexagon_patch_text((void *)ip, new, sizeof(actual));
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec, unsigned long addr)
{
	u32 call[2];

	make_call(call, rec->ip, addr);
	return replace_call(rec->ip, call, nops);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	u32 call[2];

	make_call(call, rec->ip, addr);
	return replace_call(rec->ip, nops, call);
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	WRITE_ONCE(trace_func, func);
	return 0;
}
#endif

void notrace ftrace_stub(unsigned long ip, unsigned long parent_ip,
			 struct ftrace_ops *ops, struct ftrace_regs *fregs)
{
}

void notrace hexagon_ftrace_caller(unsigned long ip, unsigned long parent_ip,
				   unsigned long *parent)
{
#ifdef CONFIG_DYNAMIC_FTRACE
	ftrace_func_t func = READ_ONCE(trace_func);
#else
	ftrace_func_t func = READ_ONCE(ftrace_trace_function);
#endif

	if (func != ftrace_stub)
		func(ip, parent_ip, READ_ONCE(function_trace_op), NULL);
}
NOKPROBE_SYMBOL(hexagon_ftrace_caller);
NOKPROBE_SYMBOL(ftrace_stub);
NOKPROBE_SYMBOL(mcount);
