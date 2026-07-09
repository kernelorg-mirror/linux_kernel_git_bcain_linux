// SPDX-License-Identifier: GPL-2.0
/*
 * FastRPC test client for the AP (apps processor) side.
 *
 * Exercises the upstream drivers/misc/fastrpc.c UAPI end to end
 * against a remote DSP running the fastrpc_device handler and the
 * fastrpc_dispatcher daemon: attaches to the root protection domain,
 * then invokes the dispatcher's test handle (echo and add methods)
 * with copy-based buffer arguments.
 *
 * Build:
 *   aarch64-linux-gnu-gcc -static -O2 -o fastrpc_test \
 *       -I include/uapi tools/fastrpc/fastrpc_test.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/types.h>
#include <misc/fastrpc.h>

#define FASTRPC_DEV	"/dev/fastrpc-cdsp"

#define TEST_HANDLE	3
#define METHOD_ECHO	0
#define METHOD_ADD	1

#define SCALARS(method, in, out) \
	(((method) & 0x1f) << 24 | ((in) & 0xff) << 16 | ((out) & 0xff) << 8)

static int invoke(int fd, uint32_t handle, uint32_t sc,
		  struct fastrpc_invoke_args *args)
{
	struct fastrpc_invoke inv = {
		.handle = handle,
		.sc = sc,
		.args = (uint64_t)(uintptr_t)args,
	};

	return ioctl(fd, FASTRPC_IOCTL_INVOKE, &inv);
}

static int test_add(int fd)
{
	uint32_t in[2] = { 41000, 1042 };
	uint32_t out = 0;
	struct fastrpc_invoke_args args[2] = {
		{ .ptr = (uint64_t)(uintptr_t)in,  .length = sizeof(in),  .fd = -1 },
		{ .ptr = (uint64_t)(uintptr_t)&out, .length = sizeof(out), .fd = -1 },
	};

	if (invoke(fd, TEST_HANDLE, SCALARS(METHOD_ADD, 1, 1), args)) {
		perror("FASTRPC_IOCTL_INVOKE (add)");
		return 1;
	}

	printf("fastrpc_test: add(%u, %u) = %u\n", in[0], in[1], out);
	if (out != in[0] + in[1]) {
		fprintf(stderr, "fastrpc_test: ADD FAILED (expected %u)\n",
			in[0] + in[1]);
		return 1;
	}
	return 0;
}

static int test_echo(int fd)
{
	const char msg[] = "hello from the apps processor";
	char reply[sizeof(msg)] = { 0 };
	struct fastrpc_invoke_args args[2] = {
		{ .ptr = (uint64_t)(uintptr_t)msg,   .length = sizeof(msg),   .fd = -1 },
		{ .ptr = (uint64_t)(uintptr_t)reply, .length = sizeof(reply), .fd = -1 },
	};

	if (invoke(fd, TEST_HANDLE, SCALARS(METHOD_ECHO, 1, 1), args)) {
		perror("FASTRPC_IOCTL_INVOKE (echo)");
		return 1;
	}

	printf("fastrpc_test: echo -> \"%s\"\n", reply);
	if (memcmp(msg, reply, sizeof(msg))) {
		fprintf(stderr, "fastrpc_test: ECHO FAILED\n");
		return 1;
	}
	return 0;
}

int main(void)
{
	int fd, failed = 0;

	fd = open(FASTRPC_DEV, O_RDWR);
	if (fd < 0) {
		perror("open " FASTRPC_DEV);
		return 1;
	}

	if (ioctl(fd, FASTRPC_IOCTL_INIT_ATTACH)) {
		perror("FASTRPC_IOCTL_INIT_ATTACH");
		close(fd);
		return 1;
	}
	printf("fastrpc_test: attached to root PD\n");

	failed |= test_add(fd);
	failed |= test_echo(fd);

	printf(failed ? "fastrpc_test: FAIL\n" : "fastrpc_test: PASS\n");
	close(fd);
	return failed;
}
