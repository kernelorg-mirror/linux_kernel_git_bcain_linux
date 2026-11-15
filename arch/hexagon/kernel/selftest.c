// SPDX-License-Identifier: GPL-2.0-only
/* Opt-in runtime checks, including actual protection faults with fixups. */
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/set_memory.h>
#include <linux/vmalloc.h>

int hexagon_test_write(void *addr);
int hexagon_test_execute_nx(void);
void hexagon_test_nx_target(void);

static const unsigned int readonly_value = 0x12345678;
static DECLARE_COMPLETION(busy_started);
static DECLARE_COMPLETION(busy_finished);
static bool busy, woke_while_busy, wake_done;
static int probe_hits;

static noinline int module_text_target(void)
{
	return READ_ONCE(readonly_value);
}

static int __kprobes probe_entry(struct kprobe *p, struct pt_regs *regs)
{
	probe_hits++;
	return 0;
}

static bool test_text_patching(void)
{
	struct kprobe probe = {
		.addr = (kprobe_opcode_t *)module_text_target,
		.pre_handler = probe_entry,
	};
	bool passed;

	if (register_kprobe(&probe))
		return false;
	passed = module_text_target() == 0x12345678 && probe_hits == 1;
	unregister_kprobe(&probe);
	return passed && module_text_target() == 0x12345678 && probe_hits == 1;
}

static int busy_thread(void *unused)
{
	unsigned long deadline;

	WRITE_ONCE(busy, true);
	complete(&busy_started);
	deadline = jiffies + HZ;
	/* No calls, locks, cond_resched(), or preempt_enable() in this loop. */
	while (!READ_ONCE(wake_done) && time_before(jiffies, deadline))
		asm volatile("nop" ::: "memory");
	WRITE_ONCE(busy, false);
	complete(&busy_finished);
	while (!kthread_should_stop())
		schedule_timeout_interruptible(HZ);
	return 0;
}

static int wake_thread(void *unused)
{
	wait_for_completion(&busy_started);
	msleep(20);
	WRITE_ONCE(woke_while_busy, READ_ONCE(busy));
	WRITE_ONCE(wake_done, true);
	while (!kthread_should_stop())
		schedule_timeout_interruptible(HZ);
	return 0;
}

static bool test_preemption(void)
{
	struct task_struct *low, *high;
	int cpu = get_cpu();
	bool passed;

	put_cpu();
	low = kthread_create(busy_thread, NULL, "hexagon-busy");
	if (IS_ERR(low))
		return false;
	high = kthread_create(wake_thread, NULL, "hexagon-wake");
	if (IS_ERR(high)) {
		kthread_stop(low);
		return false;
	}
	kthread_bind(low, cpu);
	kthread_bind(high, cpu);
	sched_set_fifo(high);
	wake_up_process(high);
	wake_up_process(low);
	passed = wait_for_completion_timeout(&busy_finished, 2 * HZ) &&
		 READ_ONCE(woke_while_busy);
	kthread_stop(high);
	kthread_stop(low);
	return passed;
}

static int __init hexagon_arch_test_init(void)
{
	void *page;
	unsigned long nx_page = (unsigned long)hexagon_test_nx_target & PAGE_MASK;
	pte_t *data_pte;
	bool passed = true;

#define CHECK(condition, name) do { \
	bool ok = (condition); \
	pr_info("hexagon arch test: %s: %s\n", name, ok ? "PASS" : "FAIL"); \
	passed &= ok; \
} while (0)
	CHECK(hexagon_test_write((void *)memcpy) == -EFAULT, "kernel text RO");
	CHECK(hexagon_test_write((void *)module_text_target) == -EFAULT,
	      "module text RO");
	CHECK(hexagon_test_write((void *)&readonly_value) == -EFAULT,
	      "module rodata RO");
	data_pte = pte_offset_kernel((pmd_t *)pgd_offset(current->active_mm,
					(unsigned long)&busy), (unsigned long)&busy);
	CHECK(!pte_exec(*data_pte), "module data NX");
	CHECK(!set_memory_nx(nx_page, 1) && hexagon_test_execute_nx() == 1,
	      "NX instruction-fetch fault");
	CHECK(!set_memory_rox(nx_page, 1), "restore executable test text");
	CHECK(module_text_target() == 0x12345678, "module text executes");
	CHECK(test_text_patching(), "kprobe patching of protected module text");
	page = vmalloc(PAGE_SIZE);
	if (!page)
		return -ENOMEM;
	CHECK(!set_memory_ro((unsigned long)page, 1) &&
	      hexagon_test_write(page) == -EFAULT, "set_memory_ro");
	CHECK(!set_memory_rw((unsigned long)page, 1) &&
	      !hexagon_test_write(page), "set_memory_rw");
	vfree(page);
	CHECK(test_preemption(), "IRQ-return preemption of a kernel busy loop");
	return passed ? 0 : -EINVAL;
#undef CHECK
}

static void __exit hexagon_arch_test_exit(void)
{
}

module_init(hexagon_arch_test_init);
module_exit(hexagon_arch_test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hexagon permission and interrupt-preemption tests");
