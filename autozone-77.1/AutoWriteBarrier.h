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
#ifndef __AUTO_WRITEBARRIER__
#define __AUTO_WRITEBARRIER__

#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoRange.h"


namespace Auto {

    
    //
    // Forward declarations
    //
    class MemoryScanner;
    class Zone;
    

    //----- WriteBarrier -----//

    class WriteBarrier : public Range {
    
      private:
      
        void           *_base;                              // base address of managed range
        usword_t       _protect;                            // protected space to base of write barrier
        
      public:

        //
        // card values
        //
        enum {
            card_unmarked = 0,
            card_marked = 1,
            card_marked_untouched = 0x3
        };
      
      
        //
        // Constructor
        //
        // Set up cached allocation of use.
        //
        WriteBarrier(void *base, void *address, const usword_t size, const usword_t protect = 0)
            : Range(address, size), _base(base), _protect(protect)
        {
        }

        
        //
        // bytes_needed
        //
        // Returns the number of write barrier bytes needed to represent 'n' actual bytes.
        //
        static inline const usword_t bytes_needed(usword_t n) {
            return partition2(n, write_barrier_quantum_log2);
        }
        
        
        //
        // card_index
        //
        // Return the write barrier card index for the specified address.
        //
        inline const usword_t card_index(void *address) const {
            uintptr_t normalized = (uintptr_t)address - (uintptr_t)_base;
            usword_t i = normalized >> write_barrier_quantum_log2;
            ASSERTION(_protect <= i);
            ASSERTION(i < size());
            return i;
        }
        
        
        //
        // card_address
        //
        // Return the base address of the range managed by the specified card index.
        //
        inline void *card_address(usword_t i) const { return displace(_base, i << write_barrier_quantum_log2); }
        
        
        //
        // is_card_marked
        //
        // Test to see if card i is marked.
        //
        inline bool is_card_marked(usword_t i) { return ((unsigned char *)address())[i] != card_unmarked; }
        
        
        //
        // mark_card
        //
        // Marks the card at index i.
        //
        inline void mark_card(usword_t i) { ((unsigned char *)address())[i] = card_marked; }

        //
        // mark_card_untouched
        //
        // Used to indicate a card is speculatively marked, but potentially clearable, unless
        // remarked during full GC scanning.
        //
        inline void mark_card_untouched(usword_t i) { ((unsigned char *)address())[i] = card_marked_untouched; }
        
        // clear all the cards in the write barrier
        inline void clear_cards() { bzero(displace(address(), _protect), size() - _protect); }
        
        // TODO:  better commentary.
        void mark_cards_untouched();
        void clear_untouched_cards();
        
        //
        // is_card_marked
        //
        // Checks to see if the card corresponding to .
        //
        inline bool is_card_marked(void *address) {
            usword_t i = card_index(address);
            return is_card_marked(i);
        }
        
        
        //
        // mark_card
        //
        // Mark the write barrier card for the specified address.
        //
        inline void mark_card(void *address) {
            const usword_t i = card_index(address);
            mark_card(i);
        }
        
        
        //
        // mark_cards
        //
        // Mark the write barrier cards corresponding to the specified address range.
        //
        inline void mark_cards(void *address, const usword_t size) {
            usword_t i = card_index(address);
            const usword_t j = card_index(displace(address, size - 1));
            for ( ; i <= j; i++) mark_card(i);
        }

        
        //
        // scan_ranges
        //
        // Scan ranges in block that are marked in the write barrier.
        //
        void scan_ranges(void *address, const usword_t size, MemoryScanner &scanner);
        
        
    };

    
};


#endif // __AUTO_WRITEBARRIER__

