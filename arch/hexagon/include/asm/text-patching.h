/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_HEXAGON_TEXT_PATCHING_H
#define _ASM_HEXAGON_TEXT_PATCHING_H

#include <linux/types.h>

void hexagon_init_kernel_mapping(void);
/* Multiword updates require caller exclusion (stop_machine or unpublished text). */
int hexagon_patch_text(void *addr, const void *src, size_t size);

#endif
