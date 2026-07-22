// SPDX-License-Identifier: GPL-2.0
/*
 * HVX coprocessor context management for Hexagon
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
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/hvx.h>
#include <asm/hexagon_vm.h>
#include <asm/notify.h>
#include <asm/traps.h>
#include <uapi/asm/registers.h>

#define DRIVER_NAME "hexagon_hvx"

static int max_ctxts;
static int vlength;
static struct semaphore ctxts_avail;
static struct hvx_ctxt_status *ctxt_status;

struct kmem_cache *vregs_cachep;

struct rw_semaphore enabled;

atomic_t hvx_refcount = ATOMIC_INIT(0);

static unsigned long hvx_debug;

struct extbits {
	int xa;
	int xe;
};

DEFINE_PER_CPU(struct extbits, extbits_cached);

static inline int ctxt_to_xa(int ctxt)
{
	return (4 + ctxt);
}

static void set_hardware(int xe, int xa)
{
	preempt_disable();
	if (xa == -1)
		xa = this_cpu_ptr(&extbits_cached)->xa;

	if ((this_cpu_ptr(&extbits_cached)->xe != xe) ||
	    (this_cpu_ptr(&extbits_cached)->xa != xa)) {
		this_cpu_ptr(&extbits_cached)->xa = xa;
		this_cpu_ptr(&extbits_cached)->xe = xe;
		__vmhwconfig(HWCONFIG_EXTBITS, 0,
			     this_cpu_ptr(&extbits_cached)->xa,
			     this_cpu_ptr(&extbits_cached)->xe);
	}
	preempt_enable();
}

static void smp_set_hardware(void *info)
{
	struct extbits *extbits = (struct extbits *)info;

	set_hardware(extbits->xe, extbits->xa);
}

static void drop_ctxt(struct thread_info *ti)
{
	if (ti->hvx->cnum >= 0) {
		WARN_ON(HVX_CTXT_LOCK_MASK !=
			atomic_cmpxchg(&ctxt_status[ti->hvx->cnum].flags,
				       HVX_CTXT_LOCK_MASK, 0));
		ti->hvx->cnum = -1;
		/* Ensure cnum is cleared before signaling context available */
		smp_wmb();
		up(&ctxts_avail);
	}
}

static int fault_coproc(struct thread_info *ti)
{
	if (!down_read_trylock(&enabled))
		goto out_ok;

	atomic_inc(&coproc_notify_cnt);

	if (!ti->hvx) {
		atomic_inc(&hvx_refcount);
		ti->hvx = kmalloc_obj(*ti->hvx, GFP_KERNEL);
		if (!ti->hvx)
			goto out_err;
		ti->hvx->vregs = kmem_cache_alloc(vregs_cachep,
						   GFP_ATOMIC | __GFP_ZERO);
		if (!ti->hvx->vregs)
			goto out_free;

		ti->hvx->generation = 0;
		ti->hvx->cnum = -1;
		ti->hvx->prev_cnum = -1;
		goto out_up;
out_free:
		kfree(ti->hvx);
		ti->hvx = NULL;
out_err:
		atomic_dec(&hvx_refcount);
		force_sig(SIGFPE);
	}
out_up:
	up_read(&enabled);
out_ok:
	return NOTIFY_OK;
}

static int hvx_restore_all(struct thread_info *ti)
{
	if (user_mode(ti->regs) && ti->hvx) {
		int ctxt = ti->hvx->cnum;

		set_hardware(1, ctxt_to_xa(ctxt));

		if (!ctxt_status[ctxt].thread) {
			ctxt_status[ctxt].thread = ti;
			restore_hvx_context(ti->hvx->vregs);
		} else if (ctxt_status[ctxt].thread != ti) {
			ctxt_status[ti->hvx->cnum].thread = ti;
			restore_hvx_context(ti->hvx->vregs);
			ctxt_status[ctxt].generation = ti->hvx->generation;
		} else if (ctxt_status[ctxt].generation != ti->hvx->generation) {
			restore_hvx_context(ti->hvx->vregs);
		}
		ti->hvx->prev_cnum = ctxt;
	} else {
		/* Disable XE, keep the XA */
		set_hardware(0, -1);
	}
	return NOTIFY_OK;
}

static int atomic_notify(struct notifier_block *nb, unsigned long action,
			 void *arg)
{
	int ret = NOTIFY_DONE;

	switch (action) {
	case THREAD_EVENT_ENTRY:
	{
		struct thread_info *ti = arg;

		if (user_mode(ti->regs)) {
			if (ti->hvx && (ti->hvx->cnum >= 0)) {
				set_hardware(1, ctxt_to_xa(ti->hvx->cnum));
				save_hvx_context(ti->hvx->vregs);
				ctxt_status[ti->hvx->cnum].generation =
					++ti->hvx->generation;
				drop_ctxt(ti);
			}
		}
		break;
	}
	case THREAD_EVENT_RELEASE:
	{
		struct thread_info *ti = arg;

		if (ti->hvx) {
			if ((ti->hvx->cnum >= 0) &&
			    (ctxt_status[ti->hvx->cnum].thread == ti)) {
				ctxt_status[ti->hvx->cnum].thread = NULL;
				drop_ctxt(ti);
			}
			kmem_cache_free(vregs_cachep, ti->hvx->vregs);
			kfree(ti->hvx);
			atomic_dec(&hvx_refcount);
		}
		break;
	}
	case THREAD_EVENT_SWITCH:
		/* arg here is task_struct *next, not thread info */
		if (current_thread_info()->hvx)
			drop_ctxt(current_thread_info());
		break;
	case THREAD_EVENT_RESTOREALL:
		ret = hvx_restore_all((struct thread_info *)arg);
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

/* interrupts are on */
static int blocking_notify(struct notifier_block *nb, unsigned long action,
			   void *arg)
{
	struct thread_info *ti = arg;
	int i;
	int ret;

	switch (action) {
	case THREAD_EVENT_DOWORK:
		if (ti->hvx && (ti->hvx->cnum == -1)) {
			ret = down_killable(&ctxts_avail);

			if (ret != 0) {
				force_sig(SIGFPE);
				return NOTIFY_OK;
			}

			if (ti->hvx->prev_cnum >= 0) {
				if (!atomic_cmpxchg(&ctxt_status[ti->hvx->prev_cnum].flags,
						    0, HVX_CTXT_LOCK_MASK)) {
					ti->hvx->cnum = ti->hvx->prev_cnum;
					goto ctxt_found;
				}
			}
			for (i = 0; i < max_ctxts; i++) {
				if (!atomic_cmpxchg(&ctxt_status[i].flags,
						    0, HVX_CTXT_LOCK_MASK)) {
					ti->hvx->cnum = i;
					break;
				}
			}
			WARN_ON(i == max_ctxts);
		}
ctxt_found:
		break;
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static struct notifier_block blocking_nb = {
	.notifier_call = blocking_notify,
};

static int hvx_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int i;
	int ret;
	struct extbits ext;

	ret = of_property_read_u32(node, "qcom,hvx-max-ctxts", &max_ctxts);
	if (ret) {
		pr_err("unable to read qcom,hvx-max-ctxts\n");
		goto out;
	}

	ret = of_property_read_u32(node, "qcom,hvx-vlength", &vlength);
	if (ret) {
		pr_err("unable to read qcom,hvx-vlength\n");
		goto out;
	}

	ctxt_status = devm_kzalloc(&pdev->dev,
				   sizeof(struct hvx_ctxt_status) * max_ctxts,
				   GFP_KERNEL);
	if (!ctxt_status) {
		ret = -ENOMEM;
		goto out;
	}

	vregs_cachep = kmem_cache_create("hvx_vregs",
					 sizeof(struct HVX_Vectors),
					 sizeof(HVX_Vector), 0, NULL);
	if (!vregs_cachep) {
		ret = -ENOMEM;
		goto out;
	}

	__vmhwconfig(HWCONFIG_VLENGTH, 0, vlength, 0x0);

	for_each_possible_cpu(i) {
		per_cpu(extbits_cached, i).xa = 0;
		per_cpu(extbits_cached, i).xe = 1;
	}

	ext.xa = 4;
	ext.xe = 0;
	on_each_cpu(smp_set_hardware, &ext, 1);

	sema_init(&ctxts_avail, max_ctxts);

	blocking_thread_register_notify(&blocking_nb);
	atomic_thread_register_notify(&atomic_nb);

	init_rwsem(&enabled);

	ret = 0;
	goto out;

out:
	return ret;
}

static void hvx_remove(struct platform_device *pdev)
{
	int locked = 0;
	struct extbits ext;
	int i;

	if (!down_write_trylock(&enabled))
		return;

	for (i = 0; i < max_ctxts; i++) {
		if (down_trylock(&ctxts_avail))
			break;
		locked++;
	}
	if ((locked < max_ctxts) || (atomic_read(&hvx_refcount) > 0)) {
		for (i = 0; i < locked; i++)
			up(&ctxts_avail);
		up_write(&enabled);
		return;
	}

	blocking_thread_unregister_notify(&blocking_nb);
	atomic_thread_unregister_notify(&atomic_nb);

	ext.xa = -1;
	ext.xe = 0;
	/* Ensure ext fields are visible before cross-CPU read */
	smp_wmb();
	on_each_cpu(smp_set_hardware, &ext, 1);

	kmem_cache_destroy(vregs_cachep);
}

static const struct of_device_id hvx_device_id[] = {
	{ .compatible = "qcom,hexagon-hvx", },
	{ }
};
MODULE_DEVICE_TABLE(of, hvx_device_id);

static struct platform_driver hvx_driver = {
	.probe = hvx_probe,
	.remove = hvx_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(hvx_device_id),
	},
};

static struct attribute *hvx_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(hvx);

static ssize_t refcount_show(const struct class *class,
			     const struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", atomic_read(&hvx_refcount));
}

static ssize_t max_ctxts_show(const struct class *class,
			      const struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", max_ctxts);
}

static ssize_t vlength_show(const struct class *class,
			    const struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vlength);
}

static ssize_t debug_store(const struct class *class,
			   const struct class_attribute *attr,
			   const char *buf, size_t count)
{
	if (kstrtoul(buf, 0, &hvx_debug))
		return -EINVAL;

	return count;
}

static ssize_t debug_show(const struct class *class,
			  const struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%lu\n", hvx_debug);
}

static CLASS_ATTR_RW(debug);
static CLASS_ATTR_RO(max_ctxts);
static CLASS_ATTR_RO(vlength);
static CLASS_ATTR_RO(refcount);

static struct attribute *hvx_class_attrs[] = {
	&class_attr_debug.attr,
	&class_attr_max_ctxts.attr,
	&class_attr_vlength.attr,
	&class_attr_refcount.attr,
	NULL,
};
ATTRIBUTE_GROUPS(hvx_class);

static struct class hvx_class = {
	.name = "hvx",
	.dev_groups = hvx_groups,
	.class_groups = hvx_class_groups,
};

static int __init hvx_driver_init(void)
{
	int ret;

	ret = class_register(&hvx_class);
	platform_driver_register(&hvx_driver);

	return ret;
}
module_init(hvx_driver_init);

static void __exit hvx_driver_exit(void)
{
	class_unregister(&hvx_class);
	platform_driver_unregister(&hvx_driver);
}
module_exit(hvx_driver_exit);

MODULE_DESCRIPTION("Hexagon HVX Module");
MODULE_LICENSE("GPL");
