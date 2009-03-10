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


namespace Auto {

    Environment Environment::_agc_env;                      // global containing flags
        
        
    //
    // initialize
    //
    // Reads the environment variables values.
    //
    void Environment::initialize() {
#if defined(DEBUG)
        _clear_all_new     = getenv("AUTO_CLEAR_ALL_NEW")     ? true : false;
        _dirty_all_new     = getenv("AUTO_DIRTY_ALL_NEW")     ? true : false;
        _unsafe_scan       = getenv("AUTO_UNSAFE_SCAN")       ? true : false;
        _print_stats       = getenv("AUTO_PRINT_STATS")       ? true : false;
        _print_scan_stats  = getenv("AUTO_SCAN_PRINT_STATS")  ? true : false;
        _guard_pages       = getenv("AUTO_USE_GUARDS")        ? true : false;
#endif
        _dirty_all_deleted = getenv("AUTO_DIRTY_ALL_DELETED") || getenv("MallocScribble") ? true : false;
        _enable_monitor    = getenv("AUTO_ENABLE_MONITOR")    ? true : false;
        { const char *s = getenv("AUTO_USE_EXACT_SCANNING"); _use_exact_scanning = !s || strcmp(s, "NO"); }  // on by default
        //{ const char *s = getenv("AUTO_USE_EXACT_SCANNING"); _use_exact_scanning = s && !strcmp(s, "YES"); }  // off by default
    }

};
