/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * FastRPC userspace dispatcher daemon for device-side (DSP) operation.
 *
 * Reads invoke requests from /dev/fastrpc_device, dispatches them to
 * registered test handlers, and writes responses back.
 *
 * Build:
 *   hexagon-linux-musl-clang -O2 -static -o fastrpc_dispatcher \
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

#define REMOTE_SCALARS_METHOD(sc)	(((sc) >> 24) & 0x1f)
#define REMOTE_SCALARS_INBUFS(sc)	(((sc) >> 16) & 0xff)
#define REMOTE_SCALARS_OUTBUFS(sc)	(((sc) >> 8) & 0xff)

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

/* Remote argument descriptor (16 bytes) */
struct fastrpc_remote_arg {
	uint64_t buf;
	uint64_t len;
};

/* Test handle ID */
#define TEST_HANDLE	3

/* Test methods */
#define METHOD_ECHO	0
#define METHOD_ADD	1

static int devmem_fd = -1;

/*
 * Map a physical address range into our virtual address space.
 * On QEMU with identity mapping, addr is the physical address of the
 * shared invoke buffer.
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
			perror("open /dev/mem");
			return NULL;
		}
	}

	mapped = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      devmem_fd, page_base);
	if (mapped == MAP_FAILED) {
		perror("mmap /dev/mem");
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
 * Handle METHOD_ECHO: copy input buffer to output buffer.
 *
 * Expected layout:
 *   - 1 input buffer (inbufs=1), 1 output buffer (outbufs=1)
 *   - fastrpc_remote_arg[0] = input descriptor
 *   - fastrpc_remote_arg[1] = output descriptor
 *   - Inline data follows the arg descriptors
 */
static int handle_echo(void *payload, uint64_t size, uint32_t sc)
{
	struct fastrpc_remote_arg *args = payload;
	int inbufs = REMOTE_SCALARS_INBUFS(sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	int nscalars = inbufs + outbufs;
	uint8_t *base = payload;
	uint64_t in_off, in_len, out_off;
	size_t copy_len;

	if (inbufs < 1 || outbufs < 1)
		return -EINVAL;

	if ((uint64_t)nscalars * sizeof(*args) > size)
		return -EINVAL;

	/* Input buffer: offset and length relative to payload */
	in_off = args[0].buf;
	in_len = args[0].len;

	/* Output buffer: offset */
	out_off = args[1].buf;

	/* Bounds check */
	if (in_off + in_len > size || out_off + in_len > size)
		return -EFAULT;

	/* Copy input to output */
	copy_len = in_len < args[1].len ? in_len : args[1].len;
	memcpy(base + out_off, base + in_off, copy_len);

	return 0;
}

/*
 * Handle METHOD_ADD: read two uint32_t inputs, write their sum.
 *
 * Expected layout:
 *   - 1 input buffer (2 x uint32_t), 1 output buffer (1 x uint32_t)
 */
static int handle_add(void *payload, uint64_t size, uint32_t sc)
{
	struct fastrpc_remote_arg *args = payload;
	int inbufs = REMOTE_SCALARS_INBUFS(sc);
	int outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	int nscalars = inbufs + outbufs;
	uint8_t *base = payload;
	uint32_t a, b, sum;

	if (inbufs < 1 || outbufs < 1)
		return -EINVAL;

	if ((uint64_t)nscalars * sizeof(*args) > size)
		return -EINVAL;

	/* Input: two uint32_t values */
	if (args[0].len < 2 * sizeof(uint32_t))
		return -EINVAL;
	if (args[0].buf + args[0].len > size)
		return -EFAULT;

	memcpy(&a, base + args[0].buf, sizeof(a));
	memcpy(&b, base + args[0].buf + sizeof(uint32_t), sizeof(b));
	sum = a + b;

	/* Output: one uint32_t */
	if (args[1].len < sizeof(uint32_t))
		return -EINVAL;
	if (args[1].buf + sizeof(uint32_t) > size)
		return -EFAULT;

	memcpy(base + args[1].buf, &sum, sizeof(sum));

	return 0;
}

static int dispatch(uint32_t handle, uint32_t sc, void *payload, uint64_t size)
{
	uint32_t method = REMOTE_SCALARS_METHOD(sc);

	if (handle == TEST_HANDLE) {
		switch (method) {
		case METHOD_ECHO:
			return handle_echo(payload, size, sc);
		case METHOD_ADD:
			return handle_add(payload, size, sc);
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
				rsp.retval = dispatch(req.handle, req.sc,
						      payload, req.size);
				unmap_phys(payload, req.addr, req.size);
			}
		} else {
			/* No payload — dispatch with NULL */
			rsp.retval = dispatch(req.handle, req.sc, NULL, 0);
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
