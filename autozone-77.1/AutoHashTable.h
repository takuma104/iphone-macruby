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
#ifndef __AUTO_HASHTABLE__
#define __AUTO_HASHTABLE__

#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoRange.h"


namespace Auto {

    //----- HashTable -----//
    
    //
    // Manages a closed hash table of ranges.
    //
    
    class HashTable {
    
      private:
      
        enum {
            initial_size_log2 = 8,                          // initial length log2
            maximum_depth     = 8,                          // maximum search depth
        };
        
        Range    **_ranges;                                 // array of range pointers
        usword_t _length_log2;                              // ilog2(length), length = 1 << _length_log2;
        
        
        //
        // next
        //
        // Return the next slot in array.
        //
        inline Range **next(Range **slot) const { return _ranges + ((slot - _ranges + 1) & mask(_length_log2)); }
        
        
        //
        // hash
        // 
        // Return the hash of a specified range.
        //
        inline const usword_t hash(register void *address) const {
            usword_t addr = (uintptr_t)address;
            // rotation choices based on empirical study
            usword_t hash = rotate_bits_right(addr, 6);
            hash ^= rotate_bits_right(addr, 9);
            hash ^= rotate_bits_right(addr, 16);
            hash ^= rotate_bits_right(addr, 24);
            return hash & mask(_length_log2);
        }
      
      
        //
        // rehash
        //
        // Rehash all the entries of the table.
        //
        bool rehash(Range **ranges, usword_t length);

        
        //
        // grow 
        // 
        // Grow hash table to accommodate more ranges.
        //
        void grow();
                
        
        //
        // find_slot
        //
        // Find the slot the range should reside.
        //
        Range **find_slot(void *address);
        

        //
        // insert
        //
        // Inserts an entry if there is a slot.
        //
        bool insert(Range *range);


      public:

        HashTable() : _ranges(NULL), _length_log2(0) {}

        //
        // initialize
        //
        // Set up the hash table.
        //
        void initialize();

        
        //
        // dispose
        //
        // Release memory allocated for the hash table.
        //
        void dispose();
        
        
        //
        // add
        //
        // Add a range to the hash table.
        //
        void add(Range *range);

        
        //
        // find
        //
        // Return the entry corresponding to the specified address
        //
        inline Range *find(void *address) {
            // find the slot
            Range **slot = find_slot(address);
            
            // if the slot is found it is either NULL or the object else return NULL
            return slot ? *slot : NULL;
        }


        //
        // is_member
        //
        // Returns true if the range is in the hash table.
        //
        inline const bool is_member(void *address) { return find(address) != NULL; }
        
        
        //
        // remove
        //
        // Remove an entry from the table.
        //
        void remove(Range *range);
        
        
        //
        // clear
        //
        // Remove all entries from the table
        //
        inline void clear() { if (_ranges) bzero(_ranges, (1 << _length_log2) * sizeof(Range *)); }
        
        
    };


};


#endif // __AUTO_HASHTABLE__
