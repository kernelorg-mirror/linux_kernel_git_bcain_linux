/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/smp.h>
#include <linux/irqflags.h>
#include <asm/hexagon_vm.h>

static inline void __do_vmstop(void *info) {
	__vmstop((long)info);
}

void machine_power_off(void)
{
	on_each_cpu(__do_vmstop, (void *)poweroff, 0);
}

void machine_halt(void)
{
	on_each_cpu(__do_vmstop, (void *)halt, 0);
}

void machine_restart(char *cmd)
{
	on_each_cpu(__do_vmstop, (void *)restart, 0);
}

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

void hexagon_pm_power_off(void)
{
	on_each_cpu(__do_vmstop, (void *)poweroff, 0);
}

