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
#include "AutoHashTable.h"
#include "AutoRange.h"

namespace Auto {

    //----- HashTable -----//
    
    
    //
    // rehash
    //
    // Rehash all the entries of the table.
    //
    bool HashTable::rehash(Range **ranges, usword_t length) {
       // pointer to current entry 
        Range **cursor = ranges;
        
        // make sure we have an end bounds which is a multiple of chuck
        Range **near_end = cursor + (length & ~3);
        
        // real end bounds 
        Range **end = cursor + length;
        
        // rehash a chunk at a time
        while (cursor < near_end) {
            Range *range0 = cursor[0];
            Range *range1 = cursor[1];
            Range *range2 = cursor[2];
            Range *range3 = cursor[3];
            cursor += 4;
            if (range0 && !insert(range0)) return false;
            if (range1 && !insert(range1)) return false;
            if (range2 && !insert(range2)) return false;
            if (range3 && !insert(range3)) return false;
        }
        
        // rehash leftovers
        while (cursor < end) {
            Range *range = *cursor++;
            if (range && !insert(range)) return false;
        }
        
        return true;
    }
    
    
    //
    // grow 
    // 
    // Grow hash table to accommodate more ranges
    //
    void HashTable::grow() {
        // capture old information
        Range **old_ranges = _ranges;
        usword_t old_length = 1 << _length_log2;
        
        // calculate new length log2
        _length_log2 = old_ranges ? _length_log2 + 1 : initial_size_log2;
        // allocate and clear new ranges
        _ranges = (Range **)aux_calloc(1 << _length_log2, sizeof(Range **));
        
        if (old_ranges) {
            // rehash old entries
            while (!rehash(old_ranges, old_length)) {
                // need to grow the table
                aux_free(_ranges);
               
                 // calculate new length
                _length_log2++;
                
                // allocate and clear new ranges
                _ranges = (Range **)aux_calloc(1 << _length_log2, sizeof(Range **));
            }
            
            // release old hashtable
            aux_free(old_ranges);
        }
    }
    
    
    //
    // find_slot
    //
    // Find the slot the range should reside.
    //
    Range **HashTable::find_slot(void *address) {
        // check if table is allocated
        if (!_ranges) return NULL;
        
        // ptr  hash
        const usword_t h = hash(address);
        // current slot
        Range **slot = _ranges + h;

        // iterate for the closed hash depth
        for (unsigned depth = 0; depth < maximum_depth; depth++, slot = next(slot)) {
            // get old member
            Range *old_member = *slot;
            
            // return the slot if the slot is empty or already contains the address
            if (!old_member || old_member->address() == address) return slot;
            
            // otherwise check to see if the entry is a member of the same hash group
            if (h != hash(old_member->address())) return NULL;
        }
        
        // not found
        return NULL;
    }
    

    //
    // initialize
    //
    // Set up the hash table.
    //
    void HashTable::initialize() {
        _length_log2 = 0;
        _ranges = NULL;
    }

    
    //
    // dispose
    //
    // Release memory allocated for the hash table.
    //
    void HashTable::dispose() {
        if (_ranges) aux_free(_ranges);
        _length_log2 = 0;
        _ranges = NULL;
    }
    
    
    //
    // insert
    //
    // Inserts an entry if there is a slot.
    //
    bool HashTable::insert(Range *range) {
        // find the slot for the range
        Range **slot = find_slot(range->address());
        
        // if slot available
        if (slot) {
            // set the slot (may be redundant if the range is already in hashtable)
            *slot = range;
        }
        
        // not inserted
        return slot != NULL;
    }


    //
    // add
    //
    // Add a Range to the hash table.
    //
    void HashTable::add(Range *range) {
        // don't add NULL entries
        if (!range || !range->address()) return;
        
        // try and try again
        while (!insert(range)) grow();
    }

    
    //
    // remove
    //
    // Remove an entry from the table.
    //
    void HashTable::remove(Range *range) {
        // get range address
        void *address = range->address();
        
        // find the slot
        Range **slot = find_slot(address);
        
        // if the pointer is found
        if (slot && (*slot)->address() == address) {
            // find out which hash group it belongs
            const usword_t h = hash(address);
            
            // searching for other members to fill in gap
            for (Range **next_slot = next(slot); true; next_slot = next(next_slot)) {
                // get the next candidate
                Range *old_member = *next_slot;
                
                // if NULL or not member of the same hash group
                if (!old_member || h != hash(old_member->address())) {
                    // NULL out the last slot in the group
                    *slot = NULL;
                    break;
                }
                
                // shift down the slots
                *slot = *next_slot;
                slot = next_slot;
            }
        }
    }


};
