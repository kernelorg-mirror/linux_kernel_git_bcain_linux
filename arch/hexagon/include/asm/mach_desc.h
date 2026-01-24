/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Machine description structure for Hexagon platforms
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * Copyright (C) 2000 Russell King.
 * Copyright (C) 2011,2012 Code Aurora Forum. All rights reserved.
 */

#ifndef __ASSEMBLY__

struct machine_desc {
	const char		*name;
	const char *const	*dt_compat;
	void			(*setup_arch_platform)(void);
};

/*
 * Current machine - only accessible during boot.
 */
extern const struct machine_desc *mdesc;

/*
 * Machine type table - also only accessible during boot
 */
extern struct machine_desc __arch_info_begin[], __arch_info_end[];
#define for_each_machine_desc(p)			\
	for (p = __arch_info_begin; p < __arch_info_end; p++)

/*
 * Set of macros to define architecture features.  This is built into
 * a table by the linker.
 */
#define MACHINE_START(_type, _name)			\
static const struct machine_desc __mach_desc_##_type	\
	__used						\
	__section(".arch.info.init") = {			\
	.name		= _name,

#define DT_MACHINE_START(_type, _name) MACHINE_START(_type, _name)

#define MACHINE_END				\
};

#endif
