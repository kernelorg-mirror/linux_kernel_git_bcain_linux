/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ASM_PROM_H
#define __ASM_PROM_H

#include <asm/setup.h>

extern char cmd_line[COMMAND_LINE_SIZE];

void early_init_devtree(void *dtb);
#endif /* __ASM_PROM_H */
