// SPDX-License-Identifier: GPL-2.0
/*
 * IP networking over an RPMSG channel
 *
 * Carries raw IP packets, one per message, over the "IP_BRIDGE" channel of an
 * RPMSG link such as GLINK, and presents them as a point-to-point interface
 * to the processor on the other end, typically a DSP.
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rpmsg.h>

#define RPMSG_NET_MTU		8192
#define RPMSG_NET_CHANNEL	"IP_BRIDGE"

struct rpmsg_net_priv {
	struct rpmsg_device *rpdev;
	struct net_device *netdev;
};

static netdev_tx_t rpmsg_net_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rpmsg_net_priv *priv = netdev_priv(dev);
	unsigned int len = skb->len;

	/*
	 * The FIFO is transiently full: drop the packet and let the transport
	 * protocol recover.  RPMSG gives no notification to wake a stopped
	 * queue with, so stopping it would hang the interface.
	 */
	if (rpmsg_trysend(priv->rpdev->ept, skb->data, len)) {
		dev_core_stats_tx_dropped_inc(dev);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	dev_sw_netstats_tx_add(dev, 1, len);
	dev_consume_skb_any(skb);

	return NETDEV_TX_OK;
}

static int rpmsg_net_open(struct net_device *dev)
{
	netif_start_queue(dev);

	return 0;
}

static int rpmsg_net_stop(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

static const struct net_device_ops rpmsg_net_ops = {
	.ndo_open = rpmsg_net_open,
	.ndo_stop = rpmsg_net_stop,
	.ndo_start_xmit = rpmsg_net_xmit,
	.ndo_get_stats64 = dev_get_tstats64,
};

static void rpmsg_net_setup(struct net_device *dev)
{
	dev->netdev_ops = &rpmsg_net_ops;
	dev->type = ARPHRD_RAWIP;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = RPMSG_NET_MTU;
	dev->mtu = RPMSG_NET_MTU;
	dev->addr_len = 0;
	dev->tx_queue_len = 100;
	dev->pcpu_stat_type = NETDEV_PCPU_STAT_TSTATS;
}

static int rpmsg_net_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv_data, u32 src)
{
	struct rpmsg_net_priv *priv = dev_get_drvdata(&rpdev->dev);
	struct net_device *dev = priv->netdev;
	struct sk_buff *skb;

	if (!len) {
		dev_core_stats_rx_dropped_inc(dev);
		return 0;
	}

	skb = netdev_alloc_skb(dev, len);
	if (!skb) {
		dev_core_stats_rx_dropped_inc(dev);
		return -ENOMEM;
	}

	skb_put_data(skb, data, len);

	switch (skb->data[0] >> 4) {
	case 6:
		skb->protocol = htons(ETH_P_IPV6);
		break;
	default:
		skb->protocol = htons(ETH_P_IP);
		break;
	}

	dev_sw_netstats_rx_add(dev, len);
	netif_rx(skb);

	return 0;
}

static int rpmsg_net_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_net_priv *priv;
	struct net_device *dev;
	int ret;

	dev = alloc_netdev(sizeof(*priv), "dsp%d", NET_NAME_ENUM,
			   rpmsg_net_setup);
	if (!dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->rpdev = rpdev;
	priv->netdev = dev;
	dev_set_drvdata(&rpdev->dev, priv);
	SET_NETDEV_DEV(dev, &rpdev->dev);

	ret = register_netdev(dev);
	if (ret) {
		free_netdev(dev);
		return ret;
	}

	dev_info(&rpdev->dev, "%s registered\n", dev->name);

	return 0;
}

static void rpmsg_net_remove(struct rpmsg_device *rpdev)
{
	struct rpmsg_net_priv *priv = dev_get_drvdata(&rpdev->dev);

	unregister_netdev(priv->netdev);
	free_netdev(priv->netdev);
}

static const struct rpmsg_device_id rpmsg_net_id_table[] = {
	{ .name = RPMSG_NET_CHANNEL },
	{}
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_net_id_table);

static struct rpmsg_driver rpmsg_net_driver = {
	.drv.name = "rpmsg_net",
	.id_table = rpmsg_net_id_table,
	.probe = rpmsg_net_probe,
	.callback = rpmsg_net_cb,
	.remove = rpmsg_net_remove,
};
module_rpmsg_driver(rpmsg_net_driver);

MODULE_DESCRIPTION("IP networking over RPMSG");
MODULE_LICENSE("GPL");
