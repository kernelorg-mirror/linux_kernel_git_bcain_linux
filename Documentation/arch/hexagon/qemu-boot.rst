.. SPDX-License-Identifier: GPL-2.0

========================================
Booting Hexagon Linux on QEMU
========================================

This document describes how to build and boot the Hexagon Linux kernel
on the QEMU ``hexagon`` system emulator using the ``virt`` machine type.

Prerequisites
=============

The following components are required:

Linux cross-compilation toolchain
  A Clang/LLVM cross-compilation toolchain targeting
  ``hexagon-unknown-linux-musl`` is required to build the kernel.
  This provides ``hexagon-unknown-linux-musl-clang``, ``llvm-objcopy``,
  ``llvm-ar``, ``llvm-nm``, and ``llvm-strip``.

  Pre-built tarballs are available from the ``quic/toolchain_for_hexagon``
  releases page on GitHub::

    https://github.com/quic/toolchain_for_hexagon/releases/tag/v22.1.4_

  Download ``clang+llvm-22.1.4-cross-hexagon-unknown-linux-musl.tar.zst``
  from the artifacts page and extract it::

    curl -LO https://artifacts.codelinaro.org/artifactory/codelinaro-toolchain-for-hexagon/22.1.4_/clang+llvm-22.1.4-cross-hexagon-unknown-linux-musl.tar.zst
    tar -xf clang+llvm-22.1.4-cross-hexagon-unknown-linux-musl.tar.zst \
        -C /opt/hexagon-toolchain
    export LLVM_TOOLCHAIN_PATH=/opt/hexagon-toolchain/clang+llvm-22.1.4-cross-hexagon-unknown-linux-musl/x86_64-linux-gnu
    export PATH=$LLVM_TOOLCHAIN_PATH/bin:$PATH

  Verify the toolchain is accessible::

    hexagon-unknown-linux-musl-clang --version

QEMU
  ``qemu-system-hexagon`` with the ``virt`` machine type. This machine
  provides an H2 hypervisor interface, PL011 UART, Virtio MMIO devices,
  and an H2 timer.  As of the ``hexagon-sysemu-03-july-2026`` tag, QEMU
  bundles a prebuilt ``hexagon_loadlinux`` firmware image, so no
  Hexagon SDK or H2 hypervisor build is needed to boot Linux (see the
  `Appendix: Rebuilding loadlinux`_ if you need to rebuild that firmware
  yourself).

  Use the ``hexagon-sysemu-03-july-2026`` tag of Qualcomm's QEMU fork::

    git clone https://github.com/qualcomm/qemu.git
    cd qemu
    git checkout hexagon-sysemu-03-july-2026
    mkdir build && cd build
    ../configure --target-list=hexagon-softmmu
    ninja -j$(nproc) qemu-system-hexagon

  After the build, ``build/qemu-system-hexagon`` is the emulator binary.
  Add it to ``PATH`` or invoke it by full path.

Root filesystem
  An initramfs ``rootfs.cpio`` (or ``rootfs.cpio.gz``) image containing
  a minimal userspace.  A pre-built image is available from the
  toolchain release artifacts::

    curl -LO https://artifacts.codelinaro.org/artifactory/codelinaro-toolchain-for-hexagon/22.1.4_/rootfs.cpio

Build
=====

1. Configure the kernel::

    make ARCH=hexagon \
        CC=hexagon-unknown-linux-musl-clang \
        LLVM=1 LLVM_IAS=1 HOSTCC=gcc \
        qemu_defconfig

2. Build the kernel::

    make ARCH=hexagon \
        CC=hexagon-unknown-linux-musl-clang \
        LLVM=1 LLVM_IAS=1 HOSTCC=gcc \
        -j$(nproc) vmlinux

Boot
====

QEMU's ``virt`` machine defaults ``-bios`` to its bundled
``hexagon_loadlinux`` firmware, which initializes the H2 hypervisor and
jumps to a kernel loaded at physical address ``0xa0000000``.  A Linux
kernel ELF can be booted the same way as on other architectures, with
the initramfs passed via ``-initrd``::

    qemu-system-hexagon \
        -M virt \
        -kernel vmlinux \
        -initrd rootfs.cpio \
        -append 'console=ttyAMA0 mem=892M' \
        -m 4G \
        -nographic \
        -serial mon:stdio

The ``mem=`` kernel parameter is required: the hexagon port currently
sizes usable memory from the command line rather than the generated
device tree's memory node, which is informational-only.  Without it
the kernel defaults to a 64 MB memory window, which is too small to
hold the initramfs and leads to a boot-time oops.

An external device tree can be supplied with ``-dtb``; otherwise the
machine generates one.  Use ``-bios none`` to boot the kernel ELF
directly at its entry point without any firmware, or pass another
``-bios`` image (such as a manually rebuilt ``loadlinux``, see the
`Appendix: Rebuilding loadlinux`_) to replace the bundled bootloader::

    qemu-system-hexagon \
        -M virt \
        -bios /path/to/loadlinux \
        -kernel vmlinux \
        -initrd rootfs.cpio \
        -append 'console=ttyAMA0 mem=892M' \
        -m 4G \
        -nographic \
        -serial mon:stdio

On a successful boot the kernel prints messages to the PL011 UART
(``ttyAMA0``) and eventually reaches a login prompt from the embedded
initramfs.

Boot Sequence
=============

The boot process has the following stages:

1. **QEMU loads loadlinux and the kernel.**
   QEMU places the ``hexagon_loadlinux`` firmware (or the image given
   via ``-bios``) at its ELF entry point, and loads the kernel ELF
   given via ``-kernel`` at physical address ``0xa0000000``.

2. **loadlinux initializes the H2 hypervisor.**
   The bootloader sets up minimal CPU state under the H2 hypervisor
   and jumps to the kernel entry point (``stext``) at ``0xa0000000``,
   passing boot information in registers R1:0.

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
   The kernel mounts the embedded initramfs and executes
   ``/sbin/init``.

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

The kernel uses a 3G/1G user/kernel split. Physical-to-virtual
translation for lowmem: ``virt = phys + (PAGE_OFFSET - PHYS_OFFSET)``.

Appendix: Rebuilding loadlinux
===============================

QEMU's ``virt`` machine bundles a prebuilt ``hexagon_loadlinux`` image
in ``pc-bios/``, so this step is not needed for normal use.  It is only
required if you need to modify the H2 hypervisor bootloader itself, or
rebuild it against a different Hexagon SDK or ARCHV.

Building ``loadlinux`` requires the Hexagon SDK, which provides
``hexagon-clang`` and related tools:

  Download the SDK tarball from the ``snapdragon-toolchain/hexagon-sdk``
  releases page on GitHub::

    curl -LO https://github.com/snapdragon-toolchain/hexagon-sdk/releases/download/v6.4.0.2/hexagon-sdk-v6.4.0.2-amd64-lnx.tar.xz
    tar -xf hexagon-sdk-v6.4.0.2-amd64-lnx.tar.xz -C /opt/hexagon-sdk
    export HEXAGON_SDK_ROOT=/opt/hexagon-sdk/6.4.0.2
    export HEXAGON_TOOLS_ROOT=$HEXAGON_SDK_ROOT/tools/HEXAGON_Tools/19.0.04
    export PATH=$HEXAGON_TOOLS_ROOT/Tools/bin:$PATH

  Verify the toolchain is accessible::

    hexagon-clang --version

``loadlinux`` is a small Hexagon ELF bootloader that performs early
CPU and hypervisor initialization before jumping to the kernel entry
point. It is loaded by QEMU as the ``-bios`` firmware image, while the
kernel raw binary is loaded separately by QEMU at a fixed physical
address (``0xa0000000``) via a ``loader`` device.

Clone the hypervisor repository and check out tag ``h2-bcain-2-july-2026``::

    git clone https://github.com/androm3da/hexagon-hypervisor.git
    cd hexagon-hypervisor
    git checkout h2-bcain-2-july-2026

Build the H2 hypervisor libraries (required by ``loadlinux``).
``NULL_ANGEL_TRAP=1`` disables the angel semihosting handler, which
otherwise polls for a host response that the QEMU ``virt`` machine
never provides, causing a silent hang::

    ARCHV=73
    make USE_PKW=0 ARCHV=$ARCHV TARGET=opt NULL_ANGEL_TRAP=1 -j"$(nproc)"

Then build ``loadlinux`` from the ``linux/`` subdirectory.
``NO_LOAD=1`` tells ``loadlinux`` not to load the kernel from a file;
QEMU loads it instead.  ``LINUX_LINK_ADDR`` must match the kernel load
address (``0xa0000000``).  Pass ``NULL_ANGEL_TRAP=1`` again so the
bootloader is linked with the null angel stubs.

The ``linux/makefile`` defaults its build paths to ``../install`` and
``../kernel``, but the actual artifacts from the step above land in
``artifacts/v${ARCHV}/opt/``.  Export the correct paths before
invoking make::

    export INSTALLPATH=$(pwd)/artifacts/v${ARCHV}/opt/install
    export KERNELPATH=$(pwd)/artifacts/v${ARCHV}/opt/build/kernel
    make -C linux USE_PKW=0 ARCHV=$ARCHV NO_LOAD=1 \
        NULL_ANGEL_TRAP=1 LINUX_LINK_ADDR=0xa0000000 loadlinux

The resulting ``linux/loadlinux`` ELF can be passed to QEMU via the
``-bios`` flag as shown in the `Boot`_ section above, or copied into
QEMU's ``pc-bios/hexagon_loadlinux`` (and rebuilt with QEMU) to replace
the bundled default.  QEMU's own ``roms/Makefile`` provides a
``hexagon-loadlinux`` target that automates the steps above against
the ``roms/hexagon-hypervisor`` submodule.
