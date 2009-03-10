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

#include "AutoAdmin.h"
#include "AutoBitmap.h"
#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoZone.h"
#include "AutoLock.h"
#include <stdio.h>

#define COALESCE_BLOCKS

extern "C" char *__crashreporter_info__;

namespace Auto {

    //----- Admin -----//
        
        
    //
    // initialize
    //
    // Set up the admin for initial use.  Provided the data area used for the management tables, the quantum used
    // in the area managed, whether the tables are growable and whether it grows from the other end of the data.
    //
    void Admin::initialize(Zone *zone, Region *region, const usword_t quantum_log2) {
        _zone = zone;
        _region = region;
        _quantum_log2 = quantum_log2;
        _admin_lock = 0;
    }


    //
    // unused_count
    //
    // Quanta not on free list (anymore).  We shrink the heap when we can & let
    // allocations battle it out on the free lists first.
    //
    usword_t Admin::unused_count() {
        return _active_subzone->allocation_limit() - _active_subzone->allocation_count();
    }



    //
    // free_space()
    //
    // Sums the free lists.
    //
    usword_t Admin::free_space() {
        SpinLock lock(&_admin_lock);
        usword_t empty_space = 0;
        
        for (usword_t m = 0; m < cache_size; m++) {
            for (FreeListNode *node = _cache[m].head(); node != NULL; node = node->next()) {
                empty_space += node->size();
            }
        }
        
        return empty_space;
    }
    
    
    //
    // empty_space()
    //
    // Returns the size of the holes.
    //
    usword_t Admin::empty_space() {
        SpinLock lock(&_admin_lock);
        usword_t empty_space = 0;
        
        // iterate through each free list
        for (FreeListNode *node = _cache[0].head(); node != NULL; node = node->next()) {
            empty_space += node->size();
        }
        
        return empty_space;
    }
    
    bool Admin::test_node_integrity(FreeListNode *node) {
        bool node_is_valid = false;
        const Range &coverage = _zone->coverage();
        do {
            // make sure the node is a plausible address.
            if (!coverage.in_range(node)) break;
            
            Subzone *subzone = Subzone::subzone((void *)node);
            
            // get quantum number
            usword_t q = subzone->quantum_index(node->address());
            
            // make sure quantum number is in range
            if (q >= subzone->allocation_limit()) break;
            
            // make sure that address is exact quantum
            if (subzone->quantum_address(q) != node->address()) break;
            
            // make sure it is free
            if (subzone->is_used(q)) break;
            
            // check plausibility of next and previous pointers.
            FreeListNode *next = node->next();
            if (next && !coverage.in_range(next)) break;
            FreeListNode *prev = node->prev();
            if (prev && !coverage.in_range(prev)) break;
            
            // make sure of size redundancy
            if (node->size() != node->size_again()) break;
        
            node_is_valid = true;
        } while (0);
        
        if (!node_is_valid) {
            static char buffer[256];
            if (coverage.in_range(node)) {
                snprintf(buffer, sizeof(buffer), "test_node_integrity:  FreeListNode %p { _prev = %p, _next = %p, _size = %lu } failed integrity check.\n",
                         node, node->prev(), node->next(), node->size());
            } else {
                snprintf(buffer, sizeof(buffer), "test_node_integrity:  FreeListNode %p failed integrity check.\n", node);
            }
            __crashreporter_info__ = buffer;
            malloc_printf("%s", buffer);
            __builtin_trap();
        }
        
        return node_is_valid;
    }
    
    //
    // test_freelist_integrity
    //
    // Returns true if the free list seems to me okay.
    //
    bool Admin::test_freelist_integrity() {
        SpinLock lock(&_admin_lock);
        
        // iterate through each free list
        for (usword_t m = 0; m < cache_size; m++) {
            // iterate through each free list
            for (FreeListNode *node = _cache[m].head(), *prev_node = NULL; node; node = node->next()) {
                Subzone *subzone = Subzone::subzone((void *)node);
                
                // get quantum number
                usword_t q = subzone->quantum_index(node->address());
                
                // make sure quantum number is in range
                if (q >= subzone->allocation_limit()) return false;
                
                // make sure that address is exact quantum
                if (subzone->quantum_address(q) != node->address()) return false;
                
                // make sure it is free
                if (subzone->is_used(q)) return false;
                
                // make sure the previous pointer is accurate
                if (node->prev() != prev_node) return false;
                
                // make sure of size redundancy
                if (node->size() != node->size_again()) return false;
                
                // update previous for next round
                prev_node = node;
            }
        }
        
        return true;
    }


    //
    // pop_node
    //
    // Pops a node from the specified FreeList. Also
    // performs node consistency checks.
    //
    inline FreeListNode *Admin::pop_node(usword_t index) {
        FreeListNode *head = _cache[index].head();
        return (head && test_node_integrity(head) ? _cache[index].pop() : NULL);
    }


    //
    // mark_allocated
    //
    // Set tables with information for new allocation.
    //
    inline void Admin::mark_allocated(void *address, const usword_t n, const unsigned layout, const bool refcount_is_one) {
        Subzone *subzone = Subzone::subzone(address);
        // always ZERO the first word before marking an object as allocated, to avoid a race with the scanner.
        // TODO:  consider doing the bzero here, to keep the scanner from seeing stale pointers altogether.
        // TODO:  for the medium admin, might want to release the lock during block clearing, and reaquiring
        // before allocation.
        *(void **)address = NULL;
        subzone->allocate(subzone->quantum_index(address), n, layout, refcount_is_one);
    }
    
    
    //
    // find_allocation
    //
    // Find the next available quanta for the allocation.  Returns NULL if none found.
    // Allocate otherwise.
    //
    void *Admin::find_allocation(const usword_t size, const unsigned layout, const bool refcount_is_one, bool &did_grow) {
        SpinLock lock(&_admin_lock);
        
        // determine the number of quantum we needed
        usword_t n = quantum_count(size);
        ASSERTION(n < cache_size);

        // check the free list matching the quantum size
        
        FreeListNode *node = pop_node(n);
        void *address = node->address();
        
        // if successful
        if (address) {
            // mark as allocated
            ConditionBarrier barrier(_zone->needs_enlivening(), _zone->enlivening_lock());
            mark_allocated(address, n, layout, refcount_is_one);
            if (barrier) _zone->enlivening_queue().add(address);
            return address;
        }

        // Find bigger block to use, then chop off remainder as appropriate
        
        // if no block, iterate up through sizes greater than n (best fit)
        for (usword_t i = n + 1; node == NULL && i < cache_size; i++) {
            node = pop_node(i);
        }

        // Grab a free block from the big chunk free list
        if (!node) node = pop_node(0);

        if (node) {
            // Got one.  Now return extra to free list.
            
            // get the address of the free block
            address = node->address();

            // get the full size of the allocation
            Subzone *subzone = Subzone::subzone(address);
            usword_t allocation_size = subzone->quantum_size(n);
            
            // see what's left over
            ASSERTION(node->size() >= allocation_size);
            usword_t remainder_size = node->size() - allocation_size;
            
            // if there is some left over
            if (remainder_size) {
                // determine the address of the remainder
                void *remainder_address = displace(address, allocation_size);
                // figure out which cache slot it should go
                usword_t m = cache_slot(remainder_size);
                // push the remainder onto the free list
                _cache[m].push(remainder_address, remainder_size);
            }
        }
        else if (_active_subzone) {
            // See if we can get a free block from unused territory
            // mark did_grow so that the will_grow notice will go out after we release the admin lock
            did_grow = true;
            
            usword_t top = _active_subzone->allocation_count();
            usword_t unused = _active_subzone->allocation_limit() - top;
            
            ASSERTION(unused >= n);
            address = _active_subzone->quantum_address(top);
            *(void **)address = NULL;
            _active_subzone->raise_allocation_count(n);
            _zone->statistics().add_dirty(_active_subzone->quantum_size(n));   // track total committed
                        
            // if remainder fits on non-0 free list, put it there now.  That way we're guaranteed
            // to be able to satisfy any request.
            unused -= n;
            if (unused == 0) {
                set_active_subzone(NULL);
            }
            else if (unused < cache_size) {
                _cache[unused].push(_active_subzone->quantum_address(top+n), _active_subzone->quantum_size(unused));
                _active_subzone->raise_allocation_count(unused);
                set_active_subzone(NULL);
            }
        }
        else {
            return NULL;
        }
                

        // mark as allocated 
        ConditionBarrier barrier(_zone->needs_enlivening(), _zone->enlivening_lock());
        mark_allocated(address, n, layout, refcount_is_one);
        if (barrier) _zone->enlivening_queue().add(address);
        
        return address;
    }
    

    //
    // deallocate
    //
    // Clear tables of information after deallocation.
    //
    void Admin::deallocate(void *address) {
        SpinLock lock(&_admin_lock);
        
        Subzone *subzone = Subzone::subzone(address);
        usword_t q = subzone->quantum_index(address);
        usword_t n = subzone->length(q);

        // detect double-frees.
        ASSERTION(!subzone->is_free(q));
        if (subzone->is_free(q)) {
            malloc_printf("Admin::deallocate:  attempting to free already freed block %p\n", address);
            return;
        }

        // assume that just this block is free
        void *free_address = address;
        usword_t free_size = subzone->quantum_size(n);

        // coalescing seems detrimental to allocation time, but it improves memory utilization.
        // determine next block
        usword_t next_q = q + n;
        usword_t highwater = subzone->allocation_count();

        // if not past end of in use bits and the quantum is not in use
        if (next_q < highwater && subzone->is_free(next_q)) {
            // get the free block following in memory
            FreeListNode *next_node = (FreeListNode *)displace(free_address, free_size);
            if (test_node_integrity(next_node)) {
                // determine it's size
                usword_t next_size = next_node->size();
                // which cache slot is it in
                usword_t m = cache_slot(next_size);
                // remove it from the free list
                _cache[m].remove(next_node);
                // add space to current free block
                free_size += next_size;
            }
        }
        
        // check to see if prior quantum is free
        if (q && subzone->is_free(q - 1)) {
            // determine the prior free node
            FreeListNode *this_node = (FreeListNode *)address;
            FreeListNode *prev_node = this_node->prior_node();
            if (test_node_integrity(prev_node)) {
                // update the current free address to use the prior node address
                free_address = prev_node->address();
                // get the prior's size
                usword_t prev_size = prev_node->size();
                // add space to current free block
                free_size += prev_size;
                // which cache slot is the prior free block in
                usword_t m = cache_slot(prev_size);
                 // remove it from the free list
                _cache[m].remove(prev_node);
            }
        }
        
        // scribble on blocks as they are deleted.
        if (Environment::_agc_env._dirty_all_deleted) {
            memset(free_address, 0x55, free_size);
        }

        // We can reclaim the entire active subzone space but not any other.  Only the active subzone
        // has an allocation count less than the limit.  If we did lower the per-subzone in_use count, then
        // to find and allocate it we would have to linearly search all the subzones after a failed pop(0).
        // On the other hand, this is a good way to create multi-page free blocks and keep them colder after
        // a peak memory use incident - at least for medium sized subzones.  Revisit this when compaction is
        // explored.
        // What we would need would be a list of subzones with space available.  Not so hard to maintain.
        
        if (next_q == highwater && highwater < subzone->allocation_limit()) {
            subzone->lower_allocation_count(quantum_count(free_size));
            _zone->statistics().add_dirty(-free_size);       // track total committed
        }
        else {
            // determine which free list the free space should go upon
            usword_t m = cache_slot(free_size);
            // add free space to free lists
            _cache[m].push(free_address, free_size);
        }
        
        // clear side data
        subzone->deallocate(q, n);
    }

    //
    // is_pending
    //
    // Used by subzone to get pending bit for quantum.
    //
    bool Admin::is_pending(usword_t q) { return _region->is_pending(q); }
    
    
    //
    // clear_pending
    //
    // Used by subzone to clear pending bit for quantum.
    //
    void Admin::clear_pending(usword_t q) { _region->clear_pending(q); }
    
    
    //
    // set_pending
    //
    // Used by subzone to set pending bit for quantum.
    //
    void Admin::set_pending(usword_t q) { _region->set_pending(q); }
    
    
    //
    // set_mark
    //
    // Used by subzone to set mark bit for quantum.
    //
    void Admin::set_mark(usword_t q) { _region->set_mark(q); }
    
    
    //
    // is_marked
    //
    // Used by subzone to get mark bit for quantum.
    //
    bool Admin::is_marked(usword_t q) { return _region->is_marked(q); }
    
    
    //
    // clear_mark
    //
    // Used by subzone to clear mark bit for quantum.
    //
    void Admin::clear_mark(usword_t q) { _region->clear_mark(q); }
    
    //
    // test_set_mark
    //
    // Used by subzone to test and set mark bit for quantum.
    //
    bool Admin::test_set_mark(usword_t q) { return _region->test_set_mark(q); }
};
