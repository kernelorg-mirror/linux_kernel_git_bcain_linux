// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm GLINK transport over a shared-memory window
 *
 * Two processors that share DDR but have no SMEM or IPCC between them can
 * still speak GLINK: each direction is one FIFO in a window of shared memory,
 * and each side rings the other's doorbell (a bit in an IPC register that
 * raises an interrupt there) after it queues data.  The GLINK protocol itself
 * is handled by qcom_glink_native.
 *
 * A window starts with a header, followed by the FIFO:
 *
 *   0x00  head   write offset into the FIFO, updated by the sender
 *   0x04  tail   read offset into the FIFO, updated by the receiver
 *   0x08  magic  set once the window is initialized
 *   0x0c  reserved
 *   0x10  FIFO data
 *
 * The device tree gives the window this side reads first, the window it
 * writes second, and the doorbell registers third.  Exactly one side
 * initializes the windows ("qcom,shmem-init"); the other side finds them
 * initialized.  Shared memory can outlive a reset, so the side that
 * initializes must be the one that is started first and restarted last.
 */

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include "qcom_glink_native.h"

#define SHMEM_HEAD		0x00
#define SHMEM_TAIL		0x04
#define SHMEM_MAGIC		0x08
#define SHMEM_FIFO		0x10
#define SHMEM_MAGIC_VALUE	0x4d534c47	/* "GLSM" */

#define FIFO_FULL_RESERVE	8
#define TX_BLOCKED_CMD_RESERVE	8	/* size of struct read_notif_request */

static bool intentless;
module_param(intentless, bool, 0444);
MODULE_PARM_DESC(intentless,
		 "Use intentless mode even if the DT node does not ask for it");

struct qcom_glink_shmem;

struct glink_shmem_pipe {
	struct qcom_glink_pipe native;

	void __iomem *base;
	void __iomem *fifo;

	struct qcom_glink_shmem *shmem;
};

struct qcom_glink_shmem {
	struct device *dev;
	struct qcom_glink *glink;

	struct glink_shmem_pipe rx;
	struct glink_shmem_pipe tx;

	void __iomem *ipc;
	u32 ipc_offset;
	u32 ipc_bits;

	int irq;
};

#define to_shmem_pipe(p) container_of(p, struct glink_shmem_pipe, native)

static u32 pipe_head(struct glink_shmem_pipe *pipe)
{
	return ioread32(pipe->base + SHMEM_HEAD);
}

static u32 pipe_tail(struct glink_shmem_pipe *pipe)
{
	return ioread32(pipe->base + SHMEM_TAIL);
}

static size_t glink_shmem_rx_avail(struct qcom_glink_pipe *np)
{
	struct glink_shmem_pipe *pipe = to_shmem_pipe(np);
	u32 head = pipe_head(pipe);
	u32 tail = pipe_tail(pipe);

	if (head >= pipe->native.length || tail >= pipe->native.length)
		return 0;

	if (head < tail)
		return pipe->native.length - tail + head;

	return head - tail;
}

static void glink_shmem_rx_peek(struct qcom_glink_pipe *np, void *data,
				unsigned int offset, size_t count)
{
	struct glink_shmem_pipe *pipe = to_shmem_pipe(np);
	size_t len;
	u32 tail;

	tail = pipe_tail(pipe) + offset;
	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	/* Do not read data the sender has not yet published. */
	rmb();

	len = min_t(size_t, count, pipe->native.length - tail);
	if (len)
		memcpy_fromio(data, pipe->fifo + tail, len);

	if (len != count)
		memcpy_fromio(data + len, pipe->fifo, count - len);
}

static void glink_shmem_rx_advance(struct qcom_glink_pipe *np, size_t count)
{
	struct glink_shmem_pipe *pipe = to_shmem_pipe(np);
	u32 tail = pipe_tail(pipe) + count;

	if (tail >= pipe->native.length)
		tail -= pipe->native.length;

	iowrite32(tail, pipe->base + SHMEM_TAIL);
}

static size_t glink_shmem_tx_avail(struct qcom_glink_pipe *np)
{
	struct glink_shmem_pipe *pipe = to_shmem_pipe(np);
	u32 head = pipe_head(pipe);
	u32 tail = pipe_tail(pipe);
	u32 avail;

	if (tail <= head)
		avail = pipe->native.length - head + tail;
	else
		avail = tail - head;

	if (avail < FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE)
		return 0;

	return avail - (FIFO_FULL_RESERVE + TX_BLOCKED_CMD_RESERVE);
}

static unsigned int glink_shmem_tx_write_one(struct glink_shmem_pipe *pipe,
					     unsigned int head,
					     const void *data, size_t count)
{
	size_t len;

	len = min_t(size_t, count, pipe->native.length - head);
	if (len)
		memcpy_toio(pipe->fifo + head, data, len);

	if (len != count)
		memcpy_toio(pipe->fifo, data + len, count - len);

	head += count;
	if (head >= pipe->native.length)
		head -= pipe->native.length;

	return head;
}

static void glink_shmem_tx_write(struct qcom_glink_pipe *np,
				 const void *hdr, size_t hlen,
				 const void *data, size_t dlen)
{
	struct glink_shmem_pipe *pipe = to_shmem_pipe(np);
	unsigned int head = pipe_head(pipe);

	head = glink_shmem_tx_write_one(pipe, head, hdr, hlen);
	head = glink_shmem_tx_write_one(pipe, head, data, dlen);

	/* The FIFO is kept aligned to 8 bytes. */
	head = ALIGN(head, 8);
	if (head >= pipe->native.length)
		head -= pipe->native.length;

	/* The data must be visible before the head that publishes it. */
	wmb();

	iowrite32(head, pipe->base + SHMEM_HEAD);
}

static void glink_shmem_tx_kick(struct qcom_glink_pipe *np)
{
	struct glink_shmem_pipe *pipe = to_shmem_pipe(np);
	struct qcom_glink_shmem *shmem = pipe->shmem;

	/* The head update must be visible before the doorbell. */
	wmb();

	iowrite32(shmem->ipc_bits, shmem->ipc + shmem->ipc_offset);
}

static irqreturn_t qcom_glink_shmem_intr(int irq, void *data)
{
	struct qcom_glink_shmem *shmem = data;

	qcom_glink_native_rx(shmem->glink);

	return IRQ_HANDLED;
}

static void glink_shmem_pipe_init(struct qcom_glink_shmem *shmem,
				  struct glink_shmem_pipe *pipe,
				  void __iomem *base, size_t size)
{
	pipe->shmem = shmem;
	pipe->base = base;
	pipe->fifo = base + SHMEM_FIFO;
	pipe->native.length = size - SHMEM_FIFO;

	if (pipe == &shmem->rx) {
		pipe->native.avail = glink_shmem_rx_avail;
		pipe->native.peek = glink_shmem_rx_peek;
		pipe->native.advance = glink_shmem_rx_advance;
	} else {
		pipe->native.avail = glink_shmem_tx_avail;
		pipe->native.write = glink_shmem_tx_write;
		pipe->native.kick = glink_shmem_tx_kick;
	}
}

static void __iomem *glink_shmem_map(struct platform_device *pdev,
				     unsigned int index, size_t *size)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM,
						     index);

	if (!res)
		return IOMEM_ERR_PTR(-EINVAL);

	/*
	 * The windows and the doorbell registers are also described to other
	 * drivers, so do not claim the regions.
	 */
	if (size)
		*size = resource_size(res);

	return devm_ioremap(&pdev->dev, res->start, resource_size(res));
}

static void glink_shmem_init_window(void __iomem *base)
{
	iowrite32(0, base + SHMEM_HEAD);
	iowrite32(0, base + SHMEM_TAIL);
	iowrite32(SHMEM_MAGIC_VALUE, base + SHMEM_MAGIC);
}

static int glink_shmem_read_doorbell(struct device_node *np,
				     struct qcom_glink_shmem *shmem)
{
	int ret;

	ret = of_property_read_u32(np, "qcom,ipc-offset", &shmem->ipc_offset);
	if (!ret)
		return of_property_read_u32(np, "qcom,ipc-bits",
					    &shmem->ipc_bits);

#ifdef GLINK_SHMEM_RUBIKPI_HOST
	/* The old snull node names the same two values differently. */
	ret = of_property_read_u32(np, "qcom,snull-irq-offset",
				   &shmem->ipc_offset);
	if (!ret)
		return of_property_read_u32(np, "qcom,snull-irq-bitset",
					    &shmem->ipc_bits);
#endif

	return ret;
}

static int qcom_glink_shmem_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct qcom_glink_shmem *shmem;
	size_t rx_size, tx_size;
	void __iomem *rx, *tx;
	bool init;
	int ret;

	shmem = devm_kzalloc(&pdev->dev, sizeof(*shmem), GFP_KERNEL);
	if (!shmem)
		return -ENOMEM;

	shmem->dev = &pdev->dev;

	rx = glink_shmem_map(pdev, 0, &rx_size);
	tx = glink_shmem_map(pdev, 1, &tx_size);
	shmem->ipc = glink_shmem_map(pdev, 2, NULL);
	if (IS_ERR_OR_NULL(rx) || IS_ERR_OR_NULL(tx) ||
	    IS_ERR_OR_NULL(shmem->ipc))
		return dev_err_probe(&pdev->dev, -ENOMEM,
				     "failed to map the windows or doorbell\n");

	if (rx_size <= SHMEM_FIFO || tx_size <= SHMEM_FIFO)
		return dev_err_probe(&pdev->dev, -EINVAL, "window too small\n");

	ret = glink_shmem_read_doorbell(np, shmem);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "bad doorbell description\n");

	shmem->irq = platform_get_irq(pdev, 0);
	if (shmem->irq < 0)
		return shmem->irq;

	glink_shmem_pipe_init(shmem, &shmem->rx, rx, rx_size);
	glink_shmem_pipe_init(shmem, &shmem->tx, tx, tx_size);

	init = of_property_read_bool(np, "qcom,shmem-init");
	if (init) {
		glink_shmem_init_window(rx);
		glink_shmem_init_window(tx);
	} else if (ioread32(rx + SHMEM_MAGIC) != SHMEM_MAGIC_VALUE ||
		   ioread32(tx + SHMEM_MAGIC) != SHMEM_MAGIC_VALUE) {
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "the windows are not initialized\n");
	}

	ret = devm_request_irq(&pdev->dev, shmem->irq, qcom_glink_shmem_intr,
			       IRQF_NO_SUSPEND | IRQF_NO_AUTOEN,
			       "glink-shmem", shmem);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to request IRQ\n");

	shmem->glink = qcom_glink_native_probe(&pdev->dev,
					       GLINK_FEATURE_INTENT_REUSE,
					       &shmem->rx.native,
					       &shmem->tx.native,
					       intentless ||
					       of_property_read_bool(np, "qcom,intentless"));
	if (IS_ERR(shmem->glink))
		return dev_err_probe(&pdev->dev, PTR_ERR(shmem->glink),
				     "failed to start GLINK\n");

	platform_set_drvdata(pdev, shmem);
	/*
	 * The peer may have queued data before we could take the interrupt.
	 * Take it now, before the interrupt can run the same code on another
	 * CPU: it must not run twice at once, or the two of them skip messages
	 * between them.  An interrupt that comes in meanwhile is taken when it
	 * is enabled.
	 */
	qcom_glink_native_rx(shmem->glink);
	enable_irq(shmem->irq);

	return 0;
}

static void qcom_glink_shmem_remove(struct platform_device *pdev)
{
	struct qcom_glink_shmem *shmem = platform_get_drvdata(pdev);

	disable_irq(shmem->irq);
	qcom_glink_native_remove(shmem->glink);
}

static const struct of_device_id qcom_glink_shmem_of_match[] = {
	{ .compatible = "qcom,glink-shmem-edge" },
#ifdef GLINK_SHMEM_RUBIKPI_HOST
	/* The RubikPi 3 host describes the link with the old snull node. */
	{ .compatible = "qcom,snull-eth" },
#endif
	{}
};
MODULE_DEVICE_TABLE(of, qcom_glink_shmem_of_match);

static struct platform_driver qcom_glink_shmem_driver = {
	.probe = qcom_glink_shmem_probe,
	.remove = qcom_glink_shmem_remove,
	.driver = {
		.name = "qcom_glink_shmem",
		.of_match_table = qcom_glink_shmem_of_match,
	},
};
module_platform_driver(qcom_glink_shmem_driver);

MODULE_DESCRIPTION("Qualcomm GLINK shared-memory transport");
MODULE_LICENSE("GPL");
