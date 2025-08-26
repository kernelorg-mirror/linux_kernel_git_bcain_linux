#ifndef HEXAGON_DEBUG_H
#define HEXAGON_DEBUG_H

extern u32 kernel_strace;
extern u32 sig_debug;

static void inline trace_dump(long var_is_long)
{
       asm volatile("trace(%0);"
			:
			: "r" (var_is_long)
			: );
}

#endif
