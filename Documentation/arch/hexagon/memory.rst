.. SPDX-License-Identifier: GPL-2.0

Kernel protection and preemption on Hexagon
=========================================

Memory permissions
------------------

``CONFIG_STRICT_KERNEL_RWX`` and ``CONFIG_STRICT_MODULE_RWX`` default on and
can be disabled in the kernel configuration. Bootstrap uses the existing large
RWX mappings; memory initialization replaces the RAM mappings with base-page
tables, maps kernel and init text RX, and leaves ordinary data RW/NX. After
initialization, rodata, including the exception tables, becomes read-only.
Reclaimed init text becomes writable and non-executable before being poisoned
and freed. The early ``rodata=off`` option leaves text writable and skips the
late rodata protection; ordinary data still uses non-executable mappings.

The implementation uses one base page of page-table storage per 4 MiB of linear
RAM. With 64 KiB pages, this costs 16 KiB per MiB of RAM. This is a deliberate
tradeoff for page-level protection; the bootstrap large mappings remain in use
when strict kernel protection is disabled.

The generic module loader applies RO/X permissions to module text, RO/NX to
rodata, and RW/NX to data. The architecture provides ``set_memory_ro()``,
``set_memory_rw()``, ``set_memory_x()``, ``set_memory_nx()`` and
``set_memory_rox()`` with validation and synchronous TLB invalidation. Kernel
mapping generations are published after mapping updates and checked against the
global generation on address-space switches, so dormant address spaces also
discard stale permissions when resumed.

Text updates use a temporary alias with write permission and no execute
permission. With KASAN, a separate segment below the shadow is reserved for
this alias; otherwise it uses the fixmap. The alias is serialized, revoked and
locally invalidated before unlocking. Single-word updates use a word store for
kprobes; dynamic ftrace
updates whole packets under stop-machine exclusion. This path allocates no
memory and does not temporarily make the executable address writable.

Kernel preemption
-----------------

``CONFIG_PREEMPT`` enables full kernel preemption. Kernel-mode interrupt and
exception returns call ``preempt_schedule_irq()`` only when the interrupted
context had IRQs enabled, its preemption count is zero, and rescheduling is
needed. User-mode returns retain their normal work-pending loop.

KASAN kernels retain the original non-preemptible model. The attempted KASAN
and full-preemption combination stalls in preemptible RCU on QEMU, including
without function tracing; Kconfig excludes that combination pending a fix.

Guest IRQ-cache accesses are protected against CPU migration. Interrupt entry
synchronizes the cache before accessing per-CPU state and enters IRQ/RCU context
before calling entry context-management notifiers. Atomic notifier calls also
bracket RCU watching explicitly, including when an IRQ returns to idle. On
return, context restore
notifiers run with IRQs disabled; the saved return IRQ state is published only
after the final traceable call. TLB invalidations keep the current address space
stable across metadata updates and IPIs. Atomic coprocessor fault callbacks use
atomic allocation flags, and existing blocking return notifiers retain their
ability to wait for a hardware context.

PREEMPT_RT, lazy preemption and runtime preemption-model switching are not
advertised by this implementation.

Validation
----------

The optional ``CONFIG_HEXAGON_ARCH_SELFTEST`` module and the tracing selftest
are described in :doc:`tracing`. Enable ``CONFIG_DEBUG_PREEMPT`` to check CPU
accesses and ``CONFIG_DEBUG_RODATA_TEST`` for the generic rodata protection test.
