# SPDX-License-Identifier: GPL-2.0
always-$(KBUILD_BUILTIN) := vmlinux.lds

obj-y += head.o
obj-$(CONFIG_SMP) += smp.o

obj-y += setup.o irq_cpu.o traps.o syscalltab.o signal.o
obj-y += process.o trampoline.o reset.o ptrace.o vdso.o notify.o

obj-y += irq-hexagon-h2.o

ifeq ($(CONFIG_HEXAGON_H2),y)
obj-y += time-h2.o
else
obj-y += time.o
endif

obj-$(CONFIG_CFI)     += cfi.o
obj-$(CONFIG_KGDB)    += kgdb.o
obj-$(CONFIG_KPROBES) += kprobes.o
obj-$(CONFIG_HEXAGON_PMU) += perf_event.o
obj-$(CONFIG_PERF_EVENTS) += perf_callchain.o
obj-$(CONFIG_HEXAGON_H2_INFO) += h2info.o
obj-$(CONFIG_MODULES) += module.o hexagon_ksyms.o

# Modules required to work with the Hexagon Virtual Machine
obj-y += vm_entry.o vm_events.o vm_switch.o vm_ops.o vm_init_segtable.o
obj-y += vm_vectors.o

obj-$(CONFIG_HAS_DMA) += dma.o

obj-y += hvx/
obj-y += hmx/
obj-$(CONFIG_HEXAGON_HLX) += hlx.o

obj-$(CONFIG_STACKTRACE) += stacktrace.o

obj-$(CONFIG_OF) += devtree.o
