/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HMX coprocessor context definitions for Hexagon
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __ASM_HMX_H
#define __ASM_HMX_H

/*
 * A thread that uses HMX has one of these.  HMX needs no state saved by the
 * kernel, only its enable bit set while the thread runs, so this is a marker.
 */
struct hmx_threadinfo {
	int unused;
};

#endif /* __ASM_HMX_H */
