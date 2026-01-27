.. SPDX-License-Identifier: GPL-2.0

========================================
Booting Hexagon Linux on QEMU
========================================

This document describes how to build and boot the Hexagon Linux kernel
on the QEMU ``hexagon`` system emulator using the ``virt`` machine type.

Prerequisites
=============

The following components are required:

Toolchain
  A Clang/LLVM cross-compilation toolchain targeting
  ``hexagon-unknown-linux-musl``.  This provides ``clang``,
  ``ld.lld``, ``llvm-objcopy``, ``llvm-ar``, ``llvm-nm``, and
  ``llvm-strip``.

  The host Clang/LLD (with ``LLVM=1``) can cross-compile the kernel
  for Hexagon, but the cross-toolchain's ``clang`` is needed to
  resolve the correct ``libclang_rt.builtins-hexagon.a`` and
  ``libc.a`` at link time.  If using the host Clang, override
  ``LIBGCC`` and ``LIBC_ARCHIVE`` on the make command line (see
  `Build`_ below).

QEMU
  ``qemu-system-hexagon`` with the ``virt`` machine type.  This machine
  provides an H2 hypervisor interface, PL011 UART, Virtio MMIO devices,
  and an H2 timer.

loadlinux
  A small Hexagon ELF bootloader that performs early CPU and hypervisor
  initialization before jumping to the kernel entry point.  It is loaded
  by QEMU via the ``-kernel`` flag, while the actual kernel binary is
  loaded separately via ``-device loader``.

Root filesystem
  An initramfs ``rootfs.cpio`` image containing a minimal userspace.
  This can be built with Buildroot, or a minimal one can be
  hand-crafted as described in `Minimal Initramfs`_ below.

Minimal Initramfs
=================

If a full Buildroot rootfs is not available, a minimal initramfs can
be created from a statically-linked BusyBox.

1. Build BusyBox statically for Hexagon.  Create a wrapper script for
   the cross-compiler::

    cat > /tmp/hexagon-cc.sh << 'EOF'
    #!/bin/bash
    exec hexagon-unknown-linux-musl-clang \
        --sysroot=$(dirname $0)/../target/hexagon-unknown-linux-musl \
        -static "$@"
    EOF
    chmod +x /tmp/hexagon-cc.sh

   Configure and build BusyBox::

    cd /path/to/busybox-source
    make CC=/tmp/hexagon-cc.sh HOSTCC=gcc defconfig
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
    sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
    make CC=/tmp/hexagon-cc.sh HOSTCC=gcc oldconfig </dev/null
    make CC=/tmp/hexagon-cc.sh HOSTCC=gcc SKIP_STRIP=y -j$(nproc)

   Verify the result is a static Hexagon binary::

    file busybox
    # busybox: ELF 32-bit LSB executable, QUALCOMM DSP6, ... statically linked

2. Create the initramfs directory tree::

    mkdir -p /tmp/initramfs/{bin,sbin,etc,proc,sys,dev,tmp}
    cp busybox /tmp/initramfs/bin/busybox
    cd /tmp/initramfs/bin
    for cmd in sh ash ls cat echo mkdir mount umount dmesg ps uname; do
        ln -s busybox $cmd
    done
    cd /tmp/initramfs/sbin
    for cmd in init halt reboot; do
        ln -s ../bin/busybox $cmd
    done

3. Create the init script::

    cat > /tmp/initramfs/init << 'INIT'
    #!/bin/sh
    mount -t devtmpfs devtmpfs /dev
    mount -t proc proc /proc
    mount -t sysfs sysfs /sys
    mount -t tmpfs tmpfs /tmp
    echo
    echo "Hexagon Linux - QEMU Boot Successful"
    echo "Kernel: $(uname -a)"
    echo
    while true; do /bin/sh; done
    INIT
    chmod +x /tmp/initramfs/init

4. Create a device-node cpio using the kernel's ``gen_init_cpio``
   tool (needed so the kernel can open ``/dev/console`` before init
   runs)::

    gcc -o gen_init_cpio usr/gen_init_cpio.c
    cat > /tmp/devnodes.list << 'EOF'
    dir /dev 0755 0 0
    nod /dev/console 0600 0 0 c 5 1
    nod /dev/null 0666 0 0 c 1 3
    EOF
    ./gen_init_cpio /tmp/devnodes.list > /tmp/devnodes.cpio

5. Package the initramfs::

    cd /tmp/initramfs
    find . | cpio -o -H newc 2>/dev/null > /tmp/initramfs-fs.cpio
    cat /tmp/devnodes.cpio /tmp/initramfs-fs.cpio > /tmp/rootfs.cpio

Build
=====

Using the cross-toolchain
-------------------------

1. Configure the kernel::

    make ARCH=hexagon \
        CC=hexagon-unknown-linux-musl-clang \
        LD=hexagon-unknown-linux-musl-ld.lld \
        LLVM=1 LLVM_IAS=1 \
        qemu_defconfig

2. Embed the initramfs and adjust the command line::

    ./scripts/config --set-str INITRAMFS_SOURCE /tmp/rootfs.cpio
    ./scripts/config --set-val INITRAMFS_COMPRESSION_NONE y
    ./scripts/config --set-str CMDLINE \
        "console=ttyAMA0 maxcpus=1 debug mem=892M lpj=89124080 rdinit=/init"
    make ARCH=hexagon \
        CC=hexagon-unknown-linux-musl-clang \
        LD=hexagon-unknown-linux-musl-ld.lld \
        LLVM=1 LLVM_IAS=1 \
        olddefconfig

3. Build the kernel::

    make ARCH=hexagon \
        CC=hexagon-unknown-linux-musl-clang \
        LD=hexagon-unknown-linux-musl-ld.lld \
        LLVM=1 LLVM_IAS=1 \
        -j$(nproc) vmlinux

Using the host Clang
--------------------

If you only have the host Clang/LLD (not a Hexagon-specific toolchain
installation), you can still cross-compile by passing ``LLVM=1``.
However, the host Clang cannot resolve the Hexagon runtime libraries,
so you must point ``LIBGCC`` and ``LIBC_ARCHIVE`` at a Hexagon
sysroot::

    SYSROOT=/path/to/hexagon-unknown-linux-musl/usr/lib

    make ARCH=hexagon LLVM=1 LLVM_IAS=1 qemu_defconfig
    # ... set INITRAMFS_SOURCE and CMDLINE as above ...
    make ARCH=hexagon LLVM=1 LLVM_IAS=1 olddefconfig
    make ARCH=hexagon LLVM=1 LLVM_IAS=1 \
        LIBGCC=$SYSROOT/libclang_rt.builtins-hexagon.a \
        LIBC_ARCHIVE=$SYSROOT/libc.a \
        -j$(nproc) vmlinux

Create the raw binary image
----------------------------

::

    llvm-objcopy -O binary vmlinux vmlinux.bin

Boot
====

Launch QEMU with ``loadlinux`` as the ``-kernel`` and the raw kernel
binary loaded at physical address ``0xa0000000``::

    qemu-system-hexagon \
        -M virt \
        -kernel /path/to/loadlinux \
        -device loader,addr=0xa0000000,file=vmlinux.bin \
        -m 4G \
        -nographic \
        -serial mon:stdio \
        -no-reboot

On a successful boot the kernel prints messages to the PL011 UART
(``ttyAMA0``).  With the minimal initramfs above, you will see::

    ========================================
      Hexagon Linux - QEMU Boot Successful
    ========================================
    Kernel: Linux hexagon0 6.x.y ... hexagon GNU/Linux

    / #

The ``maxcpus=1`` parameter works around SMP boot hangs.  If SMP works
in your configuration, use ``maxcpus=4`` or omit it.

Boot Sequence
=============

The boot process has the following stages:

1. **QEMU loads loadlinux and vmlinux.bin.**
   QEMU places ``loadlinux`` at its ELF entry point and writes
   ``vmlinux.bin`` to physical address ``0xa0000000``.

2. **loadlinux initializes the H2 hypervisor.**
   The bootloader sets up minimal CPU state under the H2 hypervisor
   and jumps to the kernel entry point (``stext``) at ``0xa0000000``,
   passing boot information in register R0.

3. **head.S establishes virtual memory.**
   The assembly entry point in ``arch/hexagon/kernel/head.S``:

   - Derives ``PHYS_OFFSET`` from the program counter.
   - Builds an identity map (VA == PA) using 4 MB pages for the
     kernel image.
   - Builds the kernel virtual mapping at ``PAGE_OFFSET``
     (``0xc0000000``) using 16 MB pages.
   - Calls ``__vmnewmap`` to install page tables via the H2
     hypervisor.
   - Tears down the identity map, zeroes BSS, sets up the initial
     stack and thread info, and calls ``start_kernel()``.

4. **start_kernel / setup_arch.**
   ``setup_arch()`` in ``arch/hexagon/kernel/setup.c``:

   - Installs the H2 VM event vector.
   - Reads the built-in device tree (``__dtb_start``).
   - Calls ``setup_machine_fdt()`` to match the machine descriptor
     (``qcom,sm8150`` for the QEMU virt platform).
   - Calls the platform-specific ``setup_arch_platform()`` hook.
   - Registers RAM regions and unflattens the device tree.

5. **Device initialization.**
   The device tree (``arch/hexagon/boot/dts/sm8150.dts``) describes:

   - **PL011 UART** at ``0x10000000`` -- serial console
     (``ttyAMA0``).
   - **H2 timer** at ``0xab000000`` -- clock source and clock events.
   - **H2 PIC** -- interrupt controller.
   - **Virtio MMIO** devices for networking and block storage.

6. **User space.**
   The kernel mounts the embedded initramfs and executes the
   ``rdinit=`` program (``/init`` or ``/sbin/init``).

Key Kernel Configuration
========================

The ``qemu_defconfig`` enables these options relevant to QEMU booting:

==================================  =========================================
Option                              Purpose
==================================  =========================================
``CONFIG_HEXAGON_SM8150=y``         SM8150 platform (matched by device tree)
``CONFIG_H2=y``                     H2 hypervisor interface
``CONFIG_USE_OF=y``                 Device tree support
``CONFIG_SERIAL_AMBA_PL011=y``      PL011 UART driver (QEMU console)
``CONFIG_SERIAL_AMBA_PL011_CONSOLE``  PL011 as boot console
``CONFIG_HEXAGON_ANGEL_TRAPS=y``    Angel debug traps (early printk)
``CONFIG_BLK_DEV_INITRD=y``         Initramfs/initrd support
``CONFIG_VIRTIO_MMIO=y``            Virtio over MMIO transport
``CONFIG_PAGE_SIZE_64KB=y``         64 KB page size
==================================  =========================================

Memory Layout
=============

Physical addresses (QEMU virt machine):

======================  ============================================
Address                 Description
======================  ============================================
``0x08600000``          VTCM (Vector Tightly Coupled Memory, 256 KB)
``0x10000000``          PL011 UART (4 KB)
``0x11000000``          Virtio MMIO network (256 B)
``0x12000000``          Virtio MMIO block (256 B)
``0xa0000000``          Kernel image load address
``0xab000000``          H2 timer registers (4 KB)
======================  ============================================

Virtual address space:

======================  ============================================
Address                 Description
======================  ============================================
``0xc0000000``          ``PAGE_OFFSET`` -- kernel virtual base
======================  ============================================

The kernel uses a 3G/1G user/kernel split.  Physical-to-virtual
translation for lowmem: ``virt = phys + (PAGE_OFFSET - PHYS_OFFSET)``.