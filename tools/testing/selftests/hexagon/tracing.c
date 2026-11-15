// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include "../kselftest.h"

#ifndef __hexagon__
int main(void)
{
	ksft_exit_skip("Hexagon only\n");
}
#else
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern int probe_entry(int value);
extern void probe_packet(int *out, int value);
extern int probe_fault(const int *address);
extern void probe_branch(void);
extern void probe_pc(void);
extern void probe_loop(void);
extern const char probe_middle[];

/* Explicit packets make packet relocation and mid-packet rejection testable. */
asm(".text\n"
	".p2align 4\n"
	".global probe_entry\n"
	".type probe_entry,@function\n"
	"probe_entry:\n"
	"{ allocframe(#0) }\n"
	"{ r0 = add(r0,#7) }\n"
	"{ dealloc_return }\n"
	".size probe_entry, .-probe_entry\n"
	".global probe_packet\n"
	".type probe_packet,@function\n"
	"probe_packet:\n"
	"{ r2 = add(r1,#1); r3 = add(r1,#2);\n"
	"r4 = add(r1,#3); r5 = add(r1,#4) }\n"
	".global probe_middle\n"
	".set probe_middle, probe_packet+4\n"
	"{ memw(r0+#0) = r2; memw(r0+#4) = r3 }\n"
	"{ memw(r0+#8) = r4; memw(r0+#12) = r5 }\n"
	"{ jumpr r31 }\n"
	".size probe_packet, .-probe_packet\n"
	".global probe_fault\n"
	".type probe_fault,@function\n"
	"probe_fault:\n"
	"{ r0 = memw(r0) }\n"
	"{ jumpr r31 }\n"
	".size probe_fault, .-probe_fault\n"
	".global probe_branch\n"
	".type probe_branch,@function\n"
	"probe_branch:\n"
	"{ jumpr r31 }\n"
	".size probe_branch, .-probe_branch\n"
	".global probe_pc\n"
	"probe_pc:\n"
	"{ r0 = add(pc,##0) }\n"
	"{ jumpr r31 }\n"
	".global probe_loop\n"
	"probe_loop:\n"
	"{ nop; nop }:endloop0\n"
	"{ jumpr r31 }\n");

static char instance[256], group[64], executable[4096];
static const char * const names[] = { "entry", "ret", "packet", "fault", "bad" };
static sigjmp_buf fault_env;
static volatile sig_atomic_t fault_pc_ok;
static volatile sig_atomic_t signal_count, signal_ok = 1;

static int write_control(const char *path, const char *value)
{
	size_t len = strlen(value);
	int fd = open(path, O_WRONLY | O_APPEND);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = write(fd, value, len);
	close(fd);
	return n == (ssize_t)len ? 0 : -1;
}

static int control(const char *file, const char *value)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", instance, file);
	return write_control(path, value);
}

static char *read_trace(void)
{
	char path[512];
	char *buf = calloc(1, 1024 * 1024);
	int fd;
	ssize_t n;
	size_t used = 0;

	if (!buf)
		ksft_exit_fail_msg("allocate trace buffer\n");
	snprintf(path, sizeof(path), "%s/trace", instance);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		ksft_exit_fail_msg("open trace: %s\n", strerror(errno));
	while (used < 1024 * 1024 - 1 &&
	       (n = read(fd, buf + used, 1024 * 1024 - 1 - used)) > 0)
		used += n;
	close(fd);
	return buf;
}

static void cleanup(void)
{
	char path[512], command[128];
	unsigned int i;

	control("tracing_on", "0");
	control("events/enable", "0");
	control("current_tracer", "nop");
	for (i = 0; i < ARRAY_SIZE(names); i++) {
		snprintf(command, sizeof(command), "-:%s/%s\n", group, names[i]);
		write_control("/sys/kernel/tracing/uprobe_events", command);
	}
	snprintf(path, sizeof(path), "%s", instance);
	rmdir(path);
}

static unsigned long file_offset(const void *address)
{
	unsigned long start, end, off, pc = (uintptr_t)address;
	char line[8192];
	FILE *maps = fopen("/proc/self/maps", "r");

	if (!maps)
		ksft_exit_fail_msg("open maps\n");
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %*s %lx", &start, &end, &off) == 3 &&
		    pc >= start && pc < end) {
			fclose(maps);
			return pc - start + off;
		}
	}
	fclose(maps);
	ksft_exit_fail_msg("find executable mapping\n");
}

static int add_probe(char type, const char *name, const void *address)
{
	char command[4608];

	snprintf(command, sizeof(command), "%c:%s/%s %s:0x%lx\n",
		 type, group, name, executable, file_offset(address));
	return write_control("/sys/kernel/tracing/uprobe_events", command);
}

static int rejected_probe(const void *address)
{
	char path[128], command[128];
	int ret = add_probe('p', "bad", address);

	if (!ret) {
		snprintf(path, sizeof(path), "events/%s/bad/enable", group);
		ret = control(path, "1");
		control(path, "0");
		snprintf(command, sizeof(command), "-:%s/bad\n", group);
		write_control("/sys/kernel/tracing/uprobe_events", command);
	}
	return ret < 0;
}

static void segv_handler(int sig, siginfo_t *info, void *context)
{
	ucontext_t *uc = context;

	(void)sig;
	(void)info;
	fault_pc_ok = uc->uc_mcontext.pc == (uintptr_t)probe_fault;
	siglongjmp(fault_env, 1);
}

static void alarm_handler(int sig)
{
	(void)sig;
	signal_count++;
	if (probe_entry(1) != 8)
		signal_ok = 0;
}

int main(int argc, char **argv)
{
	struct sigaction sa = { .sa_sigaction = segv_handler, .sa_flags = SA_SIGINFO };
	struct itimerval timer = { .it_interval.tv_usec = 10000,
				 .it_value.tv_usec = 10000 };
	char path[512], *trace;
	ssize_t len;
	int i, out[4], value = 42, status;
	pid_t child;

	ksft_print_header();
	ksft_set_plan(13);
	if (geteuid())
		ksft_exit_skip("requires root and tracefs at /sys/kernel/tracing\n");
	len = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
	if (len < 0)
		ksft_exit_fail_msg("readlink executable\n");
	executable[len] = 0;
	snprintf(group, sizeof(group), "hexagon_test_%d", getpid());
	snprintf(instance, sizeof(instance), "/sys/kernel/tracing/instances/%s", group);
	if (mkdir(instance, 0700))
		ksft_exit_skip("requires tracing instances: %s\n", strerror(errno));
	atexit(cleanup);
	control("tracing_on", "0");
	control("trace", "\n");
	if (control("current_tracer", "function")) {
		ksft_test_result_skip("function tracer not configured\n");
	} else {
		control("tracing_on", "1");
		syscall(SYS_getpid);
		control("tracing_on", "0");
		trace = read_trace();
		ksft_test_result(strstr(trace, "sys_getpid"), "function tracing\n");
		free(trace);
		control("current_tracer", "nop");
		control("trace", "\n");
	}
	if (!control("set_ftrace_filter", "__hexagon_sys_getpid")) {
		control("current_tracer", "function");
		control("tracing_on", "1");
		syscall(SYS_getpid);
		control("tracing_on", "0");
		trace = read_trace();
		ksft_test_result(strstr(trace, "sys_getpid") &&
				 !strstr(trace, "sys_write"), "dynamic function filtering\n");
		free(trace);
		control("current_tracer", "nop");
		control("set_ftrace_filter", "\n");
		control("trace", "\n");
	} else {
		ksft_test_result_skip("dynamic function tracing not configured\n");
	}
	if (!control("set_ftrace_filter", "__hexagon_sys_getpid") &&
	    !control("current_tracer", "function_graph")) {
		control("tracing_on", "1");
		syscall(SYS_getpid);
		control("tracing_on", "0");
		trace = read_trace();
		ksft_test_result(strstr(trace, "sys_getpid();"),
				 "function graph entry and return\n");
		free(trace);
		control("current_tracer", "nop");
		control("set_ftrace_filter", "\n");
		control("trace", "\n");
	} else {
		ksft_test_result_skip("function graph tracer not configured\n");
	}

	/* Register before the real packet probe so it cannot mask the boundary. */
	ksft_test_result(rejected_probe(probe_middle),
			 "reject a probe inside an instruction packet\n");
	ksft_test_result(rejected_probe(probe_branch),
			 "reject a control-flow packet\n");
	ksft_test_result(rejected_probe(probe_pc), "reject a PC-relative packet\n");
	ksft_test_result(rejected_probe(probe_loop), "reject a hardware-loop ending\n");
	if (add_probe('p', "entry", probe_entry) ||
	    add_probe('r', "ret", probe_entry) ||
	    add_probe('p', "packet", probe_packet) ||
	    add_probe('p', "fault", probe_fault))
		ksft_exit_fail_msg("register probes: %s\n", strerror(errno));
	snprintf(path, sizeof(path), "events/%s/enable", group);
	if (control(path, "1") || control("tracing_on", "1"))
		ksft_exit_fail_msg("enable probes\n");
	status = 1;
	for (i = 0; i < 100; i++) {
		probe_packet(out, i);
		if (probe_entry(i) != i + 7 || out[0] != i + 1 ||
		    out[1] != i + 2 || out[2] != i + 3 || out[3] != i + 4)
			status = 0;
	}
	ksft_test_result(status, "entry/return probes preserve packet results\n");

	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	if (!sigsetjmp(fault_env, 1))
		probe_fault(NULL);
	ksft_test_result(fault_pc_ok && probe_fault(&value) == value,
			 "fault recovery reports original PC and allows later probes\n");

	child = fork();
	if (!child) {
		for (i = 0; i < 100; i++)
			if (probe_entry(i) != i + 7)
				_exit(1);
		_exit(0);
	}
	status = -1;
	if (child > 0)
		waitpid(child, &status, 0);
	ksft_test_result(child > 0 && WIFEXITED(status) && !WEXITSTATUS(status),
			 "fork inherits probes and recreates XOL state\n");
	control("tracing_on", "0");
	trace = read_trace();
	ksft_test_result(strstr(trace, "entry:") && strstr(trace, "ret:") &&
			 strstr(trace, "packet:") && strstr(trace, "fault:"),
			 "all uprobe event types were recorded\n");
	free(trace);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	status = !setitimer(ITIMER_REAL, &timer, NULL);
	for (i = 0; i < 200; i++)
		if (probe_entry(i) != i + 7)
			status = 0;
	memset(&timer, 0, sizeof(timer));
	setitimer(ITIMER_REAL, &timer, NULL);
	ksft_test_result(status && signal_count && signal_ok,
			 "signals and probes in signal handlers preserve execution\n");
	if (argc > 1) {
		int fd = open(argv[1], O_RDONLY);

		status = fd >= 0 && !syscall(SYS_finit_module, fd, "", 0);
		if (fd >= 0)
			close(fd);
		ksft_test_result(status, "kernel/module permissions and IRQ preemption\n");
		if (status)
			syscall(SYS_delete_module, "hexagon_arch_test", O_NONBLOCK);
	} else {
		ksft_test_result_skip("pass hexagon_arch_test.ko to test kernel protection/preemption\n");
	}
	ksft_finished();
}
#endif
