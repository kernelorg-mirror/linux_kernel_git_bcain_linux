// SPDX-License-Identifier: GPL-2.0-only
/* Fault-driven HLX context management for H2 guests. */
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/percpu.h>
#include <linux/sched/signal.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <asm/hexagon_vm.h>
#include <asm/hlx.h>
#include <asm/notify.h>
#include <asm/thread_info.h>

#define HLX_REG_SIZE 2048
#define HLX_REGS 16
#define HLX_STATE_SIZE (HLX_REG_SIZE * HLX_REGS)
#define HLX_MAX_CONTEXTS 16

struct hlx_threadinfo {
	void *regs;
	int context;
};

static void __iomem *hlx_scratch;
static int hlx_contexts;
static atomic_t hlx_context_busy[HLX_MAX_CONTEXTS];
static struct semaphore hlx_available;

/*
 * HLX loads and stores use VTCM, so each CPU has a private 2 KiB staging
 * area. The task's complete state lives in ordinary kernel memory. A store
 * must consume a .new value; a load writes a .tmp value. The assign in each
 * packet copies the register to itself, forwarding the temporary/new value.
 * The assembler has no HLX support yet; these words use QEMU's HLX encodings.
 */
#define HLX_SAVE_REG(n) do { \
	register unsigned long addr asm("r0") = (unsigned long)scratch; \
	asm volatile(".word %c1\n.word 0xa480c002\n" \
		: "+r" (addr) : "i" (0xe5007820 + (n) * 0x42) : "memory"); \
	memcpy_fromio((char *)state + (n) * HLX_REG_SIZE, scratch, HLX_REG_SIZE); \
} while (0)
#define HLX_RESTORE_REG(n) do { \
	register unsigned long addr asm("r0") = (unsigned long)scratch; \
	memcpy_toio(scratch, (char *)state + (n) * HLX_REG_SIZE, HLX_REG_SIZE); \
	asm volatile(".word %c1\n.word %c2\n" \
		: "+r" (addr) : "i" (0xe5007820 + (n) * 0x42), \
		"i" (0x9800d000 + (n)) : "memory"); \
} while (0)
#define HLX_EACH_REG(op) do { \
	op(0); op(1); op(2); op(3); op(4); op(5); op(6); op(7); \
	op(8); op(9); op(10); op(11); op(12); op(13); op(14); op(15); \
} while (0)

static void hlx_save(void *state)
{
	void __iomem *scratch = hlx_scratch + smp_processor_id() * HLX_REG_SIZE;

	HLX_EACH_REG(HLX_SAVE_REG);
}

static void hlx_restore(void *state)
{
	void __iomem *scratch = hlx_scratch + smp_processor_id() * HLX_REG_SIZE;

	HLX_EACH_REG(HLX_RESTORE_REG);
}

static void hlx_drop(struct hlx_threadinfo *h)
{
	if (h->context < 0)
		return;
	WARN_ON_ONCE(atomic_cmpxchg(&hlx_context_busy[h->context], 1, 0) != 1);
	h->context = -1;
	up(&hlx_available);
}

static void hlx_free(struct thread_info *ti)
{
	struct hlx_threadinfo *h = ti->extensions.hlx;

	if (!h)
		return;
	hlx_drop(h);
	free_pages((unsigned long)h->regs, get_order(HLX_STATE_SIZE));
	kfree(h);
	ti->extensions.hlx = NULL;
}

void hexagon_hlx_flush_thread(void)
{
	unsigned long flags;

	local_irq_save(flags);
	if (current_thread_info()->extensions.hlx) {
		__vmhwconfig(HWCONFIG_HLXBITS, 0, 0, 0);
		hlx_free(current_thread_info());
	}
	local_irq_restore(flags);
}

static int hlx_atomic_notify(struct notifier_block *nb, unsigned long event,
			     void *arg)
{
	struct thread_info *ti = arg;
	struct hlx_threadinfo *h = ti->extensions.hlx;

	switch (event) {
	case THREAD_EVENT_FAULT_HLX:
		coproc_fault_claim();
		if (h)
			break;
		h = kzalloc(sizeof(*h), GFP_ATOMIC);
		if (h)
			h->regs = (void *)__get_free_pages(GFP_ATOMIC | __GFP_ZERO,
							 get_order(HLX_STATE_SIZE));
		if (!h || !h->regs) {
			kfree(h);
			force_sig(SIGFPE);
			break;
		}
		h->context = -1;
		ti->extensions.hlx = h;
		break;
	case THREAD_EVENT_ENTRY:
		if (user_mode(ti->regs) && h && h->context >= 0) {
			hlx_save(h->regs);
			WARN_ON_ONCE(__vmhwconfig(HWCONFIG_HLXBITS, 0, 0, 0));
			hlx_drop(h);
		}
		break;
	case THREAD_EVENT_RESTOREALL:
		if (user_mode(ti->regs) && h && h->context >= 0) {
			WARN_ON_ONCE(__vmhwconfig(HWCONFIG_HLXBITS, 0, h->context, 1));
			hlx_restore(h->regs);
		}
		break;
	case THREAD_EVENT_RELEASE:
		hlx_free(ti);
		break;
	}
	return NOTIFY_OK;
}

static int hlx_blocking_notify(struct notifier_block *nb, unsigned long event,
			       void *arg)
{
	struct thread_info *ti = arg;
	struct hlx_threadinfo *h = ti->extensions.hlx;
	int i;

	if (event != THREAD_EVENT_DOWORK || !h || h->context >= 0)
		return NOTIFY_DONE;
	if (down_killable(&hlx_available)) {
		force_sig(SIGFPE);
		return NOTIFY_OK;
	}
	for (i = 0; i < hlx_contexts; i++) {
		if (!atomic_cmpxchg(&hlx_context_busy[i], 0, 1)) {
			h->context = i;
			return NOTIFY_OK;
		}
	}
	WARN_ON_ONCE(1);
	up(&hlx_available);
	force_sig(SIGFPE);
	return NOTIFY_OK;
}

static struct notifier_block hlx_atomic_nb = { .notifier_call = hlx_atomic_notify };
static struct notifier_block hlx_blocking_nb = { .notifier_call = hlx_blocking_notify };

static int __init hlx_init(void)
{
	struct device_node *node;
	struct resource res;
	long contexts;
	int ret;

	node = of_find_compatible_node(NULL, NULL, "qcom,hexagon-hlx");
	if (!node)
		return 0;
	ret = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (ret || resource_size(&res) < nr_cpu_ids * HLX_REG_SIZE)
		return -EINVAL;
	contexts = __vmgetinfo(HVM_INFO_HLX_CONTEXTS, 0, 0);
	if (contexts <= 0 || contexts > HLX_MAX_CONTEXTS)
		return -ENODEV;
	hlx_scratch = ioremap(res.start, resource_size(&res));
	if (!hlx_scratch)
		return -ENOMEM;
	hlx_contexts = contexts;
	sema_init(&hlx_available, hlx_contexts);
	ret = blocking_thread_register_notify(&hlx_blocking_nb);
	if (ret)
		goto unmap;
	ret = atomic_thread_register_notify(&hlx_atomic_nb);
	if (!ret)
		return 0;
	blocking_thread_unregister_notify(&hlx_blocking_nb);
unmap:
	iounmap(hlx_scratch);
	return ret;
}
device_initcall(hlx_init);
