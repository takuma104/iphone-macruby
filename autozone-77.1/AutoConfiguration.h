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
#ifndef __AUTO_CONFIGURATION__
#define __AUTO_CONFIGURATION__

#include "AutoDefs.h"

/*    
    The collector suballocates memory in multiples of 3 different quanta sizes, small, medium, and large.
    Medium is 64*small, large is 64*medium.
    (64 is a maximum derived from the leftover bits in a secondary 'admin' byte)
    A "Region" is a data structure that manages a large virtual memory space of "subzone"s, each subzone
    is dedicated to either small or medium quanta (multiples) allocation blocks.  The region has an 'admin'
    data structure containing 64 free lists of that multiple of quanta sized blocks.  There is one admin
    for all small quanta blocks and one for all large quanta blcoks.
    There is a bitmap for all subzones in use so as to help easily deny false pointers - each subzone in use
    actually starts with a reference to its controlling admin data structure.  Also in this subzone area at
    the beginning is space for all the write-barrier bytes and the allocation administrative data.
    
    Regions are chained together.
    
    Large quanta objects are freely allocated on large quanta alignment, and are tracked in their own bitmap
    again to easily deny false pointers.  There is administrative data (the "Auto::Large" instance) that
    actually starts on that alignment - the data provided to the client is an offset from that alignment.
    It is is thus very cheap to find the collector's admin data for an allocated block.
    
    
    If the "Arena" logic is used, all Regions and Large quanta objects are actually suballocated from a fixed
    sized 'Arena' that is allocated at the beginning.  This is useful on 64-bit systems where the bitmaps for
    all large or even subzones would be too huge to search if, say, the kernel handed out widely dispersed
    large allocations.  Its also useful if space is at a premium in 32-bit worlds.
    
 */

//
// Notes:
// 1M subzone - 8K (2 pages) required for a 128 byte quantum write barrier
//            - 16 bytes (4 words) unused at beginning of each subzone
//              subzone_size / (write_barrier_quantum * write_barrier_quantum)
//            - 1 page is 256 x 16 byte quantum
//              or 64 bytes in allocation bit map blocked out for write barrier
// 32 bit world - contains 1M of pages
//              - contains 4096 1M subzones
//                512 (128 words) byte bit map required
//              - 32K (8 pages) bit map required to mark every page
//              - 8k (2 pages) bit map required to mark 64K quantum (large allocations)
// 16 byte quantum - 64K worth per subzone
//                   64K (16 pages) required for side data
// 1024 quantum - 1024 worth per subzone
//                1024 bytes (1/4 page) required for side data
// 64K large quantum - 64K worth per 32 bit world
//                   - 32K (8 pages) per bit map
//
    
//
// On 64-bit systems we can't keep maps of every 1M subzone or, worse, 64K Large quantum.
// Instead, we preallocate an Arena and suballocate both Large nodes and Regions of small/medium
// quanta from that space.  The Arena can be of any reasonable power of two size on that power of two boundary.
//
// Note: Arenas have not been tested on 32-bit
// Note: Only an Arena size of 4G has been tested on 64-bit

#if defined(__ppc64__) || defined(__x86_64__)
#   define UseArena   1
#else
#   define UseArena   0
#endif

namespace Auto {

    enum {
        // Maximum number of quanta per allocation (64) in the small and medium admins
        maximum_quanta_log2          = 6u,
        maximum_quanta               = (1ul << maximum_quanta_log2),
    
        // small allocation quantum size (16/32)
#if defined(__ppc64__) || defined(__x86_64__)
        allocate_quantum_small_log2  = 5u, // 32 byte quantum (FreeBlock is 32 bytes)
#elif defined(__ppc__) || defined(__i386__)
        allocate_quantum_small_log2  = 4u, // 16 byte quantum
#else
#error unknown architecture
#endif
        allocate_quantum_small       = (1ul << allocate_quantum_small_log2),
        
        // medium allocation quantum size (1024/2048 bytes)
        allocate_quantum_medium_log2 = (allocate_quantum_small_log2 + maximum_quanta_log2),
        allocate_quantum_medium      = (1ul << allocate_quantum_medium_log2),

        // large allocation quantum size (64K/128K bytes) aka memory quantum
        allocate_quantum_large_log2  = (allocate_quantum_medium_log2 + maximum_quanta_log2),
        allocate_quantum_large       = (1ul << allocate_quantum_large_log2),
        
        // arena size
#if defined(__ppc64__) || defined(__x86_64__)
        arena_size_log2              = 35ul,        // 32G
#elif defined(__ppc__) || defined(__i386__)
        arena_size_log2              = 32ul,         // 4G
#else
#error unknown architecture
#endif
        
        // maximum number of large quantum that can be allocated
        allocate_quantum_large_max_log2 = arena_size_log2 - allocate_quantum_large_log2,
        allocate_quantum_large_max   = (1ul << allocate_quantum_large_max_log2),

        // subzone quantum size (2^20 == 1M)
        subzone_quantum_log2         = 20u,
        subzone_quantum              = (1ul << subzone_quantum_log2),
		
        // bytes needed per subzone to represent a bitmap of smallest quantum
        subzone_bitmap_bytes_log2    = subzone_quantum_log2 - allocate_quantum_small_log2 - 3, // 3 == byte_log2
        subzone_bitmap_bytes         = (1ul << subzone_bitmap_bytes_log2),
        
        bitmaps_per_region           = 2,
        
        // maximum number of subzone quantum that can be allocated
        subzone_quantum_max_log2     = arena_size_log2 - subzone_quantum_log2,
        subzone_quantum_max          = (1ul << subzone_quantum_max_log2),

        // initial subzone allocation attempt
        initial_subzone_count        = 128u,

       // minimum subzone allocation  (one for each quantum type)              
        initial_subzone_min_count    = 2u,        

        // number of bytes in write barrier quantum (card == 128 bytes)
        write_barrier_quantum_log2   = 7u,
        write_barrier_quantum        = (1ul << write_barrier_quantum_log2),                
        
        // maximum number of write barrier bytes per subzone
        subzone_write_barrier_max    = (subzone_quantum >> write_barrier_quantum_log2)
    };
    
};

#endif // __AUTO_CONFIGURATION__
