======================
Architecture Features
======================

CPU Architecture
================

Hexagon is a 32-bit VLIW (Very Long Instruction Word) processor architecture
from Qualcomm, designed for high performance and low power.  Instructions are
grouped into "packets" of up to four instructions that execute in parallel.

Key Characteristics
===================

- **Page sizes**: 4KB, 16KB, 64KB, or 256KB (selected at build time)
- **Thread-info register**: R19 is reserved (``-ffixed-r19``) for per-thread
  kernel state
- **Endianness**: Little-endian only
- **SMP**: Up to 6 hardware threads per core

Virtual Machine Interface
=========================

Linux runs as a guest under the H2 hypervisor, which provides:

- Privilege separation via trap1 hypercalls
- Virtual interrupt controller (vmintop)
- TLB management (vmnewmap, vmclrmap)
- Timer services (vmtimerop, vmsettime, vmgettime)
- Inter-processor interrupts (vmintop post)

Coprocessor Extensions
======================

- **HVX** (Hexagon Vector eXtensions): SIMD vector processing with
  configurable vector lengths (512-bit or 1024-bit)
- **HMX** (Hexagon Matrix eXtensions): Matrix/tensor acceleration

Both use lazy context save/restore via a thread notification framework
(``asm/notify.h``), allocating context on first use and restoring on
context switch.

Platforms
=========

- **QEMU virt**: Virtual machine for development and testing
- **Comet**: Hardware development board
- **SM8150/MSS**: Modem subsystem on Snapdragon SoCs
