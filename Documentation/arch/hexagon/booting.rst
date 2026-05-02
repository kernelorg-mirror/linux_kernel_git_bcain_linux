================
Boot Flow
================

Overview
========

The Hexagon kernel boots through a two-stage process using the H2
hypervisor as its boot stub.

Boot Stages
===========

1. **loadlinux** (H2 hypervisor bootloader): Sets up initial page tables,
   enables the MMU, and jumps to the kernel entry point.  It passes the
   device tree blob (DTB) physical address in register R0.

2. **Kernel entry** (``arch/hexagon/kernel/head.S``): Builds identity-mapped
   and kernel-virtual page tables, transitions to virtual addressing, zeros
   BSS, saves the boot info pointer, and calls ``start_kernel()``.

QEMU Boot
=========

The QEMU Hexagon virt machine boots with::

    qemu-system-hexagon \
        -kernel loadlinux \
        -device "loader,addr=0xa0000000,file=./vmlinux.bin" \
        -m 8G -no-reboot -M virt \
        -nographic -serial stdio

``loadlinux`` is an untracked H2 hypervisor binary.  ``vmlinux.bin`` is the
raw binary kernel image, generated with::

    llvm-objcopy -O binary vmlinux vmlinux.bin

Device Tree
===========

The kernel includes a built-in DTB (``arch/hexagon/boot/dts/qemu_virt.dts``)
which is used when the bootloader does not provide one.  If ``loadlinux``
passes a valid FDT pointer in R0, the kernel uses the bootloader-provided
tree instead.

Kernel Command Line
====================

The command line is sourced from (in priority order):

1. An external buffer filled by the bootloader (``external_buffer``)
2. The ``bootargs`` property in the DTB ``/chosen`` node
3. ``CONFIG_CMDLINE`` from the kernel configuration
