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

#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoRange.h"
#include "AutoMemoryScanner.h"
#include "AutoWriteBarrier.h"
#include "AutoZone.h"


namespace Auto {

    //----- WriteBarrier -----//
    
    
    //
    // scan_ranges
    //
    // Scan ranges in block that are marked in the write barrier.
    //
    void WriteBarrier::scan_ranges(void *address, const usword_t size, MemoryScanner &scanner) {
        // determine the end address
        void *end = displace(address, size);
        // determine the last used address
        void *last = displace(address, size - 1);
        // get the write barrier index for the begining of the block
        usword_t i = card_index(address);
        // get the write barrier index for the end of the block
        const usword_t j = card_index(last);
        
        WriteBarrier *wb = scanner.zone()->repair_write_barrier() ? this : NULL;
        while (true) {
            // skip over unmarked ranges
            for ( ; i <= j && !is_card_marked(i); i++) {}
            
            // if no marks then we are done
            if (i > j) break;
            
            // scan the marks
            usword_t k = i;
            for ( ; i <= j && is_card_marked(i); i++) {}
            
            // set up the begin and end of the marked range
            void *range_begin = card_address(k);
            void *range_end = card_address(i);
            
            // truncate the range to reflect address range
            if (range_begin < address) range_begin = address;
            if (range_end > end) range_end = end;

            // scan range
            Range range(range_begin, range_end);
            scanner.scan_range(range, wb);
        }
    }

    // this is only called from Zone::mark_write_barriers_untouched().
    void WriteBarrier::mark_cards_untouched() {
        for (unsigned char *card = (unsigned char*)address() + _protect, *limit = (unsigned char *)end(); card != limit; ++card) {
            if (*card == card_marked_untouched) *card = card_marked_untouched;
        }
    }
    
    // this is only called from Zone::clear_untouched_write_barriers().
    void WriteBarrier::clear_untouched_cards() {
        for (unsigned char *card = (unsigned char*)address() + _protect, *limit = (unsigned char *)end(); card != limit; ++card) {
            if (*card == card_marked_untouched) *card = card_unmarked;
        }
    }
};
