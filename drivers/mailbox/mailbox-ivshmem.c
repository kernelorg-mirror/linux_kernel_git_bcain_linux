// SPDX-License-Identifier: GPL-2.0-only
/*
 * ivshmem doorbell mailbox controller
 *
 * A mailbox controller for the ivshmem doorbell registers, as
 * implemented by QEMU's ivshmem-flat device (see
 * docs/specs/ivshmem-spec.rst in the QEMU tree).  Used to signal
 * between VMs sharing memory through an ivshmem-server.
 *
 * MMIO register layout (16 bytes, revision 1 semantics):
 *   0x00: INTRMASK   - reserved, reads 0
 *   0x04: INTRSTATUS - reserved, reads 0
 *   0x08: IVPOSITION (read-only) - this VM's peer id
 *   0x0C: DOORBELL (write-only)  - write (peer_id << 16 | vector) to signal
 *
 * There is no latched interrupt status: the doorbell interrupt is a
 * pulse, so the interrupt must be configured as edge-triggered and any
 * firing is treated as a notification.
 */

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mailbox_controller.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define IVSHMEM_IVPOSITION_REG	0x08
#define IVSHMEM_DOORBELL_REG	0x0C

struct ivshmem_mbox {
	void __iomem *base;
	u32 peer_id;
	u32 signal_vector;
	int irq;
	struct mbox_chan chan;
	struct mbox_controller mbox;
};

static irqreturn_t ivshmem_mbox_irq(int irq, void *data)
{
	struct ivshmem_mbox *m = data;

	/*
	 * Doorbells rung by the peer before a client bound this channel
	 * (the peer VM may boot much earlier) have nobody to deliver to.
	 */
	if (!READ_ONCE(m->chan.cl))
		return IRQ_HANDLED;

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
	return 0;
}

static void ivshmem_mbox_shutdown(struct mbox_chan *chan)
{
	struct ivshmem_mbox *m = container_of(chan, struct ivshmem_mbox, chan);

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

	/*
	 * In a two-peer ivshmem setup the peer to signal is the other
	 * id; allow the device tree to override for larger topologies.
	 */
	ret = of_property_read_u32(dev->of_node, "peer-id", &m->peer_id);
	if (ret)
		m->peer_id = readl(m->base + IVSHMEM_IVPOSITION_REG) ^ 1;

	ret = of_property_read_u32(dev->of_node, "signal-vector",
				   &m->signal_vector);
	if (ret)
		m->signal_vector = 0;

	m->irq = irq;

	/* IRQ starts disabled; enabled when a client binds via startup() */
	ret = devm_request_irq(dev, irq, ivshmem_mbox_irq, IRQF_NO_AUTOEN,
			       dev_name(dev), m);
	if (ret)
		return dev_err_probe(dev, ret, "failed to request IRQ\n");

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
