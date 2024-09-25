
#include "csp.h"
#include <stdio.h>

#if __STDC_VERSION__ > 201710L
#else
typedef void* nullptr_t;
#define nullptr (nullptr_t)0
#endif

static void *csp_print(void *args, void *c) {
	const char *arg = args;
	puts(arg);
	char buf[20] = {0};
	puts("Trying to recv");
	channel_recv(c, buf);
	puts(buf);

	puts("Synchronize csp_print");
	channel_recv(c, nullptr);
	return nullptr;
}

static void *hello_world(void *args)
{
	if (args) puts(args);
	return nullptr;
}

extern int fgetc(FILE *);
extern void exit(int);
int main(void) {
	channel c = channel_make(&c, true); // Buffered channel
	const char str[] = "hello world!";
	const size_t str_size = sizeof(str);
	channel_send(&c, str, str_size);

	/* EXAMPLE: MANUALLY CREATING CSP while getting a pointer to the context. */
	MAYBE_UNUSED
	csp_ctx *p = csp(csp_print, &c, "test");
	MAYBE_UNUSED
	csp_ctx *p2 = csp(hello_world, nullptr, "yippeee");

	/* EXAMPLE: Using the GO macro.*/
	GO(hello_world);
	GO(hello_world, "test");
	GO(hello_world, "test2", &c);
	GO(csp_print, "kek", &c);

	puts("Synchronize main");
	channel_send(&c, nullptr, 0); // Synchronize.
	puts("End of main.");
	exit(0);
	return 0;
}
