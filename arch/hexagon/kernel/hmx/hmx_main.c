/*
 * SPDX-License-Identifier: GPL-2.0
 * HMX (Hexagon Matrix eXtension) coprocessor context management
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/hmx.h>
#include <asm/hexagon_vm.h>
#include <asm/notify.h>
#include <asm/traps.h>
#include <uapi/asm/registers.h>

#define DRIVER_NAME "hexagon_hmx"

static struct rw_semaphore drv_enabled;

static atomic_t hmx_refcount = ATOMIC_INIT(0);

static unsigned long hmx_debug;

struct hmx_extbits {
	int xe2;
};

static DEFINE_PER_CPU(struct hmx_extbits, hmx_extbits_cached);

static void set_hardware(int xe2)
{
	preempt_disable();
	if (this_cpu_ptr(&hmx_extbits_cached)->xe2 != xe2) {
		this_cpu_ptr(&hmx_extbits_cached)->xe2 = xe2;
		__vmhwconfig(HWCONFIG_HMXBITS, 0,
			     this_cpu_ptr(&hmx_extbits_cached)->xe2, 0);
	}
	preempt_enable();
}

static void smp_set_hardware(void *info)
{
	struct hmx_extbits *extbits = (struct hmx_extbits *)info;

	set_hardware(extbits->xe2);
}

static int fault_coproc(struct thread_info *ti)
{
	if (!down_read_trylock(&drv_enabled))
		return NOTIFY_OK;

	atomic_inc(&coproc_notify_cnt);

	if (!ti->extensions.hmx) {
		ti->extensions.hmx = kmalloc_obj(*ti->extensions.hmx,
					       GFP_KERNEL);
		if (!ti->extensions.hmx) {
			up_read(&drv_enabled);
			force_sig(SIGFPE);
			return NOTIFY_OK;
		}
		atomic_inc(&hmx_refcount);
	}

	up_read(&drv_enabled);
	return NOTIFY_OK;
}

static int hmx_restore_all(struct thread_info *ti)
{
	if (user_mode(ti->regs) && ti->extensions.hmx)
		set_hardware(1);
	else
		set_hardware(0);
	return NOTIFY_OK;
}

static void thread_event_release(struct thread_info *ti)
{
	if (!ti->extensions.hmx)
		return;

	kfree(ti->extensions.hmx);
	ti->extensions.hmx = NULL;
	atomic_dec(&hmx_refcount);
}

static int atomic_notify(struct notifier_block *nb, unsigned long action,
			 void *arg)
{
	int ret = NOTIFY_DONE;

	switch (action) {
	case THREAD_EVENT_RELEASE:
		thread_event_release((struct thread_info *)arg);
		break;
	case THREAD_EVENT_RESTOREALL:
		ret = hmx_restore_all((struct thread_info *)arg);
		break;
	case THREAD_EVENT_FAULT_COPROC:
		ret = fault_coproc((struct thread_info *)arg);
		break;
	default:
		break;
	}
	return ret;
}

static struct notifier_block atomic_nb = {
	.notifier_call = atomic_notify,
};

static int hmx_probe(struct platform_device *pdev)
{
	struct hmx_extbits ext;
	int i;

	for_each_possible_cpu(i)
		per_cpu(hmx_extbits_cached, i).xe2 = 1;

	ext.xe2 = 0;
	on_each_cpu(smp_set_hardware, &ext, 1);

	atomic_thread_register_notify(&atomic_nb);

	init_rwsem(&drv_enabled);

	return 0;
}

static void hmx_remove(struct platform_device *pdev)
{
	struct hmx_extbits ext;

	if (!down_write_trylock(&drv_enabled))
		return;

	if (atomic_read(&hmx_refcount) > 0) {
		up_write(&drv_enabled);
		return;
	}

	atomic_thread_unregister_notify(&atomic_nb);

	ext.xe2 = 0;
	/* Ensure ext fields are visible before cross-CPU read */
	smp_wmb();
	on_each_cpu(smp_set_hardware, &ext, 1);
}

static const struct of_device_id hmx_device_id[] = {
	{ .compatible = "qcom,hexagon-hmx", },
	{ }
};
MODULE_DEVICE_TABLE(of, hmx_device_id);

static struct platform_driver hmx_driver = {
	.probe = hmx_probe,
	.remove = hmx_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(hmx_device_id),
	},
};

static ssize_t refcount_show(const struct class *class,
			     const struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", atomic_read(&hmx_refcount));
}

static ssize_t release_store(const struct class *class,
			     const struct class_attribute *attr,
			     const char *buf, size_t count)
{
	thread_event_release(current_thread_info());
	return count;
}

static ssize_t debug_show(const struct class *class,
			  const struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", hmx_debug);
}

static ssize_t debug_store(const struct class *class,
			   const struct class_attribute *attr,
			   const char *buf, size_t count)
{
	if (kstrtoul(buf, 0, &hmx_debug))
		return -EINVAL;

	return count;
}

static CLASS_ATTR_RW(debug);
static CLASS_ATTR_WO(release);
static CLASS_ATTR_RO(refcount);

static struct attribute *hmx_class_attrs[] = {
	&class_attr_debug.attr,
	&class_attr_release.attr,
	&class_attr_refcount.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hmx_class);

static struct class hmx_class = {
	.name = "hmx",
	.class_groups = hmx_class_groups,
};

static int __init hmx_driver_init(void)
{
	int ret;

	ret = class_register(&hmx_class);
	if (ret)
		return ret;

	ret = platform_driver_register(&hmx_driver);
	if (ret)
		class_unregister(&hmx_class);

	return ret;
}
module_init(hmx_driver_init);

static void __exit hmx_driver_exit(void)
{
	class_unregister(&hmx_class);
	platform_driver_unregister(&hmx_driver);
}
module_exit(hmx_driver_exit);

MODULE_DESCRIPTION("Hexagon HMX Module");
MODULE_LICENSE("GPL");
