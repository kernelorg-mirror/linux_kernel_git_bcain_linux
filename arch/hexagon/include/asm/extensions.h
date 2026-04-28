/* SPDX-License-Identifier: GPL-2.0-only */
/* Arch extensions that might need some housekeeping to be referenced per-thread */

#include <asm/hmx.h>
#include <asm/hvx.h>

struct extinfo {
	struct hvx_threadinfo *hvx;
	struct hmx_threadinfo *hmx;
};
