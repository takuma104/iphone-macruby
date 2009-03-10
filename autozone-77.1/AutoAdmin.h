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
#ifndef __AUTO_ADMIN__
#define __AUTO_ADMIN__

#include "AutoBitmap.h"
#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoFreeList.h"
#include "AutoRange.h"
#include "AutoStatistics.h"
#include "auto_impl_utilities.h"

namespace Auto {

    // Forward declarations
    //
    class Region;
    class Subzone;
    class Zone;
    
    
    //----- Admin -----//
    
    class Admin {
    
        enum {
            cache_size = maximum_quanta + 1                 // size of the free list cache 
        };
    
      private:
      
        Zone           *_zone;                              // managing zone
        Region         *_region;                            // containing region
        usword_t       _quantum_log2;                       // ilog2 of the quantum used in this admin
        FreeList       _cache[cache_size];                  // free lists, one for each quanta size, slot 0 is for large clumps
        Subzone        *_active_subzone;                    // subzone with unused memory
        spin_lock_t    _admin_lock;                         // protects free list, subzone data.
        
      public:
      
        //
        // Accessors
        //
        Zone *zone()            const { return _zone; }
        Region *region()        const { return _region; }
        usword_t quantum_log2() const { return _quantum_log2; }
        spin_lock_t *lock()           { return &_admin_lock; }
      

        //
        // is_small
        //
        // Return true if it is a small admin.
        //
        inline bool is_small() const { return _quantum_log2 == allocate_quantum_small_log2; }
        
        
        //
        // is_medium
        //
        // Return true if it is a medium admin.
        //
        inline bool is_medium() const { return _quantum_log2 == allocate_quantum_medium_log2; }
        
        
        //
        // quantum_count
        //
        // Returns a number of quantum for a given size.
        //
        inline const usword_t quantum_count(const size_t size) const {
            return partition2(size, _quantum_log2);
        }
        

        //
        // unused_count
        //
        // Returns a number of quantum for a given size.
        //
        usword_t unused_count();
        

        //
        // active_subzone
        //
        // Returns the most recently added subzone
        //
        inline Subzone *active_subzone() { return _active_subzone; }
        

        //
        // set_active_subzone
        //
        // Remember the most recently added subzone.  This holds never used space.
        //
        inline void set_active_subzone(Subzone *sz) { _active_subzone = sz; }
        

        //
        // cache_slot
        //
        // Return the cache slot a free size resides.
        inline usword_t cache_slot(usword_t size) const {
            usword_t n = quantum_count(size);
            return n < cache_size ? n : 0;
        }

        
        //
        // initialize
        //
        // Set up the admin for initial use.
        //
        void initialize(Zone *zone, Region *region, const usword_t quantum_log2);


        //
        // free_space()
        //
        // Sums the free lists.
        //
        usword_t free_space();
        
        
        //
        // empty_space()
        //
        // Returns the size of the space that has yet to be allocated.
        //
        usword_t empty_space();
        
        
        //
        // test_freelist_integrity
        //
        // Returns true if the free list seems to be okay.
        //
        bool test_freelist_integrity();
        
        //
        // test_node_integrity
        //
        // Returns true if the free list node seems to be okay.
        //
        bool test_node_integrity(FreeListNode *node);
                        
        //
        // find_allocation
        //
        // Find the next available quanta for the allocation.  Returns NULL if none found.
        // Allocate otherwise.
        //
        void *find_allocation(const usword_t size, const unsigned layout, const bool refcount_is_one, bool &did_grow);


        //
        // deallocate
        //
        // Mark address as available.
        // Currently, this relinks it onto the free lists & clears the side table data.
        //
        void deallocate(void *address);
        
        
        //
        // is_pending
        //
        // Used by subzone to get pending bit for quantum.
        //
        bool is_pending(usword_t q);
        
        
        //
        // clear_pending
        //
        // Used by subzone to clear pending bit for quantum.
        //
        void clear_pending(usword_t q);
        
        
        //
        // set_pending
        //
        // Used by subzone to set pending bit for quantum.
        //
        void set_pending(usword_t q);
        
        //
        // set_mark
        //
        // Used by subzone to set mark bit for quantum.
        //
        void set_mark(usword_t q);
        
        //
        // is_marked
        //
        // Used by subzone to get mark bit for quantum.
        //
        bool is_marked(usword_t q);
        
        //
        // clear_mark
        //
        // Used by subzone to clear mark bit for quantum.
        //
        void clear_mark(usword_t q);
        
        //
        // test_set_mark
        //
        // Used by subzone to test and set mark bit for quantum.
        //
        bool test_set_mark(usword_t q);
        
      private:
        //
        // pop_node
        //
        // Pops a node from the specified FreeList. Also
        // performs node consistency checks.
        //
        FreeListNode *pop_node(usword_t index);
        
        //
        // mark_allocated
        //
        // Set tables with information for new allocation.
        // Must be called with _cache_lock acquired.
        //
        void mark_allocated(void *address, const usword_t n, const unsigned layout, const bool refcount_is_one);
        
    };
        
};


#endif // __AUTO_ADMIN__
