.. SPDX-License-Identifier: GPL-2.0

===============================================
GLINK and FastRPC between two QEMU instances
===============================================

This document describes how to run Qualcomm GLINK inter-processor
communication — and on top of it IP networking and FastRPC — between
two QEMU guests: an arm64 "apps processor" (AP) and a hexagon "compute
DSP" (cDSP), each running Linux.

Overview
========

On real hardware the AP and the DSPs share DDR: GLINK's SMEM transport
lives in that shared memory and the processors signal each other
through doorbell interrupts, with an SMMU making AP memory visible to
the DSP for FastRPC.  Between two QEMU instances the same roles are
played by:

- QEMU's ``ivshmem-flat`` device: a shared-memory window plus doorbell
  registers, coordinated by the ``ivshmem-server`` utility, which hands
  every peer the shared memory fd and an eventfd per peer for
  interrupts.
- The ``ivshmem-doorbell`` mailbox driver
  (``drivers/mailbox/mailbox-ivshmem.c``), which GLINK uses to ring the
  peer.
- A carve-up of the shared window that both machines advertise
  identically in their generated device trees:

  ============  =========  ========================================
  Offset        Size       Use
  ============  =========  ========================================
  0x000000      2 MiB      Qualcomm SMEM region (``qcom,smem``)
  0x200000      128 KiB    tcsr-mutex hardware spinlock page
  0x220000      ~1.9 MiB   FastRPC coherent DMA pool (AP side)
  ============  =========  ========================================

- Since no SMMU is modeled, both machines map the window at the *same*
  guest physical address (``0x90900000``), so physical addresses
  exchanged over FastRPC are valid on both ends.  This is the main
  approximation relative to real hardware.

::

  +--------------------+                        +--------------------+
  |  arm64 QEMU (AP)   |                        | hexagon QEMU (cDSP)|
  |                    |    shared memory       |                    |
  |  0x90900000 -------+---- (ivshmem-flat) ----+------- 0x90900000  |
  |                    |                        |                    |
  |  fastrpc.c         |      eventfds via      |  fastrpc_device    |
  |  rpmsg_net (dsp0)  |<--- ivshmem-server --->|  rpmsg_net (dsp0)  |
  |  rpmsg_echo        |      (doorbells)       |  rpmsg_echo        |
  +--------------------+                        +--------------------+

Both ``qemu-system-hexagon`` and ``qemu-system-aarch64`` gain an
``ivshmem-chardev`` machine property.  When it names a chardev that is
connected to an ivshmem-server socket, the machine creates the
ivshmem-flat device and generates the SMEM, tcsr-mutex,
ivshmem-doorbell mailbox and ``qcom,glink-smem-edge`` device tree
nodes.  The hexagon side is described as the remote (``qcom,is-remote``,
intentless); the arm64 side as the intentless host, with a
``qcom,fastrpc`` channel node in the shape ``drivers/misc/fastrpc.c``
expects.

Prerequisites
=============

- A QEMU tree with the hexagon virt machine and the ``ivshmem-chardev``
  property on both machines, built with both targets::

    ./configure --target-list=hexagon-softmmu,aarch64-softmmu
    ninja -C build qemu-system-hexagon qemu-system-aarch64
    ninja -C build contrib/ivshmem-server/ivshmem-server

- Hexagon cross toolchain (clang + musl, see qemu-boot.rst) and an
  arm64 cross compiler (``aarch64-linux-gnu-gcc``).
- The H2 hypervisor ``loadlinux`` boot shim (see qemu-boot.rst).

Step 1: Build the kernels
=========================

Hexagon (cDSP)::

  ./build-kernel.sh <hexagon-build-dir>

``qemu_defconfig`` already enables SMEM-backed GLINK, the ivshmem
mailbox, the rpmsg echo service, ``RPMSG_NET``, ``FASTRPC_DEVICE`` and
QRTR.

arm64 (AP)::

  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- O=<arm64-build-dir> defconfig
  <src>/scripts/config --file <arm64-build-dir>/.config \
      --enable QCOM_SMEM --enable HWSPINLOCK --enable HWSPINLOCK_QCOM \
      --enable MAILBOX --enable IVSHMEM_MBOX \
      --enable RPMSG_QCOM_GLINK_SMEM --enable RPMSG_ECHO \
      --enable RPMSG_NET --enable QRTR --enable QRTR_SMD \
      --enable QCOM_FASTRPC
  make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- O=<arm64-build-dir> \
      olddefconfig Image

Step 2: Build the userspace pieces
==================================

DSP-side FastRPC dispatcher (serves invocations; see
``arch/hexagon/tools/fastrpc_dispatcher.c``)::

  hexagon-unknown-linux-musl-clang -O2 -static \
      -o fastrpc_dispatcher arch/hexagon/tools/fastrpc_dispatcher.c

AP-side FastRPC test client (drives the upstream
``drivers/misc/fastrpc.c`` UAPI)::

  aarch64-linux-gnu-gcc -static -O2 -I include/uapi \
      -o fastrpc_test tools/fastrpc/fastrpc_test.c

Add ``fastrpc_dispatcher`` to the hexagon rootfs.  An existing cpio
archive can simply be extended (the kernel unpacks concatenated
archives)::

  mkdir -p extra/usr/bin extra/etc/init.d
  cp fastrpc_dispatcher extra/usr/bin/
  cat > extra/etc/init.d/S99fastrpc << 'EOF'
  #!/bin/sh
  case "$1" in
    start)
        if [ ! -c /dev/fastrpc_device ] && [ -e /sys/class/misc/fastrpc_device/dev ]; then
                mknod /dev/fastrpc_device c \
                        $(cut -d: -f1 /sys/class/misc/fastrpc_device/dev) \
                        $(cut -d: -f2 /sys/class/misc/fastrpc_device/dev)
        fi
        [ -c /dev/fastrpc_device ] && \
                /usr/bin/fastrpc_dispatcher > /var/log/fastrpc_dispatcher.log 2>&1 &
        ;;
  esac
  EOF
  chmod +x extra/etc/init.d/S99fastrpc
  (cd extra && find . | cpio -o -H newc) >> dsp_rootfs.cpio

For the AP a minimal busybox initramfs suffices; include
``fastrpc_test`` and an ``/init`` that mounts proc/sys/devtmpfs and
execs a shell.

Step 3: Pre-initialize the shared memory
========================================

The SMEM region must look initialized before either guest boots (on
real hardware boot firmware does this).  Generate a SMEM v11 image with
the GLINK descriptor and FIFO items and seed the shared memory object
that ivshmem-server will hand out::

  python3 scripts/hexagon/gen_smem_image.py smem.bin 2097152
  dd if=/dev/zero of=/dev/shm/frpc-smem bs=1M count=4
  dd if=smem.bin of=/dev/shm/frpc-smem conv=notrunc

Step 4: Launch
==============

Start the ivshmem-server on the pre-seeded shared memory::

  ivshmem-server -F -S /tmp/frpc-ivshmem.sock -M frpc-smem -l 4M -n 1 &

Start the hexagon cDSP guest::

  qemu-system-hexagon \
      -M virt,ivshmem-chardev=ivs \
      -m 4G -display none \
      -bios <path-to>/loadlinux \
      -kernel <hexagon-build-dir>/vmlinux \
      -initrd dsp_rootfs.cpio \
      -append "console=ttyAMA1 mem=892M rdinit=/sbin/init" \
      -chardev socket,id=ivs,path=/tmp/frpc-ivshmem.sock \
      -serial mon:stdio -no-reboot

Start the arm64 AP guest::

  qemu-system-aarch64 \
      -M virt,ivshmem-chardev=ivs \
      -cpu cortex-a57 -m 1G -smp 1 -display none \
      -kernel <arm64-build-dir>/arch/arm64/boot/Image \
      -initrd ap_initramfs.cpio \
      -append "console=ttyAMA0 rdinit=/init" \
      -chardev socket,id=ivs,path=/tmp/frpc-ivshmem.sock \
      -serial mon:stdio -no-reboot -nic none

Notes:

- Boot order does not matter: the mailbox driver derives the peer id
  from the ivshmem ``IVPOSITION`` register, and doorbells rung before
  the other side is up are dropped harmlessly.
- The AP's RAM must not reach 0x90900000 (i.e. ``-m`` at most ~1290M);
  the machine refuses to start otherwise.
- The hexagon guest takes a few minutes of wall clock to reach the
  login prompt under TCG.

Step 5: Verify GLINK (rpmsg echo)
=================================

The AP-side echo initiator opens the ``glink-echo`` channel and
retries a test message until the DSP echoes it.  Once both guests are
up, the AP console shows::

  rpmsg_echo ...: send attempt 1 (ret=0)
  rpmsg_echo ...: received 15 bytes (src: 0xffffffff)
  rpmsg_echo ...: ECHO SUCCESS after 1 attempts

Step 6: Verify IP over GLINK (rpmsg_net)
========================================

Both kernels register a point-to-point raw-IP interface ``dsp0``
backed by the ``IP_BRIDGE`` GLINK channel.  On the DSP::

  ip link set dsp0 up
  ip addr add 10.42.0.2 peer 10.42.0.1 dev dsp0

On the AP::

  ip link set dsp0 up
  ip addr add 10.42.0.1 peer 10.42.0.2 dev dsp0
  ping -c 3 10.42.0.2

TCP works across the link, e.g. serve a file from the AP with
``httpd -p 8080 -h /tmp`` and fetch it on the DSP with ``wget
http://10.42.0.1:8080/...``.

Step 7: Verify FastRPC
======================

The DSP side is served by ``drivers/misc/fastrpc_device.c`` (kernel,
handles the ``fastrpcglink-apps-dsp`` channel and process
attach/release) and the ``fastrpc_dispatcher`` daemon (userspace,
executes invocations).  The daemon is started by the ``S99fastrpc``
init script, or manually::

  /usr/bin/fastrpc_dispatcher &

On the AP, the unmodified upstream FastRPC driver exposes
``/dev/fastrpc-cdsp``.  Run the test client::

  # /bin/fastrpc_test
  fastrpc_test: attached to root PD
  fastrpc_test: add(41000, 1042) = 42042
  fastrpc_test: echo -> "hello from the apps processor"
  fastrpc_test: PASS

What happens under the hood:

1. ``FASTRPC_IOCTL_INIT_ATTACH`` sends an invoke of the init handle
   over GLINK; the DSP kernel driver acknowledges it.
2. ``FASTRPC_IOCTL_INVOKE`` marshals the arguments: copy-based buffers
   are gathered into a metadata + inline-args payload allocated with
   ``dma_alloc_coherent()``.  Because the channel's compute context
   bank has no IOMMU and sid 0, the allocation comes from the
   ``shared-dma-pool`` carved out of the ivshmem window, so the DSP can
   see it.
3. The payload's physical address travels in the ``fastrpc_msg``; the
   DSP-side daemon maps it via ``/dev/mem`` (same physical address in
   both guests), locates each argument through the remote-arg and
   phy-page tables, executes the method, writes outputs in place and
   returns a ``fastrpc_invoke_rsp``.

Limitations
===========

- No SMMU: only copy-based FastRPC arguments (``fd == -1``) work.
  dma-buf arguments would reference AP memory outside the shared
  window.  The shared window position/size and the equal-address
  mapping are demo conveniences.
- The tcsr-mutex page is plain shared memory; the write-owner/read-back
  protocol is not atomic across VMs, which real TCSR hardware
  guarantees.
- GLINK runs intentless on both sides (the remote side always does;
  the host side must match).
- ``rpmsg_net`` drops packets when the GLINK FIFO is transiently full
  and relies on TCP retransmission; there is no TX flow control.
