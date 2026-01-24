/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Angel semihosting console interface for Hexagon
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 */

#ifndef _ASM_HEXAGON_ANGEL_CONSOLE_H
#define _ASM_HEXAGON_ANGEL_CONSOLE_H

struct console;

void __init register_angel_console(void);

void angel_write(struct console *c, const char *s, unsigned int n);

void __init angel_get_command_line(char *s, unsigned int n);

#endif
