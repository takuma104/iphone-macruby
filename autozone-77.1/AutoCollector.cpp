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

#include "AutoCollector.h"
#include "AutoDefs.h"
#include "AutoEnvironment.h"
#include "AutoList.h"
#include "AutoListTypes.h"
#include "AutoRange.h"
#include "AutoWriteBarrier.h"
#include "AutoZone.h"


namespace Auto {

    //----- Collector -----//
    
        
    //
    // check_roots
    //
    // Scan root blocks.
    //
    void Collector::check_roots() {
        if (_is_partial) {
            scan_retained_and_old_blocks();
        } else {
            scan_retained_blocks();
        }
       
        scan_root_ranges();
    }


    //
    // collect
    //
    // Collect scans memory for reachable objects.  Unmarked blocks are available to
    // be garbaged.  Returns "uninterrupted"
    //
    void Collector::collect(bool use_pending) {
        // scan memory
        if (use_pending)
            _zone->set_use_pending();
        else
            _zone->clear_use_pending();
        scan();
    }


    //
    // scan_barrier
    //
    // Used by collectors to synchronize with concurrent mutators.
    //
    void Collector::scan_barrier() {
        scan_end = auto_date_now();
        // write barriers should no longer repend blocks
        // NOTE:  this assumes NO THREADS ARE SUSPENDED at this point.
        // NOTE: we exit scanning with the enlivening lock held, so that Zone::collect() can
        // hold onto it until weak references and generational side data is cleared.
        spin_lock(_zone->enlivening_lock());
        PointerList& enlivening_queue = _zone->enlivening_queue();
        // pointers in the enlivening queue need to be transitively scanned.
        void **buffer = (void**)enlivening_queue.buffer();
        for (usword_t i = 0, count = enlivening_queue.count(); i < count; ++i) {
            _zone->repend(buffer[i]);
        }
        enlivening_queue.clear_count();
        enlivening_queue.uncommit();
    }
};

