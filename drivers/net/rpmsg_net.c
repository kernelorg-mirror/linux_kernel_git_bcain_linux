/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
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
	int ret;

	ret = rpmsg_trysend(priv->rpdev->ept, skb->data, skb->len);
	if (ret) {
		netif_stop_queue(dev);
		dev->stats.tx_dropped++;
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;
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
};

static void rpmsg_net_setup(struct net_device *dev)
{
	dev->netdev_ops = &rpmsg_net_ops;
	dev->type = ARPHRD_RAWIP;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP;
	dev->mtu = RPMSG_NET_MTU;
	dev->needed_headroom = 0;
	dev->needed_tailroom = 0;
	dev->addr_len = 0;
	dev->tx_queue_len = 100;
}

static int rpmsg_net_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv_data, u32 src)
{
	struct rpmsg_net_priv *priv = dev_get_drvdata(&rpdev->dev);
	struct net_device *dev = priv->netdev;
	struct sk_buff *skb;
	u8 version;

	skb = netdev_alloc_skb(dev, len);
	if (!skb) {
		dev->stats.rx_dropped++;
		return -ENOMEM;
	}

	skb_put_data(skb, data, len);

	/* Detect protocol from IP version nibble */
	version = skb->data[0] >> 4;
	if (version == 4)
		skb->protocol = htons(ETH_P_IP);
	else if (version == 6)
		skb->protocol = htons(ETH_P_IPV6);
	else
		skb->protocol = htons(ETH_P_IP);

	dev->stats.rx_packets++;
	dev->stats.rx_bytes += len;

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

	ret = register_netdev(dev);
	if (ret) {
		free_netdev(dev);
		return ret;
	}

	netif_wake_queue(dev);
	dev_info(&rpdev->dev, "rpmsg_net: %s registered\n", dev->name);
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

MODULE_DESCRIPTION("RPMSG IP networking driver");
MODULE_LICENSE("GPL");
