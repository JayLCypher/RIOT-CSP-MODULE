/*
 * Copyright (C) 2014 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     examples
 * @{
 *
 * @file
 * @brief       CSP IPC pingpong application
 *
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Jonathan L. Claudius <jcl005@uit.no> (CSP/Channel usage)
 *
 * @}
 */

#include "csp.h"
#include "timex.h"
#include "ztimer.h"

#define ENABLE_DEBUG 0
#include "debug.h"

#include <stdio.h>

#if __STDC_VERSION__ > 201710L
#else
typedef nullptr_t void*;
#define nullptr (nullptr_t)0
#endif

#define PINGPONG_MULTIPLIER 4
static constexpr auto max_pingpong = (UINT16_MAX-1) * PINGPONG_MULTIPLIER;

static void *second_thread(void *arg)
{
	(void) arg;

	DEBUG("2nd thread started, pid: %" PRIkernel_pid "\n", thread_getpid());
	msg_t m = {0};

	while (1) {
		msg_receive(&m);
		DEBUG("2nd: Got msg from %" PRIkernel_pid " with value %u\n", m.sender_pid, m.content.value);
		++m.content.value;
		msg_reply(&m, &m);
		if (m.content.value == max_pingpong) { break; }
	}

	return nullptr;
}

char second_thread_stack[THREAD_STACKSIZE_MAIN];

static void *second_csp(void *arg)
{
	kernel_pid_t my_pid = thread_getpid();
	DEBUG("2nd csp thread started, pid: %" PRIkernel_pid " and arg %p\n", my_pid, arg);
	channel *const c = csp_get_channel(arg);

	msg_t m = {my_pid, 0, {.value = 0}};

	while (1) {
		if (channel_recv(c, &m) == 0) { break; }
		DEBUG("2nd: Got msg from %" PRIkernel_pid " with value: %u\n", m.sender_pid, (unsigned)m.content.value);
		m.sender_pid = my_pid;
		++m.content.value;
		if (channel_send(c, &m, sizeof (m)) == 0) { break; }
		if (m.content.value == max_pingpong) { break; }
	}

	channel_close(c);
	DEBUG_PUTS("Second csp: Ded");
	return nullptr;
}

static void print_result(const char label[static const restrict 1], const size_t timestamp, const size_t avg_count);
extern void exit(int);
int main(void)
{
	puts("Starting IPC Ping-pong measuring example...");
	kernel_pid_t my_pid = thread_getpid();
	DEBUG("1st thread started, pid: %" PRIkernel_pid " and arg ", my_pid);

	volatile ztimer_now_t thread_time[2] = {0};
	volatile ztimer_now_t csp_time[2] = {0};

	// THREAD
	static char stack[THREAD_STACKSIZE_SMALL] = {0};
	thread_time[0] = ztimer_now(ZTIMER_USEC);
	{
		kernel_pid_t pid =
			thread_create(stack, sizeof (stack) / sizeof (*stack), THREAD_PRIORITY_MAIN - 1, 0, second_thread, nullptr, "THR_0");
		msg_t m = {.content.value = 1};
		while (1) {
			msg_send_receive(&m, &m, pid);
			DEBUG("1st: Got msg with content %u\n", m.content.value);
			if (m.content.value == max_pingpong) { break; }
		}
	}
	thread_time[1] = ztimer_now(ZTIMER_USEC);

	DEBUG("1st csp thread started, pid: %" PRIkernel_pid " and arg ", my_pid);
	// CSP
	csp_time[0] = ztimer_now(ZTIMER_USEC);
	{

		channel c = channel_make(&c);
		DEBUG("address %p\n", (void*)&c);
		GO(second_csp, &c, "pong");

		msg_t m = {
			.sender_pid = thread_getpid(),
			.content.value = 1,
		};
		// printf("msg size: %zu\n", sizeof (m));
		while (1) {
			if (channel_send(&c, &m, sizeof (m)) == 0) { break; }
			if (channel_recv(&c, &m) == 0) { break; }
			DEBUG("1st: Got msg from %"PRIkernel_pid" with content %u\n", m.sender_pid, (unsigned)m.content.value);
			m.sender_pid = my_pid;
			if (m.content.value == max_pingpong) { break; }
			++m.content.value;
			// Even
		}
		channel_close(&c);

		while (csp_running(&second_csp_ctx)) { thread_yield(); }
	}
	csp_time[1] = ztimer_now(ZTIMER_USEC);

	print_result("Thread:", (thread_time[1] - thread_time[0]), 1);
	print_result("CSP:", (csp_time[1] - csp_time[0]), 1);

	char buf[TIMEX_MAX_STR_LEN] = {0};
	puts("Execution time:");
	printf("    %s\n", timex_to_str(timex_from_uint64((csp_time[1] - csp_time[0]) + (thread_time[1] - thread_time[0])), buf));
	DEBUG_PUTS("Main thread: Ded");
	exit(0);
	return 0;
}

static void print_result(const char label[static const restrict 1], const size_t timestamp, const size_t avg_count)
{
	char buf[TIMEX_MAX_STR_LEN] = {0};
	puts(label);
	printf("    Sum: %s\n", timex_to_str(timex_from_uint64(timestamp), buf));
	printf("    Avg: %s\n", timex_to_str(timex_from_uint64(timestamp/avg_count), buf));
}
