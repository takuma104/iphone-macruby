/*
 * Copyright (c) 2006-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_APACHE_LICENSE_HEADER_START@
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * @APPLE_APACHE_LICENSE_HEADER_END@
 */
// cribbed from ~rc/Updates/Panther/Panther7B85/Projects/Libc/pthreads/pthread_internals.h

#include <mach/mach.h>
#include <mach/mach_error.h>
#include <sys/queue.h>      /* For POSIX scheduling policy & parameter */

/*
 * Compiled-in limits
 */
#undef _POSIX_THREAD_KEYS_MAX
#define _POSIX_THREAD_KEYS_MAX        128

#define _PTHREAD_SIG            0x54485244  /* 'THRD' */

/*
 * Threads
 */
typedef struct _pthread
{
    long           sig;       /* Unique signature for this structure */
    struct _pthread_handler_rec *cleanup_stack;
    pthread_lock_t lock;          /* Used for internal mutex on structure */
    u_int32_t   detached:8,
            inherit:8,
            policy:8,
            pad:8;
    size_t         guardsize;   /* size in bytes to guard stack overflow */
    int        pad0;
    struct sched_param param;
    struct _pthread_mutex *mutexes;
    struct _pthread *joiner;
    int         pad1;
    void           *exit_value;
    semaphore_t    death;       /* pthread_join() uses this to wait for death's call */
    mach_port_t    kernel_thread; /* kernel thread this thread is bound to */
    void           *(*fun)(void*);/* Thread start routine */
        void           *arg;          /* Argment for thread start routine */
    int        cancel_state;  /* Whether thread can be cancelled */
    int        err_no;      /* thread-local errno */
    void           *tsd[_POSIX_THREAD_KEYS_MAX];  /* Thread specific data */
        void           *stackaddr;     /* Base of the stack (is aligned on vm_page_size boundary */
        size_t         stacksize;      /* Size of the stack (is a multiple of vm_page_size and >= PTHREAD_STACK_MIN) */
    mach_port_t    reply_port;     /* Cached MiG reply port */
        void           *cthread_self;  /* cthread_self() if somebody calls cthread_set_self() */
        boolean_t      freeStackOnExit; /* Should we free the stack when we're done? */
    LIST_ENTRY(_pthread) plist;
} *_pthread_t;
