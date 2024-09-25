/*
 * Copyright (C) 2020 TUBA Freiberg
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include "csp.h"
#include "ztimer.h"
#include "timex.h"
#include "sched_round_robin.h"

#define ENABLE_DEBUG 0
#include "debug.h"

#include <stdio.h>
#include <stdint.h>

#define PRINT_STEPS 10
#define WORK_SCALE  1000
#define STEPS_PER_SET 10

#ifndef MAYBE_UNUSED
#if __STDC_VERSION__ > 201710L
#define MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
#define MAYBE_UNUSED __attribute__((unused))
#else
#define MAYBE_UNUSED
#endif
#endif

MAYBE_UNUSED
static void print_result(const char label[static const restrict 1], const size_t timestamp, const size_t count);

MAYBE_UNUSED
static void bad_wait(uint32_t us)
{
    /* keep the CPU busy waiting for some time to pass simulate working */
    ztimer_spin(ZTIMER_USEC, us);
}

static void (* const do_work)(uint32_t us) = bad_wait;

MAYBE_UNUSED
static void nice_wait(uint32_t us)
{
    /* be nice give the CPU some time to do other things or rest */
    ztimer_sleep(ZTIMER_USEC, us);
}

MAYBE_UNUSED
static void yield_wait(uint32_t unused)
{
    (void) unused;
    /* do not wait just yield */
    thread_yield();
}

MAYBE_UNUSED
static void no_wait(uint32_t unused)
{
    (void) unused;
    /* do not wait */
}

/* worker_config is a small configuration structure for the thread_worker */
struct worker_config {
    void (*waitfn)(uint32_t);  /**< the resting strategy */
    uint32_t workload;         /**< the amount of work to do per set */
};

/*
 * the following are threads that count and wait with different strategies and
 * print their current count in steps.
 * the ration of active (doing hard work like checking the timer)
 * to passive (wait to be informed when a certain time is there) waiting
 * is determined by there value given to the thread.
 * no_wait and yield_wait threads are restless an therefore never pause.
 */

static constexpr unsigned measure_count = 1 * WORK_SCALE;
static void worker(struct worker_config *wc) {
#ifdef DEVELHELP
    const char *name = thread_get_active()->name;
#else
    int16_t pid = thread_getpid();
#endif
	uint32_t w = 0;
    /* Each set consists of STEPS_PER_SET steps which are divided into work (busy waiting)
     * and resting.
     * E.g. if there are 10 steps per set, the maximum workload is 10, which means no rest.
     * If the given value is out of range work ratio is set to half of STEPS_PER_SET */
    uint32_t work = wc->workload;
    if (work > STEPS_PER_SET) {
        work = STEPS_PER_SET / 2;
    }
    uint32_t rest = (STEPS_PER_SET - work);
    uint32_t step = 0;

    /* work some time and rest */
    for (;w != measure_count;) {
        if (w - step >= PRINT_STEPS) {
#ifdef DEVELHELP
            DEBUG("%s: %" PRIu32 ", %" PRIu32 "\n", name, w, work);
#else
            DEBUG("T-Pid %i:%" PRIu32 ", %" PRIu32 "\n", pid, w, work);
#endif
            step = w;
        }
        do_work(work * WORK_SCALE);
		w += work;
        wc->waitfn(rest * WORK_SCALE);
    }
}

#include "irq.h"
static auto worker_count = 6;
static void finish_work(void)
{
	const unsigned state = irq_disable();
	--worker_count;
	irq_restore(state);
}

MAYBE_UNUSED
static void * thread_worker(void * d)
{
	volatile ztimer_now_t time[2] = {0};
	nice_wait(200 *  US_PER_MS);  /* always be nice at start */

	time[0] = ztimer_now(ZTIMER_USEC);

	struct worker_config *wc = d;
	worker(wc);

	time[1] = ztimer_now(ZTIMER_USEC);

	finish_work();

	print_result("THREAD:", time[1] - time[0], 0);
	return nullptr;
}

MAYBE_UNUSED
static void * csp_worker(void * args)
{
	volatile ztimer_now_t time[2] = {0};
	nice_wait(200 *  US_PER_MS);  /* always be nice at start */

	time[0] = ztimer_now(ZTIMER_USEC);

	struct worker_config wc = {0};
	channel_recv(args, &wc);
	worker(&wc);

	time[1] = ztimer_now(ZTIMER_USEC);

	finish_work();

	print_result("CSP:", time[1] - time[0], 0);
	return nullptr;
}

/*
 * nice_wait ->  a thread does nice breaks giving other threads time to do something
 * bad_wait ->   a thread that waits by spinning (intensely looking at the clock)
 * yield_wait -> a restless thread that yields before continuing with the next work package
 * no_wait ->    a restless thread always working until it is preempted
 */
/* yield_wait and nice_wait threads are able to work in "parallel" without sched_round_robin */

#ifndef  THREAD_1
#define  THREAD_1 {no_wait, 5}
#endif

#ifndef  THREAD_2
#define  THREAD_2 {no_wait, 5}
#endif

#ifndef  THREAD_3
#define  THREAD_3 {no_wait, 5}
#endif

/*a TINY Stack should be enough*/
#ifndef WORKER_STACKSIZE
#define WORKER_STACKSIZE (THREAD_STACKSIZE_TINY+THREAD_EXTRA_STACKSIZE_PRINTF)
#endif

extern void exit(int);
int main(void)
{
	// THREADS
	volatile ztimer_now_t a = 0;
	volatile ztimer_now_t b = 0;
	ztimer_now_t thread_total = 0;
    {
		static struct worker_config wc = THREAD_2;   /* 0-10 workness */
		a = ztimer_now(ZTIMER_USEC);
        static char stack[WORKER_STACKSIZE] = {0};
        thread_create(stack, sizeof (stack), 7, 0, thread_worker, &wc, "T1");
		b = ztimer_now(ZTIMER_USEC);
		thread_total += (b - a);
    }
    {
		static struct worker_config wc = THREAD_2;   /* 0-10 workness */
		a = ztimer_now(ZTIMER_USEC);
        static char stack[WORKER_STACKSIZE] = {0};
        thread_create(stack, sizeof (stack), 7, 0, thread_worker, &wc, "T2");
		b = ztimer_now(ZTIMER_USEC);
		thread_total += (b - a);
    }
    {
		static struct worker_config wc = THREAD_3;   /* 0-10 workness */
		a = ztimer_now(ZTIMER_USEC);
        static char stack[WORKER_STACKSIZE] = {0};
        thread_create(stack, sizeof (stack), 7, 0, thread_worker, &wc, "T3");
		b = ztimer_now(ZTIMER_USEC);
		thread_total += (b - a);
    }

	// CSP
	ztimer_now_t csp_total = 0;
	{
		static struct worker_config wc = THREAD_1;   /* 0-10 workness */
		// static channel c = {0};
		// c = channel_make(&c, 0);
		a = ztimer_now(ZTIMER_USEC);
		// GO(csp_worker, &c);
		GO(thread_worker, &wc);
		b = ztimer_now(ZTIMER_USEC);
		csp_total += (b - a);
		// channel_send(&c, &wc, sizeof (wc));
	}
	{
		static struct worker_config wc = THREAD_2;   /* 0-10 workness */
		// static channel c = {0};
		// c = channel_make(&c, 0);
		a = ztimer_now(ZTIMER_USEC);
		// GO(csp_worker, &c);
		GO(thread_worker, &wc);
		b = ztimer_now(ZTIMER_USEC);
		csp_total += (b - a);
		// channel_send(&c, &wc, sizeof (wc));
	}
	{
		static struct worker_config wc = THREAD_3;   /* 0-10 workness */
		// static channel c = {0};
		// c = channel_make(&c, 0);
		a = ztimer_now(ZTIMER_USEC);
		// GO(csp_worker, &c);
		GO(thread_worker, &wc);
		b = ztimer_now(ZTIMER_USEC);
		csp_total += (b - a);
		// channel_send(&c, &wc, sizeof (wc));
	}

	while (worker_count != 0) { thread_yield(); }

	puts("");
	print_result("thread_total:", thread_total, 3);
	print_result("csp_total:", csp_total, 3);
	puts("Finished.");
	exit(0);
	return 0;
}

static void print_result(const char label[static const restrict 1], const size_t timestamp, const size_t count)
{
	char buf[TIMEX_MAX_STR_LEN] = {0};
	puts(label);
	printf("    Sum: %s\n", timex_to_str(timex_from_uint64(timestamp), buf));
	if (count) { printf("    Avg: %s\n", timex_to_str(timex_from_uint64(timestamp/count), buf)); }
}
