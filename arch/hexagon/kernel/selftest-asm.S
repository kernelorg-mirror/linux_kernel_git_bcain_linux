/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/page.h>

	.text
/* Attempt an unchanged byte store; a protection fault returns -EFAULT. */
SYM_FUNC_START(hexagon_test_write)
	r1 = memb(r0)
1:
	memb(r0) = r1
	r0 = #0
	jumpr r31
2:
	r0 = #-EFAULT
	jumpr r31
SYM_FUNC_END(hexagon_test_write)
	.pushsection __ex_table,"a"
	.long 1b, 2b
	.popsection

SYM_FUNC_START(hexagon_test_execute_nx)
	allocframe(#0)
	call ##hexagon_test_nx_target
	r0 = #0
	dealloc_return
1:
	r0 = #1
	dealloc_return
SYM_FUNC_END(hexagon_test_execute_nx)
	.pushsection __ex_table,"a"
	.long hexagon_test_nx_target, 1b
	.popsection

	/* Isolate this page so the test can temporarily remove execute permission. */
	.section .text.hexagon_nx_test,"ax"
	.p2align PAGE_SHIFT
SYM_FUNC_START(hexagon_test_nx_target)
	jumpr r31
SYM_FUNC_END(hexagon_test_nx_target)
