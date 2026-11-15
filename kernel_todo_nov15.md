# Hexagon kernel opportunities — November 15, 2026

This report describes the current checkout, including its local additions, rather
than the upstream port alone. The opportunity list is based on source inspection; effort estimates are
relative. Implementation and runtime validation are recorded separately below.

The tree already contains an eBPF JIT, kprobes/kretprobes, perf support, KASAN,
CFI, shadow call stacks, and HVX/HMX/HLX support. Several architecture feature
tables under `Documentation/features/` have not caught up with these additions.

## Recommended projects

| Feature | Status / remaining gap | Benefit | Estimated effort |
| --- | --- | --- | --- |
| `clone3()` | Explicitly disabled by `__ARCH_BROKEN_SYS_CLONE3` | Modern process/thread creation, including atomic pidfd creation | Small–medium |
| Accelerator signal ABI | Signal frames contain scalar registers but no HVX/HLX state | Signal handlers can use accelerators without corrupting interrupted computations | Medium–large |
| Seccomp filters | No architecture selection or syscall-entry enforcement | Syscall sandboxing for services, runtimes, and container workloads | Medium |
| ELF hardware capabilities | `ELF_HWCAP` is zero; `ELF_PLATFORM` is null | Standard userspace discovery and library dispatch for instructions and accelerators | Small–medium |
| Function tracing and uprobes | Implemented: dynamic/function-graph tracing and conservative packet-based uprobes | Kernel and application performance investigation | Large |
| Full userspace ASLR | No `ARCH_HAS_ELF_RANDOMIZE` support | Randomize PIE, mmap, and heap placement beyond generic stack randomization | Medium |
| Strict kernel/module permissions | Implemented: protected text/rodata, NX data, and runtime permission changes | Read-only text/rodata and non-executable ordinary kernel data | Large |
| Restartable sequences (`rseq`) | No `HAVE_RSEQ` or architecture integration | Faster userspace per-CPU operations and allocator paths | Medium |
| Timekeeping vDSO | Existing vDSO contains only the signal-return trampoline | Reduce frequent time-query overhead | Medium–large; hardware dependent |
| Kernel preemption | Implemented: full kernel preemption without KASAN; IRQ-return rescheduling; PREEMPT_RT remains separate | Improve response latency under kernel load | Large |

## Implementation considerations

### clone3

`arch/hexagon/include/asm/unistd.h` disables the syscall. `copy_thread()` in
`arch/hexagon/kernel/process.c` already accepts `kernel_clone_args`, but needs
the stack-base-plus-size convention handled correctly. Verify the child return
path, TLS, stack alignment, and pidfd behavior before removing the disabling flag.

### Accelerator signals and debugging

`arch/hexagon/kernel/signal.c` saves scalar state only. Define an extensible
signal-frame ABI with state save/restore, nested-signal behavior, validation of
user-supplied state on sigreturn, and compatibility with existing signal frames.
HVX already has a ptrace/core-dump regset in `arch/hexagon/kernel/ptrace.c`;
HLX does not. Assess HMX separately to establish which state can be preserved.

### Seccomp

`arch/hexagon/include/asm/syscall.h` already provides architecture identification
and argument access. Add filtering and immediate syscall suppression, correct
ptrace/filter ordering, rollback semantics, and SIGSYS behavior. Follow the
[kernel seccomp requirements](https://docs.kernel.org/userspace-api/seccomp_filter.html)
and run the seccomp selftests. Kernel support also needs matching userspace
architecture support in the sandbox tools being used.

### ELF capabilities

Replace the placeholders in `arch/hexagon/include/asm/elf.h` with a documented
capability ABI. Advertise features actually usable by userspace under the guest
configuration rather than just the configured core version. Coordinate capability
bits and library dispatch with userspace; do not expose speculative capabilities.

### Function tracing and uprobes

The initial `arch/hexagon/include/asm/ftrace.h` implemented syscall-name matching.
Function tracing needs compiler instrumentation and an ABI-correct entry hook.
Dynamic tracing additionally requires safe packet-aware text patching. Function
graph tracing needs return-address interception and unwind integration.

Uprobes need instruction-packet validation, out-of-line execution or emulation,
breakpoint handling, return probes, and fault/signal recovery. Hexagon packets
must be treated as units: branches, PC-relative operations, duplex instructions,
and hardware-loop endings require explicit handling or rejection. Existing
kprobes provide a useful starting point, but user probes must also handle task
migration, sleeping, signals, and userspace memory faults.

See the [function tracing documentation](https://docs.kernel.org/trace/ftrace.html).

### ASLR and memory protection

Add ELF/mmap/brk randomization with entropy appropriate to the 32-bit address
space and each supported page size. Generic stack randomization already exists;
this is not a claim that all ASLR is absent.

Strict RWX requires separate text, rodata, and data permissions in the boot map,
runtime permission updates and TLB/cache synchronization, and module allocation
and relocation transitions. Coordinate this with tracing and probe text patching.
Guarded vmapped stacks are another useful follow-on, subject to virtual-address
space constraints and reliable exception entry on stack overflow.

### rseq

Define the userspace signature ABI, wire restart/abort handling into signal and
return-to-userspace paths, and validate migration/preemption behavior. Kernel
support needs matching libc/librseq support to produce application benefits.

### Timekeeping

The current vDSO in `arch/hexagon/kernel/vdso.c` is a signal trampoline, not an
ELF timekeeping implementation. A fast time vDSO needs a suitable, stable,
userspace-readable counter and consistent conversion data. If H2 offers only
trapped reads, start by assessing coarse-time functions.

`arch/hexagon/kernel/time-h2.c` uses CPU 0's clockevent and broadcast IPIs for
other CPUs. Per-CPU clockevents could improve scalability if H2 exposes them.

### Preemption

The initial `arch/hexagon/Kconfig` selected `ARCH_NO_PREEMPT`; `vm_entry.S` lacked
rescheduling on kernel-mode interrupt return. The follow-up below enables full
preemption for non-KASAN kernels. Implement interrupt-return rescheduling
with correct preemption counts and interrupt state, then audit per-CPU accesses,
coprocessor ownership, assembly paths, and context switches. PREEMPT_RT would be
a separate, larger undertaking.

## Extend existing perf support

`arch/hexagon/kernel/perf_event.c` provides a core-wide PMU, rejects task-attached
hardware events, and samples with a timer. Existing frame-pointer callchains are
useful, but perf register sampling, userspace stack dumps, and tools/perf
architecture support would broaden tooling. Accurate per-task hardware event
attribution and overflow-interrupt sampling depend on H2 capabilities; do not
attribute core-wide counts to a task without hardware/hypervisor support.

## Suggested order

For broad usability: clone3, accelerator signal-state preservation, seccomp,
then HWCAP and tracing. For DSP development and tuning, move HWCAP and tracing
earlier. Keep the feature tables synchronized as implementations land.

The requested implementation work following this report is function tracing and
uprobes. Its actual supported scope and validation results should be documented
with the implementation rather than assumed from this opportunity list.

## First implementation: static tracing and uprobes

Implemented in this working tree:

- Static kernel function tracing through LLVM's `-pg`/`mcount` ABI, including
  module symbol export and preservation of scratch registers, predicates, loop
  registers and circular-addressing registers. Profiling call sites remain in
  the kernel when tracing is disabled.
- Packet-based uprobes and return probes, using a complete copied packet and a
  completion trap rather than hardware single stepping. Includes per-mapping
  boundary validation, original-PC restoration for faults, pending-signal
  handling, fork support, and restoration of a debugger's single-step state.
- Trace-buffer doubleword alignment, exclusions for IRQ-state/exception-entry
  functions and secondary-CPU bootstrap, IRQ re-enabling after VM idle, and
  publication of the saved return IRQ state after the restore notifiers.
- Architecture documentation in `Documentation/arch/hexagon/tracing.rst` and a
  registered kselftest in `tools/testing/selftests/hexagon/`.

That first implementation did not include dynamic ftrace or function-graph
tracing; these are added by the large-project follow-up below. Register-bearing
ftrace callbacks and BPF fentry/fexit trampolines remain unsupported. Uprobes conservatively reject
branches/calls/traps, PC/control-register operations, hardware-loop endings,
dealloc-return instructions and duplex groups containing L2. Probes must start
at a packet boundary. A preceding active uprobe, or a mapping-start address with
a nonzero file page offset, is rejected when the boundary cannot be established.
These restrictions are documented alongside the implementation.

### Focused validation

Built with Clang/LLD 23.1.0, starting from `qemu_defconfig`, using 64 KiB pages
and four CPUs. Tested on QEMU 11.1.90's `virt,hvm=on` machine, with and without
kernel CFI enabled. Both configurations passed all ten architecture selftests:

1. Kernel function events.
2. Rejection of an address inside a packet.
3. Rejection of a control-flow packet.
4. Rejection of a PC-relative packet.
5. Rejection of a hardware-loop ending.
6. Entry/return probes and four-word packet results.
7. Fault delivery at the original PC, followed by a successful probe.
8. Fork inheritance and recreation of out-of-line state.
9. Recording all installed uprobe event types.
10. Asynchronous signals and probed calls from signal handlers.

Both runs reported `pass:10 fail:0` and exited with status zero. The plain kernel
also passed the generic function/nop tracer startup checks and the ftrace-filter
check. The generic register-callback check reports no architecture support, as
expected for this implementation. The host build of the architecture selftest
returns the expected skip on a non-Hexagon machine. The new architecture C and
assembly implementation files pass strict checkpatch without diagnostics, and
`git diff --check` passes.

The guest test programs were linked using `-static -no-pie`; this toolchain's
default static-PIE startup failed before reaching the test program. The runtime
checks establish emulated v68 behavior, not validation on physical hardware or
every supported page size/core revision.

The earlier broad startup runs also exposed a separate kprobe tracing selftest
failure in `process_fetch_insn`; the plain focused startup configuration disables
`KPROBE_EVENTS` while retaining `KPROBES`. The CFI configuration retains
`KPROBE_EVENTS`, with startup selftests disabled.

### Full event/function stress validation

The final code also passed `CONFIG_EVENT_TRACE_STARTUP_TEST` on four CPUs with
multithreaded TCG. All configured events passed individually, by event system,
and enabled together, both without and with the function-tracing callback.
The ten architecture selftests then passed again and the init process reported
status zero. There were no kernel WARNING/Oops reports or RCU stalls in that run.
The expected event-enable diagnostics from the four negative probe tests are
part of successful rejection testing.

An earlier stress run exposed an exception-return bug: `vm_entry.S` published
the saved return IRQ state through `vmsetie_rte_disable()` and then called
`atomic_thread_notify()`. A profiling callback in that notifier could restore
IRQs to enabled while the event frame was still being restored. The final code
keeps the cache disabled through the notifier and publishes the return state
as its last C call. This removed the reproducible SMP RCU stall. The CFI-enabled
ten-test run was repeated successfully after this correction as well.


## Large-project implementation follow-up

Implemented the three projects whose effort was labeled **Large**. Accelerator
signal-state preservation and the timekeeping vDSO were labeled Medium–large
and remain separate projects.

### Dynamic ftrace and function graphs

- Added Hexagon relocation handling to `recordmcount`, recording the extender
  address of LLVM's two-word profiling call packet exactly once.
- Added validated call/NOP packet replacement through a writable, non-executable
  alias, with generic stop-machine exclusion for live dynamic-ftrace updates.
- Added function-graph entry/return interception, preservation of the profiling
  ABI's scratch/control registers, and graph-aware stack, perf and wait-channel
  unwinding. Typed callback stubs support kernel CFI.
- Fixed generic function-graph shadow-stack/data alignment for architectures
  requiring aligned doubleword accesses. Frame and reserved-data footer words
  now participate in alignment; generic graph storage selftests exercise this.

Register-bearing callbacks, direct/BPF trampolines and fentry/fexit are not
advertised. Generic Kconfig excludes function graphs when shadow call stacks
are enabled. Conservative packet restrictions on uprobes remain in force.

### Strict kernel and module permissions

- Added optional, default-enabled strict kernel/module RWX and the architecture
  `set_memory_*()` APIs. RAM uses base-page mappings with RX text, RO/NX rodata
  and RW/NX ordinary data. Exception tables are protected with rodata. Freed
  init text becomes RW/NX before poisoning and release.
- Kept the boot page directory in writable data and provided serialized,
  temporary RW/NX aliases for ftrace and kprobe writes. Single-word kprobe
  patches use atomic word stores. Under KASAN the alias reserves a segment
  below the shadow instead of replacing a shadow page-directory entry.
- Corrected mapping-generation publication and address-space switch checks so
  dormant address spaces pick up new kernel mappings. Permission changes
  invalidate translations synchronously across CPUs.
- Fixed kprobe packet-boundary validation at the beginning of a module mapping:
  a known symbol start is accepted without dereferencing an unmapped previous
  page; other instruction reads use nofault access.

Base-page linear mappings cost one base page of page-table memory per 4 MiB
of RAM (16 KiB per MiB with 64 KiB pages). This overhead is avoided when strict
kernel protection is disabled. `rodata=off` leaves text writable and skips late
rodata protection; ordinary data remains NX.

### Full kernel preemption

- Made `ARCH_NO_PREEMPT` conditional on KASAN and added kernel IRQ-return
  scheduling when saved
  IRQ state, preemption count and reschedule state allow it.
- Protected per-CPU IRQ-cache and TLB operations against migration, synchronized
  IRQ entry before per-CPU accesses, and guarded notifier callbacks with RCU
  watching even when returning to idle.
- Retained IRQ-state publication after the final restore notifier, and changed
  accelerator fault-path allocations that run atomically to `GFP_ATOMIC`.

This implements `CONFIG_PREEMPT`; PREEMPT_RT, lazy preemption and runtime
preemption-model switching remain separate work.

### Follow-up validation

Clang/LLD 23.1.0 cross-builds and four-vCPU QEMU `virt,hvm=on` runs exercise
strict kernel/module permissions, full preemption, dynamic/function-graph
tracing and packet uprobes together. `DEBUG_PREEMPT`, `DEBUG_RODATA_TEST` and
generic ftrace startup tests are enabled.

The 64 KiB configuration, its kernel-CFI variant, and a 4 KiB configuration
each passed all **13** architecture tests (`pass:13 fail:0 skip:0`), plus generic
dynamic-ftrace,
function-graph, recursion, graph-storage and rodata startup tests. No kernel
WARNING, Oops or RCU stall appeared in these runs. These focused configurations
retain `KPROBES` and disable `KPROBE_EVENTS`; the earlier generic kprobe-event
fetch selftest failure described above remains separate.

The expanded suite checks dynamic filtering, graph entry/return records,
actual write faults on kernel/module text and module rodata, an actual NX
instruction-fetch fault with exception-table recovery, permission restoration,
vmalloc RO/RW transitions, kprobe patching of protected module text, and forced
IRQ-return preemption of a kernel busy loop by a higher-priority thread pinned
to the same CPU. The busy loop contains no voluntary scheduling calls.

See `Documentation/arch/hexagon/memory.rst` and `tracing.rst` for configuration,
implementation limits and test instructions. Runtime results describe emulated
v68 behavior; physical hardware and other core revisions remain unvalidated.

The first 4 KiB test run exposed an invalid CPU number in the selftest's
`cpumask_first()` affinity setup. The test now captures the current CPU with
`get_cpu()`/`put_cpu()` before creating and pinning its threads; the corrected
run passed all tests, including the forced-preemption check.

The full `EVENT_TRACE_STARTUP_TEST` stress run was repeated with the new dynamic
tracer, strict permissions and full preemption on four CPUs using multithreaded
TCG. All configured events passed individually, by event system and enabled
together, both without and with the function callback. Generic graph/storage
checks and all 13 architecture tests then passed (`status=0`). No kernel
WARNING, Oops or RCU stall appeared in that run.

### KASAN compatibility limit and remaining preemption work

The attempted four-CPU KASAN + full-preemption configuration booted and passed
generic tracer/graph startup tests, but runtime operations stalled in RCU
grace-period waits. This reproduced with a narrow function filter and with
instruction-counted QEMU time. Crucially, a separate build with function
tracing disabled also stalled while registering negative uprobe tests.
`RCU_EQS_DEBUG` did not report an accounting imbalance. The cause is unresolved;
these runs are **not** counted as passes.

Kconfig therefore retains the port's original `ARCH_NO_PREEMPT` model for KASAN
kernels. Function tracing, uprobes and strict permissions remain available with
KASAN. Removing the preemption restriction requires resolving the preemptible-RCU
stall and rerunning the combined suite. IRQ-cache helpers are excluded from
sanitizer instrumentation because they run before IRQ/RCU entry and after
return-state publication.

The final KASAN configuration retains `PREEMPT_NONE`, strict kernel/module
permissions, dynamic ftrace and function graphs. On four CPUs with multithreaded
TCG it passed all **12** tracing/uprobe tests (`pass:12 fail:0 skip:1`, status
zero), plus the generic rodata test, without kernel warnings or RCU stalls.
The optional protection/preemption module test was skipped because it depends
on full preemption. This isolates the supported compatibility boundary:
KASAN tracing works with the original preemption model; full preemption works
in the non-KASAN configurations above.

| Final configuration | Runtime result |
| --- | --- |
| 64 KiB pages, strict RWX, full preemption, dynamic/function graphs | 13 passed; generic tracer/graph/rodata checks passed |
| Same, with kernel CFI | 13 passed; generic tracer/graph/rodata checks passed |
| 4 KiB pages, strict RWX, full preemption, dynamic/function graphs | 13 passed; generic tracer/graph/rodata checks passed |
| 64 KiB, full SMP event/function startup stress | All event modes passed, then 13 architecture tests passed |
| 64 KiB, KASAN, non-preemptible kernel, dynamic/function graphs | 12 passed; optional preemption module skipped; rodata check passed |
| KASAN plus full preemption | Excluded by Kconfig pending preemptible-RCU stall fix |

All final tested configurations use four emulated CPUs. The host selftest
build returns its expected non-Hexagon skip, and `git diff --check` passes.
