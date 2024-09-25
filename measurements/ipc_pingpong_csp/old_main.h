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
 * @brief       IPC pingpong application
 *
 * @author      Kaspar Schleiser <kaspar@schleiser.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 *
 * @}
 */

#include "csp.h"

#include <stdio.h>

#if __STDC_VERSION__ > 201710L
#else
typedef nullptr_t void*;
#define nullptr (nullptr_t)0
#endif

void *second_thread(void *arg)
{
	kernel_pid_t my_pid = thread_getpid();
    printf("2nd thread started, pid: %" PRIkernel_pid " and arg %p\n", my_pid, arg);
	if (!arg) { return nullptr; }
    channel *c = arg;

    msg_t m = {0};

    while (1) {
		m.sender_pid = my_pid;
        m.content.value++;
		if (channel_send(c, &m, sizeof (m)) == 0) { break; }
		if (channel_recv(c, &m) == 0) { break; }
        printf("2nd: Got msg from %" PRIkernel_pid " with value: %u\n", m.sender_pid, m.content.value);
    }

	channel_close(c);
	thread_zombify();
	puts("Second thread: Ded");
    return nullptr;
}

char second_thread_stack[THREAD_STACKSIZE_MAIN] = {0};

int main(void)
{
    printf("Starting IPC Ping-pong example...\n");
	kernel_pid_t my_pid = thread_getpid();
    printf("1st thread started, pid: %" PRIkernel_pid " and arg ", my_pid);

	channel c = channel_make(&c);
	printf("%p\n", (void*)&c);
    kernel_pid_t pid = thread_create(second_thread_stack, sizeof (second_thread_stack),
                            THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_STACKTEST,
                            second_thread, &c, "pong");

    msg_t m = {
		.sender_pid = thread_getpid(),
		.content.value = 1,
	};
	// printf("msg size: %zu\n", sizeof (m));
    while (1) {
		if (channel_send(&c, &m, sizeof (m)) == 0) { break; }
		if (channel_recv(&c, &m) == 0) { break; }
        printf("1st: Got msg from %"PRIkernel_pid" with content %u\n", m.sender_pid, m.content.value);
		++m.content.value;
		m.sender_pid = my_pid;
    }
	channel_close(&c);
	while (thread_kill_zombie(pid) != 1) { thread_yield(); }
	puts("Main thread: Ded");
	return 0;
}
