/*
 * arch/hexagon/platform/simulator/board.c
 *
 * Board file for the "simulator" platform: a Linux guest running under
 * the H2 hypervisor, either hosted on hexagon-sim (via
 * hexagon-hypervisor's semihosting loadlinux path, DTB compatible
 * "qcom,hexagon-simulator") or on qemu-system-hexagon's "virt" machine,
 * which synthesizes its own DTB (compatible "qemu,hexagon-virt",
 * "qcom,sm8150"). Both dt_compat entries are listed so the same kernel
 * image boots under either engine -- see arch/hexagon/boot/dts/
 * simulator_h2.dts and simulator_h2_qemu.dts.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <asm/angel_console.h>
#include <asm/irq.h>
#include <asm/mach_desc.h>

static const char *simulator_dt_compat[] __initconst = {
	"qcom,hexagon-simulator",
	"qemu,hexagon-virt",
	NULL
};

struct of_device_id platform_of_irq_matches[] __initdata = {
	{ .compatible = "qcom,h2-pic", .data = hexagon_pic_of_init, },
	{},
};

static int __init simulator_init(void)
{
	of_platform_populate(of_find_node_by_path("/soc"), NULL, NULL, NULL);
	return 0;
}
arch_initcall(simulator_init);

static void setup_arch_platform_simulator(void)
{
	/*
	 * angel_console.c's angel_write() branches on this global, which no
	 * platform in the tree ever sets (it's declared extern and left at
	 * its BSS-zeroed default everywhere) -- so angel_write() always took
	 * the "!on_simulator" pointer-based trap0(R0=0x3) path instead of
	 * the call-by-value trap0(R0=0x43) path, and hexagon-sim only
	 * services the latter, silently dropping all console output. This
	 * platform is always hosted by a simulator (hexagon-sim or QEMU's
	 * synthesized "virt" machine), so it's always correct here.
	 */
	on_simulator = 1;
}

MACHINE_START(SIMULATOR, "simulator")
	.setup_arch_platform = setup_arch_platform_simulator,
	.dt_compat = simulator_dt_compat
MACHINE_END
