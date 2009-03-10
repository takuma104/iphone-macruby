/*
 * Copyright (c) 2004-2008 Apple Inc. All rights reserved.
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

#include "AutoDefs.h"
#include "AutoMemoryScanner.h"
#include "AutoThread.h"
#include "AutoZone.h"

#if defined(__ppc__) || defined(__ppc64__)
// get definitions for C_RED_ZONE.
// http://developer.apple.com/documentation/DeveloperTools/Conceptual/LowLevelABI/Articles/32bitPowerPC.html#//apple_ref/doc/uid/TP40002438-SW6
// http://developer.apple.com/documentation/DeveloperTools/Conceptual/LowLevelABI/Articles/64bitPowerPC.html#//apple_ref/doc/uid/TP40002471-SW17
// NOTE:  the following header file contradicts the public ppc64 ABI, specifying a larger value for C_RED_ZONE.
#include <architecture/ppc/cframe.h>
#elif defined(__i386__)
// 32-bit x86 uses no red zone.
#define C_RED_ZONE 0
#elif defined(__x86_64__)
// according to  http://www.x86-64.org/documentation/abi.pdf (page 15)
#define C_RED_ZONE 128
#else
#error Unknown Architecture
#endif

extern "C" char *__crashreporter_info__;

namespace Auto {

    //----- Thread -----//


    //
    // scan_current_thread
    //
    // Scan the current thread stack and registers for block references.
    //
    void Thread::scan_current_thread(MemoryScanner &scanner) {
        // capture non-volatile registers
        NonVolatileRegisters registers;
        
        // scan the registers
        Range range = registers.buffer_range();
        scanner.scan_range_from_registers(range, this, 0);

        // scan the stack
        range.set_range(scanner.current_stack_bottom(), _stack);
        scanner.scan_range_from_thread(range, this);
    }

    union ThreadState {
#if defined(__i386__)
        i386_thread_state_t  regs;
#define THREAD_STATE_COUNT i386_THREAD_STATE_COUNT
#define THREAD_STATE_FLAVOR i386_THREAD_STATE
#define THREAD_STATE_SP __esp
#elif defined(__ppc__)
        ppc_thread_state_t   regs;
#define THREAD_STATE_COUNT PPC_THREAD_STATE_COUNT
#define THREAD_STATE_FLAVOR PPC_THREAD_STATE
#define THREAD_STATE_SP __r1
#elif defined(__ppc64__)
        ppc_thread_state64_t regs;
#define THREAD_STATE_COUNT PPC_THREAD_STATE64_COUNT
#define THREAD_STATE_FLAVOR PPC_THREAD_STATE64
#define THREAD_STATE_SP __r1
#elif defined(__x86_64__)
        x86_thread_state64_t regs;
#define THREAD_STATE_COUNT x86_THREAD_STATE64_COUNT
#define THREAD_STATE_FLAVOR x86_THREAD_STATE64
#define THREAD_STATE_SP __rsp
#else
#error Unknown Architecture
#endif
        thread_state_data_t  data;
        
        void* get_stack_pointer() {
            return reinterpret_cast<void*>(regs.THREAD_STATE_SP - C_RED_ZONE);
        }
    };
    

    //
    // scan_other_thread
    //
    // Scan a thread other than the current thread stack and registers for block references.
    //
    void Thread::scan_other_thread(MemoryScanner &scanner) {
        if (_deadPort) return;
        // select the register capture flavor
        unsigned user_count = THREAD_STATE_COUNT;
        thread_state_flavor_t flavor = THREAD_STATE_FLAVOR;
        ThreadState state;

        // get the thread register state
        kern_return_t err = thread_get_state(_thread, flavor, state.data, &user_count);
        int retryCount = 0;
        
        // We saw KERN_ABORTED in a test case involving a tool that calls fork(). Typically a single retry succeeds.
        while (err == KERN_ABORTED && retryCount < 10) {
            //malloc_printf("*** %s: unable to get thread state %d. Retrying (retry count: %d)\n", prelude(), err, retryCount);
            retryCount++;
            err = thread_get_state(_thread, flavor, state.data, &user_count);
        }

        if (err == MACH_SEND_INVALID_DEST) { // happens if unregister_thread not called
            // XXX also mark it dead so that we can pull it off later under thread lock so that
            // this message only happens once
            malloc_printf("*** %s: mach thread port invalid, cannot scan registers or stack\n", prelude());
            _deadPort = true;
        } else if (err) {
            // this is a fatal error. the program will crash if we can't scan this thread's state.
            static char buffer[256];
            snprintf(buffer, sizeof(buffer), "scan_other_thread:  unable to get thread state:  err = %d, this = %p, _thread = %p, _exiting = %s\n", err, this, (void*)_thread, _exiting ? "YES" : "NO");
            __crashreporter_info__ = buffer;
            __builtin_trap();
        } else {
            // scan the registers
            Range range((void *)state.data, user_count * sizeof(natural_t));
            scanner.scan_range_from_registers(range, this, 0);
            
            // scan the stack
            range.set_range(state.get_stack_pointer(), _stack);
            scanner.scan_range_from_thread(range, this);
        }
    }


    //
    // scan_thread_with_suspend_and_closure
    //
    // Scan the current thread stack and registers for block references.  This
    // performs a complete scan of a suspended thread looking for roots.  This method
    // is called as a last pass during scanning to catch all thread's block references.
    //
    void Thread::scan_thread_with_suspend_and_closure(MemoryScanner &scanner) {
        if (_deadPort) return;
        // suspend thread to make sure maves are not moved
        suspend();
        
        // scan registers and stack
        if (is_current_thread()) scan_current_thread(scanner);
        else                     scan_other_thread(scanner);
        
        // complete transitive close of new values
        scanner.scan_pending_until_done();
        
        // always resume thread since the weak read-barrier is now in place.
        resume();
    }


    //
    // scan_thread_without_suspend
    //
    // Scan the current thread stack and registers for block references.  This
    // performs a fast scan an unsuspended thread looking for roots. This method
    // is called as a first pass during scanning to catch most of the thread's 
    // block references.
    //
    void Thread::scan_thread_without_suspend(MemoryScanner &scanner) {
        // scan registers and stack
        if (_deadPort) return;
        if (is_current_thread()) scan_current_thread(scanner);
        else                     scan_other_thread(scanner);
    }


    //
    // suspend
    //
    // Temporarily suspend the thread from further execution.  Returns true if the thread is
    // still alive.
    //
    bool Thread::suspend()  {
        // do not suspend this thread
        if (is_current_thread()) return true;
        
        if (_suspended == 0) {
            // request thread suspension
            kern_return_t err = thread_suspend(_thread);
            
            if (err != KERN_SUCCESS) {
                if (!_exiting) {
                    static char buffer[256];
                    snprintf(buffer, sizeof(buffer), "Thread::suspend:  unable to suspend a thread:  err = %d, this = %p, _thread = %p\n", err, this, (void*)_thread);
                    __crashreporter_info__ = buffer;
                    __builtin_trap();
                }
                return false;
            }
        }
        _suspended++;
        return true;
    }


    //
    // resume
    //
    // Resume a suspended thread.
    //
    bool Thread::resume() {
        // do not resume this thread
        if (is_current_thread()) return true;

        if (_suspended == 1) {
            // request thread resumption
            kern_return_t err = thread_resume(_thread);
            
            if (err != KERN_SUCCESS) {
                if (!_exiting) malloc_printf("*** %s: unable to resume a thread, err = %d\n", prelude(), err);
                return false;
            }
        }
        _suspended--;
        return true;
    }



    //
    // destroy_registered_thread
    //
    // Since the pthreads library automatically clears the registered thread tsd before calling us,
    // we set it back, to keep the thread registration alive until somebody calls unregister_thread().
    //
    void Thread::destroy_registered_thread(void *data) {
        Thread *thread = (Thread *)data;
        // reset tsd
        pthread_setspecific(thread->_zone->registered_thread_key(), thread);
        // mark the thread as destroyed
        thread->_exiting = true;
    }


    extern "C" void auto_print_registered_threads() {
        Zone *zone = Zone::zone();
        SpinLock lock(zone->threads_lock());
        Thread *thread = zone->threads();
        while (thread != NULL) {
            malloc_printf("thread = 0x%x, is_exiting = %s, _deadPort = %s\n", thread->thread(), thread->is_exiting() ? "YES" : "NO", thread->deadPort() ? "YES" : "NO");
            thread = thread->next();
        }
    }

};
