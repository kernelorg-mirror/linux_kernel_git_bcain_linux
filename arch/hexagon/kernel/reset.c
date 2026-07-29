// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/reboot.h>
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
