# GO-like Communicating Sequential Processes module for the Embedded RTOS RIOT OS

A library module for [RIOT OS](https://github.com/RIOT-OS/RIOT) which allows for a Go language like syntax and concurrency module,
utilizing available kernel resources from RIOT OS.
The goal is to provide easier access to multi-threaded Embedded C development similar syntax to Golang's goroutines.

## Dependencies

Project:
* [RIOT OS](https://github.com/RIOT-OS/RIOT) -- Run:
```bash
git submodule update --init
```

Independent Requirements:
* At least C18, but ideally a C23 compatible compiler with the following features: storage class compound literals

## Introduction

*Super "simple" example.*
```c
#include "csp.h"

void *hello(channel *c)
{
    int argc = 0;
    channel_recv(c, &argc);
    char buf[256] = {0};
    for (int i = 0; i != argc; ++i) {
        size_t str_len = 0;
        channel_recv(c, &str_len);
        if (str_len < 256) {
            channel_recv(c, buf);
        }
        else {
            channel_drop(c);
        }
    }
    channel_recv(c, nullptr); // Sync
    channel_close(c);
    return nullptr;
}

int main(const int argc, const char *argv[argc]) {
    channel c = channel_make(&c, 0);
    GO (hello, &c);
    channel_send(&c, &argc, sizeof (argc));
    for (int i = 0; i != argc; ++i) {
        size_t str_len = strlen(argv[i]);
        channel_send(&c, &str_len, sizeof (str_len));
        channel_send(&c, argv[i], str_len);
    }
    channel_send(&c, nullptr, 0); // Sync
    return 0;
}
```

This is a CSP module for RIOT OS, using their module support.
Either put the modules within sys/modules or as an [out-of-tree module](https://doc.riot-os.org/creating-an-application.html#autotoc_md2209)
 according to documentation linked.

Add the following to your RIOT OS Makefile:
```make
    USEMODULE += csp
```

For a scheduling example, look at the RIOT OS [example thread_duel makefile](https://github.com/RIOT-OS/RIOT/blob/master/examples/thread_duel/Makefile).
This has the RIOT OS Round Robin scheduler as an example.

I hope you will have a good time writing easier C for embedded devices :)

For examples, go to the examples folder and do:
```bash
make -Bj
```
This will make every example for you. Individual examples can be make'd using the folder name.

Once you're ready to GO, just create a main file and a main function, and run make in the core folder.


## Usage

Include the csp.h header file into your project files where necessary.
Both channels and csp structure is available there.
For more examples than the readme, see the Examples and Measurements folders.

### Channels

The Channel API consists of the channel structure and supporting functions.
*Note: maybe_unused attribute for optional error-checking interface.*
0 indicates an error occurred and no action taken. Any value > 0 means an operation happened.
If the return value == size of the data, then operation is successful.
For sending, this is the data size specified.
For receiving, this is the data size expected.

**Warning:** *All send/recv functions assume a pointer to storage of (at least) big enough size to store the data.*

#### Channel API Reference
```c
channel c;
channel channel_make(channel c[1], bool is_buffered);

// Data transfer and communication
[[maybe_unused]]
size_t channel_send(channel c[1], void data[.data_size], size_t data_size);
[[maybe_unused]]
size_t channel_recv(channel c[1], void data[.data_size]);

// Channel manipulation
void channel_open(channel c[1]);
void channel_close(channel c[1]);

void channel_set_owner(channel c[1], kernel_pid_t id); // Sets the "owner" side of the channel.
void channel_set_buffered(channel c[1], bool buffered); // Toggles buffering in accordance to flag passed.

// Convenience functions:
/* Converts a void pointer to a channel pointer with debug assertion.
 * For use when manually using RIOT OS Threading interface.
 */
channel *channel_from_args(void *args);

// A wrapper around channel_recv that handles error checking against 0 (returns nullptr)
// and returns a pointer to the passed buffer instead, like a memcpy function.
// Used to assign a pointer directly with assignment operator on static or dynamic memory.
// Example:
/*
    struct big_struct {
        ...
        void *p;
    } my_struct = {0};

    // Local/static data
    my_struct.p = channel_recv_ptr(c, (char[1024]){0});

    // Dynamic data
    my_struct.p = channel_recv_ptr(c, malloc(dynamic_size_required));

    // Reassignment of previous data:
    my_struct.p = channel_recv_ptr(c, p);
*/
void *channel_recv_ptr(channel c[static const restrict 1], void *const buffer);

// Other:
/*
Selection expression
For a process, you may want to wait on data and pass it along.
To that end there are two functions available:
    * channel_send_select
    * channel_recv_select
These functions take an array of channels, and tries to recieve or send on each of them.
If there is data, they return with the index of the channel that sent/received.
Since array passes into the function, reason stands that it's available outside.
Usage would be in a loop, where the return value is where data sent/recv to/from,
and you can decide what happens to the array before re-entry into the function.
 */

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

```

### Communicating Sequential Processes

The Communicating Sequential Processes stand as a wrapper around RIOT OS Threading.
It uses RIOT OS threads to create new execution environments running one of two types of functions:
* thread_task_func_t, which are RIOT OS Functions, allowing for drop-in replacement in existing code.
* csp_func_t, which are the same as thread_task_func_t, but with an extra parameter to pass channels through.

Functions can at max permit two parameters due to C's strict function pointer prototyping.
*NOTE: The current implementation might stand to change in the future.*

#### CSP API Reference

```c

// thread_task_func_t compatible function prototype
void *function(void *args);
thread_task_func_t f_ptr = function;

// csp_func_t compatible function prototype
void *function(void *args, void *channel);
csp_func_t f_ptr = function;

// Remember that pointers are implicit cast between void * and all other pointers
void *function(void *args, channel *c);

csp_ctx ctx = {0}; // Context structure.
// Manually creating a csp process and keep content structure
// The csp macro takes care of generating enough space for a stack.
csp_ctx *ctx = csp(function, channels, args);

// Automatically create stack space and function, like go
GO(function);

// RIOT OS Threads require a stack. To allow for manual tweaking, either
// - Globally define stacksize.
#define THREAD_STACKSIZE_CSP 1024

// - Use the csp_sz sized macro
csp_ctx *ctx = csp_sz(1024, function, channels, args);

// - Use the csp_obj macro
csp_ctx *ctx = csp_obj((char[1024]){0}, function, channels, args);

// - Use the function directly
char stack[STACKSIZE] = {0};
csp_ctx *ctx = _csp(((void *){0} = stack), function, channels, args);

// Unlike core RIOT threads, dynamically allocated resources can return by pointer:
void *data = csp_ret(GO(function));

// There are also some control functions:
void csp_stop(csp_ctx ctx[static const restrict 1]); // Stop the process
void csp_wait(csp_ctx ctx[static const restrict 1]); // Wait for process to finish
int csp_kill(csp_ctx ctx[static const restrict 1]);  // Kill process


```

## GO to library comparison

The goroutine folder within examples contain a go code and c code comparison.
The example Go code is from [gobyexample](https://gobyexample.com/goroutines).

<table>
<tr>
<th> CSP </th>
<th> GO </th>
</tr>
<tr>
<td>

```c
#include "csp.h" // CSP

#include <stdio.h> // fmt
#include "ztimer.h" // time

void *f(const char *from)
{
  for (int i = 0; i != 3; ++i) {
    printf("%s : %d\n", from, i);
    // Mimic go runtime schedule
    ztimer_sleep(ZTIMER_MSEC, 1);
  }
  return nullptr;
}

// C does not have lambdas yet.
void *lambda(const char *msg) {
  printf("%s\n", msg);
  return nullptr;
}

int main(void) {
  f("direct");

  GO(f, "goroutine?");

  GO(lambda, "going");

  ztimer_sleep(ZTIMER_MSEC, 1000); // 1s
  puts("done");
  return 0;
}
```

</td>
<td>

```go
package main

import (
  "fmt"
  "time"
)

func f(from string) {
  for i := 0; i < 3; i++ {
    fmt.Println(from, ":", i)
  }
}

func main() {
  f("direct")
  
  go f("goroutine")
  
  go func(msg string) {
  	fmt.Println(msg)
  }("going")
  
  time.Sleep(time.Second)
  fmt.Println("done")
}
```

</td>
</tr>
</table>

### Examples

These are some simple examples to get you started.
More extensive examples are available in the examples/ subfolder.

#### Channels

Creating a new channel:
```c
#include "csp.h"
int main(void) {
    channel c = channel_make(&c, 0);

    /* Use channel c. */

    /* ... */

    return 0;
}
```

Sending and receiving:
```c
#include "csp.h"
#include <stdio.h>
// thread_task_func_t compatible function
void *second_thread(void *args)
{
    channel *c = args; // Implicit cast to channel.

    size_t item = 0;
    /* Optional error handling: */
    // size_t ret =
    channel_recv(c, &item);

    /*
    if (ret != sizeof (item)) {
        // Handle error.
    }
    */

    printf("Item: %zu\n", )
    return nullptr;
}

int main(void) {
    channel c = channel_make(&c, 0);

    // RIOT OS thread creation:
    static char stack[THREAD_STACKSIZE_TINY] = {0};
    // [[maybe_unused]]
    thread_create(stack, sizeof (stack), THREAD_PRIORITY_MAIN - 1, 0,
                  second_thread, nullptr, "second_thread");

    // Using compound literal
    channel_send(&c, &(size_t){69420}, sizeof (size_t));

    return 0;
}
```

Selecting:
```c

#include "csp.h"
#include <stdio.h>

// csp_func_t compatible function
void *selector(void *args, channel *c)
{
    // Riot OS Message type.
    msg_t m = {0};
    while (true) {
        switch (channel_recv_select(10, c, &m)) {
            case 0: { puts(m.content.ptr); } break;
            case 1: { ++m.content.value; channel_send(&c[1], &m, sizeof (m)); } break;
            // Handle case 2-9
            default: break;
        }
    }

    return nullptr;
}


static channel chans[10] = {0};
int main(void) {
    // Initialize all channels
    for (int i = 0; i != 10; ++i) {
        chans[i] = channel_make(&chans[i], 0);
    }

    GO(selector, nullptr, &chans); // Using the CSP interface

    msg_t m = {0};
    m.content.ptr = "I guess we're doing circles now.";
    channel_send(&chans[0], &m, sizeof (m));

    return 0;
}
```

#### CSP

Creating new processes
```c
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
void *hello_both(void *args, channel *channel)
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
```

## Notes on source code

<em>

In this project, you will notice some (anno 2024) *unconventional* C. Good amount of techniques derived from Jens Gustedt's **HIGHLY RECOMMENDED** ["Modern C"](https://gustedt.gitlabpages.inria.fr/modern-c/) book.
Specifically:
* Array notation in function parameters for pointers
    This is a technique to try compile-time check arguments against nulled parameters (-Wnon-null).
    The way you do this is either expect N compile-time sizes (such as a string literal of size N) or by prefixing the size
    as an argument and using that as a "size" parameter to the array.
    When wanting a check on a pointer to a single object, use the number 1.

* Liberal use of __STDC_VERSION__ > 201710L
    This feature test macro allows me to make new C23 features ""backwards-compatible"" (more like backwards-equivalent).
    Features such as nullptr.
    Also allows to predefine cool new macros using C23 features while also having (less secure/featurefull) variants for older C versions.
    I believe in C23 features, but also try not to "discriminate" against older compilers/versions where I can.

* ((void *){0} = ptr)
    A technique lifted from Modern C book.
    This is a void pointer compound literal wrapper to avoid directly casting,
    essentially being a "safer" c-style cast more akin to
    ```cpp
        static_cast<ptrtypeA>(ptrtypeB)
    ```
    Non-pointer values will throw an error if attempted to use with the compound literal.

</em>
