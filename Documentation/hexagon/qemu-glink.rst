.. SPDX-License-Identifier: GPL-2.0

====================================
GLINK IPC Testing with Dual QEMU
====================================

This document describes how to set up and run a GLINK inter-processor
communication session between two QEMU instances: an ARM64 host (AP) and
a Hexagon remote (cDSP).

Overview
========

GLINK is Qualcomm's inter-processor communication protocol over shared
memory (SMEM).  The setup uses:

- A shared memory file (``/tmp/smem.bin``) mapped by both QEMU instances
- A Unix socket for doorbell notification between instances
- The ``smem-shm`` QEMU device that bridges shared memory and doorbells
- An ``rpmsg_echo`` kernel driver to validate message exchange

::

  +------------------+                     +-------------------+
  |  ARM64 QEMU      |   /tmp/smem.bin     |  Hexagon QEMU     |
  |  (AP / host)     |<----- SMEM -------->|  (cDSP / remote)  |
  |                  |                     |                   |
  |  glink-echo      |   /tmp/glink.sock   |  glink-echo       |
  |  (sends msgs)    |<---- doorbell ----->|  (echoes msgs)    |
  +------------------+                     +-------------------+

Prerequisites
=============

Toolchains
----------

Hexagon cross-compiler::

  HEX_CC="/path/to/clang --target=hexagon-unknown-linux-musl"
  HEX_LD=/path/to/ld.eld

ARM64 cross-compiler::

  CROSS_COMPILE=aarch64-linux-gnu-

QEMU binaries (must include ``smem-shm`` device and ``glink-role``
machine property)::

  QEMU_HEX=/path/to/qemu-system-hexagon
  QEMU_ARM=/path/to/qemu-system-aarch64

Step 1: Generate the SMEM Image
================================

The SMEM image is a 2 MiB file containing a pre-initialized SMEM v11
header with GLINK descriptor and FIFO items::

  python3 scripts/hexagon/gen_smem_image.py /tmp/smem.bin

This creates:

========  ========================  ==========
Item      Description               Size
========  ========================  ==========
478       GLINK descriptor          32 bytes
479       GLINK FIFO 0 (AP TX)      16 KiB
480       GLINK FIFO 1 (cDSP TX)    16 KiB
========  ========================  ==========

Step 2: Build the Hexagon (cDSP) Kernel
========================================

Configure and build::

  make ARCH=hexagon LLVM=1 CC="$HEX_CC" LD=$HEX_LD qemu_glink_defconfig
  make ARCH=hexagon LLVM=1 CC="$HEX_CC" LD=$HEX_LD -j$(nproc)

The ``qemu_glink_defconfig`` enables ``QCOM_SMEM``, ``RPMSG_QCOM_GLINK_SMEM``,
``IVSHMEM_MBOX``, ``HWSPINLOCK_QCOM``, and ``RPMSG_ECHO``.

Init binary
-----------

The Hexagon kernel needs a minimal init that stays alive.  A simple
approach using raw syscalls (no libc dependency)::

  cat > /tmp/init_glink.c << 'INIT_EOF'
  #define __NR_write 64
  #define __NR_mount 40
  #define __NR_nanosleep 101

  static long my_syscall(long n, long a, long b, long c, long d, long e) {
      register long r0 __asm__("r0") = a;
      register long r1 __asm__("r1") = b;
      register long r2 __asm__("r2") = c;
      register long r3 __asm__("r3") = d;
      register long r4 __asm__("r4") = e;
      register long r6 __asm__("r6") = n;
      __asm__ volatile("trap0(#1)"
          : "+r"(r0)
          : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r6)
          : "memory");
      return r0;
  }

  static void my_mount(const char *s, const char *t, const char *fs, long f) {
      my_syscall(__NR_mount, (long)s, (long)t, (long)fs, f, 0);
  }

  static void my_nanosleep(long sec) {
      long ts[2] = {sec, 0};
      my_syscall(__NR_nanosleep, (long)ts, 0, 0, 0, 0);
  }

  void _start(void) {
      my_mount("none", "/proc", "proc", 0);
      my_mount("none", "/sys", "sysfs", 0);
      my_mount("none", "/dev", "devtmpfs", 0);
      for (;;) my_nanosleep(60);
  }
  INIT_EOF

  $HEX_CC -static -nostdlib -O2 \
      -Xclang -target-feature -Xclang -duplex \
      -o /tmp/init_glink /tmp/init_glink.c

.. note::

   The ``-duplex`` flag works around an lld relocation bug that corrupts
   duplex sub-instructions.

Initramfs
---------

Create a cpio list file and embed it in the kernel::

  cat > /tmp/glink_initramfs.list << 'EOF'
  dir /dev 0755 0 0
  nod /dev/console 0600 0 0 c 5 1
  nod /dev/null 0666 0 0 c 1 3
  dir /proc 0755 0 0
  dir /sys 0755 0 0
  dir /tmp 0755 0 0
  file /init /tmp/init_glink 0755 0 0
  EOF

Set ``CONFIG_INITRAMFS_SOURCE="/tmp/glink_initramfs.list"`` in your
config (already set in ``qemu_glink_defconfig``), then rebuild::

  rm -f usr/initramfs_data.o usr/initramfs_inc_data
  make ARCH=hexagon LLVM=1 CC="$HEX_CC" LD=$HEX_LD -j$(nproc)

Step 3: Build the ARM64 (AP) Kernel
=====================================

Build out-of-tree to avoid conflicts with the Hexagon build::

  make ARCH=arm64 CROSS_COMPILE=$CROSS_COMPILE O=/tmp/arm64_build defconfig

Enable required options::

  cd /tmp/arm64_build
  scripts/config --enable IVSHMEM_MBOX
  scripts/config --enable RPMSG_ECHO
  make ARCH=arm64 CROSS_COMPILE=$CROSS_COMPILE olddefconfig
  make ARCH=arm64 CROSS_COMPILE=$CROSS_COMPILE -j$(nproc)

ARM64 initramfs
---------------

Create a minimal initramfs with an init script::

  mkdir -p /tmp/arm64_rootfs/{dev,proc,sys,tmp}
  cat > /tmp/arm64_rootfs/init << 'INITEOF'
  #!/bin/sh
  mount -t proc none /proc
  mount -t sysfs none /sys
  mount -t devtmpfs none /dev
  echo "Waiting for GLINK echo test..."
  sleep 30
  dmesg | grep -i "echo\|glink"
  echo "Test complete"
  sleep 3600
  INITEOF
  chmod 755 /tmp/arm64_rootfs/init

  cd /tmp/arm64_rootfs
  find . | cpio -o -H newc > /tmp/arm64_initramfs.cpio

Step 4: Launch QEMU Instances
==============================

Start the Hexagon cDSP first (socket server)::

  $QEMU_HEX \
      -M virt,glink-role=remote \
      -m 1G -smp 1 \
      -kernel vmlinux \
      -chardev socket,id=glink,path=/tmp/glink.sock,server=on,wait=off \
      -serial file:/tmp/cdsp_serial.log \
      -display none -no-reboot &

Wait for the cDSP to finish booting (about 30 seconds), then start the
ARM64 AP (socket client)::

  sleep 30

  $QEMU_ARM \
      -M virt,glink-role=host \
      -cpu cortex-a57 -m 1G -smp 1 \
      -kernel /tmp/arm64_build/arch/arm64/boot/Image \
      -initrd /tmp/arm64_initramfs.cpio \
      -chardev socket,id=glink,path=/tmp/glink.sock \
      -append "console=ttyAMA0 earlycon loglevel=8 rdinit=/init" \
      -serial file:/tmp/ap_serial.log \
      -display none -no-reboot -nic none &

Key parameters:

``-M virt,glink-role=<host|remote>``
  Selects AP (host) or cDSP (remote) perspective.  This controls
  FIFO direction and ``qcom,is-remote`` in the generated device tree.

``-chardev socket,id=glink,path=/tmp/glink.sock,...``
  The doorbell transport.  One side must be ``server=on,wait=off``
  (cDSP) and the other connects as a client (AP).

Both instances automatically share ``/tmp/smem.bin`` (configured via
the ``smem-shm`` device's ``mem-path`` property in the QEMU machine).

Step 5: Verify the GLINK Session
==================================

Monitor the serial logs for successful handshake and echo exchange.

Expected cDSP output (``/tmp/cdsp_serial.log``)::

  glink-edge: GLINK SMEM: forcing intentless for remote
  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: glink-echo channel opened
  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: received 15 bytes
  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: echoed 15 bytes

Expected AP output (``/tmp/ap_serial.log``)::

  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: glink-echo channel opened
  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: send attempt 1 (ret=0)
  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: received 15 bytes
  rpmsg_echo platform:glink-edge.glink-echo.-1.-1: ECHO SUCCESS after 1 attempts

The echo driver on the AP side sends a test message every 2 seconds
until it receives an echo back.  On success, it logs ``ECHO SUCCESS``.
The cDSP side echoes every message it receives.

To extract results from binary serial logs::

  strings /tmp/cdsp_serial.log | grep -i "echo\|glink"
  strings /tmp/ap_serial.log | grep -i "echo\|glink"

Architecture Details
====================

Shared memory layout
--------------------

Both QEMU instances map the SMEM file at guest physical address
``0x90900000``.  The SMEM header uses version 11 format with a
global table of contents.

GLINK uses three SMEM items:

- **Item 478** (descriptor, 32 bytes): Four 32-bit head/tail pointers
  shared between AP and cDSP for flow control.
- **Item 479** (FIFO 0, 16 KiB): AP-to-cDSP data channel.
- **Item 480** (FIFO 1, 16 KiB): cDSP-to-AP data channel.

In remote mode, the FIFO assignments are swapped: the cDSP's TX FIFO
is item 480 and its RX FIFO is item 479.

Doorbell mechanism
------------------

The ``smem-shm`` QEMU device exposes MMIO registers for doorbell
signaling.  Writing to the ``DOORBELL`` register (offset ``0x0C``)
sends a byte over the Unix socket to the peer QEMU instance.
Receiving a byte pulses an IRQ to the guest kernel, which triggers
the GLINK receive path.

GLINK handshake
---------------

1. Both sides probe ``qcom,glink-smem-edge`` from device tree
2. Version negotiation: ``CMD_VERSION`` / ``CMD_VERSION_ACK``
3. Channel open: ``CMD_OPEN`` / ``CMD_OPEN_ACK`` (four-way handshake)
4. Data exchange via SMEM FIFOs with doorbell notifications

In intentless mode (used by the remote side), RX intents are allocated
on-the-fly rather than pre-negotiated.

Troubleshooting
===============

No serial output from cDSP
  Ensure the Hexagon kernel was built with ``CONFIG_SERIAL_AMBA_PL011=y``
  and the boot command line includes ``console=ttyAMA0``.

GLINK channel never opens
  Check that both QEMU instances are connected via the same socket
  (``/tmp/glink.sock``).  The cDSP must start first as the socket
  server.

"no intent found" errors on cDSP
  The cDSP must run in intentless mode.  Verify that ``qcom,is-remote``
  is set in the device tree (automatic with ``glink-role=remote``).

Echo messages sent but never received
  Verify the SMEM image was freshly generated.  Stale FIFO data from
  a previous run can confuse the GLINK state machine::

    python3 scripts/hexagon/gen_smem_image.py /tmp/smem.bin

Binary data in serial logs
  QEMU PL011 serial output may contain binary framing bytes.  Use
  ``strings`` to extract readable kernel messages.
