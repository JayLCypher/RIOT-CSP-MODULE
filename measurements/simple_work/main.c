
#define ENABLE_DEBUG 0
#include "debug.h"
#include "csp.h"
#include "timex.h"
#include "ztimer.h"

/*
 * Simple sending/receiving messages example used for measuring performance.
 * The performance is measured against thread messages built into the threads and the channel interface provided by CSP.
 *
 */

#define WORK_COUNT (UINT16_MAX * 1)

static void *work_thread(void *args)
{
	(void) args;
	msg_t m = {0};

	while (1) {
		msg_receive(&m);
		DEBUG("Thread %"PRIkernel_pid " got msg  %u from %" PRIkernel_pid "\n", thread_getpid(), m.content.value, m.sender_pid);
		if (m.content.value == WORK_COUNT-1) { break; }
	}

	DEBUG("Thread %" PRIkernel_pid " is finished.\n", thread_getpid());
	return nullptr;
}

MAYBE_UNUSED
static ztimer_now_t wrapper_thread1(void)
{
	static char stack[THREAD_STACKSIZE_MINIMUM] = {0};
	msg_t m = {0};

	// Test thread creation time
	volatile ztimer_now_t before = ztimer_now(ZTIMER_USEC);
	kernel_pid_t thread_pid = thread_create(stack, sizeof (stack), THREAD_PRIORITY_MAIN - 1, 0, work_thread, nullptr, "THREAD_MEASURE");
	for (volatile unsigned i = 0; i != WORK_COUNT; ++i) {
		m.content.value = i;
		msg_send(&m, thread_pid);
		DEBUG("Thread %"PRIkernel_pid " sent msg %u from %" PRIkernel_pid "\n", thread_getpid(), m.content.value, m.sender_pid);
	}
	while (thread_getstatus(thread_pid) != (STATUS_STOPPED | STATUS_NOT_FOUND)) { thread_yield(); }
	volatile ztimer_now_t after = ztimer_now(ZTIMER_USEC);

	DEBUG("Thread %" PRIkernel_pid " is finished\n", thread_getpid());
	return after - before;
}

MAYBE_UNUSED
static ztimer_now_t wrapper_thread2(void)
{
	static char stack[THREAD_STACKSIZE_MINIMUM] = {0};
	msg_t m = {0};

	// Tests pre-creating a thread and waking it up before execution instead.
	kernel_pid_t thread_pid = thread_create(stack, sizeof (stack), THREAD_PRIORITY_MAIN - 1, THREAD_CREATE_SLEEPING, work_thread, nullptr, "THREAD_MEASURE");
	volatile ztimer_now_t before = ztimer_now(ZTIMER_USEC);
	thread_wakeup(thread_pid);
	for (volatile unsigned i = 0; i != WORK_COUNT; ++i) {
		m.content.value = i;
		msg_send(&m, thread_pid);
		DEBUG("Thread %"PRIkernel_pid " sent msg %u from %" PRIkernel_pid "\n", thread_getpid(), m.content.value, m.sender_pid);
	}
	while (thread_getstatus(thread_pid) != (STATUS_STOPPED | STATUS_NOT_FOUND)) { thread_yield(); }
	volatile ztimer_now_t after = ztimer_now(ZTIMER_USEC);

	DEBUG("Thread %" PRIkernel_pid " is finished\n", thread_getpid());
	return after - before;
}

static void *work_csp(void *args)
{
	msg_t m = {0};

	while (1) {
		channel_recv(args, &m);
		DEBUG("Thread %"PRIkernel_pid " got msg  %u from %" PRIkernel_pid "\n", thread_getpid(), m.content.value, m.sender_pid);
		if (m.content.value == WORK_COUNT-1) { break; }
	}

	DEBUG("Thread %" PRIkernel_pid " is finished.\n", thread_getpid());
	return nullptr;
}

MAYBE_UNUSED
static ztimer_now_t wrapper_csp(void)
{
	static channel c = {0};
	c = channel_make(&c, 1);
	msg_t m = {0};
	m.sender_pid = thread_getpid(); // Manually set sender_pid, msg_send does it for you.

	// Includes the thread creation time in here.
	volatile ztimer_now_t before = ztimer_now(ZTIMER_USEC);
	GO(work_csp, &c);
	for (volatile unsigned i = 0; i != WORK_COUNT; ++i) {
		m.content.value = i;
		channel_send(&c, &m, sizeof (m));
		DEBUG("Thread %"PRIkernel_pid " sent msg %u from %" PRIkernel_pid "\n", thread_getpid(), m.content.value, m.sender_pid);
	}
	volatile ztimer_now_t after = ztimer_now(ZTIMER_USEC);

	DEBUG("Thread %" PRIkernel_pid " is finished.\n", thread_getpid());
	return after - before;
}

static void print_result(const char label[static const restrict 1], const ztimer_now_t timestamp);

static constexpr int measure_count = 10;
extern void exit(int);
int main(void) {
	// timex_t thread_timex = {0};
	// timex_t csp_timex = {0};
	char buf[TIMEX_MAX_STR_LEN] = {0};

	ztimer_now_t thread1_sum = 0;
	ztimer_now_t thread2_sum = 0;
	ztimer_now_t csp_sum = 0;

	// Volatile in attempt to force the compiler to not pass barriers with optimizations.
	volatile ztimer_now_t execution_start = ztimer_now(ZTIMER_USEC);
	for (int i = 0; i != measure_count; ++i) {
		thread1_sum += wrapper_thread1();
		thread2_sum += wrapper_thread2();
		csp_sum += wrapper_csp();
	}
	volatile ztimer_now_t execution_end = ztimer_now(ZTIMER_USEC);

	puts("Computation finished. Results:");
	print_result("Thread1:", thread1_sum);
	print_result("Thread2:", thread2_sum);
	print_result("CSP:", csp_sum);

	// puts("Thread1:");
	// printf("    Sum: %s\n", timex_to_str(timex_from_uint64(thread1_sum), buf));
	// printf("    Avg: %s\n", timex_to_str(timex_from_uint64(thread1_sum/measure_count), buf));
	//
	// puts("Thread2:");
	// printf("    Sum: %s\n", timex_to_str(timex_from_uint64(thread2_sum), buf));
	// printf("    Avg: %s\n", timex_to_str(timex_from_uint64(thread2_sum/measure_count), buf));
	//
	// puts(" CSP:");
	// printf("    Sum: %s\n", timex_to_str(timex_from_uint64(csp_sum), buf));
	// printf("    Avg: %s\n", timex_to_str(timex_from_uint64(csp_sum/measure_count), buf));

	puts("Program:");
	printf("    Execution: %s\n", timex_to_str(timex_from_uint64(execution_end - execution_start), buf));
	DEBUG("Thread %" PRIkernel_pid " is finished.\n", thread_getpid());
	exit(0);
	return 0;
}

static void print_result(const char label[static const restrict 1], const ztimer_now_t timestamp)
{
	char buf[TIMEX_MAX_STR_LEN] = {0};
	puts(label);
	printf("    Sum: %s\n", timex_to_str(timex_from_uint64(timestamp), buf));
	printf("    Avg: %s\n", timex_to_str(timex_from_uint64(timestamp/measure_count), buf));
}
