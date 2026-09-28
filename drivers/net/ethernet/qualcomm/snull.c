// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ethernet over shared memory between a Qualcomm SoC's compute DSP and its
 * application processor.
 *
 * Each side owns one 64 KiB window of shared memory that the other side
 * reads, and rings the other side's IPC doorbell after changing it.  A
 * window holds a single frame, a length word followed by the data, and two
 * flag words at its end.  RX_READY, set by the sender in the receiver's
 * window, says a frame is waiting; TX_DONE, set by the receiver in the
 * sender's window, says the frame was taken.  As there is only one frame
 * buffer, the transmit queue is stopped from each transmit until TX_DONE.
 *
 * The protocol is the one of the "snull" module that ran on the RubikPi 3.
 */

#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>

#define SNULL_WINDOW_SIZE	SZ_64K
#define SNULL_DATALEN		0x0
#define SNULL_DATA		0x4
#define SNULL_MAC		(SNULL_WINDOW_SIZE - 16)
#define SNULL_RX_READY_FLAGS	(SNULL_WINDOW_SIZE - 8)
#define SNULL_TX_DONE_FLAGS	(SNULL_WINDOW_SIZE - 4)

#define SNULL_RX_READY		BIT(0)
#define SNULL_TX_DONE		BIT(1)

#define SNULL_MAX_FRAME		(SNULL_MAC - SNULL_DATA)
#define SNULL_TX_TIMEOUT	(5 * HZ)

struct snull_priv {
	struct net_device *ndev;
	/* Written by the peer, read by us */
	void __iomem *rx;
	/* Written by us, read by the peer */
	void __iomem *tx;
	void __iomem *ipc;
	u32 ipc_offset;
	u32 ipc_bits;
	int irq;
	/* Protects the frame in flight and the flag words we set */
	spinlock_t lock;
	struct sk_buff *tx_skb;
};

static void snull_ring_doorbell(struct snull_priv *priv)
{
	/* The peer must see our changes to the window before the doorbell. */
	wmb();
	iowrite32(priv->ipc_bits, priv->ipc + priv->ipc_offset);
}

static void snull_rx(struct snull_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	struct sk_buff *skb;
	u32 len;

	len = ioread32(priv->rx + SNULL_DATALEN);
	if (len < ETH_HLEN || len > SNULL_MAX_FRAME) {
		dev_core_stats_rx_dropped_inc(ndev);
		goto done;
	}

	skb = netdev_alloc_skb_ip_align(ndev, len);
	if (!skb) {
		dev_core_stats_rx_dropped_inc(ndev);
		goto done;
	}

	memcpy_fromio(skb_put(skb, len), priv->rx + SNULL_DATA, len);
	skb->protocol = eth_type_trans(skb, ndev);
	dev_sw_netstats_rx_add(ndev, len);
	netif_rx(skb);

done:
	iowrite32(SNULL_TX_DONE, priv->tx + SNULL_TX_DONE_FLAGS);
	snull_ring_doorbell(priv);
}

static void snull_tx_done(struct snull_priv *priv)
{
	struct sk_buff *skb = priv->tx_skb;

	if (!skb)
		return;

	priv->tx_skb = NULL;
	dev_sw_netstats_tx_add(priv->ndev, 1, skb->len);
	dev_consume_skb_irq(skb);
	netif_wake_queue(priv->ndev);
}

static bool snull_service(struct snull_priv *priv)
{
	bool handled = false;

	spin_lock(&priv->lock);

	if (ioread32(priv->rx + SNULL_RX_READY_FLAGS) & SNULL_RX_READY) {
		iowrite32(0, priv->rx + SNULL_RX_READY_FLAGS);
		snull_rx(priv);
		handled = true;
	}

	if (ioread32(priv->rx + SNULL_TX_DONE_FLAGS) & SNULL_TX_DONE) {
		iowrite32(0, priv->rx + SNULL_TX_DONE_FLAGS);
		snull_tx_done(priv);
		handled = true;
	}

	spin_unlock(&priv->lock);

	return handled;
}

static irqreturn_t snull_interrupt(int irq, void *dev_id)
{
	return snull_service(dev_id) ? IRQ_HANDLED : IRQ_NONE;
}

static int snull_open(struct net_device *ndev)
{
	struct snull_priv *priv = netdev_priv(ndev);
	int ret;

	ret = request_irq(priv->irq, snull_interrupt, 0, ndev->name, priv);
	if (ret)
		return ret;

	netif_start_queue(ndev);

	/* The peer may have signalled before we were listening. */
	snull_service(priv);

	return 0;
}

static int snull_stop(struct net_device *ndev)
{
	struct snull_priv *priv = netdev_priv(ndev);
	unsigned long flags;

	netif_stop_queue(ndev);
	free_irq(priv->irq, priv);

	spin_lock_irqsave(&priv->lock, flags);
	dev_kfree_skb_any(priv->tx_skb);
	priv->tx_skb = NULL;
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static netdev_tx_t snull_start_xmit(struct sk_buff *skb,
				    struct net_device *ndev)
{
	struct snull_priv *priv = netdev_priv(ndev);
	unsigned long flags;
	u32 len;

	if (skb_put_padto(skb, ETH_ZLEN))
		return NETDEV_TX_OK;

	len = skb->len;
	if (len > SNULL_MAX_FRAME) {
		dev_kfree_skb_any(skb);
		dev_core_stats_tx_dropped_inc(ndev);
		return NETDEV_TX_OK;
	}

	spin_lock_irqsave(&priv->lock, flags);

	netif_stop_queue(ndev);
	priv->tx_skb = skb;

	memcpy_toio(priv->tx + SNULL_DATA, skb->data, len);
	iowrite32(len, priv->tx + SNULL_DATALEN);
	/* The frame must be complete before the peer is told it is there. */
	wmb();
	iowrite32(ioread32(priv->tx + SNULL_RX_READY_FLAGS) | SNULL_RX_READY,
		  priv->tx + SNULL_RX_READY_FLAGS);
	snull_ring_doorbell(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	return NETDEV_TX_OK;
}

static void snull_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct snull_priv *priv = netdev_priv(ndev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->tx_skb) {
		dev_kfree_skb_any(priv->tx_skb);
		priv->tx_skb = NULL;
		dev_core_stats_tx_dropped_inc(ndev);
	}
	netif_wake_queue(ndev);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static const struct net_device_ops snull_netdev_ops = {
	.ndo_open		= snull_open,
	.ndo_stop		= snull_stop,
	.ndo_start_xmit		= snull_start_xmit,
	.ndo_tx_timeout		= snull_tx_timeout,
	.ndo_get_stats64	= dev_get_tstats64,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void snull_setup_mac(struct snull_priv *priv)
{
	u8 mac[ETH_ALEN];

	/* The peer may leave a MAC address for us at the end of our window. */
	memcpy_fromio(mac, priv->rx + SNULL_MAC, ETH_ALEN);
	if (is_valid_ether_addr(mac))
		eth_hw_addr_set(priv->ndev, mac);
	else
		eth_hw_addr_random(priv->ndev);
}

static int snull_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct net_device *ndev;
	struct snull_priv *priv;
	int ret;

	ndev = devm_alloc_etherdev(dev, sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, dev);
	strscpy(ndev->name, "sn%d", sizeof(ndev->name));
	ndev->netdev_ops = &snull_netdev_ops;
	ndev->watchdog_timeo = SNULL_TX_TIMEOUT;
	ndev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;

	priv = netdev_priv(ndev);
	priv->ndev = ndev;
	spin_lock_init(&priv->lock);

	priv->rx = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->rx))
		return PTR_ERR(priv->rx);

	priv->tx = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(priv->tx))
		return PTR_ERR(priv->tx);

	priv->ipc = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(priv->ipc))
		return PTR_ERR(priv->ipc);

	ret = of_property_read_u32(dev->of_node, "qcom,snull-irq-offset",
				   &priv->ipc_offset);
	if (ret)
		return dev_err_probe(dev, ret, "no qcom,snull-irq-offset\n");

	ret = of_property_read_u32(dev->of_node, "qcom,snull-irq-bitset",
				   &priv->ipc_bits);
	if (ret)
		return dev_err_probe(dev, ret, "no qcom,snull-irq-bitset\n");

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		return priv->irq;

	/* One side clears the windows before the other looks at them. */
	if (of_property_read_bool(dev->of_node, "qcom,snull-zero-mem")) {
		memset_io(priv->rx, 0, SNULL_WINDOW_SIZE);
		memset_io(priv->tx, 0, SNULL_WINDOW_SIZE);
	}

	snull_setup_mac(priv);

	return devm_register_netdev(dev, ndev);
}

static const struct of_device_id snull_of_match[] = {
	{ .compatible = "qcom,snull-eth" },
	{ }
};
MODULE_DEVICE_TABLE(of, snull_of_match);

static struct platform_driver snull_driver = {
	.probe = snull_probe,
	.driver = {
		.name = "snull",
		.of_match_table = snull_of_match,
	},
};
module_platform_driver(snull_driver);

MODULE_DESCRIPTION("Ethernet over shared memory between a Qualcomm DSP and its host");
MODULE_LICENSE("GPL");
