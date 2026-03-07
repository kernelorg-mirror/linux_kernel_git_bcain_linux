// SPDX-License-Identifier: GPL-2.0-only
/*
 * ivshmem doorbell mailbox controller
 *
 * A simple mailbox controller for ivshmem-style doorbell registers.
 * Used to signal between QEMU instances sharing memory via a socket-based
 * doorbell mechanism.
 *
 * MMIO register layout (16 bytes):
 *   0x00: READY    (read-only)  - 1 when peer connected
 *   0x04: STATUS   (read-clear) - 1 when doorbell pending
 *   0x08: reserved
 *   0x0C: DOORBELL (write-only) - write (peer_id << 16 | vector) to signal
 *
 * Under QEMU TCG, L2VIC interrupt delivery can be unreliable (the single-
 * active-VID design means a stuck timer interrupt blocks all other IRQs).
 * To work around this, we poll the STATUS register from a timer in addition
 * to using the hardware IRQ.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/timer.h>

#define IVSHMEM_STATUS_REG	0x04
#define IVSHMEM_DOORBELL_REG	0x0C

/* Poll interval in jiffies (10ms at HZ=100) */
#define IVSHMEM_POLL_INTERVAL	(HZ / 100 ? : 1)

struct ivshmem_mbox {
	void __iomem *base;
	u32 peer_id;
	u32 signal_vector;
	int irq;
	struct mbox_chan chan;
	struct mbox_controller mbox;
	struct timer_list poll_timer;
	bool polling_active;
};

static void ivshmem_mbox_poll(struct timer_list *t)
{
	struct ivshmem_mbox *m = timer_container_of(m, t, poll_timer);
	u32 status;

	status = readl(m->base + IVSHMEM_STATUS_REG);
	if (status)
		mbox_chan_received_data(&m->chan, NULL);

	if (m->polling_active)
		mod_timer(&m->poll_timer, jiffies + IVSHMEM_POLL_INTERVAL);
}

static irqreturn_t ivshmem_mbox_irq(int irq, void *data)
{
	struct ivshmem_mbox *m = data;
	u32 status;

	/* Read STATUS register to acknowledge and lower the IRQ */
	status = readl(m->base + IVSHMEM_STATUS_REG);
	if (!status)
		return IRQ_NONE;

	mbox_chan_received_data(&m->chan, NULL);
	return IRQ_HANDLED;
}

static int ivshmem_mbox_send_data(struct mbox_chan *chan, void *data)
{
	struct ivshmem_mbox *m = container_of(chan, struct ivshmem_mbox, chan);
	u32 val = (m->peer_id << 16) | m->signal_vector;

	writel(val, m->base + IVSHMEM_DOORBELL_REG);
	return 0;
}

static int ivshmem_mbox_startup(struct mbox_chan *chan)
{
	struct ivshmem_mbox *m = container_of(chan, struct ivshmem_mbox, chan);

	enable_irq(m->irq);

	/* Start polling as backup for IRQ delivery */
	m->polling_active = true;
	mod_timer(&m->poll_timer, jiffies + IVSHMEM_POLL_INTERVAL);

	return 0;
}

static void ivshmem_mbox_shutdown(struct mbox_chan *chan)
{
	struct ivshmem_mbox *m = container_of(chan, struct ivshmem_mbox, chan);

	m->polling_active = false;
	timer_delete_sync(&m->poll_timer);
	disable_irq(m->irq);
}

static bool ivshmem_mbox_last_tx_done(struct mbox_chan *chan)
{
	return true;
}

static const struct mbox_chan_ops ivshmem_mbox_ops = {
	.send_data = ivshmem_mbox_send_data,
	.startup = ivshmem_mbox_startup,
	.shutdown = ivshmem_mbox_shutdown,
	.last_tx_done = ivshmem_mbox_last_tx_done,
};

static struct mbox_chan *ivshmem_mbox_of_xlate(struct mbox_controller *mbox,
					       const struct of_phandle_args *sp)
{
	/* Single channel controller, #mbox-cells = <0> */
	return &mbox->chans[0];
}

static int ivshmem_mbox_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ivshmem_mbox *m;
	int irq, ret;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;

	m->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(m->base))
		return PTR_ERR(m->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = of_property_read_u32(dev->of_node, "peer-id", &m->peer_id);
	if (ret)
		return dev_err_probe(dev, ret, "missing peer-id property\n");

	ret = of_property_read_u32(dev->of_node, "signal-vector",
				   &m->signal_vector);
	if (ret)
		return dev_err_probe(dev, ret,
				     "missing signal-vector property\n");

	m->irq = irq;

	ret = devm_request_irq(dev, irq, ivshmem_mbox_irq, 0,
			       dev_name(dev), m);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

	/* IRQ starts disabled; enabled when a client binds via startup() */
	disable_irq(irq);

	timer_setup(&m->poll_timer, ivshmem_mbox_poll, 0);

	m->mbox.dev = dev;
	m->mbox.chans = &m->chan;
	m->mbox.num_chans = 1;
	m->mbox.ops = &ivshmem_mbox_ops;
	m->mbox.of_xlate = ivshmem_mbox_of_xlate;
	m->mbox.txdone_irq = false;
	m->mbox.txdone_poll = true;
	m->mbox.txpoll_period = 1;

	platform_set_drvdata(pdev, m);

	ret = devm_mbox_controller_register(dev, &m->mbox);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register mailbox\n");

	dev_info(dev, "ivshmem doorbell mailbox registered (peer=%u vec=%u)\n",
		 m->peer_id, m->signal_vector);
	return 0;
}

static const struct of_device_id ivshmem_mbox_of_match[] = {
	{ .compatible = "ivshmem-doorbell" },
	{}
};
MODULE_DEVICE_TABLE(of, ivshmem_mbox_of_match);

static struct platform_driver ivshmem_mbox_driver = {
	.probe = ivshmem_mbox_probe,
	.driver = {
		.name = "ivshmem-mbox",
		.of_match_table = ivshmem_mbox_of_match,
	},
};
module_platform_driver(ivshmem_mbox_driver);

MODULE_DESCRIPTION("ivshmem doorbell mailbox controller");
MODULE_LICENSE("GPL v2");
