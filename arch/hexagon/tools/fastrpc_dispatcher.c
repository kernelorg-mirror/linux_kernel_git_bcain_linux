/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * FastRPC userspace dispatcher daemon for device-side (DSP) operation.
 *
 * Reads invoke requests from /dev/fastrpc_device, dispatches them to
 * registered test handlers, and writes responses back.
 *
 * The payload is the metadata buffer built by the AP-side
 * drivers/misc/fastrpc.c fastrpc_get_args():
 *
 *   +----------------------------------------+
 *   | union fastrpc_remote_arg rpra[nscalars]|  {u64 pv; u64 len;}
 *   +----------------------------------------+
 *   | struct fastrpc_invoke_buf list[n]      |  {u32 num; u32 pgidx;}
 *   +----------------------------------------+
 *   | struct fastrpc_phy_page pages[n]       |  {u64 addr; u64 size;}
 *   +----------------------------------------+
 *   | fdlist / crclist / inline arg data     |
 *   +----------------------------------------+
 *
 * For copy-based arguments (fd == -1), rpra[i].pv is an AP kernel
 * virtual address; pages[i].addr is the AP-physical, AP-page-aligned
 * address of the same data.  With the shared-memory window mapped at
 * the same physical address in both guests, the data for argument i
 * lives at payload offset:
 *
 *   pages[i].addr - msg.addr + (rpra[i].pv & (AP_PAGE_SIZE - 1))
 *
 * Build:
 *   hexagon-unknown-linux-musl-clang -O2 -static -o fastrpc_dispatcher \
 *       arch/hexagon/tools/fastrpc_dispatcher.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>

#define FASTRPC_DEV	"/dev/fastrpc_device"
#define DEV_MEM		"/dev/mem"
#define PAGE_SIZE	65536
#define PAGE_MASK	(~((uint64_t)PAGE_SIZE - 1))

/* Page size of the AP guest, which lays out the payload buffer */
#define AP_PAGE_SIZE	4096

#define REMOTE_SCALARS_METHOD(sc)	(((sc) >> 24) & 0x1f)
#define REMOTE_SCALARS_INBUFS(sc)	(((sc) >> 16) & 0xff)
#define REMOTE_SCALARS_OUTBUFS(sc)	(((sc) >> 8) & 0xff)
#define REMOTE_SCALARS_INHANDLES(sc)	(((sc) >> 4) & 0xf)
#define REMOTE_SCALARS_OUTHANDLES(sc)	((sc) & 0xf)

/* Must match kernel driver structures */
struct fastrpc_user_req {
	uint64_t ctx;
	uint32_t handle;
	uint32_t sc;
	uint64_t addr;
	uint64_t size;
};

struct fastrpc_user_rsp {
	uint64_t ctx;
	int32_t retval;
};

/* Payload metadata, matching drivers/misc/fastrpc.c wire layout */
struct fastrpc_remote_arg {
	uint64_t pv;
	uint64_t len;
};

struct fastrpc_invoke_buf {
	uint32_t num;
	uint32_t pgidx;
};

struct fastrpc_phy_page {
	uint64_t addr;
	uint64_t size;
};

/* Test handle ID */
#define TEST_HANDLE	3

/* Test methods */
#define METHOD_ECHO	0
#define METHOD_ADD	1

static int devmem_fd = -1;

/* A parsed invoke: direct pointers into the mapped payload */
#define MAX_ARGS	8
struct parsed_invoke {
	int nbufs;
	int inbufs;
	void *buf[MAX_ARGS];
	uint64_t len[MAX_ARGS];
};

/*
 * Map a physical address range into our virtual address space.  The
 * shared-memory window is mapped at the same physical address in both
 * guests, so msg.addr can be used directly.
 */
static void *map_phys(uint64_t addr, uint64_t size)
{
	uint64_t page_base = addr & PAGE_MASK;
	uint64_t offset = addr - page_base;
	uint64_t map_size = size + offset;
	void *mapped;

	if (devmem_fd < 0) {
		devmem_fd = open(DEV_MEM, O_RDWR | O_SYNC);
		if (devmem_fd < 0) {
			perror("open " DEV_MEM);
			return NULL;
		}
	}

	mapped = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      devmem_fd, page_base);
	if (mapped == MAP_FAILED) {
		perror("mmap " DEV_MEM);
		return NULL;
	}

	return (char *)mapped + offset;
}

static void unmap_phys(void *ptr, uint64_t addr, uint64_t size)
{
	uint64_t page_base = addr & PAGE_MASK;
	uint64_t offset = addr - page_base;
	uint64_t map_size = size + offset;

	munmap((char *)ptr - offset, map_size);
}

/*
 * Locate each buffer argument within the mapped payload.  Only
 * copy-based buffers are supported: dma-buf arguments would reference
 * memory outside the shared window.
 */
static int parse_payload(void *payload, uint64_t phys, uint64_t size,
			 uint32_t sc, struct parsed_invoke *pi)
{
	struct fastrpc_remote_arg *rpra = payload;
	struct fastrpc_invoke_buf *list;
	struct fastrpc_phy_page *pages;
	int inbufs = REMOTE_SCALARS_INBUFS(sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	int nbufs = inbufs + outbufs;
	uint64_t meta;
	int i;

	if (REMOTE_SCALARS_INHANDLES(sc) || REMOTE_SCALARS_OUTHANDLES(sc))
		return -ENOSYS;
	if (nbufs > MAX_ARGS)
		return -E2BIG;

	meta = (uint64_t)nbufs * (sizeof(*rpra) + sizeof(*list) +
				  sizeof(*pages));
	if (meta > size)
		return -EINVAL;

	list = (struct fastrpc_invoke_buf *)&rpra[nbufs];
	pages = (struct fastrpc_phy_page *)&list[nbufs];

	pi->nbufs = nbufs;
	pi->inbufs = inbufs;

	for (i = 0; i < nbufs; i++) {
		uint64_t off;

		pi->len[i] = rpra[i].len;
		pi->buf[i] = NULL;
		if (!rpra[i].len)
			continue;

		off = pages[i].addr - phys +
		      (rpra[i].pv & (AP_PAGE_SIZE - 1));
		if (pages[i].addr < phys || off + rpra[i].len > size)
			return -EFAULT;

		pi->buf[i] = (char *)payload + off;
	}

	return 0;
}

/* METHOD_ECHO: copy the input buffer to the output buffer */
static int handle_echo(struct parsed_invoke *pi)
{
	size_t len;

	if (pi->inbufs < 1 || pi->nbufs - pi->inbufs < 1)
		return -EINVAL;
	if (!pi->buf[0] || !pi->buf[pi->inbufs])
		return -EINVAL;

	len = pi->len[0] < pi->len[pi->inbufs] ?
	      pi->len[0] : pi->len[pi->inbufs];
	memcpy(pi->buf[pi->inbufs], pi->buf[0], len);

	return 0;
}

/* METHOD_ADD: read two uint32_t inputs, write their sum */
static int handle_add(struct parsed_invoke *pi)
{
	uint32_t a, b, sum;

	if (pi->inbufs < 1 || pi->nbufs - pi->inbufs < 1)
		return -EINVAL;
	if (!pi->buf[0] || pi->len[0] < 2 * sizeof(uint32_t))
		return -EINVAL;
	if (!pi->buf[pi->inbufs] || pi->len[pi->inbufs] < sizeof(uint32_t))
		return -EINVAL;

	memcpy(&a, pi->buf[0], sizeof(a));
	memcpy(&b, (char *)pi->buf[0] + sizeof(uint32_t), sizeof(b));
	sum = a + b;
	memcpy(pi->buf[pi->inbufs], &sum, sizeof(sum));

	printf("fastrpc_dispatcher: add %u + %u = %u\n", a, b, sum);
	return 0;
}

static int dispatch(uint32_t handle, uint32_t sc, struct parsed_invoke *pi)
{
	uint32_t method = REMOTE_SCALARS_METHOD(sc);

	if (handle == TEST_HANDLE) {
		switch (method) {
		case METHOD_ECHO:
			return handle_echo(pi);
		case METHOD_ADD:
			return handle_add(pi);
		default:
			fprintf(stderr, "fastrpc_dispatcher: unknown method %u "
				"on test handle\n", method);
			return -ENOSYS;
		}
	}

	fprintf(stderr, "fastrpc_dispatcher: unknown handle %u method %u\n",
		handle, method);
	return -ENOSYS;
}

int main(int argc, char *argv[])
{
	int fd;
	struct fastrpc_user_req req;
	struct fastrpc_user_rsp rsp;
	ssize_t n;

	printf("fastrpc_dispatcher: starting\n");

	fd = open(FASTRPC_DEV, O_RDWR);
	if (fd < 0) {
		perror("open " FASTRPC_DEV);
		return 1;
	}

	printf("fastrpc_dispatcher: opened %s, waiting for requests\n",
	       FASTRPC_DEV);

	for (;;) {
		n = read(fd, &req, sizeof(req));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("read");
			break;
		}
		if (n < (ssize_t)sizeof(req)) {
			fprintf(stderr, "fastrpc_dispatcher: short read (%zd)\n", n);
			continue;
		}

		printf("fastrpc_dispatcher: invoke handle=%u method=%u "
		       "ctx=0x%llx addr=0x%llx size=%llu\n",
		       req.handle, REMOTE_SCALARS_METHOD(req.sc),
		       (unsigned long long)req.ctx,
		       (unsigned long long)req.addr,
		       (unsigned long long)req.size);

		rsp.ctx = req.ctx;

		if (req.addr && req.size) {
			void *payload = map_phys(req.addr, req.size);

			if (!payload) {
				fprintf(stderr, "fastrpc_dispatcher: "
					"failed to map payload\n");
				rsp.retval = -EFAULT;
			} else {
				struct parsed_invoke pi;

				rsp.retval = parse_payload(payload, req.addr,
							   req.size, req.sc,
							   &pi);
				if (!rsp.retval)
					rsp.retval = dispatch(req.handle,
							      req.sc, &pi);
				unmap_phys(payload, req.addr, req.size);
			}
		} else {
			struct parsed_invoke pi = { 0 };

			rsp.retval = dispatch(req.handle, req.sc, &pi);
		}

		n = write(fd, &rsp, sizeof(rsp));
		if (n < 0) {
			perror("write response");
			break;
		}

		printf("fastrpc_dispatcher: response ctx=0x%llx retval=%d\n",
		       (unsigned long long)rsp.ctx, rsp.retval);
	}

	close(fd);
	if (devmem_fd >= 0)
		close(devmem_fd);
	return 0;
}
