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
#include "AutoEnvironment.h"
#include "AutoRegion.h"
#include "AutoZone.h"

namespace Auto {


    //----- Region -----//
    
    
    //
    // new_region
    //
    // Construct and initialize a new region.
    // First we get the memory that we will parcel out, then we build up the Region object itself
    //
    Region *Region::new_region(Zone *zone) {
        usword_t allocation_size;                           // size of subzone region
        void *allocation_address = NULL;                    // address of subzone region
        unsigned nzones;

#if UseArena
        // take half the space for small/medium.  A better scheme might, in effect, preallocate the entire space,
        // and then guard crossing into the large space in add_subzone.  The top of the large area would be before
        // the bitmaps - stealing from the top subzones.
        // For now, take half.  Chances are there won't be enough physical memory to exhaust this before swapping.
        nzones = 1 << (arena_size_log2 - subzone_quantum_log2 - 1);
        allocation_size = managed_size(nzones);
        allocation_address = zone->arena_allocate_region(allocation_size);  // only succeeds once
#else
        // try to allocate a region until we get space
        for (unsigned n = initial_subzone_count; n >= initial_subzone_min_count && !allocation_address; n--) {
            // size of next attempt
            allocation_size = managed_size(n);            
            // attempt to allocate data that size
            allocation_address = allocate_memory(allocation_size, subzone_quantum, VM_MEMORY_MALLOC_SMALL);
            nzones = n;
        }
#endif

        // handle error
        if (!allocation_address) {
            error("Can not allocate new region");
            return NULL;
        }
        
        // create region and admin data
        Region *region = new Region(zone, allocation_address, allocation_size, nzones);
        
        if (!region) {
            error("Can not allocate new region");
            zone->arena_deallocate(allocation_address, allocation_size);
        }
        
        return region;
    }


    // 
    // Constructor
    //
    Region::Region(Zone *zone, void *address, usword_t size, usword_t nsubzones) {
        // allocation may fail (and we don't want to use throw)
        if (!this) return;
        
        // initialize
        
        // remember our total size before lopping off pending/stack and mark bit space
        set_range(address, size);
        _next = NULL;
        _zone = zone;
        _subzone_lock = 0;
        
        // grab space for use for scanning/stack
        // We need, worse case, 1 bit per smallest quantum (16 bytes), so this should
        // have an assert of something like size/allocate_quantum_small/8
        unsigned long bytes_per_bitmap = nsubzones << subzone_bitmap_bytes_log2;
        size -= bytes_per_bitmap;
        zone->statistics().add_admin(bytes_per_bitmap);
        // we prefer to use the stack, but if it overflows, we have (virtual) space enough
        // to do a pending bit iterative scan as fallback
        _scan_space.set_range(displace(address, size), bytes_per_bitmap);
        // start out as zero length; adding subzones will grow it
        _pending.set_address(_scan_space.address());
        _pending.set_size(0);
        
        // the scanning thread needs exclusive access to the mark bits so they are indpendent
        // of other 'admin' data.  Reserve enough space for the case of all subzones being of smallest quanta.
        size -= bytes_per_bitmap;
        zone->statistics().add_admin(bytes_per_bitmap);
        _marks.set_address(displace(address, size));
        _marks.set_size(0);
        
        // track number of subzones
        _i_subzones = 0;
        _n_subzones = size >> subzone_quantum_log2;
        if (_n_subzones != nsubzones) {
            // we could, in principle, compute the 'tax' of the bitmaps as a percentage and then confirm that
            // size-tax is a multiple of subzone size.  Easier to pass in the 'nsubzones' and confirm.
            //printf("size %lx, subzones %d, nsubzones %d\n", size, (int)_n_subzones, (int)nsubzones);
            error("region: size inconsistent with number of subzones");
        }
        _n_quantum = 0;

        // initialize the small and medium admin
        _small_admin.initialize(zone, this, allocate_quantum_small_log2);
        _medium_admin.initialize(zone, this, allocate_quantum_medium_log2);
        
        // prime the small and medium admins with a subzone each (will handle correctly if there are no subzones)
        add_subzone(&_small_admin);
        add_subzone(&_medium_admin);

        // update statistics
        zone->statistics().add_admin(Region::bytes_needed());   // XXX counted via aux I think
        zone->statistics().add_allocated(size);
        zone->statistics().increment_regions_in_use();
    }

        
    //
    // Destructor
    //
    Region::~Region() {
        // update statistics
        _zone->statistics().add_admin(-Region::bytes_needed());
        // XXX never happens, never will
        // update other statistics...
    }
        
        
    //
    // allocate
    //
    // Allocate a block of memory from a subzone.
    //
    void *Region::allocate(const size_t size, const unsigned layout, bool clear, bool refcount_is_one) {
        // determine which admin should manage the allocation
        Admin *admin = size < allocate_quantum_medium ? &_small_admin : &_medium_admin;
        bool did_grow = false;
        void *block = NULL;
        
        while (!(block = admin->find_allocation(size, layout, refcount_is_one, did_grow))) {
        
            // Only the last region can add subzones.
            if (next()) return NULL;
            _zone->control.will_grow((auto_zone_t *)_zone, AUTO_HEAP_SUBZONE_EXHAUSTED);
            
            // We're the last region.
            if (!add_subzone(admin)) {
                // No more subzones.
                _zone->control.will_grow((auto_zone_t *)_zone, AUTO_HEAP_REGION_EXHAUSTED);
                // allocate another region
                return NULL;
            }
            // try again.
        }
        
        if (did_grow) _zone->control.will_grow((auto_zone_t *)_zone, AUTO_HEAP_HOLES_EXHAUSTED);

        Subzone *subzone = Subzone::subzone(block);
        usword_t allocated_size = subzone->size(block);
        

        // initialize block
        if (clear) {
            void **end = (void **)displace(block, allocated_size);
            switch (allocated_size/sizeof(void *)) {
                    
                case 12: end[-12] = NULL;
                case 11: end[-11] = NULL;
                case 10: end[-10] = NULL;
                case 9: end[-9] = NULL;
                case 8: end[-8] = NULL;
                case 7: end[-7] = NULL;
                case 6: end[-6] = NULL;
                case 5: end[-5] = NULL;
                case 4: end[-4] = NULL;
                case 3: end[-3] = NULL;
                case 2: end[-2] = NULL;
                case 1: end[-1] = NULL;
                case 0: break;
                default:
                    bzero(block, allocated_size);
                    break;
            }
        }
        // Performance work
        // before (always use bzero):  allocating 10000 blocks of size 12 took auto 1624 somethings and malloc 1048 somethings
        // after:                      allocating 10000 blocks of size 12 took auto 1375 microseconds and malloc 1111 microseconds
        // and no work here is even better
        else if (layout & AUTO_UNSCANNED) {
            // for now, need to worry about clients going from scanned to unscanned and finding stray/bad pointers there.
            // XXX need to outlaw going from scanned to unscanned since this is a very expensive operation. rdar://5558048
            // XXX or else make all such callers do their zeroing.  This is a leak issue, not correctness issue. rdar://5557990
            usword_t remainder_size = (allocated_size - size);  
            void **end = (void **)displace(block, allocated_size);
            switch (remainder_size/sizeof(void *)) { // ignore remainder - it can't be a pointer
            case 3: end[-3] = NULL;
            case 2: end[-2] = NULL;
            case 1: end[-1] = NULL;
            case 0: break;
            default:
                bzero(displace(block, size), remainder_size);
                break;
            }
        }
        
        // update statistics
        _zone->statistics().add_count(1);
        _zone->statistics().add_size(allocated_size);
        _zone->add_allocated_bytes(allocated_size);
        
        return block;
    }


    //
    // deallocate
    //
    // Release memory allocated for a block.
    //
    void Region::deallocate(Subzone *subzone, void *block) {
        // update statistics
        usword_t size = subzone->size(block);
        _zone->statistics().add_count(-1);
        _zone->statistics().add_size(-size);

        // get the admin for the subzone
        Admin *admin = subzone->admin();
        
        // remove the block from admin
        admin->deallocate(block);
    }


    //
    // add_subzone
    //
    // Add a new subzone to one of the admin.
    //
    bool Region::add_subzone(Admin *admin) {
        // BEGIN CRITICAL SECTION
        SpinLock admin_lock(admin->lock());
        
        // There may have been a race to get here. Verify that the admin has no active subzone
        // as a quick check that we still need to add one.
        if (admin->active_subzone()) return true;
        
        Subzone *subzone = NULL;
        {
            SpinLock subzone_lock(&_subzone_lock);

            // if there are no subzones available then not much we can do
            if (_i_subzones == _n_subzones) return false;
            
            // Get next subzone
            subzone = new(subzone_address(_i_subzones++)) Subzone(admin, admin->quantum_log2(), _n_quantum);

            // advance quantum count
            _n_quantum += subzone->allocation_limit();
            
            // update pending bitmap to total quanta available to be allocated in this region
            _pending.set_size(Bitmap::bytes_needed(_n_quantum));
            _marks.set_size(Bitmap::bytes_needed(_n_quantum));
        }

        // Add free allocation space to admin 
        admin->set_active_subzone(subzone);

        // update statistics
        _zone->statistics().add_admin(subzone_write_barrier_max);
        _zone->statistics().increment_subzones_in_use();
        
        // let the zone know the subzone is active.
        _zone->activate_subzone(subzone);
        
        // END CRITICAL SECTION
        
        return true;
    }
};
