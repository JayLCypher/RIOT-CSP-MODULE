
#define ENABLE_DEBUG 1
#include "debug.h"
#include "csp.h"

#define THREAD_IDENTIFY_SELF thread_identify(__func__)
static inline void thread_identify(const char *function_name) {
	printf("I am thread %" PRIkernel_pid " doing %s() \n", thread_getpid(), function_name);
}

typedef int (*job_func)(void *);

int task1(MAYBE_UNUSED void *args)
{
	puts(__func__);
	for (int i = 0; i != 1; ++i) {
		printf("%d\n", i);
	}
	return 1;
}

int task2(MAYBE_UNUSED void *args)
{
	puts(__func__);
	for (int i = 0; i != 2; ++i) {
		printf("%d\n", i+i);
	}
	return 2;
}

int task3(MAYBE_UNUSED void *args)
{
	puts(__func__);
	for (int i = 0; i != 3; ++i) {
		printf("%d\n", i+i+i);
	}
	return 3;
}

job_func tasks[] = {
	task1,
	task2,
	task3,
};
static constexpr size_t nTasks = sizeof (tasks) / sizeof (*tasks);

static constexpr size_t nWorkers = 2;
static channel jobs[nWorkers] = {0};
static channel results[nWorkers] = {0};

static csp_ctx *ctxs[nWorkers] = {0};

static void *jobber(void *args, channel **channels)
{
	DEBUG_PUTS(__func__);
	DEBUG("Thread %zu\n", thread_getpid());

	channel *const job_c = channels[0];
	// channel_send(job_c, &(const int){42}, sizeof (const int));
	channel *const result_c = channels[1];
	// channel_send(result_c, &(const int){69}, sizeof (const int));
	DEBUG("%s Channel pointers: %p %p\n", __func__, (void*)job_c, (void*)result_c);

	const size_t jobs_n = *((size_t *){0} = args);
	DEBUG("Jobs: %zu\n", jobs_n);

	channel_send(job_c, &jobs_n, sizeof (jobs_n));

	for (size_t i = 0; i != jobs_n; ++i) {
		job_func job = 0;
		channel_recv(job_c, &job);
		MAYBE_UNUSED
		int retval = job(nullptr);
		channel_send(result_c, &retval, sizeof (retval));
	}
	DEBUG("Thread %" PRIkernel_pid " - %s:%d: Hare\n", thread_getpid(), __func__, __LINE__);
	return nullptr;
}

int main(void) {
	static char stacks[nWorkers][THREAD_STACKSIZE_CSP] = {0};
	for (size_t i = 0; i != nWorkers; ++i) {
		jobs[i] = channel_make(&jobs[i], 1);
		results[i] = channel_make(&results[i], 1);
		ctxs[i] = csp_obj(stacks[i], jobber, (channel*)((channel*[nWorkers]){&jobs[i], &results[i]}), &(size_t){nTasks});
		DEBUG("%s Channel pointers: %p %p\n", __func__, (void*)&jobs[i], (void*)&results[i]);
	}
	// ctxs[0] = GO(jobber, &(size_t){nTasks}, (channel*)((channel*[nWorkers]){&jobs[0], &results[0]}));
	// ctxs[1] = GO(jobber, &(size_t){nTasks}, (channel*)((channel*[nWorkers]){&jobs[1], &results[1]}));

	DEBUG_PUTS("Main");

	size_t nJobs = 0;
	size_t temp = 0;
	channel_recv(&jobs[0], &temp);
	assert(temp == 3);
	nJobs += temp;

	channel_recv(&jobs[1], &temp);
	assert(temp == 3);
	nJobs += temp;

	channel_send(&jobs[0], &tasks[0], sizeof (*tasks));
	channel_send(&jobs[0], &tasks[1], sizeof (*tasks));
	channel_send(&jobs[0], &tasks[2], sizeof (*tasks));

	channel_send(&jobs[1], &tasks[0], sizeof (*tasks));
	channel_send(&jobs[1], &tasks[1], sizeof (*tasks));
	channel_send(&jobs[1], &tasks[2], sizeof (*tasks));

	for (size_t i = 0; i != nJobs; ++i) {
		int retval = -1;
		channel_recv(&results[i % nWorkers], &retval);
		DEBUG("Retval for job %zu: %d\n", i, retval);
	}

	DEBUG_PUTS("Finished");
	return 0;
}
