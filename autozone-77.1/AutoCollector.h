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
#ifndef __AUTO_COLLECTOR__
#define __AUTO_COLLECTOR__

#include "AutoDefs.h"
#include "AutoList.h"
#include "AutoListTypes.h"
#include "AutoRange.h"
#include "AutoMemoryScanner.h"


namespace Auto {

    //
    // Forward declarations.
    //
    class Zone;
    

    //----- Collector -----//
    
    //
    // Responsible for garbage collection.
    //
    

    class Collector : public MemoryScanner {
    
      private:
      
        bool                      _is_partial;                              // is generational collection
      
      public:
      
        auto_date_t               scan_end;
      
        //
        // Constructor
        //
        Collector(Zone *zone, void *current_stack_bottom, bool is_partial)
        : MemoryScanner(zone, current_stack_bottom, is_partial, false)
        , _is_partial(is_partial)
        {
            // mark instance as a collecting MemoryScanner subclass.
            _is_collector = true;
        }
        virtual ~Collector() {}
        
        
        //
        // check_roots
        //
        // Scan root blocks.
        //
        virtual void check_roots();
        

        //
        // collect
        //
        // Collect scans memory for reachable objects.  Unmarked blocks are available to
        // be garbaged.
        //
        void collect(bool use_pending);
        
        
        //
        // scan_barrier
        //
        // Used by collectors to synchronize with concurrent mutators.
        //
        virtual void scan_barrier();
    };

};

#endif // __AUTO_COLLECTOR__

