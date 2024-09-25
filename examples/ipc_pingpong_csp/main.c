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
 * @author      Jonathan L. Claudius <jcl005@uit.no> (CSP/Channel module conversion)
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

void *second_thread(void *, channel *c)
{
	printf("2nd thread started, pid: %" PRIkernel_pid "\n", thread_getpid());

	msg_t m = {0};

	while (1) {
		if (channel_recv(c, &m) == 0) { break; }
		m.sender_pid = thread_getpid(); m.content.value++;
		if (channel_send(c, &m, sizeof (m)) == 0) { break; }
		printf("2nd: Got msg from %" PRIkernel_pid " with value: %u\n", m.sender_pid, (unsigned)m.content.value);
	}

	channel_recv(c, nullptr); // Synchronize
	channel_close(c);
	return nullptr;
}

int main(void)
{
	printf("Starting IPC Ping-pong example...\n");
	printf("1st thread started, pid: %" PRIkernel_pid " and arg ", thread_getpid());

	channel c = channel_make(&c, 0);

	// How to manually set a local variable pre-C23:
	// static csp_ctx second = {0};
	// second = *csp(&second, second_thread, &c, "pong");

	// How to use the GO macro pre-C23
	// GO(second_thread, &c, "pong");
	// now you have a second_thread_ctxN available, where N is the counter for calling the GO macro.

	GO(second_thread, "pong", &c);

	msg_t m = {
		.sender_pid = thread_getpid(),
		.content.value = 1,
	};

	while (1) {
		if (channel_send(&c, &m, sizeof (m)) == 0) { break; }
		if (channel_recv(&c, &m) == 0) { break; }
		printf("1st: Got msg from %"PRIkernel_pid" with content %u\n", m.sender_pid, (unsigned)m.content.value);
		++m.content.value;
		m.sender_pid = thread_getpid();
	}

	channel_send(&c, nullptr, 0);
	channel_close(&c);
	return 0;
}
