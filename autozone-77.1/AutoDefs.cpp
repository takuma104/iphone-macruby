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
#include "AutoEnvironment.h"
#include "AutoZone.h"


#if defined(AUTO_STANDALONE)
bool auto_prelude_log_thread = false;                       // change this to true to have thread id printed
#endif

namespace Auto {
    
    //
    // Shadow malloc zone functions
    //
#if defined(AUTO_STANDALONE)
    malloc_zone_t *aux_zone = NULL;                         // malloc zone used to make small allocations
#endif


    //
    // micro_time
    // 
    // Returns execution time in microseconds (not real time.)
    //
    uint64_t micro_time() {
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        return ((uint64_t)usage.ru_utime.tv_sec * (uint64_t)1000000) + (uint64_t)usage.ru_utime.tv_usec +
               ((uint64_t)usage.ru_stime.tv_sec * (uint64_t)1000000) + (uint64_t)usage.ru_stime.tv_usec;
    }
    

    //
    // nano_time
    // 
    // Returns machine time in nanoseconds (rolls over rapidly.)
    //
    double nano_time() {
        static mach_timebase_info_data_t timebase_info;
        static double scale = 1.0;
        static uint64_t delta; 
        if (!timebase_info.denom) {
            mach_timebase_info(&timebase_info);
            scale = ((double)timebase_info.numer / (double)timebase_info.denom) * 1.0E-9;
            delta = mach_absolute_time();
        }
        return (double)(mach_absolute_time() - delta) * scale;
    }

    mach_timebase_info_data_t &StopWatch::cached_timebase_info() {
        static mach_timebase_info_data_t timebase_info;
        if (!timebase_info.denom) mach_timebase_info(&timebase_info);
        return timebase_info;
    }
    
#if defined(AUTO_STANDALONE)
    //
    // auto_prelude
    //
    // Generate the prelude used for error reporting
    //
    const char *prelude(void) {
        static bool key_created = false; 
        static pthread_key_t key;
        
        if (!key_created) {
            key_created = true;
            if (pthread_key_create(&key, free)) {
                malloc_printf("*** Error creating thread_key\n");
            }
        }
        
        char    *buf = (char *)pthread_getspecific(key);
        
        if (!buf) {
            buf = (char *)aux_malloc(48);
            
            if (auto_prelude_log_thread) {
                unsigned thread = (unsigned)(&buf);
                malloc_printf(buf, "auto malloc[%d#%p]", getpid(), thread >> 12);
            } else {
                malloc_printf(buf, "auto malloc[%d]", getpid());
            }
            
            pthread_setspecific(key, buf);
        }
        return (const char *)buf;
    }
 #endif


    //
    // Shadow malloc zone functions
    //
#if defined(AUTO_STANDALONE)
    extern malloc_zone_t *aux_zone;                    // malloc zone used to make small allocations
#endif
    void *aux_malloc(size_t size) {
        ASSERTION(aux_zone);
        void *new_ptr = malloc_zone_malloc(aux_zone, size);
#if defined(DEBUG)
        if (Environment::_agc_env._print_allocs) malloc_printf("malloc_zone_malloc @%p %d\n", new_ptr, size);
#endif
        return new_ptr;
    }
    void *aux_calloc(size_t count, size_t size) {
        ASSERTION(aux_zone);
        void *new_ptr = malloc_zone_calloc(aux_zone, count, size);
#if defined(DEBUG)
        if (Environment::_agc_env._print_allocs) malloc_printf("malloc_zone_calloc @%p %d\n", new_ptr, count * size);
#endif
        return new_ptr;
    }
    void *aux_valloc(size_t size) {
        ASSERTION(aux_zone);
        void *new_ptr = malloc_zone_valloc(aux_zone, size);
#if defined(DEBUG)
        if (Environment::_agc_env._print_allocs) malloc_printf("malloc_zone_valloc @%p %d\n", new_ptr, size);
#endif
        return new_ptr;
    }
    void *aux_realloc(void *ptr, size_t size) {
        ASSERTION(aux_zone);
        void *new_ptr = malloc_zone_realloc(aux_zone, ptr, size);
#if defined(DEBUG)
        if (Environment::_agc_env._print_allocs) malloc_printf("malloc_zone_realloc @%p %d\n", new_ptr, size);
#endif
        return new_ptr;
    }
    void aux_free(void *ptr) {
        ASSERTION(aux_zone);
#if defined(DEBUG)
        if (Environment::_agc_env._print_allocs) malloc_printf("malloc_zone_free @%p\n", ptr);
#endif
        malloc_zone_free(aux_zone, ptr);
    }


};
