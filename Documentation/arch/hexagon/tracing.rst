.. SPDX-License-Identifier: GPL-2.0

Tracing on Hexagon
==================

Kernel functions
----------------

With ``CONFIG_KASAN``, Kconfig retains the non-preemptible kernel model.
Preemptible RCU stalls in that configuration even when function tracing is
disabled; the compatibility restriction belongs to preemption.

``CONFIG_FUNCTION_TRACER`` enables the ``function`` tracer in tracefs. Hexagon
uses LLVM's ``-pg`` instrumentation, with ``mcount`` called after the function
has established its frame. The hook preserves argument and scratch registers,
predicates, hardware-loop state, and circular-addressing state. The recorded
address is the profiling call's packet address, inside the traced function.

``CONFIG_DYNAMIC_FTRACE`` records the extender of each extended profiling call
with recordmcount. It replaces the complete eight-byte call packet with a
two-word NOP packet when disabled, and supports function filters and module
instrumentation. Runtime updates use the generic stop-machine mechanism. Text
writes use a serialized, temporary RW/NX alias, so executable text can
remain read-only. Without dynamic ftrace, the profiling calls remain present.

``CONFIG_FUNCTION_GRAPH_TRACER`` intercepts the return address at FP+4 and
records function entries and returns. The return trampoline preserves return
registers and control state. Stack traces and perf callchains resolve graph
trampolines back to their original return addresses. The generic Kconfig rules
exclude combining graph tracing with shadow call stacks.

Register-bearing ftrace callbacks and BPF fentry/fexit trampolines are not
provided. Functions marked ``notrace`` are excluded. The exception entry points
and interrupt-state helpers
must remain uninstrumented: tracing before they synchronize the cached guest
interrupt state can change the interrupt state of the interrupted context.
On exception return, restore notifiers run while the cache still reflects
disabled interrupts. Publishing the saved return state must be the final C call
before the assembly restores registers and returns to the interrupted context.

User probes
-----------

``CONFIG_UPROBE_EVENTS`` enables tracefs uprobes and return probes. A probe
address must be four-byte aligned and identify the first word of a packet.
The implementation replaces that word with ``trap0(#0xd9)``, executes a copy
of the complete packet in a per-task out-of-line slot, and completes through
``trap0(#0xda)``. It does not depend on hypervisor hardware single stepping.

Only packets that can execute unchanged at another address are accepted.
Compare-and-jump, new-value jump, jump/call/trap and control-register instruction
classes, dealloc-return instructions, hardware-loop ending packets, and duplex
types containing the L2 subinstruction group are rejected. This is intentionally
conservative; instruction emulation is not implemented.

Boundary validation takes place for each mapping when its breakpoint is
installed. A preceding word that continues a packet is rejected. A preceding
uprobe breakpoint is also rejected, since it could hide a continuing original
packet. A probe at the beginning of a mapping with a nonzero file page offset
is rejected because the preceding packet boundary cannot be checked there.

Faults during out-of-line execution restore the original instruction pointer
before signal delivery. Ordinary pending signals are deferred until execution
completes. Return probes replace the link register before the entry packet runs,
so an allocframe entry packet saves the trampoline as its return address.

Tests
-----

Enable ``CONFIG_FTRACE``, ``CONFIG_FUNCTION_TRACER``, ``CONFIG_PERF_EVENTS`` and
``CONFIG_UPROBE_EVENTS``. ``CONFIG_FTRACE_STARTUP_TEST`` additionally runs the
generic tracer startup checks, including dynamic and graph checks when enabled.
Mount tracefs and run the architecture selftest
as root::

    mount -t tracefs tracefs /sys/kernel/tracing
    make -C tools/testing/selftests TARGETS=hexagon ARCH=hexagon \
        CC=hexagon-linux-musl-clang
    tools/testing/selftests/hexagon/tracing

The selftest creates its own tracing instance and event group. It checks function
events, entry/return probes, four-word packet execution, rejection of interior,
control-flow, PC-relative and hardware-loop probes, fault-PC restoration, fork
inheritance, probes in signal handlers, dynamic filtering and graph returns.
It requires
procfs and an executable backed by a filesystem that supports uprobes.

With ``CONFIG_DEBUG_KERNEL``, ``CONFIG_PREEMPT``, ``CONFIG_KPROBES``,
``CONFIG_STRICT_KERNEL_RWX`` and ``CONFIG_STRICT_MODULE_RWX``, build
``CONFIG_HEXAGON_ARCH_SELFTEST=m`` and enable module unloading. Pass the resulting
test module to the userspace test::

    tools/testing/selftests/hexagon/tracing /path/to/hexagon_arch_test.ko

Loading the module checks actual read-only store faults, a recoverable NX fetch
fault, module data permissions, writable/read-only transitions, and kprobe
patching of protected module text. It also pins a normal-priority busy-loop
thread and a sleeping FIFO thread to the same CPU: the timer must preempt the
busy loop on interrupt return for the test to pass. Both threads are stopped
before module initialization returns. The userspace test unloads the module.
