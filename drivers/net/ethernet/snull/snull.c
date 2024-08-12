/*
 * snull.c --  the Simple Network Utility
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: snull.c,v 1.21 2004/11/05 02:36:03 rubini Exp $
 */


#define DEBUG

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/io.h>

#include <linux/sched.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/errno.h>  /* error codes */
#include <linux/types.h>  /* size_t */
#include <linux/interrupt.h> /* mark_bh */
#include <linux/sizes.h>

#include <linux/in.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
#include <linux/skbuff.h>
#include <linux/version.h>

#include "snull.h"

#include <linux/in6.h>
#include <asm/checksum.h>

MODULE_AUTHOR("Alessandro Rubini, Jonathan Corbet");
MODULE_LICENSE("Dual BSD/GPL");


static int timeout = SNULL_TIMEOUT;
module_param(timeout, int, 0);

/*
 * Do we run in NAPI mode?
 */
/*  RKuo -- ignore napi mode for now  */
static int use_napi = 0;
module_param(use_napi, int, 0);

/*
 * A structure representing an in-flight packet.
 */
struct snull_packet {
	//struct snull_packet *next;	/*  Not used with a 1-packet pool  */
	u32	datalen;
	u8 data[ETH_DATA_LEN];
};

/*
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */

/*  alloc'd with kzalloc so it's all null at start */
struct snull_priv {
	struct net_device_stats stats;
	u32 *rx_statusptr[2];  /*  RX queue, 0 is RX ready, 1 is TX done  */
	u32 *tx_statusptr[2];
	struct snull_packet *rx_buffer;
	struct snull_packet *tx_buffer;
	int rx_int_enabled;
	int tx_packetlen;
	struct sk_buff *skb;
	spinlock_t lock;
	struct net_device *dev;
	struct napi_struct napi;
	int rx_irq;
	int tx_irq;
	unsigned tx_irq_offset;
	unsigned tx_irq_bitset;
	unsigned zero_mem;
	void *tx_iobase;
};

static void snull_tx_timeout(struct net_device *dev);
static void (*snull_interrupt)(int, void *, struct pt_regs *);

static void send_interrupt(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	iowrite32(priv->tx_irq_bitset, priv->tx_iobase + priv->tx_irq_offset);
}

/*
 * Buffer/pool management.
 */

struct snull_packet *snull_get_tx_buffer(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	unsigned long flags;
	struct snull_packet *pkt;

	spin_lock_irqsave(&priv->lock, flags);

	pkt = priv->tx_buffer;
	/*  RKuo -- with only 1 buffer, we stop every time.  */
	netif_stop_queue(dev);

	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}


/*  RKuo -- This is a TX side operation upon receving TX done  */
void snull_release_buffer(struct net_device *dev)
{
	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);
}

/*  RKuo -- dev here is the dest, hence so is the priv  */
void snull_enqueue_buf(struct net_device *dev, struct snull_packet *pkt)
{
}

/*  Uh, this is just rx_buffer?  */
struct snull_packet *snull_dequeue_buf(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt = NULL;

	pkt = priv->rx_buffer;

	BUG_ON(!pkt);
	return pkt;
}

/*
 * Enable and disable receive interrupts.
 * Mostly used by NAPI.  Since there's no "device" per se we just gotta disable/mask the interrupt.
 */
static void snull_rx_ints(struct net_device *dev, int enable)
{
	struct snull_priv *priv = netdev_priv(dev);
	priv->rx_int_enabled = enable;
}

/*
 * Configuration changes (passed on by ifconfig)
 */
int snull_config(struct net_device *dev, struct ifmap *map)
{
	if (dev->flags & IFF_UP) /* can't act on a running interface */
		return -EBUSY;

	/* Don't allow changing the I/O address */
	if (map->base_addr != dev->base_addr) {
		printk(KERN_WARNING "snull: Can't change I/O address\n");
		return -EOPNOTSUPP;
	}

	/* ignore other fields */
	return 0;
}

/*
 * Receive a packet: retrieve, encapsulate and pass over to upper levels
 */
void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
	struct sk_buff *skb;
	struct snull_priv *priv = netdev_priv(dev);
	int datalen;

	datalen = ioread32(&pkt->datalen);

	/*
	 * The packet has been retrieved from the transmission
	 * medium. Build an skb around it, so upper layers can handle it
	 */
	skb = dev_alloc_skb(datalen + 2);
	if (!skb) {
		if (printk_ratelimit())
			printk(KERN_WARNING "snull rx: low on mem - packet dropped\n");
		priv->stats.rx_dropped++;
		goto out;
	}
	skb_reserve(skb, 2); /* align IP on 16B boundary */
	/*  this is actully a monster size IO read  */
	memcpy_fromio(skb_put(skb,datalen), pkt->data, datalen);
	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += datalen;
	netif_rx(skb);
	iowrite32(SNULL_TX_INTR, priv->tx_statusptr[1]);
	barrier();
	send_interrupt(dev);
  out:
	return;
}

/*
 * The poll implementation.
 */
static int snull_poll(struct napi_struct *napi, int budget)
{
	int npackets = 0;
	struct sk_buff *skb;
	struct snull_priv *priv = container_of(napi, struct snull_priv, napi);
	struct net_device *dev = priv->dev;
	struct snull_packet *pkt;

	BUG();  /*  This routine is not ready, bro.  */

	while (npackets < budget && priv->rx_buffer) {
		pkt = snull_dequeue_buf(dev);
		skb = dev_alloc_skb(pkt->datalen + 2);
		if (! skb) {
			if (printk_ratelimit())
				printk(KERN_WARNING "snull: packet dropped\n");
			priv->stats.rx_dropped++;
/*			snull_release_buffer(pkt); */
			continue;
		}
		skb_reserve(skb, 2); /* align IP on 16B boundary */
		memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
		netif_receive_skb(skb);

        	/* Maintain stats */
		npackets++;
		priv->stats.rx_packets++;
		priv->stats.rx_bytes += pkt->datalen;
/*		snull_release_buffer(pkt); */
	}
	/* If we processed all packets, we're done; tell the kernel and reenable ints */
	if (! priv->rx_buffer) {
		napi_complete(napi);
		snull_rx_ints(dev, 1);
		return 0;
	}
	/* We couldn't process everything. */
	return npackets;
}


/*
 * The typical interrupt entry point
 */

/*  RKuo -- this seems like kind of a merged interrupt signaler and handler; most of this is actually handler.  */
static void snull_regular_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;
	struct snull_packet *pkt = NULL;
	/*
	 * As usual, check the "device" pointer to be sure it is
	 * really interrupting.
	 * Then assign "struct device *dev"
	 */
	struct net_device *dev = (struct net_device *)dev_id;

	if (!dev)
		return;

	/* Lock the device */
	/*  I think these locks are for touching each other's status words; will need to revisit.  */
	priv = netdev_priv(dev);
	spin_lock(&priv->lock);

	/* retrieve statusword: real netdevices use I/O instructions */
	/* RKuo -- so use an IO read here.  */
	statusword = ioread32(priv->rx_statusptr[0]);  /* probably should use endian-tocpu whatever  */
	if (statusword & SNULL_RX_INTR) {
		iowrite32(0, priv->rx_statusptr[0]);
		barrier();
		/* send it to snull_rx for handling */
		pkt = priv->rx_buffer;
		snull_rx(dev, pkt);
	}
	statusword = ioread32(priv->rx_statusptr[1]);  /* probably should use endian-tocpu whatever  */
	if (statusword & SNULL_TX_INTR) {
		/* a transmission is over: free the skb */
		iowrite32(0, priv->rx_statusptr[1]);
		barrier();
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		if (priv->tx_packetlen != 0) {
			dev_kfree_skb_irq(priv->skb);
			priv->tx_packetlen = 0;
		}
	}

	/* Unlock the device and we are done */
	spin_unlock(&priv->lock);
	if (statusword & SNULL_TX_INTR) snull_release_buffer(dev); /* Do this outside the lock! */
	return;
}

/*
 * A NAPI interrupt handler.
 */
static void snull_napi_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;

	/*
	 * As usual, check the "device" pointer for shared handlers.
	 * Then assign "struct device *dev"
	 */
	struct net_device *dev = (struct net_device *)dev_id;
	/* ... and check with hw if it's really ours */

	/* paranoid */
	if (!dev)
		return;

	/* Lock the device */
	priv = netdev_priv(dev);
	spin_lock(&priv->lock);

	/* retrieve statusword: real netdevices use I/O instructions */
	statusword = ioread32(priv->rx_statusptr[0]);
	if (statusword & SNULL_RX_INTR) {
		iowrite32(0, priv->rx_statusptr[0]);
		barrier();
		snull_rx_ints(dev, 0);  /* Disable further interrupts */
		napi_schedule(&priv->napi);
	}
	statusword = ioread32(priv->rx_statusptr[1]);
	if (statusword & SNULL_TX_INTR) {
		iowrite32(0, priv->rx_statusptr[1]);
		barrier();
        	/* a transmission is over: free the skb */
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}

	/* Unlock the device and we are done */
	spin_unlock(&priv->lock);
	return;
}


static irqreturn_t snull_real_inthandler(int irq, void *dev)
{
	/*  Actually these are so close might as well just call it.  */
	snull_regular_interrupt(irq, dev, 0);
	return IRQ_HANDLED;
}

/*
 * Open and close
 */

int snull_open(struct net_device *dev)
{
	int ret;
	struct snull_priv *priv = netdev_priv(dev);
	/* request_region(), request_irq(), ....  (like fops->open) */

	/*
	 * Assign the hardware address of the board: use "\0SNULx", where
	 * x is 0 or 1. The first byte is '\0' to avoid being a multicast
	 * address (the first byte of multicast addrs is odd).
	 */

//#ifndef CONFIG_HEXAGON
//	memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
//#endif
	ret = request_irq(priv->rx_irq, snull_real_inthandler, IRQF_SHARED, dev->name, dev);
	BUG_ON(ret);

	netif_start_queue(dev);
	return 0;
}

int snull_release(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	/* release ports, irq and such -- like fops->close */

	netif_stop_queue(dev); /* can't transmit any more */

	free_irq(priv->rx_irq, dev);

	return 0;
}



/*
 * Transmit a packet (low level interface)
 */

static void snull_hw_tx(char *buf, int len, struct net_device *dev)
{
	/*
	 * This function deals with hw details. This interface loops
	 * back the packet to the other snull interface (if any).
	 * In other words, this function implements the snull behaviour,
	 * while all other procedures are rather device-independent
	 */
	struct snull_priv *priv;
	struct snull_packet *tx_buffer;

	/*
	 * Ok, now the packet is ready for transmission: first simulate a
	 * receive interrupt on the twin device, then  a
	 * transmission-done on the transmitting device
	 */
	priv = netdev_priv(dev);

	tx_buffer = snull_get_tx_buffer(dev);
	//tx_buffer->datalen = len;
	iowrite32(len, &tx_buffer->datalen);
	/*  And this is a monster sized iowrite  */
	memcpy_toio(tx_buffer->data, buf, len);
	snull_enqueue_buf(NULL, tx_buffer);

	/* Indicate RX ready to the TX buffer */
	iowrite32(ioread32(priv->tx_statusptr[0]) | SNULL_RX_INTR, priv->tx_statusptr[0]);
	barrier();
	send_interrupt(dev);

	priv->tx_packetlen = len;
}

/*
 * Transmit a packet (called by the kernel)
 */
int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	char *data, shortpkt[ETH_ZLEN];
	struct snull_priv *priv = netdev_priv(dev);

	data = skb->data;
	len = skb->len;
	if (len < ETH_ZLEN) {
		memset(shortpkt, 0, ETH_ZLEN);
		memcpy(shortpkt, skb->data, skb->len);
		len = ETH_ZLEN;
		data = shortpkt;
	}
#if LINUX_VERSION_CODE <= KERNEL_VERSION(4,9,0)
	dev->trans_start = jiffies; /* save the timestamp */
#else
	netif_trans_update(dev);
#endif

	/* Remember the skb, so we can free it at interrupt time */
	priv->skb = skb;

	/* actual delivery of data is device-specific, and not shown here */
	snull_hw_tx(data, len, dev);

	return NETDEV_TX_OK; /* Our simple device can not fail */
}

/*
 * Deal with a transmit timeout.
 */
void snull_tx_timeout (struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);

        /* Simulate a transmission interrupt (to ourselves) to get things moving.  Kinda sketchy.  */
	iowrite32(SNULL_TX_INTR, priv->rx_statusptr[1]);
	barrier();
	snull_interrupt(0, dev, NULL);
	priv->stats.tx_errors++;
	netif_wake_queue(dev);
	return;
}



/*
 * Ioctl commands
 */
int snull_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	PDEBUG("ioctl\n");
	return 0;
}

/*
 * Return statistics to the caller
 */
struct net_device_stats *snull_stats(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	return &priv->stats;
}


/*
 * The "change_mtu" method is usually not needed.
 * If you need it, it must be like this.
 */
int snull_change_mtu(struct net_device *dev, int new_mtu)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);
	spinlock_t *lock = &priv->lock;

	/* check ranges */
	if ((new_mtu < 68) || (new_mtu > 1500))
		return -EINVAL;
	/*
	 * Do anything you need, and the accept the value
	 */
	spin_lock_irqsave(lock, flags);
	dev->mtu = new_mtu;
	spin_unlock_irqrestore(lock, flags);
	return 0; /* success */
}

static const struct net_device_ops snull_netdev_ops = {
	.ndo_open            = snull_open,
	.ndo_stop            = snull_release,
	.ndo_start_xmit      = snull_tx,
	.ndo_do_ioctl        = snull_ioctl,
	.ndo_set_config      = snull_config,
	.ndo_get_stats       = snull_stats,
	.ndo_change_mtu      = snull_change_mtu,
	.ndo_tx_timeout      = snull_tx_timeout
};

/*
 * Finally, the module stuff
 */

void snull_cleanup(void)
{
	/*  Uh, unregister the driver?  */
	return;
}

void snull_init_basic(struct net_device *dev)
{
	struct snull_priv *priv;

	snull_interrupt = use_napi ? snull_napi_interrupt : snull_regular_interrupt;

	/*
	 * Then, assign other fields in dev, using ether_setup() and some
	 * hand assignments
	 */
	ether_setup(dev); /* assign some of the fields */
	dev->watchdog_timeo = timeout;
	dev->netdev_ops = &snull_netdev_ops;

	/*
	 * Then, initialize the priv field. This encloses the statistics
	 * and a few private fields.
	 */
	priv = netdev_priv(dev);
	if (use_napi) {
		netif_napi_add(dev, &priv->napi, snull_poll,2);
	}
	memset(priv, 0, sizeof(struct snull_priv));
	spin_lock_init(&priv->lock);	/* superfluous lock?  */
	snull_rx_ints(dev, 1);		/* enable receive interrupts; bogus */

}


//  OK so technically these should be mutually exclusive I think
#define MAC_OPTION_SET (1<<0)
#define MAC_OPTION_GET (1<<1)


struct snull_plat_data {
	int mac_options;
};

/*  This is cheesy but expedient.  */

static const struct snull_plat_data snull_data_noeth = {
	.mac_options = 0,
};

static const struct snull_plat_data snull_data_eth = {
	.mac_options = MAC_OPTION_GET,
};

static const struct of_device_id snull_net_of_match[] = {
	{.compatible = "qcom,snull-eth", .data = &snull_data_eth},
	{.compatible = "qcom,snull-noeth", .data = &snull_data_noeth},
	{},
};
MODULE_DEVICE_TABLE(of, snull_net_of_match);

/*  Looks pretty much just like the previous mangled inits  */

/*  ToDo:  pass parent device as param? */
struct net_device *get_ndev_bridge_parent(void)
{
	struct net_device *ndev;
	void *ret = NULL;

	read_lock(&dev_base_lock);
	ndev = first_net_device(&init_net);
	while (ndev) {
		printk(KERN_WARNING "%s %pM\n", ndev->name, ndev->dev_addr);
		if (strncmp(ndev->name, "eth0", IFNAMSIZ) == 0) {
			ret = ndev;
		}
		/*  Actually should break but whatevs, I like holding locks */
		ndev = next_net_device(ndev);
	}
//found:
	read_unlock(&dev_base_lock);
	return ret;
}

static int snull_net_probe(struct platform_device *pdev)
{
	int ret = -ENODEV;
	struct net_device *ndev;
	struct snull_priv *priv;
	struct resource *res;
	const struct snull_plat_data *pdata = NULL;
	void *ptr, *end;
	const struct of_device_id *match;
	struct net_device *ndev_bridge_parent;
	struct device_node *np;
	int rval;
	unsigned char new_mac[ETH_ALEN];

	/*  Have to manually grab the cheese.  */
	match = of_match_device(snull_net_of_match, &pdev->dev);
	if (match && match->data) {
		pdata = match->data;
	}
	BUG_ON(!pdata);

	if (pdata->mac_options & MAC_OPTION_SET) {
		ndev_bridge_parent = get_ndev_bridge_parent();
		if (!ndev_bridge_parent) {
			return -EAGAIN;
		}  /* wait for parent to come up */
		memcpy(new_mac, ndev_bridge_parent->dev_addr, ETH_ALEN);
		/*  Set the child's MAC to qualcomm's vendor ID  */
		new_mac[0] = 0x00;
		new_mac[1] = 0xA0;
		new_mac[2] = 0xC6;
	}  /*  don't tell anyone, but we're the "master"  */

	ndev = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_ENUM,
			snull_init_basic);

	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	BUG_ON(!priv);
	platform_set_drvdata(pdev, ndev);

	/*  Set up the "pool"  */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	printk(KERN_WARNING "%s: rx_buffer = %llx\n", __FUNCTION__, (u64) res->start);
	priv->rx_buffer = devm_ioremap_nocache(&pdev->dev,res->start,resource_size(res));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	printk(KERN_WARNING "%s: tx_buffer = %llx\n", __FUNCTION__, (u64) res->start);
	priv->tx_buffer = devm_ioremap_nocache(&pdev->dev,res->start,resource_size(res));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	printk(KERN_WARNING "%s: io_base = %llx\n", __FUNCTION__, (u64) res->start);
	priv->tx_iobase = devm_ioremap_nocache(&pdev->dev, res->start,resource_size(res));

	BUG_ON(!priv->rx_buffer);
	BUG_ON(!priv->tx_buffer);
	BUG_ON(!priv->tx_iobase);

	np = pdev->dev.of_node;
	rval = of_property_read_u32(np, "qcom,snull-irq-offset", &(priv->tx_irq_offset));
	if (rval) {
		printk("snull: error %d: reading irq_offset\n", rval);
		goto out;
	}
	printk(KERN_INFO "%s: tx_irq_offset %u\n", __FUNCTION__, priv->tx_irq_offset);

	rval = of_property_read_u32(np, "qcom,snull-irq-bitset", &(priv->tx_irq_bitset));
	if (rval) {
		printk("snull: error %d: reading irq_offset\n", rval);
		goto out;
	}
	printk(KERN_INFO "%s: tx_irq_bitset 0x%x\n", __FUNCTION__, priv->tx_irq_bitset);

	rval = of_property_read_u32(np, "qcom,snull-zero-mem", &(priv->zero_mem));
	if (rval) {
		printk("snull: error %d: reading zero_mem\n", rval);
		goto out;
	}
	printk(KERN_INFO "%s: zero_mem 0x%x\n", __FUNCTION__, priv->zero_mem);

	priv->rx_statusptr[0] = (void *) (priv->rx_buffer) + SZ_64K - 8;
	priv->rx_statusptr[1] = (void *) (priv->rx_buffer) + SZ_64K - 4;
	priv->tx_statusptr[0] = (void *) (priv->tx_buffer) + SZ_64K - 8;
	priv->tx_statusptr[1] = (void *) (priv->tx_buffer) + SZ_64K - 4;

	/*  One guy is expected to zero out the memory.  Probably should be a devtree property but whatever.  */
	//  This is all still a horrible mess.
	if (priv->zero_mem) {
		ptr = end = (void *) priv->rx_buffer;
		end += SZ_64K;
		while (ptr < end) {
			iowrite8(0, ptr++);
		}

		ptr = end = (void *) priv->tx_buffer;
		end += SZ_64K;
		while (ptr < end) {
			iowrite8(0, ptr++);
		}
		//  Might make this independent of zero_mem; but has to come afterwards
		if (pdata->mac_options & MAC_OPTION_SET) {
			ptr = (void *) priv->tx_buffer + SZ_64K - 16;
			memcpy_toio(ptr, new_mac, ETH_ALEN);
			printk(KERN_WARNING "setting slave mac: %pM\n", new_mac);
		}
		memcpy(ndev->dev_addr, "\0SNUL0", ETH_ALEN);  // "parent" is always SNUL0
	}
	else {
		if (pdata->mac_options & MAC_OPTION_GET) {
			ptr = (void *) priv->rx_buffer + SZ_64K - 16;
			memcpy_fromio(ndev->dev_addr, ptr, ETH_ALEN);
			printk(KERN_WARNING "mac: %pM\n", ndev->dev_addr);
		}	/*  set the dev_addr instead  */
		else {
			memcpy(ndev->dev_addr, "\0SNUL1", ETH_ALEN);
		}
	}

	barrier();
	priv->rx_irq = platform_get_irq(pdev, 0);

	dev_dbg(&pdev->dev, "irq %d\n", priv->rx_irq);

	ret = register_netdev(ndev);
	if (ret) {
		printk("snull: error %i registering device \"%s\"\n",
			ret, ndev->name);
		goto out;
	}

	/*  FIXME: add proper cleanup  */
out:
	return ret;

}

static int snull_net_remove(struct platform_device *pdev)
{
	/*  Placeholder  */
	return 0;
}

static struct platform_driver snull_net_driver = {
	.driver = {
		.name = "snull",
		.of_match_table = of_match_ptr(snull_net_of_match),
	},
	.probe = snull_net_probe,
	.remove = snull_net_remove
};

static int snull_net_init(void)
{
	return platform_driver_register(&snull_net_driver);
}


module_init(snull_net_init);
module_exit(snull_cleanup);
