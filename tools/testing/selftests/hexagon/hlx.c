// SPDX-License-Identifier: GPL-2.0-only
/* SM8975 QEMU HLX smoke test; the baseline assembler has no HLX support. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* Encodings are from QEMU's imported/hlx/encode_ext.def:
 * L3.h = #9; { L2.h = add(L3.h, L3.h); mem(R0) = L2.new; }
 */
static void hlx_init(void)
{
	asm volatile(".word 0xe529e826\n" : : : "memory");
}

static void hlx_init_worker(int worker)
{
	switch (worker) {
	case 0: asm volatile(".word 0xe529e826" : : : "memory"); break;
	case 1: asm volatile(".word 0xe52ae826" : : : "memory"); break;
	case 2: asm volatile(".word 0xe52be826" : : : "memory"); break;
	case 3: asm volatile(".word 0xe52ce826" : : : "memory"); break;
	}
}

static void hlx_add_store(volatile uint16_t *buf)
{
	register uintptr_t r0 asm("r0") = (uintptr_t)buf;

	asm volatile(".word 0xe5066cc4\n"
		     ".word 0xa480c002\n" : "+r"(r0) : : "memory");
}

/* More simultaneous users than the two SM8975 HLX contexts. */
static int test_concurrent(volatile uint16_t *buf)
{
	int ready[2], go[2], i, j, status;
	pid_t children[4];
	char byte = 1;

	if (pipe(ready) || pipe(go))
		return -1;
	for (i = 0; i < 4; i++) {
		children[i] = fork();
		if (children[i] < 0)
			return -1;
		if (!children[i]) {
			volatile uint16_t *slot = buf + (i + 1) * 1024;

			close(ready[0]);
			close(go[1]);
			/* The VTCM mapping is inherited; HLX itself needs no privilege. */
			if (!geteuid() && setuid(65534))
				_exit(1);
			hlx_init_worker(i);
			if (write(ready[1], &byte, 1) != 1 ||
			    read(go[0], &byte, 1) != 1)
				_exit(2);
			for (j = 0; j < 10; j++) {
				int lane;

				sched_yield();
				hlx_add_store(slot);
				for (lane = 0; lane < 1024; lane++)
					if (slot[lane] != 2 * (9 + i))
						_exit(3);
			}
			_exit(0);
		}
	}
	close(ready[1]);
	close(go[0]);
	for (i = 0; i < 4; i++)
		if (read(ready[0], &byte, 1) != 1)
			return -1;
	for (i = 0; i < 4; i++)
		if (write(go[1], &byte, 1) != 1)
			return -1;
	close(ready[0]);
	close(go[1]);
	for (i = 0; i < 4; i++) {
		if (waitpid(children[i], &status, 0) < 0 ||
		    !WIFEXITED(status) || WEXITSTATUS(status)) {
			fprintf(stderr, "HLX worker %d failed: status %#x\n", i, status);
			return -1;
		}
	}
	printf("HLX concurrent PASS: 4 unprivileged tasks, 1024 lanes each\n");
	return 0;
}

int main(void)
{
	volatile uint16_t *buf;
	cpu_set_t cpus;
	int fd, cpu, i, tested = 0;

	/* The first instruction requests a context by taking a fault. */
	hlx_init();
	fd = open("/dev/uio0", O_RDWR);
	if (fd < 0) {
		perror("open VTCM");
		return 1;
	}
	buf = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED) {
		perror("map VTCM");
		return 1;
	}
	close(fd);
	if (sched_getaffinity(0, sizeof(cpus), &cpus)) {
		perror("get CPU affinity");
		return 1;
	}
	/* Keep L3 live across each affinity change and scheduler migration. */
	hlx_init();
	for (cpu = 0; cpu < CPU_SETSIZE; cpu++) {
		cpu_set_t one;

		if (!CPU_ISSET(cpu, &cpus))
			continue;
		printf("HLX checking CPU %d\n", cpu);
		fflush(stdout);
		CPU_ZERO(&one);
		CPU_SET(cpu, &one);
		if (sched_setaffinity(0, sizeof(one), &one)) {
			perror("set CPU affinity");
			return 1;
		}
		memset((void *)buf, 0, 2048);
		hlx_add_store(buf);
		for (i = 0; i < 1024; i++) {
			if (buf[i] != 18) {
				fprintf(stderr, "CPU %d lane %d: got %u, expected 18\n",
					cpu, i, buf[i]);
				return 1;
			}
		}
		tested++;
	}
	if (sched_setaffinity(0, sizeof(cpus), &cpus) || test_concurrent(buf))
		return 1;
	munmap((void *)buf, 65536);
	printf("HLX PASS: 1024 lanes on %d CPUs\n", tested);
	return tested ? 0 : 1;
}
