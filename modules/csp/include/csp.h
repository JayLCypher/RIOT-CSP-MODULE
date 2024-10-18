/*
 * Copyright (C) 2024 UiT
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    sys_csp CSP
 * @ingroup     sys
 * @brief       A module containing an implementation of a GO-like take on Tony Hoare's
 * Communicating Sequential Processes, using RIOT OS Threads and a channel structure for communication.
 * An attempt to mimic a Golang like syntax and structure.
 *
 * @{
 *
 * @file csp.h
 *
 * @author      Jonathan L. Claudius <jaylcypher@github.com>
 */

#ifndef CSP_H
#define CSP_H

/* Add header includes here */
#include "thread.h"

#ifdef TSRB
#include "tsrb.h"
#define RB_INIT(buf) TSRB_INIT(buf)
#define rb_t tsrb_t
#define rb_add tsrb_add
#define rb_add_one tsrb_add_one
#define rb_get tsrb_get
#define rb_drop tsrb_drop
#define rb_peek tsrb_peek
#define rb_peek_one tsrb_peek_one
#define rb_empty tsrb_empty
#define rb_avail tsrb_avail
#define rb_sizetype unsigned
#define rb_buftype unsigned char
#else
#include "ringbuffer.h"
#define RB_INIT(buf) RINGBUFFER_INIT(buf)
#define rb_t ringbuffer_t
#define rb_add ringbuffer_add
#define rb_add_one ringbuffer_add_one
#define rb_get ringbuffer_get
#define rb_drop ringbuffer_remove
#define rb_peek ringbuffer_peek
#define rb_peek_one ringbuffer_peek_one
#define rb_empty ringbuffer_empty
#define rb_avail ringbuffer_get_free
#define rb_sizetype unsigned
#define rb_buftype char
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Declare the API of the module */

/*
 * The one who makes the channel is registered such that we know if the creator
 * is sending or not. That will be the basis for if we're on the A file or the B
 * file.
 */

enum CHANNEL_FLAGS
#if __STDC_VERSION__ > 201710L
	: int
#endif
{
	CHANNEL_CLOSED = (1 << 0),
	CHANNEL_BUFFERED = (1 << 1),
	CHANNEL_SEND_READY = (1 << 2),
	CHANNEL_RECV_READY = (1 << 3),
};

typedef struct channel_message channel_msg;
struct channel_message {
	const size_t data_size;
	const void *data;
};
#define NEW_MSG(data, sz) (channel_msg) { (sz), (data) }

#ifndef CHANNEL_BUFSIZE
#define CHANNEL_BUFSIZE 32
#endif

typedef struct channel channel;
struct channel {
	// The channel should be created in the main function by the "parent thread".
	// However, since we only need to know one side, who created it is a good metric for a monochannel.
	// Kernel_pid_t is aliased short.
	kernel_pid_t creator;
	int flags;

	thread_t *thread_read_blocked;	 // The thread(s) waiting for reading.
	thread_t *thread_write_blocked;	 // The thread(s) waiting for writing.

	// A channel file.
	// The channel needs two files to communicate. Each side has a read and a write end.
	// These sides cross eachother depending on who is the parent/child.
	// Because these things are local to the channel creation point,
	// trying to copy the channel to send to another channel will not work.
	struct channel_file {
		rb_t rb;
		rb_buftype buffer[CHANNEL_BUFSIZE];
	} files[2];
};

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif
inline channel *channel_from_args(void *arg) {
	assert(arg);
	return arg;
}
#if __clang__
#pragma clang diagnostic pop
#endif

// Using array notation to get compile-time null check.
channel channel_make(channel c[static const restrict 1], const bool buffered);

// Recognize which side we're on and close that file.
void channel_close(channel c[static const restrict 1]);

inline void channel_set_owner(channel c[static const restrict 1], const kernel_pid_t thread_id)
{ c->creator = thread_id; }
inline void channel_set_buffered(channel c[static const restrict 1], const bool buffered)
{ c->flags |= (buffered << (CHANNEL_BUFFERED - 1)); }
inline bool channel_is_closed(channel c[static const restrict 1])
{ return (c->flags & CHANNEL_CLOSED); }

// ch <- var
size_t channel_send(channel c[static const restrict 1], const void *restrict data, size_t data_size);
size_t channel_try_send(channel c[static const restrict 1], const void *restrict data, size_t data_size);
size_t channel_send_msg(channel c[static const restrict 1], const channel_msg m);

// var <- ch
size_t channel_recv(channel c[static const restrict 1], void *const restrict buffer);
size_t channel_try_recv(channel c[static const restrict 1], void *const restrict buffer);
channel_msg channel_recv_msg(channel c[static const restrict 1], void *const restrict out);

size_t channel_drop(channel c[static const restrict 1]);

// Selects the first linearly available channel of the channel array to send to.
// Returns the index of the channel sent to.
inline size_t channel_send_select(
	const size_t channel_count,
	channel *c[static const restrict channel_count],
	const void *restrict data,
	const size_t data_size
)
{
	for (;;) {
		for (size_t i = 0; i != channel_count; ++i) {
			size_t send_size = channel_try_send(c[i], data, data_size);
			if (send_size > 0) {
				return i;
			}
		}
	}
}

// Selects the first linearly available channel to recieve data from.
// Returns the index of the channel with data available to.
inline size_t channel_recv_select(
	const size_t channel_count,
	channel *c[static const restrict channel_count],
	void *restrict data
)
{
	for (;;) {
		for (size_t i = 0; i != channel_count; ++i) {
			size_t recv_size = channel_try_recv(c[i], data);
			if (recv_size > 0) {
				return i;
			}
		}
	}
}

// ptr = channel_recv(c, ptr);
inline void *channel_recv_ptr(channel c[static const restrict 1], void *const buffer) {
	const size_t data_size = channel_recv(c, buffer);
	if (data_size == 0) {
		return (void *)0;
	}
	return buffer;
}

#ifndef THREAD_STACKSIZE_CSP
#define THREAD_STACKSIZE_CSP (THREAD_STACKSIZE_MINIMUM)
#endif
#ifndef CSP_PRIORITY
#define CSP_PRIORITY THREAD_PRIORITY_MAIN - 1
#endif
#ifndef THREAD_FLAGS_CSP
#define THREAD_FLAGS_CSP 0
#endif
#define CSP_STACK(size) \
	size, (char[size]) { 0 }

typedef struct csp_stack csp_stack;
struct csp_stack {
	char *stackp;
	size_t size;
};

#ifndef CSP_NAME_PROTOTYPE
#define CSP_NAME_PROTOTYPE "CSP_00"
#endif
#define CSP_NAME_LENGTH sizeof(CSP_NAME_PROTOTYPE)

typedef void *(*csp_func_t)(void *const args, void *const c);

#if __STDC_VERSION__ > 201710L
#define CSP_PARAM ...
#else
#define CSP_PARAM void *, ...
#endif

typedef void *(*csp_func_param)(CSP_PARAM);
typedef struct csp_context csp_ctx;
struct csp_context {
	kernel_pid_t id;
	volatile int flags;

	csp_func_param proc;  // Function pointer of type FPTR_T
	struct csp_params {
		void *args;
		channel *c;
	} params;
	void *retval;
	// size_t stack_size;
	// char stack[THREAD_STACKSIZE_CSP];

#if defined(CONFIG_THREAD_NAMES) || defined(DOXYGEN)
	char name[CSP_NAME_LENGTH];
#endif
};

enum CSP_FLAGS
#if __STDC_VERSION__ > 201710L
: short
#endif
{
	CSP_STOP = (0 << 0),
	CSP_SKIP = (1 << 0),
	CSP_RUNNING = (1 << 1),

	CSP_MAX = (1 << sizeof(short)),
};

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

// Copies the arguments to a local variable using memcpy, compound literal and type T.
#if defined(__GNUC__) || defined(__clang__) || __has_builtin(__builtin_memcpy)
extern void *memcpy(void *, const void *, unsigned);
#define CSP_GET_ARGS_T(args, T) memcpy(&(T){0}, (args), sizeof(T))
#else
#define CSP_GET_ARGS_T(args, T)                       \
	((void *){0} = ((unsigned char *)_pp = &(T){0})); \
	do {                                              \
		for (size_t i = 0; i != sizeof(T); ++i) {     \
			_pp[i] = ((unsigned char *)args)[i];      \
		}                                             \
	} while (0)
#endif

// Fetches the return value from a csp compatible function.
inline void *csp_ret(csp_ctx ctx[static const restrict 1])
{ return ctx->retval; }

#if __clang__
#pragma clang diagnostic pop
#endif

// csp_ctx csp_init(csp_ctx *restrict ctx, thread_task_func_t f, channel c[static const restrict 1], void *args);
// void csp(csp_ctx *restrict, char *restrict stack, size_t stack_sz);

/* From various sources, but final idea here: https://gist.github.com/aprell/3722962?permalink_comment_id=4198502#gistcomment-4198502 */
#if defined(__GNUC__) || defined(__clang__)	 // supports passing 0 arguments using gcc/clang non-standard features
#define VA_NARGS_IMPL(_0, _1, _2, _3, N, ...) N
#define VA_NARGS(...) VA_NARGS_IMPL(_, ##__VA_ARGS__, 3, 2, 1, 0)
#else
#define VA_NARGS_IMPL(_1, _2, N, ...) N	 // 1 or more arguments only
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 2, 1)
#endif

#define CSP_GET_ARGS_0(...) nullptr, nullptr
#define CSP_GET_ARGS_1(_1, ...) nullptr, _1
#define CSP_GET_ARGS_2(_1, _2, ...) _2, _1
#define CSP_GET_ARGS_3(_1, _2, _3, ...) \
	_1, (void *[]) { _2, _3 }
#define CSP_GET_ARGS_N(_1, ...) \
	_1, (void *[]) { __VA_ARGS__ }
#define CSP_GET_ARGS_(N, ...) CSP_GET_ARGS_##N(__VA_ARGS__)
#define CSP_GET_ARGS(N, ...) CSP_GET_ARGS_(N, __VA_ARGS__)

#define CSP_INIT_CTX \
	(CSP_C23_CLIT_STORAGE csp_ctx) { 0 }
#define CSP_INIT(sz) ((struct csp_stack){(CSP_C23_CLIT_STORAGE char[sz]){0}, sz})

/*
 * Convenience macro to emulate Golangs version of a Go function.
 * Allows for syntax such as:
	GO (my_function, nullptr)
*/
#if ((__STDC_VERSION__ > 201710L))
/*
 * This just runs the function with an anonymous compound literal.
 * Storage durations on compound literals is a C23 feature.
 */
#define CSP_C23_CLIT_STORAGE static
#define CSP_DO_LABEL(func, label)
#define CSP_INNER(func, ...) _csp(CSP_INIT(THREAD_STACKSIZE_CSP), ((csp_func_param)func), CSP_GET_ARGS(VA_NARGS(__VA_ARGS__), __VA_ARGS__))
#define CSP_INNER_SZ(size, func, ...) _csp(CSP_INIT(size), ((csp_func_param)func), CSP_GET_ARGS(VA_NARGS(__VA_ARGS__), __VA_ARGS__))
#define GO_SIZ(size, func, ...) CSP_INNER_SZ((size), (func), __VA_ARGS__)
#define GO(func, ...) CSP_INNER((func), __VA_ARGS__)
#else
/*
 * Since Storage duration is a C23
 * Provides "func"_ctx variable which is statically available for the lifetime of the program.
 * To allow for multiple calls on the same function, uses the __COUNTER__ macro.
 * Alternatively, if __COUNTER__ is not supported, you can revert to using __LINE__ with definition of GO_USE_LINE
 */
#define CSP_C23_CLIT_STORAGE
#define CSP_EXPAND(name, label, count) name##_##label##count
#define CSP_LABEL(name, label, count) CSP_EXPAND(name, label, count)
#define CSP_INNER(label, func, ...)           \
	MAYBE_UNUSED static csp_ctx label = {0}; \
	label = *_csp(CSP_INIT(THREAD_STACKSIZE_CSP), ((csp_func_param)func), CSP_GET_ARGS(VA_NARGS(__VA_ARGS__), __VA_ARGS__))

#ifdef GO_LINE
#define CSP_DO_LABEL(func, label) CSP_LABEL(func, label, __LINE__)
#define GO(func, ...) CSP_INNER(CSP_LABEL(func, ctx, __LINE__), (func), __VA_ARGS__)
#else
#define CSP_DO_LABEL(func, label) CSP_LABEL(func, label, __COUNTER__),
#define GO(func, ...) CSP_INNER(CSP_LABEL(func, ctx, __COUNTER__), (func), __VA_ARGS__)
#endif

#endif

#define csp(func, channel, args) _csp(CSP_INIT(THREAD_STACKSIZE_CSP), (csp_func_param)(func), (channel), (args))
#define csp_sz(sz, func, channel, args) _csp(CSP_INIT(sz), ((csp_func_param)(func)), (channel), (args))
#define csp_obj(obj, func, channel, args) _csp((csp_stack){(obj), sizeof (obj)}, ((csp_func_param)(func)), (channel), (args))

// Initialize an object of type csp_ctx. Adds any channels and args to it. Will pass on channels and args.
csp_ctx *_csp(
	struct csp_stack sp,
	csp_func_param f,
	channel *const restrict c,
	void *const restrict args);

/* Sets the csp ctx thread to zombie and kills it. Only the context will remain. */
int csp_kill(csp_ctx ctx[static const restrict 1]);
bool csp_running(csp_ctx ctx[static const restrict 1]);
inline void csp_wait(csp_ctx ctx[static const restrict 1])
{ while (csp_running(ctx)) { } }

#ifdef __cplusplus
}
#endif

#endif /* CSP_H */
/** @} */

/* SECTION OF IDEAS AND NOT-IMPLEMETNED POTENTIAL FEATURES */
#if 0

/* Tagged Union Function Pointers */
typedef void *(* csp_func)(channel c[static const restrict 1]);
typedef void *(* csp_thread_task_func_t)(void *args, channel c[static const restrict 1]);

enum csp_function_prototypes_tag
#if __STDC_VERSION__ > 201710L
: int
#endif
{
    RIOT_FUNC,
    CSP_FUNC,
    RIOT_CSP_FUNC,
    CSP_FUNC_PROTO_COUNT
};
union csp_function_prototypes
{
    thread_task_func_t func_task;
    csp_func func_csp;
    csp_thread_task_func_t func_csp_task;
};

/* Using a struct as arguments for the Process */
// Get a pointer to the channel
inline void *csp_get_args(void *const restrict arg) {
	assert(arg && "CSP Expected argument but found null instead.");
	struct csp_params *params = arg;
	return params->args;
}

// Get a pointer to the channel
inline channel *csp_get_channel(void *const restrict arg) {
	assert(arg && "CSP Expected channel but found null instead.");
	struct csp_params *params = arg;
	return params->c;
}


// Get a pointer to channels from ctx params
inline void *csp_params_channel(void *const restrict arg) {
	assert(arg);
	struct csp_params *params = arg;
	return params->c;
}

// Get a pointer to args from ctx params
inline void *csp_params_args(void *const restrict arg) {
	assert(arg);
	struct csp_params *params = arg;
	return params->args;
}
#endif
