// SPDX-License-Identifier: GPL-2.0-only
/*
 * rpmsg echo service - echoes back any received messages
 *
 * Used for testing GLINK/rpmsg communication between processors.
 * When probed as the "host" side (has a DT node), sends an initial
 * message periodically until an echo response is received from the
 * remote.  The "remote" side simply echoes back any message.
 *
 * The initial send is deferred to a work queue because probe() runs
 * in the GLINK rx_work context — calling rpmsg_send() there would
 * deadlock.
 *
 * With intentless GLINK, early sends may succeed (data written to FIFO)
 * but get dropped by the remote before the channel is established.
 * The retry loop handles this by re-sending until an echo arrives.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/workqueue.h>

#define ECHO_MSG	"glink-echo-test"

struct echo_priv {
	struct rpmsg_device *rpdev;
	struct delayed_work send_work;
	int retries;
	bool got_echo;
};

static void echo_send_initial(struct work_struct *work)
{
	struct echo_priv *priv = container_of(to_delayed_work(work),
					      struct echo_priv, send_work);
	struct rpmsg_device *rpdev = priv->rpdev;
	int ret;

	if (priv->got_echo)
		return;

	ret = rpmsg_trysend(rpdev->ept, ECHO_MSG, strlen(ECHO_MSG));
	if (ret && ret != -EBUSY && ret != -ENXIO && ret != -EAGAIN)
		dev_err(&rpdev->dev, "rpmsg_trysend failed: %d\n", ret);

	priv->retries++;
	if (priv->retries <= 5 || (priv->retries % 30) == 0)
		dev_info(&rpdev->dev, "send attempt %d (ret=%d)\n",
			 priv->retries, ret);

	if (priv->retries < 600)
		schedule_delayed_work(&priv->send_work, 2 * HZ);
	else
		dev_err(&rpdev->dev, "giving up after %d attempts\n",
			priv->retries);
}

static int rpmsg_echo_cb(struct rpmsg_device *rpdev, void *data, int len,
			  void *priv_data, u32 src)
{
	struct echo_priv *priv = dev_get_drvdata(&rpdev->dev);
	int ret;

	dev_info(&rpdev->dev, "received %d bytes (src: 0x%x)\n", len, src);

	/*
	 * The initiator side only validates the response; echoing it back
	 * would ping-pong with the remote echoer forever.
	 */
	if (priv) {
		if (!priv->got_echo) {
			priv->got_echo = true;
			cancel_delayed_work(&priv->send_work);
			dev_info(&rpdev->dev,
				 "ECHO SUCCESS after %d attempts\n",
				 priv->retries);
		}
		return 0;
	}

	/* Use trysend — this callback runs under the GLINK recv_lock */
	ret = rpmsg_trysend(rpdev->ept, data, len);
	if (ret)
		dev_err(&rpdev->dev, "echo rpmsg_trysend failed: %d\n", ret);
	else
		dev_info(&rpdev->dev, "echoed %d bytes\n", len);

	return 0;
}

static int rpmsg_echo_probe(struct rpmsg_device *rpdev)
{
	struct echo_priv *priv;

	dev_info(&rpdev->dev, "glink-echo channel opened: 0x%x -> 0x%x\n",
		 rpdev->src, rpdev->dst);

	/*
	 * The host side has a DT node (from qcom,glink-channels).
	 * Send the first message to kick off the echo exchange.
	 */
	if (rpdev->dev.of_node) {
		priv = devm_kzalloc(&rpdev->dev, sizeof(*priv), GFP_KERNEL);
		if (!priv)
			return -ENOMEM;

		priv->rpdev = rpdev;
		INIT_DELAYED_WORK(&priv->send_work, echo_send_initial);
		dev_set_drvdata(&rpdev->dev, priv);
		schedule_delayed_work(&priv->send_work, 2 * HZ);
	}

	return 0;
}

static void rpmsg_echo_remove(struct rpmsg_device *rpdev)
{
	struct echo_priv *priv = dev_get_drvdata(&rpdev->dev);

	if (priv)
		cancel_delayed_work_sync(&priv->send_work);
	dev_info(&rpdev->dev, "glink-echo channel closed\n");
}

static const struct rpmsg_device_id rpmsg_echo_id_table[] = {
	{ .name = "glink-echo" },
	{}
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_echo_id_table);

static struct rpmsg_driver rpmsg_echo_driver = {
	.drv.name = "rpmsg_echo",
	.id_table = rpmsg_echo_id_table,
	.probe = rpmsg_echo_probe,
	.callback = rpmsg_echo_cb,
	.remove = rpmsg_echo_remove,
};
module_rpmsg_driver(rpmsg_echo_driver);

MODULE_DESCRIPTION("rpmsg echo service for GLINK testing");
MODULE_LICENSE("GPL v2");
