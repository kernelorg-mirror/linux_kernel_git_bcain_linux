/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Arch extensions that need some housekeeping to be referenced per-thread
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _ASM_EXTENSIONS_H
#define _ASM_EXTENSIONS_H

struct hvx_threadinfo;
struct hmx_threadinfo;

/* The state of a thread's coprocessors, if it uses them. */
struct extinfo {
	struct hvx_threadinfo *hvx;
	struct hmx_threadinfo *hmx;
};

#endif /* _ASM_EXTENSIONS_H */
