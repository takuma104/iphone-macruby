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
#include "AutoBlockIterator.h"
#include "AutoCollector.h"
#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoEnvironment.h"
#include "AutoHashTable.h"
#include "AutoLarge.h"
#include "AutoList.h"
#include "AutoListTypes.h"
#include "AutoLock.h"
#include "AutoMonitor.h"
#include "AutoRange.h"
#include "AutoRegion.h"
#include "AutoStatistics.h"
#include "AutoSubzone.h"
#include "AutoMemoryScanner.h"
#include "AutoThread.h"
#include "AutoWriteBarrierIterator.h"
#include "AutoZone.h"

#include "auto_weak.h"
#include "auto_trace.h"

namespace Auto {

#if defined(DEBUG)
#warning DEBUG is set
#endif


    //
    // Shared information
    //
    bool Zone::_is_auto_initialized = false;
    Zone *Zone::_last_created = NULL;

    //
    // setup_shared
    //
    // Initialize information used by all zones.
    //
    void Zone::setup_shared() {        
        // set the initialization flag
        _is_auto_initialized = true;
        
        // initialize the environment
        Environment::_agc_env.initialize();
        
        // if auxiliary malloc zone hasn't been allocated, use the default zone.
        if (!aux_zone && !Zone::zone()) {
            aux_zone = malloc_default_zone();
        }
    }
    
    
    //
    // Constructor
    //
    Zone::Zone() 
    : _enlivening_queue(_stats), _garbage_list(_stats)
    {
        ASSERTION(page_size == vm_page_size);
    
        // check to see if global information initialized
        if (!_is_auto_initialized) setup_shared();
        
        // zone is at the beginning of data
        void *next = displace(this, admin_offset());
        
        //XXX_JML initialize basic zone information
        pthread_key_create(&_registered_thread_key, Thread::destroy_registered_thread);
        _registered_threads = NULL;
        _registered_threads_lock = 0;
        
        // create key used to mark a thread as finalizing.
        pthread_key_create(&_thread_finalizing_key, NULL);
        
        // initialize subzone tracking bit map
        _in_subzone.initialize(subzone_quantum_max, next);
        next = displace(next, Bitmap::bytes_needed(subzone_quantum_max));
        
        // initialize large block tracking bit map
        _in_large.initialize(allocate_quantum_large_max, next);
        next = displace(next, Bitmap::bytes_needed(allocate_quantum_large_max));
        
#if UseArena
        // initialize arena of large block & region tracking bit map
        _large_bits.initialize(allocate_quantum_large_max, next);
        _large_bits_lock = 0;
        next = displace(next, Bitmap::bytes_needed(allocate_quantum_large_max));
        
        _arena = allocate_memory(1ul << arena_size_log2, 1ul << arena_size_log2);
        if (!_arena) {
            malloc_printf("can't allocate arena for GC\n");
            abort();
        }
        
        _large_start = NULL;
        // set the coverage to everything. We probably don't need to use at all w/arenas
        _coverage.set_range(_arena, 1ul << arena_size_log2);
#else
        // set up the coverage range
        _coverage.set_range((void *)~0, (void *)0);
#endif
        
        
        // initialize the large list
        _large_list = NULL;
        _large_lock = 0;
        
        // initialize roots hash set.
        _roots_lock = 0;
        _zombies_lock = 0;
        
        // initialize regions list
        _region_list = NULL;
        _region_lock = 0;
        _retains_lock = 0;
        _coverage_lock = 0;
        
        // initialize flags
        _is_partial = false;
        _repair_write_barrier = false;
        _some_pending = false;
        _use_pending = true;
        _needs_enlivening = false;
        _enlivening_lock = 0;
        _state = idle;
        
        // initialize statistics
        _stats.reset();
        usword_t data_size = bytes_needed();
        _stats.add_admin(data_size);
        _bytes_allocated = 0;
        
        // prime the first region
        allocate_region();
        
        // get the current monitor
        _monitor = Monitor::monitor();
        
        // initialize large deallocation function.
        _deallocate_large = &Zone::deallocate_large_normal;
        
        // record for debugging
        _last_created = this;
    }
    
    
    //
    // Destructor
    //
    Zone::~Zone() {
        // release memory used by large
        for (Large *large = _large_list; large; ) {
            Large *next = large->next();
            large->deallocate(this);
            large = next;
        }
        
        // release memory used by regions
        for (Region *region = _region_list; region != NULL; region = region->next()) { 
            Region *next = region->next();
            delete region;
            region = next;
        }
        _region_list = NULL;
    }


    //
    // memory allocation from within arena
    //
#if UseArena  
    // low half of arena in one region, top half used for large allocations
    void *Zone::arena_allocate_large(usword_t size) {
        usword_t seeksize = (size + allocate_quantum_large - 1) & ~(allocate_quantum_large-1);
        usword_t nbits = seeksize >> allocate_quantum_large_log2;
        // look through our arena for free space on this alignment
        usword_t start = 0;
        // someday... track _first_free
        usword_t end = 1ul << (arena_size_log2 - allocate_quantum_large_log2 - 1);
        if (nbits > (end - start)) {
            return NULL;
        }
        end -= nbits; // can't find anything that big past this point :-)
        SpinLock lock(&_large_bits_lock);
        while (start <= end) {
            // actually, find last clear bit. If 0, we have an allocation, otherwise we have a new start XXX
            if (_large_bits.bits_are_clear(start, nbits)) {
                _large_bits.set_bits(start, nbits);
                return displace(_large_start, start << allocate_quantum_large_log2);
            }
            start += 1;
        }
        // out of memory
        return NULL;

    }
    
    void *Zone::arena_allocate_region(usword_t newsize) {
        // only one region when using arena
        if (_large_start) return NULL;
		
        // newsize includes room for bitmaps.  Just for sanity make sure it is large quantum aligned.
        usword_t roundedsize = (newsize + subzone_quantum - 1) & ~(subzone_quantum-1);
        _large_start = displace(_arena, roundedsize);
        return _arena;
    }
    
    //
    // raw memory deallocation
    //
    void Zone::arena_deallocate(void *address, size_t size) {
        usword_t seeksize = (size + allocate_quantum_large - 1) & ~(allocate_quantum_large-1);
        usword_t nbits = seeksize >> allocate_quantum_large_log2;
        usword_t start = ((char *)address - (char *)_large_start) >> allocate_quantum_large_log2;
        SpinLock lock(&_large_bits_lock);
        _large_bits.clear_bits(start, nbits);
        //if (address < _first_free) _first_free = address;
    }
#else
    // on 32-bit, goes directly to system (the entire address space is our arena)
    void *Zone::arena_allocate_large(usword_t size) {
        return allocate_memory(size, allocate_quantum_large, VM_MEMORY_MALLOC_LARGE);
    }
    
    void Zone::arena_deallocate(void *address, size_t size) {
        deallocate_memory(address, size);
    }
#endif

    
    //
    // allocate_region
    //
    // Allocate and initialize a new subzone region
    //
    Region *Zone::allocate_region() {
        // allocate new region
        Region *region = Region::new_region(this);
        
        // if allocated
        if (region) {
            SpinLock lock(&_region_lock);
            
            {
                SpinLock lock(&_coverage_lock);

                // update coverage range
                _coverage.expand_range(*region);
            }
            
            // add to end of region list.
            if (_region_list == NULL) {
                _region_list = region;
            } else {
                Region *last_region = _region_list, *next_region = last_region->next();
                while (next_region != NULL) {
                    last_region = next_region;
                    next_region = next_region->next();
                }
                last_region->set_next(region);
            }
            
            // set up scan stack
            if (!_scan_stack.is_allocated()) {
                _scan_stack.set_range(region->scan_space());
            }
        }
        return region;
    }
    
        
    
    //
    // allocate_large
    //
    // Allocates a large block from the universal pool (directly from vm_memory.)
    //
    void *Zone::allocate_large(const size_t size, const unsigned layout, bool clear, bool refcount_is_one) {
        Large *large = Large::allocate(this, size, layout, refcount_is_one);
        
        if (large) {
            // get info
            void *address = large->address();
            usword_t size = large->size();
            
#if UseArena
            bzero(address, size);
#endif

            SpinLock lock(&_large_lock);
 
            {
                // Before adding to large list we must queue this for enlivening.
                // Otherwise it might be on the list and not yet in the queue when
                // the collector enters the enlivening phase.  At that point its game over
                // because the enlivening list will be ignored and there will be this block
                // not marked and treated as garbage.
                // XXX could move the _in_large.set_bit operation here and then also
                // the _large_lock itself for a minor performance boost
                ConditionBarrier barrier(&_needs_enlivening, &_enlivening_lock);
                if (barrier) enlivening_queue().add(address);

                // add to large list
                large->set_next(_large_list);
                large->set_prev((Large*)0);
                if (_large_list) _large_list->set_prev(large);
                _large_list = large;
            }
            
            // expand coverage of zone
            {
                SpinLock lock(&_coverage_lock);
                Range large_range(address, size);
                _coverage.expand_range(large_range);
            }
            
            // mark in large bit map
            _in_large.set_bit(Large::quantum_index(address));

            // update statistics
            _stats.add_count(1);
            _stats.add_size(size);
            _stats.add_dirty(size);
            _stats.add_allocated(size);
            
            add_allocated_bytes(size);
            
            return address;
        }
        
        return NULL;
    }
    
    
    //
    // deallocate_large_normal
    //
    // Release memory allocated for a large block
    //
    // caller must have acquired _large_lock.
    //
    void Zone::deallocate_large_normal(void *block) {
        // clear in large bit map
        _in_large.clear_bit(Large::quantum_index(block));

        // locate large admin information.
        Large *large = Large::large(block);
        
        // update statistics
        usword_t size = large->size();
        _stats.add_count(-1);
        _stats.add_size(-size);             // bytes in use
        _stats.add_allocated(-size);        // vm required for bytes in use (100%)
        _stats.add_dirty(-size);            // dirty bytes required for bytes in use (100%)
        
        // remove from large list
        Large *prev = large->prev();
        Large *next = large->next();

        if (prev)  prev->set_next(next);
        else       _large_list = next;
        if (next) next->set_prev(prev);
        
        // release memory for the large block
        large->deallocate(this);
    }

    //
    // deallocate_large_collecting
    //
    // During collections, marks a large block as needing to be freed.
    //
    // caller must have acquired _large_lock.
    //
    void Zone::deallocate_large_collecting(void *block) {
        // locate large admin information.
        Large *large = Large::large(block);
        
        // mark block for lazy deallocation (during collections).
        ASSERTION(!large->is_freed());
        large->set_freed();
    }
    
    //
    // allocate_small_medium
    //
    // Allocate a block of memory from a subzone,
    //
    void *Zone::allocate_small_medium(const size_t size, const unsigned layout, bool clear, bool refcount_is_one) {
        Region *region = _region_list;
        // iterate through regions until we find one that has the space
        for (; region != NULL; region = region->next()) {
            // attempt to allocate the block
            void *block = region->allocate(size, layout, clear, refcount_is_one);
            
            // return the block if allocated
            if (block) return block;
        }

        // attempt to allocate new region
        region = allocate_region();
        
        // if no room for new regions then give up 
        if (!region) {
            control.will_grow((auto_zone_t *)this, AUTO_HEAP_ARENA_EXHAUSTED);
            return NULL;
        }
        
        return region->allocate(size, layout, clear, refcount_is_one);
    }


    //
    // deallocate_small_medium
    //
    // Release memory allocated for a small block
    //
    void Zone::deallocate_small_medium(void *block) {
        // find the subzone
        Subzone *subzone = Subzone::subzone(block);
        
        // get the region
        Admin *admin = subzone->admin();
        Region *region = admin->region();
        // deallocate from region
        region->deallocate(subzone, block);
    }
    
    
    //
    // block_allocate
    //
    // Allocate a block of memory from the zone.  layout indicates whether the block is an
    // object or not and whether it is scanned or not.
    //
    void *Zone::block_allocate(const size_t size, const unsigned layout, bool clear, bool refcount_is_one) {
        void *block;
        usword_t needed = size;

        if (needed < allocate_quantum_large) {
            // make sure we allocate at least one byte
            if (!needed) needed = 1;
            // allocate from subzones
            block = allocate_small_medium(needed, layout, clear, refcount_is_one);
        } else {
            // allocate from vm memory
            block = allocate_large(needed, layout, clear, refcount_is_one);
        }

        return block;
    }
    
    
    //
    // block_deallocate
    //
    // Release a block of memory from the zone, lazily while scanning.
    // 
    void Zone::block_deallocate(void *block) {
        
        // explicitly deallocated blocks must have no associations.
        erase_associations(block);
        
        if (in_subzone_memory(block)) {
            // TODO:  we could handle ALL block freeing this way, instead of swapping the semantics of deallocate_large().
            Subzone *subzone = Subzone::subzone(block);
            SpinLock adminLock(subzone->admin()->lock());
            dec_refcount_small_medium(subzone, block);
             // inhibit finalization for NSDeallocateObject()'d objects.
            subzone->set_layout(block, AUTO_MEMORY_UNSCANNED);
        } else if (in_large_memory(block)) {
            deallocate_large(block);
         } else {
            error("Deallocating a non-block", block);
        }
    }
    
    
    //
    // block_deallocate_internal
    //
    // Release a block memory from the zone. Only to be called by the collector itself. Assumes
    // caller has already acquired the necessary locks. TODO:  could optimize this further
    // by having the caller aquire the admin locks, and the large lock.
    //
    void Zone::block_deallocate_internal(void *block) {
        // NOTE:  since this is ONLY called by the collector thread itself, we don't need to take the enlivening lock.
        
        // explicitly deallocated blocks must have no associations.
        if (_associations.size() != 0) {
            PtrAssocHashMap::iterator iter = _associations.find(block);
            if (iter != _associations.end()) _associations.erase(iter);
        }

        if (in_subzone_memory(block)) {
            deallocate_small_medium(block);
        } else if (in_large_memory(block)) {
            deallocate_large(block);
        } else {
            error("Deallocating a non-block", block);
        }
    }
    
    
    //
    // block_start_large
    // 
    // Return the start of a large block.
    //
    void *Zone::block_start_large(void *address) {
        if (_coverage.in_range(address)) {
            SpinLock lock(&_large_lock); // guard against concurrent deallocation.
            usword_t q = Large::quantum_index(address);
            if (!_in_large.bit(q)) {
                q = _in_large.previous_set(q);
                if (q == not_found) return NULL;
            }
            
            // this could crash if another thread deallocates explicitly, but that's a bug we can't prevent
#if UseArena
            Large *large = Large::quantum_large(q, _arena);
#else
            Large *large = Large::quantum_large(q, (void *)0);
#endif
            if (!large->range().in_range(address)) return NULL;
            
            return large->address();
        }
        
        return NULL;
    }
    

    //
    // block_start
    //
    // Return the base block address of an arbitrary address.
    // Broken down because of high frequency of use.
    //
    void *Zone::block_start(void *address) {
        if (in_subzone_memory(address)) {
            return Subzone::subzone(address)->block_start(address);
        } else {
            return block_start_large(address);
        }
    }


    //
    // block_size
    //
    // Return the size of a specified block.
    //
    usword_t Zone::block_size(void *block) {
        if (in_subzone_memory(block)) {
            return Subzone::subzone(block)->size(block);
        } else if (in_large_memory(block)) {
            return Large::size(block);
        }

        return 0;
    }


    //
    // block_layout
    //
    // Return the layout of a specified block.
    //
    int Zone::block_layout(void *block) {
        if (in_subzone_memory(block)) {
            return Subzone::subzone(block)->layout(block);
        } else if (in_large_memory(block)) {
            return Large::layout(block);
        }

        return AUTO_TYPE_UNKNOWN;
    }
    
    
    //
    // block_set_layout
    //
    // Set the layout of a block.
    //
    void Zone::block_set_layout(void *block, int layout) {
        if (in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);
            SpinLock lock(subzone->admin()->lock());
            subzone->set_layout(block, layout);
        } else if (in_large_memory(block)) {
            Large::set_layout(block, layout);
        }
    }

    
    //
    // get_refcount_small_medium
    //
    // Return the refcount of a small/medium block.
    //
    int Zone::get_refcount_small_medium(Subzone *subzone, void *block) {
        int refcount = subzone->refcount(block);
        if (refcount == 2) {
            // non-zero reference count, check the overflow table.
            SpinLock lock(&_retains_lock);
            PtrIntHashMap::iterator retain_iter = _retains.find(block);
            if (retain_iter != _retains.end() && retain_iter->first == block) {
                refcount = retain_iter->second;
            }
        }
        return refcount;
    }
    
    
    //
    // inc_refcount_small_medium
    //
    // Increments the refcount of a small/medium block, returning the new value.
    // Requires subzone->admin()->lock() to be held, to protect side data.
    //
    int Zone::inc_refcount_small_medium(Subzone *subzone, void *block) {
        usword_t q = subzone->quantum_index(block);
        int refcount = subzone->refcount(q);
        if (refcount == 2) {
            // non-trivial reference count, check the overflow table.
            SpinLock lock(&_retains_lock);
            PtrIntHashMap::iterator retain_iter = _retains.find(block);
            if (retain_iter != _retains.end() && retain_iter->first == block) {
                refcount = ++retain_iter->second;
            } else {
                // transition from 2 -> 3
                refcount = (_retains[block] = 3);
            }
        } else {
            // transition from 0 -> 1, 1 -> 2
            subzone->incr_refcount(q);
        }
        return refcount;
    }
    
    
    //
    // dec_refcount_small_medium
    //
    // Decrements the refcount of a small/medium block, returning the new value.
    // Requires subzone->admin()->lock() to be held, to protect side data.
    //
    
    int Zone::dec_refcount_small_medium(Subzone *subzone, void *block) {
        usword_t q = subzone->quantum_index(block);
        int refcount = subzone->refcount(q);
        if (refcount == 2) {
            // non-trivial reference count, check the overflow table.
            SpinLock lock(&_retains_lock);
            PtrIntHashMap::iterator retain_iter = _retains.find(block);
            if (retain_iter != _retains.end() && retain_iter->first == block) {
                if (--retain_iter->second == 2) {
                    // transition from 3 -> 2
                    _retains.erase(retain_iter);
                    return 2;
                } else {
                    return retain_iter->second;
                }
            } else {
                // transition from 2 -> 1.
                subzone->decr_refcount(q);
                return 1;
            }
        } else if (refcount == 1) {
            subzone->decr_refcount(q);
            return 0;
        }
        // underflow.
        malloc_printf("reference count underflow for %p, break on auto_refcount_underflow_error to debug.\n", block);
        auto_refcount_underflow_error(block);
        return -1;
    }
    
    
    //
    // block_refcount
    //
    // Returns the reference count of the specified block, or zero.
    //
    int Zone::block_refcount(void *block) {
        if (in_subzone_memory(block)) {
            return get_refcount_small_medium(Subzone::subzone(block), block);
        } else if (in_large_memory(block)) {
            SpinLock lock(&_large_lock);
            return Large::refcount(block);
        }

        return 0;
    }

#if 0    
    void Zone::testRefcounting(void *block) {
        for (int j = 0; j < 7; ++j) {
            printf("\nloop start refcount is %d for %p\n", block_refcount(block), block);
            for (int i = 0; i < 5; ++i) {
                block_increment_refcount(block);
                printf("after increment, it now has refcount %d\n", block_refcount(block));
            }
            for (int i = 0; i < 5; ++i) {
                block_decrement_refcount(block);
                printf("after decrement, it now has refcount %d\n", block_refcount(block));
            }
            for (int i = 0; i < 5; ++i) {
                block_increment_refcount(block);
                printf("after increment, it now has refcount %d\n", block_refcount(block));
            }
            for (int i = 0; i < 5; ++i) {
                block_decrement_refcount(block);
                printf("after decrement, it now has refcount %d\n", block_refcount(block));
            }
            printf("maturing block...\n");
            Subzone::subzone(block)->mature(block);
        }
    }
#endif
        


    //
    // block_increment_refcount
    //
    // Increment the reference count of the specified block.
    //
    int Zone::block_increment_refcount(void *block) {
        int refcount = 0;
        
        if (in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);
            SpinLock lock(subzone->admin()->lock());
            refcount = inc_refcount_small_medium(subzone, block);
            // 0->1 transition.  Must read _needs_enlivening while inside allocation lock.
            // Otherwise might miss transition and then collector might have passed over this block while refcount was 0.
            if (refcount == 1) {
                ConditionBarrier barrier(&_needs_enlivening, &_enlivening_lock);
                if (barrier && !block_is_marked(block)) _enlivening_queue.add(block);
            }
        } else if (in_large_memory(block)) {
            SpinLock lock(&_large_lock);
            refcount = Large::refcount(block) + 1;
            Large::set_refcount(block, refcount);
            if (refcount == 1) {
                ConditionBarrier barrier(&_needs_enlivening, &_enlivening_lock);
                if (barrier && !block_is_marked(block)) _enlivening_queue.add(block);
            }
        }
        
        return refcount;
    }
        

    //
    // block_decrement_refcount
    //
    // Decrement the reference count of the specified block.
    //
    int Zone::block_decrement_refcount(void *block) {
        if (in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);
            SpinLock lock(subzone->admin()->lock());
            return dec_refcount_small_medium(subzone, block);
        } else if (in_large_memory(block)) {
            SpinLock lock(&_large_lock);
            int refcount = Large::refcount(block);
            if (refcount <= 0) {
                malloc_printf("reference count underflow for %p, break on auto_refcount_underflow_error to debug\n", block);
                auto_refcount_underflow_error(block);
            }
            else {
                refcount = refcount - 1;
                Large::set_refcount(block, refcount);
            }
            return refcount;
        }
        return 0;
    }
    
    
    void Zone::block_refcount_and_layout(void *block, int *refcount, int *layout) {
        if (in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);
            SpinLock lock(subzone->admin()->lock());
            *refcount = get_refcount_small_medium(subzone, block);
            *layout = subzone->layout(block);
        } else if (in_large_memory(block)) {
            SpinLock lock(&_large_lock);
            Large *large = Large::large(block);
            *refcount = large->refcount();
            *layout = large->layout();
        }
    }

    //
    // set_pending
    //
    // Sets a block as pending during scanning.  Return true if set.
    //
    // Does no locking, to be called only from the collector thread.
    //
    bool Zone::set_pending(void *block) {
        // exit early if NULL
        if (!block) return false;
        
        if (in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);
            unsigned char layout;

            if (_is_partial) {
                if (!subzone->should_pend_new(block, layout)) return false;
            } else {
                if (!subzone->should_pend(block, layout)) return false;
            }

            if (_scanning_associations) pend_associations(block);
            
            // if block is unscanned, we're done.
            if (layout & AUTO_UNSCANNED) return false;

            if (_use_pending) {
                subzone->set_pending(block);
                set_some_pending();
            } else {
                scan_stack_push_block(block);
            }
            
            return true;
        } else if (in_large_memory(block)) {
            if (!Large::is_start(block)) return false;
            Large *large = Large::large(block);
            if (_is_partial && !large->is_new()) return false;
            if (large->test_set_mark()) return false;
            
            if (_scanning_associations) pend_associations(block);
            
            // if block is unscanned, we're done.
            if (large->layout() & AUTO_UNSCANNED) return false;
            
            if (_use_pending) {
                large->set_pending();
                set_some_pending();
            } else {
                scan_stack_push_block(block);
            }
            
            return true;
        }
        
        return false;
    }
    
    
    //
    // repend
    //
    // Force a block to be rescanned.
    //
    void Zone::repend(void *block) {
        //void *block = block_start(address);
        //if (!block) return;
        if (in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);

            if (!subzone->is_start(block)) return;
            usword_t q = subzone->quantum_index(block);
            if (subzone->is_marked(q)) return;
            subzone->set_mark(q);
            if (subzone->layout(q) & AUTO_UNSCANNED) return;
            if (_use_pending) {
                subzone->set_pending(q);
                set_some_pending();
            } else {
                scan_stack_push_block(block);
            }
        } else if (in_large_memory(block)) {
            if (!Large::is_start(block)) return;
            Large *large = Large::large(block);
            if (large->is_marked()) return;
            large->set_mark();
            if (large->layout() & AUTO_UNSCANNED) return;
            if (_use_pending) {
                large->set_pending();
                set_some_pending();
            } else {
                scan_stack_push_block(block);
            }
        }
    }

    //
    // scan_associations
    //
    // Iteratively visits all associatively referenced objects. Only one pass over the
    // associations table is necessary, as the set_pending() method is sensitive to whether
    // associations are being scanned, and when blocks are newly marked, associations are also
    // recursively pended.
    //
    void Zone::scan_associations(MemoryScanner &scanner) {
        // Prevent other threads from breaking existing associations. We already own the enlivening lock.
        SpinLock lock(&_associations_lock);
    
        // tell set_pending() to recursively pend associative references.
        _scanning_associations = true;
        
        // consider associative references. these are only reachable if their primary block is.
        for (PtrAssocHashMap::iterator i = _associations.begin(); i != _associations.end(); i++) {
            void *block = i->first;
            if (block_is_marked(block)) {
                PtrPtrHashMap &refs = i->second;
                for (PtrPtrHashMap::iterator j = refs.begin(); j != refs.end(); j++) {
                    set_pending(j->second);
                }
            }
        }
        
        // scan through all pending blocks until there are no new pending
        scanner.scan_pending_until_done();
        
        _scanning_associations = false;
    }
        
    //
    // set_write_barrier
    //
    // Set the write barrier byte corresponding to the specified address.
    // If scanning is going on then the value is marked pending.
    //
    bool Zone::set_write_barrier(void *address, void *value) {
        if (in_subzone_memory(address)) {
            // find the subzone
            Subzone *subzone = Subzone::subzone(address);
            
            UnconditionalBarrier condition(&_needs_enlivening, &_enlivening_lock);
            if (condition && !block_is_marked(value)) _enlivening_queue.add(value);
            *(void **)address = value;   // rdar://5512883

            // mark the write barrier
            subzone->write_barrier().mark_card(address);
            return true;
        }
        else if (void *block = block_start_large(address)) {
            // get the large block
            Large *large = Large::large(block);
            
            UnconditionalBarrier condition(&_needs_enlivening, &_enlivening_lock);
            if (condition && !block_is_marked(value)) _enlivening_queue.add(value);
            *(void **)address = value;   // rdar://5512883

            // mark the write barrier
            large->write_barrier().mark_card(address);
            return true;
        }
        return false;
    }
    
    //
    // set_write_barrier_range
    //
    // Set a range of write barrier bytes to the specified mark value.
    //
    bool Zone::set_write_barrier_range(void *destination, const usword_t size) {
        // First, mark the card(s) associated with the destination.
        if (in_subzone_memory(destination)) {
            // find the subzone
            Subzone *subzone = Subzone::subzone(destination);
            
            // mark the write barrier
            subzone->write_barrier().mark_cards(destination, size);
            return true;
        } else if (void *block = block_start_large(destination)) {
            Large *large = Large::large(block);
            
            // mark the write barrier
            large->write_barrier().mark_cards(destination, size);
            return true;
        }
        return false;
    }


    //
    // set_write_barrier
    //
    // Set the write barrier byte corresponding to the specified address.
    //
    bool Zone::set_write_barrier(void *address) {
        if (in_subzone_memory(address)) {
            // find the subzone
            Subzone *subzone = Subzone::subzone(address);
            
            // mark the write barrier
            subzone->write_barrier().mark_card(address);
            return true;
        }
        else if (void *block = block_start_large(address)) {
            // get the large block
            Large *large = Large::large(block);
            
            // mark the write barrier
            large->write_barrier().mark_card(address);
            return true;
        }
        return false;
    }
    
    //
    // write_barrier_scan_unmarked_content
    //
    // Scan ranges in block that are marked in the write barrier.
    //
    void Zone::write_barrier_scan_unmarked_content(void *block, const usword_t size, MemoryScanner &scanner) {
        if (in_subzone_memory(block)) {
            // find the subzone
            Subzone *subzone = Subzone::subzone(block);
            
            // get the small medium write barrier
            WriteBarrier wb = subzone->write_barrier();
            wb.scan_ranges(block, size, scanner);
        } else if (in_large_memory(block)) {
            // get the large block
            Large *large = Large::large(block);
            
            // get the large write barrier;
            WriteBarrier wb = large->write_barrier();
            wb.scan_ranges(block, size, scanner);
        }
    }

    struct mark_write_barriers_untouched_visitor {
        // visitor function 
        inline bool visit(Zone *zone, WriteBarrier &wb) {
            // clear the write barrier 
            wb.mark_cards_untouched();
            // always continue
            return true;
        }
    };
    void Zone::mark_write_barriers_untouched() {
        mark_write_barriers_untouched_visitor visitor;
        visitWriteBarriers(this, visitor);
    }

    //
    // clear_untouched_write_barriers
    //
    // iterate through all the write barriers and clear marks.
    //
    struct clear_untouched_write_barriers_visitor {
        // visitor function 
        inline bool visit(Zone *zone, WriteBarrier &wb) {
            // clear the untouched cards. 
            wb.clear_untouched_cards();
            
            // always continue
            return true;
        }
    };
    void Zone::clear_untouched_write_barriers() {
        // this must be done while the _enlivening_lock is held, to keep stragglers from missing writes.
        clear_untouched_write_barriers_visitor visitor;
        visitWriteBarriers(this, visitor);
    }
    
    
    //
    // clear_all_write_barriers
    //
    // iterate through all the write barriers and clear marks.
    //
    struct clear_all_write_barriers_visitor {
        // visitor function 
        inline bool visit(Zone *zone, WriteBarrier &wb) {
            // clear the write barrier 
            wb.clear();
            
            // always continue
            return true;
        }
    };
    void Zone::clear_all_write_barriers() {
        // this is done while the _enlivening_lock is held, to keep stragglers from missing writes.
        // set up the visitor
        clear_all_write_barriers_visitor visitor;
        visitWriteBarriers(this, visitor);
    }

    //
    // reset_all_marks
    //
    // Clears the mark flags on all blocks
    //
    struct reset_all_marks_visitor {
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q, void *block) {
            // clear block's new flag
            subzone->clear_mark(q);
            
            // always continue
            return true;
        }
        
        inline bool visit(Zone *zone, Large *large, void *block) {
            // clear block's new flag
            large->clear_mark();
            
            // always continue
            return true;
        }
    };
    void Zone::reset_all_marks() {
#if 1
        // XXX_PCB:  marks are now in their own separate BitMaps, so just clear their live ranges.
        for (Region *region = _region_list; region != NULL; region = region->next()) {
            region->clear_all_marks();
        }
        
        // this is called from collect_end() so should be safe.
        SpinLock lock(&_large_lock);
        for (Large *large = _large_list; large != NULL; large = large->next()) {
            large->clear_mark();
        }
#else
        // set up all marks visitor
        reset_all_marks_visitor visitor;
        
        // set up iterator
        BlockIterator<reset_all_marks_visitor> iterator(this, visitor);
        
        // visit all the admins
        iterator.visit();
#endif
    }
       
    
    //
    // reset_all_marks_and_pending
    //
    // Clears the mark and ending flags on all blocks
    //
    struct reset_all_marks_and_pending_visitor {
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q) {
            // clear block's mark & pending flags
            subzone->clear_mark(q);
            subzone->clear_pending(q);
            
            // always continue
            return true;
        }
        
        inline bool visit(Zone *zone, Large *large) {
            // clear block's mark & pending flags
            large->clear_mark();
            large->clear_pending();
            
            // always continue
            return true;
        }
    };
    void Zone::reset_all_marks_and_pending() {
#if 1
        // FIXME: the enlivening lock must be held here, which should keep the region and large lists stable.
        // XXX_PCB:  marks are now in their own separate BitMaps, so just clear their live ranges.
        for (Region *region = _region_list; region != NULL; region = region->next()) {
            region->clear_all_marks();
            region->clear_all_pending();
        }
        
        SpinLock lock(&_large_lock);
        for (Large *large = _large_list; large != NULL; large = large->next()) {
            large->clear_mark();
            large->clear_pending();
        }
#else
        // set up all marks and pending visitor
        reset_all_marks_and_pending_visitor visitor;
        
        // visit all the blocks
        visitAllocatedBlocks(this, visitor);
#endif
    }
    
    
    
    
    //
    // statistics
    //
    // Returns the statistics for this zone.
    //
    struct statistics_visitor {
        Statistics &_stats;
        Region *_last_region;
        Subzone *_last_subzone;
        
        statistics_visitor(Statistics &stats)
        : _stats(stats)
        , _last_region(NULL)
        , _last_subzone(NULL)
        {}
        
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q) {
            if (_last_region != subzone->admin()->region()) {
                _last_region = subzone->admin()->region();
                _stats.add_admin(Region::bytes_needed());
            }
            
            if (_last_subzone != subzone) {
                _last_subzone = subzone;
                _stats.add_admin(subzone_write_barrier_max);
                _stats.add_allocated(subzone->allocation_size());
                _stats.add_dirty(subzone->allocation_size());
            }
            
            _stats.add_count(1);
            _stats.add_size(subzone->size(q));
            
            return true;
        }
        
        inline bool visit(Zone *zone, Large *large) {
            _stats.add_admin(large->vm_size() - large->size());
            _stats.add_count(1);
            _stats.add_size(large->size());
            
            return true;
        }
    };
    void Zone::statistics(Statistics &stats) {
        
        statistics_visitor visitor(stats);
        visitAllocatedBlocks(this, visitor);
    }


    //
    // block_collector
    //
    // Called by the monitor to prevent collections.
    //
    void Zone::block_collector() {
        pthread_mutex_lock(&collection_mutex);
        while (collection_status_state) {
            // block until the collector finishes the current collection.
            pthread_cond_wait(&collection_status, &collection_mutex);
        }
        // since we now own the mutex, nobody can signal the collector until we unlock it.
        suspend_all_registered_threads();
    }
    
    
    //
    // unblock_collector
    //
    // Called by the monitor to enable collections.
    //
    void Zone::unblock_collector() {
        resume_all_registered_threads();
        pthread_mutex_unlock(&collection_mutex);
    }
    
    
    //
    // collect
    //
    // Performs the collection process.
    //
    void Zone::collect(bool is_partial, void *current_stack_bottom, auto_date_t *enliveningBegin) {
        
        auto_trace_phase_begin((auto_zone_t*)this, is_partial, AUTO_TRACE_SCANNING_PHASE);

        // inform mutators that they need to add objects to the enlivening queue while scanning.
        // we lock around the rising edge to coordinate with eager block deallocation.
        // Grab all other locks that use ConditionBarrier on the enlivening_lock, then grab the enlivening_lock
        // and mark it.  This ordering guarantees that the the code using the ConditionBarrier can read the condition
        // without locking since they each have already acquired a lock necessary to change the needs_enlivening state.
        // All locks are released after setting needs_enlivening.
        set_needs_enlivening();

        // construct collector
        Collector collector(this, current_stack_bottom, is_partial);
        
        // run collector in scan stack mode
        collector.collect(false);

        // check if stack overflow occurred
        if (_scan_stack.is_overflow()) {
            _stats.increment_stack_overflow_count();
            
            reset_all_marks_and_pending();

            // let go of the _enlivening_lock.
            ASSERTION(_enlivening_lock != 0);
            spin_unlock(&_enlivening_lock);

            // try again using pending bits
            collector.collect(true);
        }
        
        _scan_stack.reset();

        auto_trace_phase_end((auto_zone_t*)this, is_partial, AUTO_TRACE_SCANNING_PHASE,
                             collector.blocks_scanned(), collector.bytes_scanned());

        auto_weak_callback_block_t *callbacks = NULL;

        // update stats - XXX_PCB only count completed collections.
        *enliveningBegin = collector.scan_end;
        _stats.increment_gc_count(is_partial);
        
        // XXX_PCB VMAddressList uses aux_malloc(), which could deadlock if some other thread holds the stack logging spinlock.
        // XXX_PCB this issue is being tracked in <rdar://problem/4501032>.
        //XXX_JML this is where we will handle finialization.
        //XXX_JML but in the meantime return the garbage ranges
        _garbage_list.clear_count();
        scavenge_blocks();

        // if weak references are present, threads will still be suspended, resume them after clearing weak references.
        if (has_weak_references()) {
            auto_trace_phase_begin((auto_zone_t*)this, is_partial, AUTO_TRACE_WEAK_REFERENCE_PHASE);
            uintptr_t weak_referents, weak_references;
            callbacks = weak_clear_references(this, _garbage_list.count(), _garbage_list.buffer(), &weak_referents, &weak_references);
            auto_trace_phase_end((auto_zone_t*)this, is_partial, AUTO_TRACE_WEAK_REFERENCE_PHASE, weak_referents, weak_references * sizeof(void*));
        }

        // FIXME:  When using aging, we SHOULD NEVER unconditionally clear write-barriers or age bits.
        // FIXME:  Instead, we promote as we mark, and SHOULD incrementally clear write-barriers during scanning.
        // TODO:  Measure CPU cost of doing write-barrier repairs.
        
        if (!is_partial) {
            // if running a full collection, mark all write-barriers as provisionally untouched.
            mark_write_barriers_untouched();
            _repair_write_barrier = true;
        } else if (_repair_write_barrier) {
            // first generational after a full, clear all cards that are known to not have intergenerational pointers.
            clear_untouched_write_barriers();
            _repair_write_barrier = false;
        }

        // notify mutators that they no longer need to enliven objects.
        // No locks are acquired since enlivening lock is already held and all code that uses ConditionBarrier
        // is blocking on the enlivening_lock already.
        clear_needs_enlivening();
        spin_unlock(&_enlivening_lock);

        // release any unused pages
        // release_pages();
        weak_call_callbacks(callbacks);
        
        // malloc_printf("Zone::collect(): partial_gc_count = %ld, full_gc_count = %ld, aborted_gc_count = %ld\n",
        //               _stats.partial_gc_count(), _stats.full_gc_count(), _stats.aborted_gc_count());
        
        // malloc_printf("collector.stack_scanned() = %u\n", collector.stack_scanned());

        if (Environment::_agc_env._print_stats) {
            malloc_printf("cnt=%d, sz=%d, max=%d, al=%d, admin=%d\n",
                _stats.count(),
                _stats.size(),
                _stats.dirty_size(),
                _stats.allocated(),
                _stats.admin_size());
        }
    }

        
    //
    // scavenge_blocks
    //
    // Constructs a list of all garbage blocks.
    //
    // Also ages non-garbage blocks, so we can do this while
    // the enlivening lock is held. This prevents a possible race
    // with mutators that adjust reference counts. <rdar://4801771>
    //
    struct scavenge_blocks_visitor {
        PointerList& _list;                               // accumulating list
        
        // Constructor
        scavenge_blocks_visitor(PointerList& list) : _list(list) {}
        
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q) {
            // always age blocks, to distinguish garbage blocks from blocks allocated during finalization [4843956].
            if (subzone->is_new(q)) subzone->mature(q);
            
            // add unmarked blocks to the garbage list.
            if (!subzone->is_marked(q)) _list.add((vm_address_t)subzone->quantum_address(q));

            // always continue
            return true;
        }
        
        inline bool visit(Zone *zone, Large *large) {
            // always age blocks, to distinguish garbage blocks from blocks allocated during finalization [4843956].
            if (large->is_new()) large->mature();
            
            // add unmarked blocks to the garbage list.
            if (!large->is_marked() && !large->is_freed()) _list.add((vm_address_t)large->address());

            // always continue
            return true;
        }
    };
    void Zone::scavenge_blocks() {
        // set up the block scanvenger visitor
        scavenge_blocks_visitor visitor(_garbage_list);
        
        // iterate through all the blocks
        visitAllocatedBlocks(this, visitor);
    }
    

    //
    // register_thread
    //
    // Add the current thread as a thread to be scanned during gc.
    //
    void Zone::register_thread() {
        // if thread is not registered yet
        Thread *thread = (Thread *)pthread_getspecific(_registered_thread_key);
        if (thread == NULL) {
            // construct a new Thread 
            pthread_t pthread = pthread_self();
            thread = new Thread(this, pthread, pthread_mach_thread_np(pthread));
            {
                // add thread to linked list of registered threads
                SpinLock lock(&_registered_threads_lock);
                thread->set_next(_registered_threads);
                _registered_threads = thread;
            }
            
            // set the new thread as tsd 
            pthread_setspecific(_registered_thread_key, thread);
        }
        thread->retain();
    }
    

    //
    // unregister_thread
    //
    // Remove the current thread as a thread to be scanned during gc.
    //
    void Zone::unregister_thread() {
        Thread *thread = (Thread *)pthread_getspecific(_registered_thread_key);
        
        // if it is still one of ours
        if (thread && thread->release() == 0) {
            // clear the tsd
            pthread_setspecific(_registered_thread_key, NULL);
            
            {
                // protect the registered threads list, in case the collector is traversing it.
                SpinLock lock(&_registered_threads_lock);
                // unlink the thread from the list of registered threads
                thread->unlink(&_registered_threads);
            }
            
            delete thread;
        }
    }


    //
    // suspend_all_registered_threads
    //
    // Suspend registered threads for scanning.
    //
    void Zone::suspend_all_registered_threads() {
        SpinLock lock(&_registered_threads_lock);

        // get first thread
        Thread *thread = _registered_threads;
        
        // while there are threads
        while (thread) {
            // get next in advance in case of deletion
            Thread *next = thread->next();
            // suspend thread, remove on failure
            if (!thread->suspend()) thread->unlink(&_registered_threads);
            // get next thread
            thread = next;
        }
    }


    //
    // resume_all_registered_threads
    //
    // Resume registered threads after scanning.
    //
    void Zone::resume_all_registered_threads() {
        SpinLock lock(&_registered_threads_lock);

        // get first thread
        Thread *thread = _registered_threads;
        
        // while there are threads
        while (thread) {
            // get next in advance in case of deletion
            Thread *next = thread->next();
            // resume thread, remove on failure
            if (!thread->resume()) thread->unlink(&_registered_threads);
            // get next thread
            thread = next;
        }
    }

    //
    // print_all_blocks
    //
    // Prints all allocated blocks.
    //
    struct print_all_blocks_visitor {
        Region *_last_region;                               // previous region visited
        Subzone *_last_subzone;                             // previous admin visited
        bool _is_large;

        // Constructor
        print_all_blocks_visitor() : _last_region(NULL), _is_large(false) {}
        
        // visitor function
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q) {
            // if transitioning then print appropriate banner
            if (_last_region != subzone->admin()->region()) {
                _last_region = subzone->admin()->region();
                malloc_printf("Region [%p..%p]\n", _last_region->address(), _last_region->end());
            }
            
            void *block = subzone->quantum_address(q);
            if (subzone->is_start_lite(q)) {
                zone->print_block(block);
            } else {
                FreeListNode *node = (FreeListNode *)block;
                malloc_printf("   %p(%6d) ### free\n", block, node->size());
            }

            // always continue
            return true;
        }
        
        inline bool visit(Zone *zone, Large *large) {
            if (!_is_large) {
                malloc_printf("Large Blocks\n");
                _is_large = true;
            }
            
            zone->print_block(large->address());
             
            // always continue
            return true;
        }
    };
    void Zone::print_all_blocks() {
        SpinLock lock(&_region_lock);
        print_all_blocks_visitor visitor;
        AllBlockIterator<print_all_blocks_visitor> iterator(this, visitor);
        iterator.visit();
    }
    
    
    //
    // print block
    //
    // Print the details of a block
    //
    void Zone::print_block(void *block) {
        print_block(block, "");
    }
    void Zone::print_block(void *block, const char *tag) {
        block = block_start(block);
        if (!block) malloc_printf("%s%p is not a block", tag, block);
        
        if (block) {
            if (in_subzone_memory(block)) {
                Subzone *subzone = Subzone::subzone(block);
                usword_t q = subzone->quantum_index(block);
                
                int rc = block_refcount(block);
                int layout = subzone->layout(q);
                bool is_unscanned = (layout & AUTO_UNSCANNED) != 0;
                bool is_object = (layout & AUTO_OBJECT) != 0;
                bool is_new = subzone->is_new(q);
                bool is_marked = subzone->is_marked(q);
                bool is_pending = false;
                char *class_name = "";
                if (is_object) {
                    void *isa = *(void **)block;
                    if (isa) class_name = *(char **)displace(isa, 8);
                }
                
                malloc_printf("%s%p(%6d) %s %s %s %s %s rc(%d) q(%u) subzone(%p) %s\n",
                                   tag, block, (unsigned)subzone->size(q),
                                   is_unscanned ? "   "  : "scn",
                                   is_object    ? "obj"  : "mem",
                                   is_new       ? "new"  : "   ",
                                   is_marked    ? "mark" : "    ",
                                   is_pending   ? "pend" : "    ",
                                   rc,
                                   q, subzone,
                                   class_name);
            }  else if (in_large_memory(block)) {
                Large *large = Large::large(block);
                
                int rc = block_refcount(block);
                int layout = large->layout();
                bool is_unscanned = (layout & AUTO_UNSCANNED) != 0;
                bool is_object = (layout & AUTO_OBJECT) != 0;
                bool is_new = large->is_new();
                bool is_marked = large->is_marked();
                bool is_pending = false;
                char *class_name = "";
                if (is_object) {
                    void *isa = *(void **)block;
                    if (isa) class_name = *(char **)displace(isa, 8); // XXX 64 bit WRONG
                }
                
                malloc_printf("%s%p(%6d) %s %s %s %s %s rc(%d) %s\n",
                                   tag, block, (unsigned)large->size(),
                                   is_unscanned ? "   "  : "scn",
                                   is_object    ? "obj"  : "mem",
                                   is_new       ? "new"  : "   ",
                                   is_marked    ? "mark" : "    ",
                                   is_pending   ? "pend" : "    ",
                                   rc,
                                   class_name);
            }
            
            return;
        }
      
        malloc_printf("%s%p is not a block", tag, block);
    }

};


