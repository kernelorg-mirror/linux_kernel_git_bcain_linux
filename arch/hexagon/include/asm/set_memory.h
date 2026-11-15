/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_HEXAGON_SET_MEMORY_H
#define _ASM_HEXAGON_SET_MEMORY_H

int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_rox(unsigned long addr, int numpages);
#define set_memory_rox set_memory_rox
void mark_rodata_ro(void);

#endif
