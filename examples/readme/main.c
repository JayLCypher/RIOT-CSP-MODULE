#include "csp.h"

#include <stdio.h>

// thread_task_func_t compatible function prototype
void *hello_world(void *args)
{
	if (args) { puts(args); }
	else { puts("hello_world"); }
	return nullptr;
}

void *hello_channel(void *args, channel *c)
{
	(void)args; // Ignore args for this function.
	const char *s = channel_recv_ptr(c, (char [14]){0});
	puts(s);

	channel_recv(c, nullptr); // Use channel as synchronization.
	return nullptr;
}

// csp_func_t compatible function prototype
void *hello_both(const char *args, channel *channel)
{
	char buf[13] = {0};
	channel_recv(channel, buf);
	puts(args);

	return channel_recv_ptr(channel, nullptr); // Use channel as synchronization aka keep thread alive.
}

int main(void) {
	GO(hello_world); // No arguments will provide nullptr to function.

	// Change from hello_world("string") to GO(hello_world, "string");
	hello_world("Normal function call");
	GO(hello_world, "CSP Function call"); // Single argument passed along as if called like a function.

	// Create a channel.
	channel c = channel_make(&c, 0); // Second argument sets buffering

	GO(hello_channel, nullptr, &c); // Need to pass both parameters. May change in future.
	// Channels are not buffered by default, need to have receiver before sending and vice versa.
    // Each call to send must match a call to recv, or you get deadlock.
	channel_send(&c, "hello_channel", sizeof ("hello_channel"));
	channel_send(&c, nullptr, 0); // Use channels as synchronization.

	GO(hello_both, "Goodbye!", &c);
	channel_send(&c, "hello_world!", sizeof ("hello_world!"));

	channel_close(&c);
	puts("Main done!");
	return 0;
}
