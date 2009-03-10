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

#pragma once
#ifndef __AUTO_THREAD__
#define __AUTO_THREAD__


#include "AutoDefs.h"


namespace Auto {

    //
    // Forward declarations
    //
    class MemoryScanner;
    class Zone;



    //----- NonVolatileRegisters -----//
    
    //
    // Used to capture the register state of the current thread.
    //

#if defined(__ppc__) || defined(__ppc64__)
    
    class NonVolatileRegisters {

      private:
      
        enum {
            first_nonvolatile_register = 13,                // used to capture registers
            number_of_nonvolatile_registers = 32 - first_nonvolatile_register,
                                                            // used to capture registers
        };
        
        usword_t _registers[number_of_nonvolatile_registers];// buffer for capturing registers
        
        //
        // capture_registers
        //
        // Capture the state of the non-volatile registers.
        //
        static inline void capture_registers(register usword_t *registers) {
#if defined(__ppc__)
            __asm__ volatile ("stmw r13,0(%[registers])" : : [registers] "b" (registers) : "memory");
#else
            __asm__ volatile ("std r13,0(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r14,8(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r15,16(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r16,24(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r17,32(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r18,40(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r19,48(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r20,56(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r21,64(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r22,72(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r23,80(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r24,88(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r25,96(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r26,104(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r27,112(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r28,120(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r29,128(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r30,136(%[registers])" : : [registers] "b" (registers) : "memory");
            __asm__ volatile ("std r31,144(%[registers])" : : [registers] "b" (registers) : "memory");
#endif
        }

      public:
      
        //
        // Constructor
        //
        NonVolatileRegisters() { capture_registers(_registers); }
        
        
        //
        // buffer_range
        //
        // Returns the range of captured registers buffer.
        //
        inline Range buffer_range() { return Range(_registers, sizeof(_registers)); }
        
    };

#elif defined(__i386__)
    
    class NonVolatileRegisters {
      private:
        // Non-volatile registers are: ebx, ebp, esp, esi, edi
        usword_t _registers[5];  // buffer for capturing registers
        
        //
        // capture_registers
        //
        // Capture the state of the non-volatile registers.
        //
        static inline void capture_registers(register usword_t *registers) {
            __asm__ volatile ("mov %%ebx,  0(%[registers]) \n" 
                              "mov %%ebp,  4(%[registers]) \n" 
                              "mov %%esp,  8(%[registers]) \n" 
                              "mov %%esi, 12(%[registers]) \n" 
                              "mov %%edi, 16(%[registers]) \n" 
                              : : [registers] "a" (registers) : "memory");
        }

      public:
      
        //
        // Constructor
        //
        NonVolatileRegisters() { capture_registers(_registers); }
        
        
        //
        // buffer_range
        //
        // Returns the range of captured registers buffer.
        //
        inline Range buffer_range() { return Range(_registers, sizeof(_registers)); }
        
    };

#elif defined(__x86_64__)
    
    class NonVolatileRegisters {
      private:
        // Non-volatile registers are: rbx rsp rbp r12-r15
        usword_t _registers[7];  // buffer for capturing registers
        
        //
        // capture_registers
        //
        // Capture the state of the non-volatile registers.
        //
        static inline void capture_registers(register usword_t *registers) {
            __asm__ volatile ("movq %%rbx,  0(%[registers]) \n" 
                              "movq %%rsp,  8(%[registers]) \n" 
                              "movq %%rbp, 16(%[registers]) \n" 
                              "movq %%r12, 24(%[registers]) \n" 
                              "movq %%r13, 32(%[registers]) \n" 
                              "movq %%r14, 40(%[registers]) \n" 
                              "movq %%r15, 48(%[registers]) \n" 
                              : : [registers] "a" (registers) : "memory");
        }

      public:
      
        //
        // Constructor
        //
        NonVolatileRegisters() { capture_registers(_registers); }
        
        
        //
        // buffer_range
        //
        // Returns the range of captured registers buffer.
        //
        inline Range buffer_range() { return Range(_registers, sizeof(_registers)); }
        
    };

#else
#error Unknown Architecture
#endif


    //----- Thread -----//
    
    //
    // Track threads that need will be scanned during gc.
    //
    
    class Thread : public AuxAllocated {
    
      private:
            
        Thread      *_next;                                 // next thread in linked list
        Zone        *_zone;                                 // managing zone
        pthread_t   _pthread;                               // posix thread
        mach_port_t _thread;                                // mach thread
        void        *_stack;                                // cached value of pthread_get_stackaddr_np(_pthread)
        bool        _exiting;                               // used to record that the thread is exiting
        bool        _deadPort;                              // set true if the mach port found dead (missing unregister_thread)
        uint32_t    _suspended;                             // records suspend count.
        uint32_t    _retains;                               // tracks number of calls to Zone::register_thread().
        
        
        
        //
        // scan_current_thread
        //
        // Scan the current thread stack and registers for block references.
        //
        void scan_current_thread(MemoryScanner &scanner);


        //
        // scan_other_thread
        //
        // Scan a thread other than the current thread stack and registers for block references.
        //
        void scan_other_thread(MemoryScanner &scanner);


      public:
      
      
        //
        // Constructor
        //
        Thread(Zone *zone, pthread_t pthread, mach_port_t thread)
            : _next(NULL), _zone(zone), _pthread(pthread), _thread(thread), _stack(pthread_get_stackaddr_np(pthread)),
              _exiting(false), _deadPort(false), _suspended(0), _retains(0)
        {
        }
        
        
        //
        // Accessors
        //
        inline Thread      *next()                { return _next; }
        inline Zone        *zone()                { return _zone; }
        inline pthread_t   pthread()              { return _pthread; }
        inline mach_port_t thread()               { return _thread; }
        inline bool        is_exiting()           { return _exiting; }
        inline void        set_next(Thread *next) { _next = next; }
        inline void        set_is_exiting()       { _exiting = true; }
        inline void        retain()               { ++_retains; }
        inline uint32_t    release()              { return --_retains; }
        inline bool        deadPort()             { return _deadPort; }
        
        
        //
        // is_current_thread
        //
        // Returns true if the this thread is the current thread.
        //
        inline bool is_current_thread() const {
            return pthread_self() == _pthread;
        }
        
        
        //
        // unlink
        //
        // Unlink the thread from the list of threads.
        //
        inline void unlink(Thread **link) {
            for (Thread *t = *link; t; link = &t->_next, t = *link) {
                // if found
                if (t == this) {
                    // mend the link
                    *link = t->_next;
                    break;
                }
            }
        }

        
        //
        // scan_thread_with_suspend_and_closure
        //
        // Scan the current thread stack and registers for block references.  This
        // performs a complete scan of a suspended thread looking for roots.  This method
        // is called as a last pass during scanning to catch all thread's block references.
        //
        void scan_thread_with_suspend_and_closure(MemoryScanner &scanner);


        //
        // scan_thread_without_suspend
        //
        // Scan the current thread stack and registers for block references.  This
        // performs a fast scan an unsuspended thread looking for roots. This method
        // is called as a first pass during scanning to catch most of the thread's 
        // block references.
        //
        void scan_thread_without_suspend(MemoryScanner &scanner);


        //
        // suspend
        //
        // Temporarily suspend the thread from further execution.  Returns true if the thread is
        // still alive.
        //
        bool suspend();


        //
        // resume
        //
        // Resume a suspended thread.
        //
        bool resume();


        //
        // destroy_registered_thread
        //
        // Since the pthreads library automatically clears the registered thread tsd before calling us,
        // we set it back, to keep the thread registration alive until somebody calls unregister_thread().
        //
        static void destroy_registered_thread(void *data);
        
        
    };

};

#endif // __AUTO_THREAD__

