
#include "csp.h"
#include "ztimer.h"
#include "timex.h"

static constexpr auto csp_measure_count = 100;
static constexpr auto chan_measure_count = 100;
static ztimer_now_t inc_cost = 0;

#define time_dispatch(test, total, measure_count, task_label) \
a = ztimer_now(ZTIMER_USEC); \
for (i = 0; i != measure_count; ++i) { (test); } \
b = ztimer_now(ZTIMER_USEC); \
t = (b - a) - (inc_cost * measure_count); \
total += t; \
printf( "%s " task_label " took %s s, avg %s\n", __func__, \
	   timex_to_str(timex_from_uint64(t), buf), \
	   timex_to_str(timex_from_uint64(t / measure_count), buf2) \
	   )

void *task_func(void*)
{ return nullptr; }
static void *static_task_func(void*)
{ return nullptr; }

void *csp_func(void*, channel*)
{ return nullptr; }
static void *static_csp_func(void *, channel *)
{ return nullptr; }

void *func(void *, channel *restrict c)
{
	char buf[TIMEX_MAX_STR_LEN] = {0};
	char buf2[TIMEX_MAX_STR_LEN] = {0};
	char fits[CHANNEL_BUFSIZE - sizeof (size_t)] = {0};
	char unfits[2 * CHANNEL_BUFSIZE] = {0};

	volatile int i = 0;
	volatile ztimer_now_t a = 0;
	volatile ztimer_now_t b = 0;
	ztimer_now_t t = 0;

	time_dispatch(channel_recv(c, fits), (int){0}, chan_measure_count, "channel_recv");

	channel_recv(c, nullptr); // Sync

	time_dispatch(channel_recv(c, unfits), (int){0}, chan_measure_count, "channel_recv");

	channel_recv(c, nullptr); // Sync

	puts("func finished");
	return nullptr;
}

static inline void populate_buf(register const size_t bufsz, char buf[static const restrict bufsz])
{
	for (size_t i = 0; i != bufsz; ++i) { buf[i] = ('a' + (char)i); }
}

MAYBE_UNUSED
static char stack[THREAD_STACKSIZE_MAIN] = {0};
int main(void) {
	char buf[TIMEX_MAX_STR_LEN] = {0};
	char buf2[TIMEX_MAX_STR_LEN] = {0};

	volatile int i = 0;
	volatile ztimer_now_t inc = ztimer_now(ZTIMER_USEC);
	for (i = 0; i != 1000; ++i) {}
	inc_cost = (ztimer_now(ZTIMER_USEC) - inc) / 1000;


	char fits[CHANNEL_BUFSIZE - sizeof (size_t)] = {0};
	populate_buf(sizeof (fits), fits);
	char unfits[2 * CHANNEL_BUFSIZE] = {0};
	populate_buf(sizeof (unfits), unfits);
	volatile ztimer_now_t channel_total = 0;
	volatile ztimer_now_t csp_total = 0;
	volatile ztimer_now_t thread_total = 0;
	volatile ztimer_now_t a = 0;
	volatile ztimer_now_t b = 0;
	ztimer_now_t t = 0;


	printf("%s counting %d CSP dispatches\n", __func__, csp_measure_count);
	{
		// time_dispatch(thread_create(stack, sizeof (stack), THREAD_PRIORITY_MAIN, 0, task_func, nullptr, ""), thread_total, csp_measure_count, "thread_task_func()");
		// time_dispatch(thread_create(stack, sizeof (stack), THREAD_PRIORITY_MAIN, 0, static_task_func, nullptr, ""), thread_total, csp_measure_count, "static_thread_task_func()");
		time_dispatch(GO(task_func), csp_total, csp_measure_count, "task_func()");
		time_dispatch(GO(static_task_func), csp_total, csp_measure_count, "static_task_func()");
		time_dispatch(GO(csp_func), csp_total, csp_measure_count, "csp_func()");
		time_dispatch(GO(static_csp_func), csp_total, csp_measure_count, "static_csp_func()");
	}
	puts("");

	printf("%s testing %d channel send/recv\n", __func__, chan_measure_count);

	printf("%s testing local unbuffered channel\n", __func__);
	{
		channel local_unbuffered = channel_make(&local_unbuffered, 0);
		GO(func, nullptr, &local_unbuffered);

		time_dispatch(
			channel_send(&local_unbuffered, fits, sizeof (fits)),
			channel_total,
			chan_measure_count,
			"local_unbuffered channel_send fits"
		);

		channel_send(&local_unbuffered, nullptr, 0); // sync

		time_dispatch(
			channel_send(&local_unbuffered, unfits, sizeof (unfits)),
			channel_total,
			chan_measure_count,
			"local_unbuffered channel_send unfits"
		);

		channel_send(&local_unbuffered, nullptr, 0); // sync
		channel_close(&local_unbuffered);
		puts("local_unbuffered finished");
	}
	puts("");

	printf("%s testing static unbuffered channel\n", __func__);
	{
		static channel static_unbuffered = {0};
		static_unbuffered = channel_make(&static_unbuffered, 0);
		GO(func, nullptr, &static_unbuffered);

		time_dispatch(
			channel_send(&static_unbuffered, fits, sizeof (fits)),
			channel_total,
			chan_measure_count,
			"static_unbuffered channel_send fits"
		);

		channel_send(&static_unbuffered, nullptr, 0); // sync

		time_dispatch(
			channel_send(&static_unbuffered, unfits, sizeof (unfits)),
			channel_total,
			chan_measure_count,
			"static_unbuffered channel_send unfits"
		);

		channel_send(&static_unbuffered, nullptr, 0); // sync
		channel_close(&static_unbuffered);
		puts("static_unbuffered finished");
	}
	puts("");
	printf("%s testing local buffered channel\n", __func__);
	{
		channel local_buffered = channel_make(&local_buffered, 1);
		GO(func, nullptr, &local_buffered);

		time_dispatch(
			channel_send(&local_buffered, fits, sizeof (fits)),
			channel_total,
			chan_measure_count,
			"local_buffered channel_send fits"
		);

		channel_send(&local_buffered, nullptr, 0);

		time_dispatch(
			channel_send(&local_buffered, unfits, sizeof (unfits)),
			channel_total,
			chan_measure_count,
			"local_buffered channel_send unfits"
		);

		channel_send(&local_buffered, nullptr, 0); // sync

		channel_close(&local_buffered);
		puts("local_buffered finished");
	}
	puts("");

	printf("%s testing static buffered channel\n", __func__);
	{
		static channel static_buffered = {0};
		static_buffered = channel_make(&static_buffered, 1);
		GO(func, nullptr, &static_buffered);

		time_dispatch(
			channel_send(&static_buffered, fits, sizeof (fits)),
			channel_total,
			chan_measure_count,
			"static_buffered channel_send fits"
		);

		channel_send(&static_buffered, nullptr, 0); // sync

		time_dispatch(
			channel_send(&static_buffered, unfits, sizeof (unfits)),
			channel_total,
			chan_measure_count,
			"static_buffered channel_send unfits"
		);

		channel_send(&static_buffered, nullptr, 0); // sync

		channel_close(&static_buffered);
		puts("static_buffered finished");
	}
	puts("");
	printf(
		"%s channel total time %s\n",
		__func__,
		timex_to_str(timex_from_uint64(channel_total), buf)
	);
	printf(
		"%s cspfunc total time %s\n",
		__func__,
		timex_to_str(timex_from_uint64(csp_total), buf)
	);
	printf(
		"%s thread total time %s\n",
		__func__,
		timex_to_str(timex_from_uint64(thread_total), buf)
	);

	return 0;
}
