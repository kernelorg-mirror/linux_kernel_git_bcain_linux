/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_HEXAGON_HLX_H
#define _ASM_HEXAGON_HLX_H

#ifdef CONFIG_HEXAGON_HLX
void hexagon_hlx_flush_thread(void);
#else
static inline void hexagon_hlx_flush_thread(void) { }
#endif

#endif
