.. SPDX-License-Identifier: GPL-2.0

SM8975 NSP on QEMU
==================

The ``sm8975_nsp_defconfig`` and ``sm8975-nsp.dts`` describe the
``sm8975-nsp`` QEMU machine, not a physical board.  Linux runs as an H2
guest.  The device tree describes 480 MiB of RAM at 0xa0000000, the H2
interrupt controller and timer, QEMU's PL011 and virtio-mmio devices, eight
128-byte HVX contexts, HMX and HLX.  The machine should be started with
1 GiB of RAM and twelve CPUs.  The Hexagon kernel can be configured for up
to sixteen CPUs; the SM8975 itself has twelve.  H2 uses one per-CPU IPI
interrupt on every CPU, leaving guest interrupt 12 reserved for the timer.

For a sixteen-CPU boundary test, a temporary QEMU build was made with the
machine CPU limit and thread mask raised to sixteen, and a temporary DTB
was extended with CPU nodes 12 through 15.  With
``-accel tcg,thread=single -icount shift=auto,align=off,sleep=off``, the
guest booted and the tests below passed on all sixteen CPUs.  The synthetic
machine reached sixteen online CPUs with default TCG timing but stalled
before its serial console appeared; the cause has not been established.
The normal twelve-CPU machine booted with default TCG timing.

Build the kernel
----------------

With a Hexagon Linux cross-compiler installed, for example::

  export PATH=/opt/clang+llvm-23.1.0-rc1-cross-hexagon-unknown-linux-musl/x86_64-ubuntu-22.04/bin:$PATH
  make O=/tmp/linux-sm8975-nsp ARCH=hexagon LLVM=1 HOSTCC=/usr/bin/gcc \
      HOSTCXX=/usr/bin/g++ CROSS_COMPILE=hexagon-linux-musl- sm8975_nsp_defconfig
  make O=/tmp/linux-sm8975-nsp ARCH=hexagon LLVM=1 HOSTCC=/usr/bin/gcc \
      HOSTCXX=/usr/bin/g++ CROSS_COMPILE=hexagon-linux-musl- -j16 vmlinux dtbs

The compiler used here supports v81 code generation; it does not accept
``-mv85``.  The v81 kernel runs on QEMU's v85 NSP model.

Build H2
--------

The generic QEMU v81 loader is linked at physical address zero, which is
outside this machine's RAM.  Build H2 and its ``loadlinux`` for the NSP RAM
map.  This example uses the Hexagon SDK compiler and a clean copy of an H2
source checkout, so it does not modify that checkout::

  export H2_SOURCE=$HOME/src/hexagon-hypervisor
  export SDK_BIN=/opt/Hexagon_SDK/6.4.0.2/tools/HEXAGON_Tools/19.0.04/Tools/bin
  export H2_ID=0x$(git -C "$H2_SOURCE" rev-parse --short=8 HEAD)
  mkdir -p /tmp/sm8975-h2
  git -C "$H2_SOURCE" archive HEAD | tar -x -C /tmp/sm8975-h2
  PATH="$SDK_BIN:$PATH" make -C /tmp/sm8975-h2 -j16 USE_PKW=0 ARCHV=81 \
      TARGET=opt H2K_GIT_COMMIT="$H2_ID" H2K_LOAD_ADDR=0x88000000 \
      NULL_ANGEL_TRAP=1 SHUTDOWN_AFTER_GUEST_EXIT=1
  PATH="$SDK_BIN:$PATH" make -C /tmp/sm8975-h2/linux -j1 USE_PKW=0 ARCHV=81 \
      TARGET=opt KERNELPATH=/tmp/sm8975-h2/artifacts/v81/opt/build/kernel \
      H2K_GIT_COMMIT="$H2_ID" H2K_LOAD_ADDR=0x88000000 NO_LOAD=1 \
      NULL_ANGEL_TRAP=1 SHUTDOWN_AFTER_GUEST_EXIT=1 \
      LINUX_LINK_ADDR=0xa0000000 loadlinux

Boot and test
-------------

A QEMU build from the ``hex-next`` branch, with support for an external DTB
and an HLX context pool attached to the board CPUs, is required.  Supply a
Hexagon initramfs with a console login and the desired
test programs as ``/tmp/sm8975-rootfs.cpio``::

  $HOME/src/qemu-hex-next2/build/qemu-system-hexagon \
      -M sm8975-nsp -m 1G -smp 12 \
      -bios /tmp/sm8975-h2/linux/loadlinux \
      -kernel /tmp/linux-sm8975-nsp/vmlinux \
      -dtb /tmp/linux-sm8975-nsp/arch/hexagon/boot/dts/sm8975-nsp.dtb \
      -initrd /tmp/sm8975-rootfs.cpio -nographic -monitor none

The following commands build QEMU's existing HVX and HMX tests for inclusion
in that initramfs.  With the toolchain above, ``-static`` alone produces a
static PIE that faults during startup even for a simple hello-world program;
``-no-pie`` avoids that failure::

  export QEMU_SOURCE=$HOME/src/qemu-hex-next2
  export HEX_CC=/opt/clang+llvm-23.1.0-rc1-cross-hexagon-unknown-linux-musl/x86_64-ubuntu-22.04/bin/hexagon-linux-musl-clang
  mkdir -p /tmp/sm8975-tests
  "$HEX_CC" -O2 -static -no-pie -mv81 -mhvx -I "$QEMU_SOURCE/tests/tcg/hexagon" \
      "$QEMU_SOURCE/tests/tcg/hexagon/hvx_misc.c" -o /tmp/sm8975-tests/hvx_misc
  for test in hmx_fxp hmx_fp; do
      "$HEX_CC" -O2 -static -no-pie -mv81 -I "$QEMU_SOURCE/tests/tcg/hexagon" \
          "$QEMU_SOURCE/tests/tcg/hexagon/$test.c" -o "/tmp/sm8975-tests/$test"
  done
  "$HEX_CC" -O2 -static -no-pie -mv81 \
      tools/testing/selftests/hexagon/hlx.c -o /tmp/sm8975-tests/hlx

After logging in, run each test and check its exit status.  Each prints
``PASS`` for its checks and exits zero.  ``/sys/class/hvx/max_ctxts`` should
read 8, and ``/sys/bus/platform/drivers/hexagon_hmx`` should contain the HMX
device.  Run ``hlx`` as root so it can map the VTCM UIO device.  Its first
HLX instruction requests a context implicitly through the coprocessor fault;
no HLX setup call or privilege is required.  The test executes broadcast, add
and new-value store instructions on each allowed CPU and checks all 1024
halfword lanes.  It also runs four unprivileged processes concurrently, with
distinct register contents, to exercise context switching with more users
than hardware contexts.  The test uses instruction words from QEMU's HLX
encodings because the compiler used here has no HLX assembler support.

``CONFIG_HEXAGON_HLX`` allocates an HLX context when a thread first executes
an HLX instruction.  The kernel saves the thread's registers when it enters
the kernel and restores them before returning to userspace, sharing the
available hardware contexts between threads.  It uses the last 64 KiB of
VTCM for per-CPU staging and leaves the rest available through the UIO
device.  These checks do not establish HMX state isolation under concurrent
guest workloads.

For the local verification, the rootfs was assembled without modifying either
source archive.  The base was the gzip-compressed Buildroot initramfs at
``$HOME/hexagon-boot-test/qcs6490-cdsp-dl/initrd``; its root password is
``root``.  It has no BusyBox login applet, so the login applet and dynamic
loader were taken from ``$HOME/hexagon-boot-test/rootfs.cpio``::

  mkdir -p /tmp/sm8975-login /tmp/sm8975-overlay/bin \
      /tmp/sm8975-overlay/lib /tmp/sm8975-overlay/etc /tmp/sm8975-overlay/usr/bin
  (cd /tmp/sm8975-login && cpio -id --quiet bin/busybox 'lib/*' \
      < "$HOME/hexagon-boot-test/rootfs.cpio")
  cp /tmp/sm8975-login/bin/busybox /tmp/sm8975-overlay/bin/busybox-login
  ln -sfn busybox-login /tmp/sm8975-overlay/bin/login
  cp /tmp/sm8975-login/lib/ld-musl-hexagon.so.1 /tmp/sm8975-overlay/lib/
  cp /tmp/sm8975-tests/* /tmp/sm8975-overlay/usr/bin/
  cat > /tmp/sm8975-overlay/etc/inittab <<'END'
  ::sysinit:/bin/mount -t proc proc /proc
  ::sysinit:/bin/mkdir -p /dev/pts /dev/shm /run/lock/subsys
  ::sysinit:/bin/hostname -F /etc/hostname
  ::sysinit:/etc/init.d/rcS
  console::respawn:/bin/login
  ::shutdown:/etc/init.d/rcK
  END
  (cd /tmp/sm8975-overlay && find . -print0 | cpio --null -o -H newc --quiet \
      > /tmp/sm8975-overlay.cpio)
  gzip -dc "$HOME/hexagon-boot-test/qcs6490-cdsp-dl/initrd" > /tmp/sm8975-rootfs.cpio
  cat /tmp/sm8975-overlay.cpio >> /tmp/sm8975-rootfs.cpio

The command above does not expose a network device; the test login is on the
emulated serial console.  The base initramfs also starts services for its
original CDSP environment, which are not needed for these tests.
