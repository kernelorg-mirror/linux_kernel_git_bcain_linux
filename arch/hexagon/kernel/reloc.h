/*
 * SPDX-License-Identifier: GPL-2.0
 * Kernel module loader support routines for Hexagon
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#ifndef RELOC_H
#define RELOC_H

extern uint32_t do_reloc(uint32_t reloc_type, uint32_t insn, uint32_t value);

#endif
