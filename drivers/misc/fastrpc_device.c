/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * FastRPC device-side handler.  Receives invoke requests over GLINK/rpmsg
 * and exposes them to userspace via /dev/fastrpc_device for handling.
 */

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/rpmsg.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/uaccess.h>

#define FASTRPC_INIT_HANDLE		1
#define FASTRPC_DSP_UTILITIES_HANDLE	2

#define FASTRPC_RMID_INIT_ATTACH	0
#define FASTRPC_RMID_INIT_RELEASE	1

#define REMOTE_SCALARS_METHOD(sc)	(((sc) >> 24) & 0x1f)
#define REMOTE_SCALARS_INBUFS(sc)	(((sc) >> 16) & 0xff)
#define REMOTE_SCALARS_OUTBUFS(sc)	(((sc) >> 8) & 0xff)

/* Wire format: invoke request from AP */
struct fastrpc_msg {
	int client_id;
	int tid;
	u64 ctx;
	u32 handle;
	u32 sc;
	u64 addr;
	u64 size;
};

/* Wire format: invoke response to AP */
struct fastrpc_invoke_rsp {
	u64 ctx;
	int retval;
};

/* Queued request exposed to userspace */
struct fastrpc_pending_req {
	struct list_head list;
	u64 ctx;
	u32 handle;
	u32 sc;
	u64 addr;
	u64 size;
};

/* Userspace reads this structure */
struct fastrpc_user_req {
	u64 ctx;
	u32 handle;
	u32 sc;
	u64 addr;
	u64 size;
};

/* Userspace writes this structure */
struct fastrpc_user_rsp {
	u64 ctx;
	int retval;
};

struct fastrpc_device_priv {
	struct rpmsg_device *rpdev;
	struct miscdevice mdev;
	struct list_head pending;
	struct mutex lock;
	wait_queue_head_t waitq;
};

static struct fastrpc_device_priv *g_priv;

static int fastrpc_send_response(struct fastrpc_device_priv *priv,
				 u64 ctx, int retval)
{
	struct fastrpc_invoke_rsp rsp = {
		.ctx = ctx,
		.retval = retval,
	};

	return rpmsg_trysend(priv->rpdev->ept, &rsp, sizeof(rsp));
}

static int fastrpc_device_cb(struct rpmsg_device *rpdev, void *data, int len,
			     void *priv_data, u32 src)
{
	struct fastrpc_device_priv *priv = dev_get_drvdata(&rpdev->dev);
	struct fastrpc_msg *msg = data;
	struct fastrpc_pending_req *req;
	u32 method;

	if (len < sizeof(*msg)) {
		dev_err(&rpdev->dev, "fastrpc: short message (%d bytes)\n", len);
		return -EINVAL;
	}

	/* System methods handled internally */
	if (msg->handle == FASTRPC_INIT_HANDLE) {
		method = REMOTE_SCALARS_METHOD(msg->sc);
		switch (method) {
		case FASTRPC_RMID_INIT_ATTACH:
			dev_info(&rpdev->dev, "fastrpc: INIT_ATTACH ctx=%llx\n",
				 msg->ctx);
			fastrpc_send_response(priv, msg->ctx, 0);
			return 0;
		case FASTRPC_RMID_INIT_RELEASE:
			dev_info(&rpdev->dev, "fastrpc: INIT_RELEASE ctx=%llx\n",
				 msg->ctx);
			fastrpc_send_response(priv, msg->ctx, 0);
			return 0;
		default:
			dev_info(&rpdev->dev,
				 "fastrpc: unsupported init method %u\n", method);
			fastrpc_send_response(priv, msg->ctx, -ENOSYS);
			return 0;
		}
	}

	/* Queue application-level invocations for userspace */
	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		fastrpc_send_response(priv, msg->ctx, -ENOMEM);
		return -ENOMEM;
	}

	req->ctx = msg->ctx;
	req->handle = msg->handle;
	req->sc = msg->sc;
	req->addr = msg->addr;
	req->size = msg->size;

	mutex_lock(&priv->lock);
	list_add_tail(&req->list, &priv->pending);
	mutex_unlock(&priv->lock);

	wake_up_interruptible(&priv->waitq);
	return 0;
}

static ssize_t fastrpc_dev_read(struct file *filp, char __user *buf,
				size_t count, loff_t *ppos)
{
	struct fastrpc_device_priv *priv = g_priv;
	struct fastrpc_pending_req *req;
	struct fastrpc_user_req ureq;
	int ret;

	if (!priv)
		return -ENODEV;

	if (count < sizeof(ureq))
		return -EINVAL;

	ret = wait_event_interruptible(priv->waitq,
		!list_empty(&priv->pending));
	if (ret)
		return ret;

	mutex_lock(&priv->lock);
	req = list_first_entry_or_null(&priv->pending,
				       struct fastrpc_pending_req, list);
	if (!req) {
		mutex_unlock(&priv->lock);
		return -EAGAIN;
	}
	list_del(&req->list);
	mutex_unlock(&priv->lock);

	ureq.ctx = req->ctx;
	ureq.handle = req->handle;
	ureq.sc = req->sc;
	ureq.addr = req->addr;
	ureq.size = req->size;
	kfree(req);

	if (copy_to_user(buf, &ureq, sizeof(ureq)))
		return -EFAULT;

	return sizeof(ureq);
}

static ssize_t fastrpc_dev_write(struct file *filp, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	struct fastrpc_device_priv *priv = g_priv;
	struct fastrpc_user_rsp ursp;

	if (!priv)
		return -ENODEV;

	if (count < sizeof(ursp))
		return -EINVAL;

	if (copy_from_user(&ursp, buf, sizeof(ursp)))
		return -EFAULT;

	fastrpc_send_response(priv, ursp.ctx, ursp.retval);
	return sizeof(ursp);
}

static __poll_t fastrpc_dev_poll(struct file *filp, poll_table *wait)
{
	struct fastrpc_device_priv *priv = g_priv;
	__poll_t mask = 0;

	if (!priv)
		return EPOLLERR;

	poll_wait(filp, &priv->waitq, wait);

	mutex_lock(&priv->lock);
	if (!list_empty(&priv->pending))
		mask |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&priv->lock);

	/* Always writable (responses can always be sent) */
	mask |= EPOLLOUT | EPOLLWRNORM;
	return mask;
}

static const struct file_operations fastrpc_dev_fops = {
	.owner = THIS_MODULE,
	.read = fastrpc_dev_read,
	.write = fastrpc_dev_write,
	.poll = fastrpc_dev_poll,
};

static int fastrpc_device_probe(struct rpmsg_device *rpdev)
{
	struct fastrpc_device_priv *priv;
	int ret;

	priv = devm_kzalloc(&rpdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rpdev = rpdev;
	INIT_LIST_HEAD(&priv->pending);
	mutex_init(&priv->lock);
	init_waitqueue_head(&priv->waitq);

	priv->mdev.minor = MISC_DYNAMIC_MINOR;
	priv->mdev.name = "fastrpc_device";
	priv->mdev.fops = &fastrpc_dev_fops;

	ret = misc_register(&priv->mdev);
	if (ret) {
		dev_err(&rpdev->dev, "fastrpc_device: misc_register failed: %d\n",
			ret);
		return ret;
	}

	dev_set_drvdata(&rpdev->dev, priv);
	g_priv = priv;

	dev_info(&rpdev->dev, "fastrpc_device: registered /dev/fastrpc_device\n");
	return 0;
}

static void fastrpc_device_remove(struct rpmsg_device *rpdev)
{
	struct fastrpc_device_priv *priv = dev_get_drvdata(&rpdev->dev);
	struct fastrpc_pending_req *req, *tmp;

	g_priv = NULL;
	misc_deregister(&priv->mdev);

	list_for_each_entry_safe(req, tmp, &priv->pending, list) {
		list_del(&req->list);
		kfree(req);
	}
}

static const struct rpmsg_device_id fastrpc_device_id_table[] = {
	{ .name = "fastrpc-cdsp-smd" },
	{}
};
MODULE_DEVICE_TABLE(rpmsg, fastrpc_device_id_table);

static struct rpmsg_driver fastrpc_device_driver = {
	.drv.name = "fastrpc_device",
	.id_table = fastrpc_device_id_table,
	.probe = fastrpc_device_probe,
	.callback = fastrpc_device_cb,
	.remove = fastrpc_device_remove,
};
module_rpmsg_driver(fastrpc_device_driver);

MODULE_DESCRIPTION("FastRPC device-side handler over GLINK/rpmsg");
MODULE_LICENSE("GPL");
