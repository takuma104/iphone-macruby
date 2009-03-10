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

#pragma
#ifndef __AUTO_ENVIRONMENT__
#define __AUTO_ENVIRONMENT__

namespace Auto {


    class Environment {
    
      public:
#if defined(DEBUG)
        bool _clear_all_new     : 1;                        // clear all new blocks
        bool _dirty_all_new     : 1;                        // dirty all new blocks
        bool _unsafe_scan       : 1;                        // perform final scan when threads are not suspended
        bool _print_stats       : 1;                        // print statistics after collection
        bool _print_scan_stats  : 1;                        // print scanning statistics
        bool _print_allocs      : 1;                        // print vm and malloc allocations and deallocations
        bool _guard_pages       : 1;                        // create guard pages for blocks >= page_size
#else
        enum {
            _clear_all_new = 0,                             // clear all new blocks
            _dirty_all_new = 0,                             // dirty all new blocks
            _unsafe_scan = 0,                               // perform final scan when threads are not suspended
            _print_stats = 0,                               // print statistics after collection
            _print_scan_stats = 0,                          // print scanning statistics
            _print_allocs = 0,                              // print vm and malloc allocations and deallocation
            _guard_pages = 0,                               // create guard pages for blocks >= page_size
        };
#endif
        bool _dirty_all_deleted : 1;                        // dirty all deleted blocks
        bool _enable_monitor    : 1;                        // enable the external debug monitor
        bool _use_exact_scanning : 1;                       // trust exact layouts when scanning
        
        // watch out for static initialization
        static Environment _agc_env;                        // global containing flags
      
        //
        // initialize
        //
        // Reads the environment variables values.
        //
        void initialize();
      
      
    };


};


#endif // __AUTO_ENVIRONMENT__
