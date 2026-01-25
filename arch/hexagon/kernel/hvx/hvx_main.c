/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rwsem.h>
#include <linux/sched/signal.h>
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

static struct extbits {
	int xa;
	int xe;
};

DEFINE_PER_CPU(struct extbits, hvx_extbits_cached);

static inline int ctxt_to_xa(int ctxt)
{
	return (4 + ctxt);
}

static void set_hardware(int xe, int xa)
{
	preempt_disable();
	if (xa == -1) {
		xa = this_cpu_ptr(&hvx_extbits_cached)->xa;
	}
	if ((this_cpu_ptr(&hvx_extbits_cached)->xe != xe) || (this_cpu_ptr(&hvx_extbits_cached)->xa != xa)) {
		this_cpu_ptr(&hvx_extbits_cached)->xa = xa;
		this_cpu_ptr(&hvx_extbits_cached)->xe = xe;
		__vmhwconfig(HWCONFIG_EXTBITS, 0, this_cpu_ptr(&hvx_extbits_cached)->xa, this_cpu_ptr(&hvx_extbits_cached)->xe);
	}
	preempt_enable();
}

static void smp_set_hardware(void *info)
{
	struct extbits *extbits = (struct extbits *)info;

	set_hardware(extbits->xa, extbits->xe);
}

static void drop_ctxt(struct thread_info *ti)
{
	if (ti->hvx->cnum >= 0) {
		BUG_ON(HVX_CTXT_LOCK_MASK != atomic_cmpxchg(&ctxt_status[ti->hvx->cnum].flags, HVX_CTXT_LOCK_MASK, 0));
		ti->hvx->cnum = -1;
		smp_wmb();
		up(&ctxts_avail);
	}

}

static int fault_coproc(struct thread_info *ti)
{
	if (!down_read_trylock(&enabled)) {
		goto out_ok;
	}
	atomic_inc(&coproc_notify_cnt);

	if (!ti->hvx) {
		atomic_inc(&hvx_refcount);
		ti->hvx = kmalloc(sizeof(struct hvx_threadinfo), GFP_KERNEL);
		if (!current_thread_info()->hvx)
			goto out_err;
		ti->hvx->vregs = kmem_cache_alloc(vregs_cachep, GFP_ATOMIC | __GFP_ZERO);
		if (!ti->hvx->vregs)
			goto out_free;

		ti->hvx->generation = 0;
		ti->hvx->cnum = -1;
		ti->hvx->prev_cnum = -1;
		goto out_up;
out_free:
		kfree(ti->hvx);
out_err:
		atomic_dec(&hvx_refcount);
		force_sig(SIGFPE, current);
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
		}  /* no previous thread for this context */
		else if (ctxt_status[ctxt].thread != ti) {
			ctxt_status[ti->hvx->cnum].thread = ti;
			restore_hvx_context(ti->hvx->vregs);
			ctxt_status[ctxt].generation = ti->hvx->generation;
		}  /* previous thread wasn't us */
		else if (ctxt_status[ctxt].generation != ti->hvx->generation) {
			restore_hvx_context(ti->hvx->vregs);
		}  /* this was a stale old context  */
		ti->hvx->prev_cnum = ctxt;
	}
	else {
		/* Disable XE, keep the XA */
		set_hardware(0, -1);
	}  /* returning thread doesn't use HVX */
	return NOTIFY_OK;
}

static void thread_event_release(struct thread_info *ti)
{
	if (ti->hvx) {
		/*  If we were holding a semaphore, go ahead and let that go  */
		if ((ti->hvx->cnum >= 0) && (ctxt_status[ti->hvx->cnum].thread == ti)) {
			ctxt_status[ti->hvx->cnum].thread = NULL;
			drop_ctxt(ti);
		}
		/*  Todo:  separate workqueue to free these in case another process is trying to restore them  */
		kmem_cache_free(vregs_cachep, ti->hvx->vregs);
		kfree(ti->hvx);
		ti->hvx = NULL;
		atomic_dec(&hvx_refcount);
	}
}

static int atomic_notify(struct notifier_block *nb, unsigned long action, void *arg)
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
					ctxt_status[ti->hvx->cnum].generation = ++ti->hvx->generation;
					drop_ctxt(ti);
				}
			}
			break;
		}
	case THREAD_EVENT_RELEASE:
		{
			struct thread_info *ti = arg;
			thread_event_release(ti);
			break;
		}
	case THREAD_EVENT_SWITCH:
		/*  arg here is task_struct *next, not thread info */
		if (current_thread_info()->hvx) {
			drop_ctxt(current_thread_info());
		}
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

/*  interrupts are on  */
static int blocking_notify(struct notifier_block *nb, unsigned long action, void *arg)
{
	struct thread_info *ti = arg;
	int i;
	int ret;

	switch (action) {
	case THREAD_EVENT_DOWORK:
		if (ti->hvx && (ti->hvx->cnum == -1)) {
			ret = down_killable(&ctxts_avail);

			if (ret != 0) {
				force_sig(SIGFPE, current);
				return NOTIFY_OK;
			}  /*  didn't actually acquire a context  */

			if (ti->hvx->prev_cnum >= 0) {
				if (!atomic_cmpxchg(&ctxt_status[ti->hvx->prev_cnum].flags, 0, HVX_CTXT_LOCK_MASK)) {
					ti->hvx->cnum = ti->hvx->prev_cnum;
					goto ctxt_found;
				}
			}
			for (i = 0; i < max_ctxts; i++) {
				if (!atomic_cmpxchg(&ctxt_status[i].flags, 0, HVX_CTXT_LOCK_MASK)) {
					ti->hvx->cnum = i;
					break;
				}
			}
			BUG_ON(i==max_ctxts);
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

	ctxt_status = devm_kzalloc(&pdev->dev, sizeof(struct hvx_ctxt_status)*max_ctxts, GFP_KERNEL);
	if (!ctxt_status) {
		ret = -ENOMEM;
		goto out;
	}

	vregs_cachep = kmem_cache_create("hvx_vregs", sizeof(struct HVX_Vectors), sizeof(HVX_Vector), 0, NULL);

	if (!vregs_cachep) {
		ret = -ENOMEM;
		goto out;
	}

	if (ret) {
		goto out_free_vregs;
	}

	__vmhwconfig(HWCONFIG_VLENGTH, 0, vlength, 0x0);

	for_each_possible_cpu(i) {
		per_cpu(hvx_extbits_cached, i).xa = 0;
		per_cpu(hvx_extbits_cached, i).xe = 1;
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

out_free_vregs:
	kmem_cache_destroy(vregs_cachep);

out:
	return ret;
}

static int hvx_remove(struct platform_device *pdev)
{
	int locked = 0;
	struct extbits ext;
	int i;
	int ret;

	if (!down_write_trylock(&enabled)) {
		ret = -EBUSY;
		goto out;
	}

	for (i=0; i < max_ctxts; i++) {
		if (down_trylock(&ctxts_avail)) {
			break;
		}  /*  failed to acquire a lock  */
		locked++;
	}
	if ((locked < max_ctxts) || (atomic_read(&hvx_refcount) > 0)) {
		for (i=0; i<locked; i++) {
			up(&ctxts_avail);
		}
		ret = -EBUSY;
		goto out_upwrite;
	}

	/*
	 * With refcount == 0, nobody should have actively been using HVX at that point.
	 * fault_coproc would be the only point where a thread could add the hvx pointer,
	 * and that should be blocked by &enabled (and it's supposed to be non-blocking).
	 */
	blocking_thread_unregister_notify(&blocking_nb);
	atomic_thread_unregister_notify(&atomic_nb);

	ext.xa = -1;
	ext.xe = 0;
	smp_wmb();
	on_each_cpu(smp_set_hardware, &ext, 1);

	kmem_cache_destroy(vregs_cachep);
	ret = 0;
	goto out;

out_upwrite:
	up_write(&enabled);
out:
	return ret;

}

/* driver Stuff */
static const struct of_device_id hvx_device_id[] = {
	{
		.compatible = "qcom,hexagon-hvx",
	},
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

static ssize_t refcount_show(struct class *class, struct class_attribute *attr,
                            char *buf)
{
        return sprintf(buf, "%d\n", atomic_read(&hvx_refcount));
}

static ssize_t max_ctxts_show(struct class *class, struct class_attribute *attr,
                            char *buf)
{
        return sprintf(buf, "%d\n", max_ctxts);
}

static ssize_t vlength_show(struct class *class, struct class_attribute *attr,
                            char *buf)
{
        return sprintf(buf, "%d\n", vlength);
}


static ssize_t release_store(struct class *class,
			   struct class_attribute *attr,
                                        const char *buf,
                                        size_t count)
{
	thread_event_release(current_thread_info());
        return count;
}

static ssize_t debug_store(struct class *class,
			   struct class_attribute *attr,
                                        const char *buf,
                                        size_t count)
{
        if (sscanf(buf, "%lu", &hvx_debug) == 1) {
                return count;
        }

        return -EINVAL;
}

static ssize_t debug_show(struct class *class, struct class_attribute *attr,
                            char *buf)
{
        return sprintf(buf, "%lu\n", hvx_debug);
}

static struct class_attribute hvx_class_attrs[] = {
	__ATTR_RW(debug),
	/*  This can/should actually be world writable, but that is against the rules.  */
	__ATTR_WO(release),
	__ATTR_RO(max_ctxts),
	__ATTR_RO(vlength),
	__ATTR_RO(refcount),
        __ATTR_NULL,
};

static struct class hvx_class = {
        .name = "hvx",
        .dev_groups = hvx_groups,
        .class_attrs = hvx_class_attrs
};

static int __init hvx_driver_init(void)
{
	int ret;

	ret = class_register(&hvx_class);
	platform_driver_register(&hvx_driver);
}
module_init(hvx_driver_init);

static void __exit hvx_driver_exit(void)
{
	class_unregister(&hvx_class);
	platform_driver_unregister(&hvx_driver);
}
module_exit(hvx_driver_exit);

MODULE_DESCRIPTION("Hexagon HVX Module");
MODULE_LICENSE("GPL v2");
