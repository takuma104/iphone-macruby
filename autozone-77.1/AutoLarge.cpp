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

#include "AutoLarge.h"
#include "AutoSubzone.h"
#include "AutoZone.h"


namespace Auto {

    //----- Large -----//
    
    Large::Large(usword_t vm_size, usword_t size, usword_t layout, usword_t refcount, usword_t age, unsigned char* write_barrier_cards)
        : _prev(NULL), _next(NULL), _vm_size(vm_size), _size(size), _layout(layout), _refcount(refcount), _age(age),
          _is_pending(false), _is_marked(false), _is_freed(false),
          _write_barrier(displace(this, side_data_size()), write_barrier_cards, WriteBarrier::bytes_needed(size))
    {
    }

    //
    // allocate
    //
    // Allocate memory used for the large block.
    //
    Large *Large::allocate(Zone *zone, const usword_t size, usword_t layout, bool refcount_is_one) {
        // determine the size of the block header
        usword_t header_size = side_data_size();
        
        // determine memory needed for allocation, guarantee minimum quantum alignment
        usword_t allocation_size = align2(size, allocate_quantum_small_log2);
        
        // determine memory for the write barrier, guarantee minimum quantum alignment
        usword_t wb_size = align2(WriteBarrier::bytes_needed(allocation_size), allocate_quantum_small_log2);

        // determine total allocation
        usword_t vm_size = align2(header_size + allocation_size + wb_size, page_size_log2);

        // allocate memory, construct with placement new.
        void *space = zone->arena_allocate_large(vm_size);
        return space != NULL ? new (space) Large(vm_size, allocation_size, layout, refcount_is_one ? 1 : 0, initial_age,
                                                 (unsigned char *)displace(space, header_size + allocation_size)) : NULL;
     }


    //
    // deallocate
    //
    // Release memory used by the large block.
    //
    void Large::deallocate(Zone *zone) {
        // release large data
        zone->arena_deallocate((void *)this, _vm_size);
    }    
};
