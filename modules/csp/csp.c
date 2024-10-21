/*
 * Copyright (C) 2024
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     module_csp
 * @{
 *
 * @file
 * @brief       CSP implementation
 *				An attempt at encapsulating a user procedure within a RIOT OS thread,
 *				as a sequential process, and an optional channel interface to boot.
 *
 * @author      Jonathan L. Claudius <jcl005@uit.no>
 *
 * @}
 */

#include "csp.h"
//#define ENABLE_DEBUG 0
#include "debug.h"
#include "irq.h"

#include <errno.h>

/* Implementation of the module */

// Helper preprocessor to deal with RIOT assert being crungo
// #if __clang__
// #pragma clang diagnostic push
// #pragma clang diagnostic ignored "-Wsign-conversion"
// #endif
// 	assert(/* assertion */ );
// #if __clang__
// #pragma clang diagnostic pop
// #endif

// Helper stuff to deal with sub_versioning below C23
#if __STDC_VERSION__ > 201710L
#define maybe_unused [[maybe_unused]]
#define PTR_CAST(ptr) ((void*){0} = (typeof_unqual (ptr))(ptr))
#else
#include <stdalign.h>
typedef void* nullptr_t;
#define nullptr (nullptr_t)0
#define maybe_unused
#define PTR_CAST(ptr) ((void*){0} = (ptr))
#endif

/* CHANNELS */

// static inline bool channel_is_file_closed(const channel c[static const restrict 1]) { return c->files[channel_is_creator(c)].is_closed; }
static inline bool channel_is_creator(const channel c[static const restrict 1])
{ return (c->creator == thread_getpid()); }
static inline bool channel_is_buffered(const channel c[static const restrict 1])
{ return (c->flags & CHANNEL_BUFFERED); }
static inline bool channel_is_empty(const channel c[static const restrict 1])
{ return rb_empty(&c->files[channel_is_creator(c)].rb); }

bool channel_is_closed(channel c[static const restrict 1]);

MAYBE_UNUSED static void channel_dump_buffer(const channel c[static const restrict 1]) {
	static const char *channel_type_str[] = {
		"Creator",
		"Other",
	};
	printf("I am %s\n", channel_type_str[!channel_is_creator(c)]);
#define SPLIT_WIDTH 4
#define BYTE_WIDTH 32
	for (size_t i = 0; i != 2; ++i) {
		puts(channel_type_str[i]);
		for (rb_sizetype j = 0; j != c->files[i].rb.size; ++j) {
			const rb_buftype ch = c->files[i].rb.buf[j];
			printf("%.2hhx ", ch);
			if ((j + 1) % SPLIT_WIDTH == 0) { putchar(' '); }
			if ((j + 1) % BYTE_WIDTH == 0) { putchar('\n'); }
		}
		putchar('\n');
	}
	putchar('\n');
	puts("End");
}

MAYBE_UNUSED static void channel_dump_rb(rb_t *rb) {
	for (rb_sizetype i = 0; i != rb->size; ++i) {
		printf("%.2hhX ", rb->buf[i]);
		if ((i + 1) % 8 == 0) {
			puts("");
		}
	}
	puts("");
}

// Schedules oneself to sleep
static unsigned channel_sched_self(thread_t * me[static const restrict 1], const unsigned irq_state) {
	*me = thread_get_active();
	sched_set_status(*me, STATUS_SLEEPING);
	irq_restore(irq_state);
	DEBUG("%s:%zu: Thread %d control yield.\n", __func__, __LINE__, thread_getpid());
	thread_yield_higher();
	DEBUG("%s:%zu: Thread %d control returned.\n", __func__, __LINE__, thread_getpid());
	return irq_disable();
}

static inline void channel_sched_self_thread(thread_t * me[static const restrict 1])
{
	if (!me) { return; }
	*me = thread_get_active();
	thread_sleep();
	*me = nullptr;
}

// Schedules another thread to run.
static unsigned channel_sched_other(thread_t * other[static const restrict 1], register const unsigned irq_state) {
	bool other_priority_set = false;
	unsigned short other_priority = 0;
	DEBUG("%s:%zu: checking for thread %d. \n", __func__, __LINE__, thread_getpid());
	if ((*other) && ((*other)->status != STATUS_STOPPED && (*other)->status != STATUS_ZOMBIE)) {
		thread_t *const thread = *other;
		*other = nullptr;
		DEBUG("%s: Thread [%p] scheduled.\n", __func__, PTR_CAST(thread));
		other_priority = thread->priority;
		other_priority_set = true;
		sched_set_status(thread, STATUS_PENDING);
	}
	DEBUG("%s:%zu: Thread %d control yield.\n", __func__, __LINE__, thread_getpid());
	irq_restore(irq_state);
	if (other_priority_set) { sched_switch(other_priority); }
	DEBUG("%s:%zu: Thread %d control returned.\n", __func__, __LINE__, thread_getpid());
	return irq_disable();
}

static inline void channel_sched_other_thread(thread_t *other[static const restrict 1])
{ if (other && *other) { thread_wakeup(thread_getpid_of(*other)); } }

static unsigned channel_synchronize(channel c[static const restrict 1], const bool sender, register const unsigned state)
{
	/* Synchronization point: Checks that both sides are ready to send/receive, unless status is set to buffered. */
	/* Synchronization steps
	 * If we're first (other is null), register self and wait. Upon re-entry, continue.
	 */
	// Default unbuffered is the same as Go
	if (!channel_is_buffered(c)) {
		// What we do is:
		// Check if ME is read/write block.
		// Set ME waiting.
		// Loop while we check for other side ready.
#if 0
		/* Flags solution */
		c->flags |= (sender) ? CHANNEL_SEND_READY : CHANNEL_RECV_READY;
		thread_t **other = sender ? &c->thread_write_blocked : &c->thread_read_blocked;
		if (!*other) {
			// Register ourselves waiting for the other side to be ready.
			channel_sched_self(sender ? &c->thread_read_blocked : &c->thread_write_blocked, state);

			// Returned from sleeping, other side should be ready.
			assert ((c->flags & ((sender) ? CHANNEL_RECV_READY : CHANNEL_SEND_READY)));

			// Clear flags for the next synchronization.
			c->flags ^= (CHANNEL_RECV_READY | CHANNEL_SEND_READY);
			return;
		}
		channel_sched_other(other, state);
#else
		/* Thread Pointer Solution */
		thread_t **other = sender ? &c->thread_write_blocked : &c->thread_read_blocked;
		if (*other) {
			return channel_sched_other(other, state);
		}
		return channel_sched_self(sender ? &c->thread_read_blocked : &c->thread_write_blocked, state);

		// (creator) ? channel_sched_other(other, state) : channel_sched_self(me, state);
		// (creator) ? channel_sched_self(me, state) : channel_sched_other(other, state);
#endif
	}
	return state;
}

static inline rb_t * channel_get_rb(channel c[static const restrict 1], const bool creator)
{ return &c->files[creator].rb; }

static rb_sizetype _channel_send_msg(channel c[static const 1], const channel_msg m, unsigned state)
{
	rb_t *const rb = channel_get_rb(c, channel_is_creator(c));
	DEBUG("ch [%p] <- %zu data size %zu bytes. (Bufspace: %zu)\n", ((const void*){0} = c), sizeof (m.data_size), m.data_size, rb_avail(rb));

	/* Potential Synchronization point: Sending data size, synchronize if buffer full. */
	register rb_sizetype data_size_sent_count = rb_add(rb, ((const void*){0} = &m.data_size), sizeof (m.data_size));
	while (data_size_sent_count != sizeof (m.data_size)) {
		if (channel_is_closed(c)) {
			DEBUG("%s:%zu: Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", __func__, __LINE__, thread_getpid(), c->flags);
			irq_restore(state);
			return 0;
		}
		rb_drop(rb, data_size_sent_count); // Since we potentially added a broken data size, drop whatever we put.
		state = channel_sched_self(&c->thread_read_blocked, state); // Synchronize.
		data_size_sent_count = rb_add(rb, ((const void*){0} = &m.data_size), sizeof (m.data_size)); // Try again.
	}

	/* Begin channel exchange */
	rb_sizetype bytes = 0;
	while (true) {
		/* Be senstive to potential IRQ changes to channel between synchronizations. */
		if (channel_is_closed(c)) {
			DEBUG("%s:%zu: Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", __func__, __LINE__, thread_getpid(), c->flags);
			irq_restore(state);
			return bytes; // Return the current bytecount, if any.
		}
		if (rb_avail(rb) != 0) {
			rb_sizetype chunk = 0; // Current sent chumk
			// First get the chunk, then add chunk to bytes. Allows to see total sent and segmented sent during debug. Chunk is transient.
			bytes += (chunk = rb_add(rb, &((const rb_buftype *){0} = m.data)[bytes], (m.data_size - bytes)));
			DEBUG("ch [%p] <- %zu sent %zu/%zu bytes. (Bufspace: %zu)\n", PTR_CAST(c), chunk, bytes, m.data_size, rb_avail(rb));
			if (chunk) {
				/* Synchronization point: Data chunk sent. */
				// After sendt data, schedule the next thread. Restores state.
				state = channel_sched_other(&c->thread_write_blocked, state);
				if (bytes == m.data_size) { irq_restore(state); return bytes; } // Total data has been sent.
				continue;
			}
		}
		// An error has occurred or we're in another IRQ.
		if (irq_is_in()) {
			irq_restore(state);
			return 0;
		}
		/* Synchronization point: Sent data chunk, still not finished. Need other thread to read, so relinquish control. */
		state = channel_sched_self(&c->thread_read_blocked, state); // I am waiting for reads.
	}
	UNREACHABLE();
}

// ch <- var
size_t channel_send(channel c[static const restrict 1], const void *const restrict data, const size_t data_size)
{
	if (channel_is_closed(c)) {
		DEBUG("%s:%zu: Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", __func__, __LINE__, thread_getpid(), c->flags);
		return 0;
	}
	unsigned state = irq_disable();

	// Synchronization point: Wait for other process to be available.
	state = channel_synchronize(c, true, state);

	// Calling channel_send with no data size or no data will allow for 1 synchronization point.
	// This means that channel_send(c, nullptr, 0) == csp_synchronize or csp_barrier.
	if (!data || !data_size) {
		irq_restore(state);
		return 0;
	}

	// Actually send.
	channel_msg m = {data_size, data};
	return _channel_send_msg(c, m, state);
}

size_t channel_try_send(channel c[static const restrict 1], const void *restrict data, const size_t data_size)
{
	if (!data_size || !data) { return 0; }
	if (channel_is_closed(c)) {
		DEBUG("%s:%zu: Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", __func__, __LINE__, thread_getpid(), c->flags);
		return 0;
	}
	unsigned state = irq_disable();
	rb_t *const rb = channel_get_rb(c, channel_is_creator(c));
	DEBUG("ch [%p] <- %zu data size %zu bytes. (Bufspace: %zu)\n", ((const void*){0} = c), sizeof (data_size), data_size, rb_avail(rb));
	size_t bytes = 0;
	if ((rb_avail(rb) < data_size) || (bytes = rb_add(rb, ((const void*){0} = &data_size), sizeof (data_size)) != sizeof (data_size))) {
		rb_drop(rb, bytes);
		irq_restore(state);
		return 0;
	}
	bytes = rb_add(rb, data, data_size);
	irq_restore(state);
	return bytes;
}

size_t channel_send_msg(channel c[static const restrict 1], const channel_msg m)
{ return _channel_send_msg(c, m, irq_disable()); }

// var <- ch
static size_t _channel_recv_msg(channel c[static const restrict 1], void *const restrict out, register unsigned state)
{
	size_t data_size = 0;
	rb_t *const rb = channel_get_rb(c, !channel_is_creator(c));
	/* Potential synchronization point: If there is no data available, we need to wait for new data. */
	while (rb_peek(rb, PTR_CAST(&data_size), sizeof (data_size)) != sizeof (data_size)) {
		state = channel_sched_self(&c->thread_write_blocked, state);
	} // Data has become available and is extracted. Remove from buffer.
	if (rb_drop(rb, sizeof (data_size)) != sizeof (data_size)) { return 0; }
	DEBUG("ch [%p] -> %zu data size %zu bytes. (Bufspace: %zu)\n", PTR_CAST(c), sizeof (data_size), data_size, rb_avail(rb));

	rb_sizetype bytes = 0;
	while (true) {
		// Consider that there can be data left over which is complete.
		// If the available space left contains an entire element, extract first.
		// We want a closed channel *completey* empty OR having an object too big such that we cannot extract it.
		if (channel_is_closed(c) && (channel_is_empty(c) || (rb_sizetype)data_size > rb_avail(rb))) {
			DEBUG("Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", thread_getpid(), c->flags);
			irq_restore(state);
			return (bytes == data_size) ? bytes : 0;
		}

		if (!rb_empty(rb)) {
			rb_sizetype chunk = 0;
			bytes += (chunk = rb_get(rb, &((rb_buftype*){0} = out)[bytes], (rb_sizetype)(data_size - bytes)));
			DEBUG("ch [%p] -> %zu received %zu/%zu bytes. (Bufspace: %zu)\n", PTR_CAST(c), chunk, bytes, data_size, rb_avail(rb));

			if (chunk) {
				/* Synchronization point: Data read, allow the other side to send more or continue. */
				state = channel_sched_other(&c->thread_read_blocked, state);
				if (bytes == data_size) { irq_restore(state); return bytes; }
				continue;
			}
		}
		if (irq_is_in()) {
			irq_restore(state);
			return 0;
		}
		/* Synchronization point: Data read, but we're incomplete. Wait for more data. */
		state = channel_sched_self(&c->thread_write_blocked, state); // I am waiting for writes.
	}
	UNREACHABLE();
}

size_t channel_recv(channel c[static const restrict 1], void *const restrict buffer)
{
	// Unlike send, we'll allow taking all items out of the buffer before recognizing the closed condition.
	if (channel_is_closed(c) && channel_is_empty(c)) {
		DEBUG("Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", thread_getpid(), c->flags);
		return 0;
	}

	unsigned state = irq_disable();
	// Synchronization point: Make sure we're ready to send.
	// If the user specifically requests unbuffered channels, then we skip this synchronization point.
	state = channel_synchronize(c, false, state);
	if (!buffer) {
		irq_restore(state);
		return 0;
	}
	return _channel_recv_msg(c, buffer, state);
}

// c -> data if any.
size_t channel_try_recv(channel c[static const restrict 1], void *const buffer)
{
	if (!buffer) { return 0; }
	const bool isnt_creator = !channel_is_creator(c);
	if (channel_is_closed(c) && channel_is_empty(c)) {
		DEBUG("Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", thread_getpid(), c->flags);
		return 0;
	}
	rb_t *const rb = &c->files[isnt_creator].rb;
	unsigned state = irq_disable();
	size_t data_size = rb_get(rb, PTR_CAST(&data_size), sizeof (data_size));
	if (!data_size) {
		irq_restore(state);
		return 0;
	}
	const size_t bytes = (size_t)rb_get(rb, ((rb_buftype*){0} = buffer), data_size);
	DEBUG("ch [%p] -> received %zu/%zu bytes. (Bufspace: %zu)\n", PTR_CAST(c), bytes, data_size, rb_avail(rb));
	irq_restore(state);
	return bytes;
}

channel_msg channel_recv_msg(channel c[static const restrict 1], void *const restrict out)
{
	size_t msg_data_size = _channel_recv_msg(c, out, irq_disable());
	return (channel_msg){msg_data_size, out};
}

size_t channel_drop(channel c[static const restrict 1])
{
	// Unlike send, we'll allow taking all items out of the buffer before recognizing the closed condition.
	if (channel_is_closed(c) && channel_is_empty(c)) {
		DEBUG("Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", thread_getpid(), c->flags);
		return 0;
	}

	unsigned state = irq_disable();
	// Synchronization point: Make sure we're ready to send.
	// If the user specifically requests unbuffered channels, then we skip this synchronization point.
	state = channel_synchronize(c, !channel_is_creator(c), state);

	size_t data_size = 0;
	rb_t *const rb = channel_get_rb(c, !channel_is_creator(c));
	/* Potential synchronization point: If there is no data available, we need to wait for new data. */
	while (rb_peek(rb, PTR_CAST(&data_size), sizeof (data_size)) != sizeof (data_size)) {
		state = channel_sched_self(&c->thread_write_blocked, state);
	} // Data has become available and is extracted. Remove from buffer.
	if (rb_drop(rb, sizeof (data_size)) != sizeof (data_size)) { return 0; }
	DEBUG("ch [%p] -> %zu data size %zu bytes. (Bufspace: %zu)\n", PTR_CAST(c), sizeof (data_size), data_size, rb_avail(rb));

	rb_sizetype bytes = 0;
	while (true) {
		// Consider that there can be data left over which is complete.
		// If the available space left contains an entire element, extract first.
		// We want a closed channel *completey* empty OR having an object too big such that we cannot extract it.
		if (channel_is_closed(c) && (channel_is_empty(c) || (rb_sizetype)data_size > rb_avail(rb))) {
			DEBUG("Thread %" PRIkernel_pid ": Channel file is closed with flags %d.\n", thread_getpid(), c->flags);
			irq_restore(state);
			return (bytes == data_size) ? bytes : 0;
		}

		// If nullptr is passed to channel_recv, it will effectively drop one message from sender.
		rb_sizetype chunk = 0;
		bytes += (chunk = rb_drop(rb, (rb_sizetype)(data_size - bytes)));
		DEBUG("ch [%p] -> %zu dropped %zu/%zu bytes. (Bufspace: %zu)\n", PTR_CAST(c), chunk, bytes, data_size, rb_avail(rb));

		if (bytes) {
			/* Synchronization point: Data read, allow the other side to send more or continue. */
			state = channel_sched_other(&c->thread_read_blocked, state);
			if (bytes == data_size) { irq_restore(state); return bytes; }
		}
		if (c->thread_write_blocked || irq_is_in()) {
			irq_restore(state);
			return 0;
		}
		/* Synchronization point: Data read, but we're incomplete. Wait for more data. */
		state = channel_sched_self(&c->thread_write_blocked, state); // I am waiting for writes.
	}
	UNREACHABLE();
}

size_t channel_send_select(
	const size_t channel_count,
	channel *c[static const restrict channel_count],
	const void *restrict data,
	const size_t data_size
);
size_t channel_recv_select(
	const size_t channel_count,
	channel *c[static const restrict channel_count],
	void *restrict data
);

void *channel_recv_ptr(channel c[static const restrict 1], void *const buffer);

// Requires the caller to pass in the parent object so we have access to it's memory location.
channel channel_make(channel c[static const restrict 1], const bool buffered)
{
	*c = (channel){
		thread_getpid(),
		0 | (buffered ? CHANNEL_BUFFERED : 0),
		nullptr,
		nullptr,
		{
			{
				RB_INIT(c->files[0].buffer),
				{0},
			},
			{
				RB_INIT(c->files[1].buffer),
				{0},
			},
		},
	};
	c->files[0].rb.buf = (c->files[0].buffer); // Reassign the pointers to the parent object
	c->files[1].rb.buf = (c->files[1].buffer); // Reassign the pointers to the parent object
	return *c;
}

// Recognize which side we're on and close that file.
inline void channel_close(channel c[static const restrict 1])
{
	// DEBUG("%s:%d: Thread %" PRIkernel_pid " closing channel.\n", __func__, __LINE__, thread_getpid());
	// c->files[channel_is_creator(c)].is_closed = 1;
	c->flags |= CHANNEL_CLOSED;
}
//void channel_open(channel c[static const restrict 1]);

void channel_set_unbuffered(channel c[static const restrict 1], const bool buffered);
void channel_set_owner(channel c[static const restrict 1], const kernel_pid_t thread_id);

/* CSP */

static void *csp_dispatch(void *args)
{
	DEBUG("%s:%zu: Dispatching process.\n", __func__, __LINE__);
#if __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
	assert(args);
#if __clang__
	#pragma clang diagnostic pop
#endif
	csp_ctx *const ctx = args;

	DEBUG("args: %p, channel %p\n", ctx->params.args, (void*)ctx->params.c);
	ctx->retval = (ctx->params.c) ? ((csp_func_t)ctx->proc)(ctx->params.args, ctx->params.c) : ((thread_task_func_t)ctx->proc)(ctx->params.args);
	DEBUG("%s:%zu: Process returned [%p].\n", __func__, __LINE__, ctx->retval);
	ctx->flags |= CSP_STOP;
	sched_task_exit();
	return ctx->retval;
}

MAYBE_UNUSED
static size_t csp_count = 0;
// maybe_unused
// static struct {
// 	size_t csp_count;
// 	size_t thread_default_stack_size;
// } _csp_internal_ctx = {0};

#if __STDC_VERSION__ > 201710L
static constexpr short csp_ctx_size = (sizeof (csp_ctx) + alignof (csp_ctx) - 1) & ~(alignof (csp_ctx) - 1);
#else
#define csp_ctx_size (sizeof (csp_ctx) + alignof (csp_ctx) - 1) & ~(alignof (csp_ctx) - 1)
#endif

csp_ctx *_csp(
	struct csp_stack sp,
    csp_func_param f,
	channel *const restrict c,
	void *args
)
{
#if __clang__
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
	assert(((int)sp.size - sizeof (csp_ctx)) > 0 && "Stack size too smol");
	DEBUG("%s:%zu: Creating new process.\n", __func__, __LINE__);
	csp_ctx *const ctx = ((void*){0} = sp.stackp);
	*ctx = (csp_ctx) {
		0,
		CSP_RUNNING,
		f,
		{
			args,
			c,
		},
		nullptr,
		//THREAD_STACKSIZE_CSP,
		// {0},
#ifdef CONFIG_THREAD_NAMES
		{0},
#endif
	};

#ifdef CONFIG_THREAD_NAMES
	// Apparently, precision for unsigned numbers is their *MINIMUM* length...
	MAYBE_UNUSED
	const int n = snprintf(ctx->name, CSP_NAME_LENGTH - 1, "CSP_%u", csp_count++ % ((MAXTHREADS) ? MAXTHREADS : SCHED_PRIO_LEVELS-1));

	assert(n);
#if __clang__
	#pragma clang diagnostic pop
#endif

#endif
	ctx->id = thread_create(
		(sp.stackp)+(csp_ctx_size),
		(int)(sp.size - (csp_ctx_size)),
		CSP_PRIORITY,
		THREAD_FLAGS_CSP,
		csp_dispatch,
		ctx,
#ifdef CONFIG_THREAD_NAMES
		ctx->name
#else
		nullptr
#endif
	);
	switch (ctx->id) {
		case -EINVAL: {
			DEBUG("%s:%zu: ERROR: THREAD INVALID - CSP cannot create thread due to priority being greater than SCHED_PRIO_LEVELS, error code %d\n", __func__, __LINE__, ctx->id);
			return nullptr;
		};
		case -EOVERFLOW: {
			DEBUG("%s:%zu: ERROR: THREAD OVERFLOW - CSP cannot create thread due to too many threads, error code %d\n", __func__, __LINE__, ctx->id);
			return nullptr;
		};
	}
	DEBUG("%s:%zu: Finished creating process %d.\n", __func__, __LINE__, ctx->id);
	return ctx;
}

// Inlined functions that may or may not emit symbols, so we add the symbols here.
channel *csp_get_channel(void*);
void *csp_ret(csp_ctx ctx[static const restrict 1]);
void csp_stop(csp_ctx ctx[static const restrict 1]);
void csp_wait(csp_ctx ctx[static const restrict 1]);

int csp_kill(csp_ctx ctx[static const restrict 1]) {
	ctx->flags &= ~CSP_RUNNING;
	sched_set_status(thread_get(ctx->id), STATUS_ZOMBIE);
	return thread_kill_zombie(ctx->id);
}

bool csp_running(csp_ctx ctx[static const restrict 1]) {
  if (ctx->flags & CSP_RUNNING) {
    thread_yield(); // Synchronization point
    return true;
  }
  return false;
}
