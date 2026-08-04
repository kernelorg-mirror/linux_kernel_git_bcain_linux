// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#include <linux/reboot.h>
#include <linux/smp.h>
#include <asm/hexagon_vm.h>

/*
 * __vmstop() does not return, so the other CPUs cannot be stopped with a
 * waiting cross-call: smp_send_stop() hands them an IPI that takes them out
 * of the VM, and then this one follows with the status the machine is
 * actually stopping for.
 */
static void hexagon_vmstop(enum VM_STOP_STATUS status)
{
	smp_send_stop();
	__vmstop(status);
}

void machine_power_off(void)
{
	hexagon_vmstop(poweroff);
}

void machine_halt(void)
{
	hexagon_vmstop(halt);
}

void machine_restart(char *cmd)
{
	hexagon_vmstop(restart);
}

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);
