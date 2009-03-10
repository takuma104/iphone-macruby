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
#ifndef __AUTO_SUBZONE__
#define __AUTO_SUBZONE__

#include "AutoAdmin.h"
#include "AutoDefs.h"
#include "AutoBitmap.h"
#include "AutoFreeList.h"
#include "AutoWriteBarrier.h"

#include "auto_zone.h"

namespace Auto {

    // Forward declarations
    class Region;
    
    
    //----- Subzone -----//
    
    //
    // A subzone is a region in vm memory managed by automatic garbage collection.  The base address of the subheap is
    // aligned to the subzone quantum size such that the containing subzone can be quickly determined from any refererence 
    // into the subzone.
    // A C++ Subzone object is constructed at this aligned address.  The first chunk of memory are the write-barrier cards
    // that keep track of write-barrier-quantum ranges of objects that have been stored into.
    // Next are the instance variables including a back pointer to the "admin" that contains the free lists.
    // Fleshing out the rest are the "admin" data, one per quantum, indicating if its the start of a block etc.
    // Quantum numbers are used for most operations - the object with quantum 0 starts just after the end of the admin data
    // at the first quantum boundary opportunity.
    // There are two quantum sizes that a Subzone can be configured to manage - small and medium.  We keep a "bias" so that
    // in a bitmap of all subzones we can quickly keep it as dense as possible.
    //
    
    
    class Subzone : public Preallocated {
    
      private:

        unsigned char  _write_barrier_cards[subzone_write_barrier_max];
        WriteBarrier   _write_barrier;
                                        // write barrier for subzone - must be first
        usword_t       _quantum_log2;   // ilog2 of the quantum used in this admin
        Admin          *_admin;         // admin managing this subzone
        usword_t       _quantum_bias;   // the value added to subzone quantum numbers to get a globally
                                        // unique quantum (used to index region pending/mark bits)
        void           *_allocation_address; // base address for allocations
        usword_t       _in_use;         // high water mark
        unsigned char  _side_data[1];   // base for side data

        // The side data byte bits are described below.  A block of quantum size 1 has a zero size bit.
        // Otherwise, the byte corresponding to the second quantum is used to hold the # of quantums.
        enum {
            size_bit = 0x80,            // indicates size is continued in low 6 bits of next byte (less 1)
            size_bit_log2 = 7,
            
            start_bit = 0x40,           // indicates beginning of block
            start_bit_log2 = 6,
            
            age_ref_mask = 0x3C,        // combined refcount/age
            age_ref_mask_log2 = 2,      // shift needed to extract mask

            layout_mask = 0x03,         // indicates block organization
            
            end_block_mark = size_bit   // marks the end of a block (no start bit, size bit, and size == 0)
        };
        
        // The four bits used to represent size and reference count bits are defined below.
        // We optimize for reference counts of 0 and 1 and somewhat for 2.  Reference counts above 2
        // are marked as 2 in the side_data byte and held in a map table.
        // Blocks with reference counts of 0 and 1 will have a generational survival count of 5 before
        // becoming old and subject to collection only via a full collection.  Blocks with reference count
        // 2 or above have only a survival count of 3.  We suspect that such objects are likely to be long
        // lived.
        //
        // The bit layouts are somewhat arbitrary.  The only condition that they satisfy is that the calculation
        // for "youngest" and "eldest" should be fast.  There are three values for each, corresponding to each
        // of the three reference counts represented.  The bit values are chosen such that the test for either
        // of the three values can be done with two operations instead of three equality checks.
        enum {
            r0_a0 = 0x0, r0_a1 = 0x1, r0_a2 = 0x2, r0_a3 = 0x3,  // 00xx
            r1_a0 = 0x4, r1_a1 = 0x5, r1_a2 = 0x6, r1_a3 = 0x7,  // 01xx
            r2_a0 = 0x8, r2_a1 = 0x9, r2_a2 = 0xa, r2_a3 = 0xb,  // 10xx
            r0_a4 = 0xc, r0_a5 = 0xd,                            // 1100, 1101
            r1_a4 = 0xe, r1_a5 = 0xf,                            // 1101, 1111
        };
        
        // the age that each combined value represents
        static const unsigned char age_map[16]; //= { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 4, 5 };
        
        // the reference count that each value represents
        static const unsigned char ref_map[16]; //= { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1 };
        
        // Aging proceeds from high to low.  This map yields the (unshifted XXX) next age value that preserves
        // the reference count.  Note that age 0 maps to next age 0.
        static const unsigned char next_age_map[16];  
        //= {
        //    r0_a0, r0_a0, r0_a1, r0_a2,
        //    r1_a0, r1_a0, r1_a1, r1_a2,
        //    r2_a0, r2_a0, r2_a1, r2_a2,
        //    r0_a3, r0_a4,
        //    r1_a3, r1_a4,
        //};
        
        static const unsigned char incr_refcount_map[16];
        // = {
        //    r1_a0, r1_a1, r1_a2, r1_a3, // refcount 0 -> refcount 1
        //    r2_a0, r2_a1, r2_a2, r2_a3, // refcount 1 -> refcount 2
        //    0xff, 0xff, 0xff, 0xff,     // not used
        //    r1_a4, r1_a5,               // refcount 0 -> refcount 1
        //    r2_a3, r2_a3,               // refcount 1 -> refcount 2 (note (a4,a5)->a3 transition tho)
        //};
        
        static const unsigned char decr_refcount_map[16];
        //= {
        //    0xff, 0xff, 0xff, 0xff,     // not used
        //    r0_a0, r0_a1, r0_a2, r0_a3, // refcount 1 -> refcount 0
        //    r1_a0, r1_a1, r1_a2, r1_a5, // refcount 2 -> refcount 1  (note r2_a3 -> r1_a5)
        //    0xff, 0xff,                 // not used
        //    r0_a4, r0_a5,               // refcount 1 -> refcount 0
        //};
        
        // Does a value represent the youngest age? (r0_a5, r1_a5, r2_a3)
        static inline bool is_youngest(unsigned char ar) { return ((ar & 9) == 9) && ((ar & 6) != 0); }
        
        // Does a value represent the eldest age? (r?_a0)
        static inline bool is_eldest(unsigned char ar) { return ((ar & 3) == 0) && ((ar & 0xc) != 0xc); }

        //
        // subzone_side_data_max
        //
        // Given a constant quantum_log2 returns a constant (optimized code) defining
        // the maximum number of quantum in the subzone.
        //
        static inline usword_t subzone_side_data_max(usword_t quantum_log2) {
            // size of subzone data (non quantum) less size of side data
            usword_t header_size = sizeof(Subzone) - sizeof(unsigned char);
            // quantum size plus one byte for side data
            usword_t bytes_per_quantum = (1LL << quantum_log2) + 1;
            // round up the maximum number quantum (max out side data)
            return (subzone_quantum - header_size + bytes_per_quantum - 1) / bytes_per_quantum;
        }
        

        //
        // subzone_base_data_size
        //
        // Given a constant quantum_log2 returns a constant (optimized code) defining
        // the size of the non quantum data rounded up to a the nearest quantum.
        //
        static inline usword_t subzone_base_data_size(usword_t quantum_log2) {
            return align2(sizeof(Subzone) - sizeof(unsigned char) + subzone_side_data_max(quantum_log2), quantum_log2);
        }
       

        //
        // subzone_allocation_size
        //
        // Given a constant quantum_log2 returns a constant (optimized code) defining
        // the size of the area available for allocating quantum.
        //
        static inline usword_t subzone_allocation_size(usword_t quantum_log2) {
            return subzone_quantum - subzone_base_data_size(quantum_log2);
        }
       

        //
        // subzone_allocation_limit
        //
        // Given a constant quantum_log2 returns a constant (optimized code) defining
        // the number of the quantum that can be allocated.
        //
        static inline usword_t subzone_allocation_limit(usword_t quantum_log2) {
            return partition2(subzone_allocation_size(quantum_log2), quantum_log2);
        }
        
        
      public:
      
      
        //
        // Constructor
        //
        Subzone(Admin *admin, usword_t quantum_log2, usword_t quantum_bias)
            : _write_barrier(_write_barrier_cards, _write_barrier_cards, WriteBarrier::bytes_needed(subzone_quantum)),
              _quantum_log2(quantum_log2), _admin(admin), _quantum_bias(quantum_bias), _allocation_address(NULL), _in_use(0)
        {
            usword_t base_data_size = is_small() ?
                                        subzone_base_data_size(allocate_quantum_small_log2) :
                                        subzone_base_data_size(allocate_quantum_medium_log2);
			// malloc_printf("subzone base_data_size = %lu\n", base_data_size);
            _allocation_address = (void *)displace(this, base_data_size);
        }
        
      
        //
        // Accessors
        //
        usword_t quantum_log2()                const { return _quantum_log2; }
        Admin *admin()                         const { return _admin; }
        usword_t quantum_bias()                const { return _quantum_bias; }
        //static usword_t initial_age()                { return age_newborn; }
        
        
        //
        // subzone
        //
        // Return the subzone of an arbitrary memory address.
        //
        static inline Subzone *subzone(void *address) { return (Subzone *)((uintptr_t)address & ~mask(subzone_quantum_log2)); }


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
        // allocation_address
        //
        // Return the first allocation address in the subzone.
        //
        inline void *allocation_address() const { return _allocation_address; }
        

        //
        // allocation_end
        //
        // Return the last allocation address in the subzone.
        //
        inline void *allocation_end() { return displace(this, subzone_quantum); }
        
        
        //
        // base_data_size
        //
        // Return the size of the base data space in the subzone.
        //
        inline usword_t base_data_size() const {
             return is_small() ? subzone_base_data_size(allocate_quantum_small_log2):
                                 subzone_base_data_size(allocate_quantum_medium_log2);
        }


        //
        // base_data_quantum_count
        //
        // Return the number quantum of the base data space occupies.
        //
        inline usword_t base_data_quantum_count(usword_t quantum_log2) const {
             return subzone_base_data_size(quantum_log2) >> quantum_log2;
        }


        //
        // allocation_size
        //
        // Return the size of the allocation space in the subzone.
        //
        inline usword_t allocation_size() const {
             return is_small() ? subzone_allocation_size(allocate_quantum_small_log2) :
                                 subzone_allocation_size(allocate_quantum_medium_log2);
        }


        //
        // allocation_limit
        //
        // Return the number quantum in the subzone.
        //
        inline usword_t allocation_limit() const {
             return is_small() ? subzone_allocation_limit(allocate_quantum_small_log2) :
                                 subzone_allocation_limit(allocate_quantum_medium_log2);
        }
        
        
        //
        // quantum_index
        //
        // Returns a quantum index for a arbitrary pointer.
        //
        inline usword_t quantum_index(void *address, usword_t quantum_log2) const {
            return (((uintptr_t)address & mask(subzone_quantum_log2)) >> quantum_log2) - base_data_quantum_count(quantum_log2);
        }
        inline usword_t quantum_index(void *address) const {
            return is_small() ? quantum_index(address, allocate_quantum_small_log2) :
                                quantum_index(address, allocate_quantum_medium_log2);
        }
        
        
        //
        // allocation_count
        //
        // High water count for this subzone
        //
        inline usword_t allocation_count() const { return _in_use; }

        //
        // add_allocation_count
        //
        // High water count for this subzone
        //
        inline void raise_allocation_count(usword_t q)  { _in_use += q; }

        //
        // subtract_allocation_count
        //
        // High water count for this subzone
        //
        inline void lower_allocation_count(usword_t q)  { _in_use -= q; }

        //
        // quantum_count
        //
        // Returns a number of quantum for a given size.
        //
        inline const usword_t quantum_count(const size_t size) const {
            return partition2(size, _quantum_log2);
        }
        
        
        //
        // quantum_size
        //
        // Returns the size if n quantum.
        //
        inline const usword_t quantum_size(const usword_t n) const { return n << _quantum_log2; }
        
        
        //
        // quantum_address
        //
        // Returns the address if a specified quantum.
        //
        inline void *quantum_address(const usword_t q) const { return displace(_allocation_address, quantum_size(q)); }
        
        
        //
        // quantum_range
        //
        // Return the range for the block at q.
        //
        inline void quantum_range(const usword_t q, Range &range) const {
            range.set_range(quantum_address(q), size(q));
        }
        inline void quantum_range(void *block, Range &range) const {
            range.set_range(block, size(quantum_index(block)));
        }
        
        //
        // Side data accessors
        //
        inline bool is_free(usword_t q)              const { return _side_data[q] == 0; }
        inline bool is_free(void *address)           const { return is_free(quantum_index(address)); }
        
        inline bool is_start_lite(usword_t q)        const { return (_side_data[q] & start_bit) != 0; }
        inline bool is_start(usword_t q)             const { return q < allocation_limit() && (_side_data[q] & start_bit) != 0; }
        inline bool is_start(void *address)          const {
            return (is_small() ? is_bit_aligned(address, allocate_quantum_small_log2) :
                                 is_bit_aligned(address, allocate_quantum_medium_log2)) &&
                   is_start(quantum_index(address));
        }

        inline usword_t length(usword_t q)           const { return !(_side_data[q] & size_bit) ? 1 : (_side_data[q + 1] + 1); }
        inline usword_t length(void *address)        const { return length(quantum_index(address)); }
        
        inline usword_t size(usword_t q)             const { return quantum_size(length(q)); }
        inline usword_t size(void *address)          const { return size(quantum_index(address)); }
        
        inline bool is_new(usword_t q)               const { return q < allocation_limit() && !is_eldest((_side_data[q] & age_ref_mask) >> age_ref_mask_log2); }
        inline bool is_new(void *address)            const { return is_new(quantum_index(address)); }
        
        inline bool is_newest(usword_t q)            const { return is_youngest((_side_data[q] & age_ref_mask) >> age_ref_mask_log2); }
        inline bool is_newest(void *address)         const { return is_newest(quantum_index(address)); }
        

        inline usword_t age(usword_t q)              const { return age_map[(_side_data[q] & age_ref_mask) >> age_ref_mask_log2]; }
        inline usword_t age(void *address)           const { return age(quantum_index(address)); }
        
        inline usword_t refcount(usword_t q)         const { return ref_map[(_side_data[q] & age_ref_mask) >> age_ref_mask_log2]; }
        inline usword_t refcount(void *address)      const { return refcount(quantum_index(address)); }
        
        inline usword_t sideData(void *address) const { return _side_data[quantum_index(address)]; }

        inline void incr_refcount(usword_t q) {
            unsigned char sd = _side_data[q];
            unsigned char ar = (sd & age_ref_mask) >> age_ref_mask_log2;
            ar = incr_refcount_map[ar];
            sd &= ~age_ref_mask;
            _side_data[q] = sd | (ar << age_ref_mask_log2);
        }
                                                                
        inline void decr_refcount(usword_t q) {
            unsigned char sd = _side_data[q];
            unsigned char ar = (sd & age_ref_mask) >> age_ref_mask_log2;
            ar = decr_refcount_map[ar];
            sd &= ~age_ref_mask;
            _side_data[q] = sd | (ar << age_ref_mask_log2);
        }
                                                                
        inline void mature(usword_t q) {
            unsigned char data = _side_data[q];
            unsigned char current = (data & age_ref_mask) >> age_ref_mask_log2;
            data &= ~age_ref_mask;
            data |= (next_age_map[current] << age_ref_mask_log2);
            _side_data[q] = data;
        }
        inline void mature(void *address)                  { mature(quantum_index(address)); }
        
        inline bool is_marked(usword_t q)            const { return q < allocation_limit() && _admin->is_marked(_quantum_bias + q); }
        inline bool is_marked(void *address)         const { return is_marked(quantum_index(address)); }
        
        inline usword_t layout(usword_t q)           const { return _side_data[q] & layout_mask; }
        inline usword_t layout(void *address)        const { return layout(quantum_index(address)); }

        inline bool is_scanned(usword_t q)           const { return !(layout(q) & AUTO_UNSCANNED); }
        inline bool is_scanned(void *address)        const { return is_scanned(quantum_index(address)); }
        
        inline bool has_refcount(usword_t q)         const { return 0 != ref_map[(_side_data[q] & age_ref_mask)>>age_ref_mask_log2]; }
        inline bool has_refcount(void *address)      const { return has_refcount(quantum_index(address)); }
        
        inline void set_mark(usword_t q)                   { _admin->set_mark(_quantum_bias + q); }
        inline void set_mark(void *address)                { set_mark(quantum_index(address)); }

        inline void clear_mark(usword_t q)                 { _admin->clear_mark(_quantum_bias + q); }
        inline void clear_mark(void *address)              { clear_mark(quantum_index(address)); }
        
        // mark (if not already marked)
        // return already-marked
        inline bool test_set_mark(usword_t q)              { return _admin->test_set_mark(_quantum_bias + q); }
        inline bool test_set_mark(void *address)           { return test_set_mark(quantum_index(address)); }
        
        inline void set_layout(usword_t q, usword_t layout) {
            unsigned d = _side_data[q];
            d &= ~layout_mask;
            d |= layout;
            _side_data[q] = d;
        }
        inline void set_layout(void *address, usword_t layout) { set_layout(quantum_index(address), layout); }
        
        inline bool is_pending(usword_t q)           const { return _admin->is_pending(_quantum_bias + q); }
        inline bool is_pending(void *address)        const { return is_pending(quantum_index(address)); }
        
        inline void set_pending(usword_t q)                { _admin->set_pending(_quantum_bias + q); }
        inline void set_pending(void *address)             { set_pending(quantum_index(address)); }
        
        inline void clear_pending(usword_t q)              { _admin->clear_pending(_quantum_bias + q); }
        inline void clear_pending(void *address)           { clear_pending(quantum_index(address)); }
        
      
        //
        // is_used
        //
        // Return true if the quantum is in a used quantum.
        //
        inline bool is_used(usword_t q) const {
            // any data indicates use
            if (_side_data[q]) return true;
            
            // otherwise find the prior start
            for (usword_t s = q; true; s--) {
                if (is_start_lite(s)) {
                    usword_t n = length(s);
                    // make sure q is in range
                    return (q - s) < n;
                }
                if (!s) break;
            }
            return false;
        }
        inline bool is_used(void *address)          const { return is_used(quantum_index(address)); }
        

        //
        // should_pend
        //
        // High performance check and set for scanning blocks in a subzone (major hotspot.)
        //
        bool should_pend(void *address, unsigned char &layout) {
            usword_t q;
            unsigned char *sdq;
            
            if (is_small()) {
                if (!is_bit_aligned(address, allocate_quantum_small_log2)) return false;
                q = quantum_index(address, allocate_quantum_small_log2);
                sdq = _side_data + q;
                if (q >= subzone_allocation_limit(allocate_quantum_small_log2)) return false;
            } else {
                if (!is_bit_aligned(address, allocate_quantum_medium_log2)) return false;
                q = quantum_index(address, allocate_quantum_medium_log2);
                sdq = _side_data + q;
                if (q >= subzone_allocation_limit(allocate_quantum_medium_log2)) return false;
            }

            usword_t sd = *sdq;
            if ((sd & start_bit) != start_bit) return false;
            if (test_set_mark(q)) return false;
            
            layout = (sd & layout_mask);
            return true;
        }
        

        //
        // should_pend_new
        //
        // High performance check and set for scanning new blocks in a subzone (major hotspot.)
        //
        bool should_pend_new(void *address, unsigned char &layout) {
            usword_t q;
            unsigned char *sdq;
            
            if (is_small()) {
                if (!is_bit_aligned(address, allocate_quantum_small_log2)) return false;
                q = quantum_index(address, allocate_quantum_small_log2);
                sdq = _side_data + q;
                if (q >= subzone_allocation_limit(allocate_quantum_small_log2)) return false;
            } else {
                if (!is_bit_aligned(address, allocate_quantum_medium_log2)) return false;
                q = quantum_index(address, allocate_quantum_medium_log2);
                sdq = _side_data + q;
                if (q >= subzone_allocation_limit(allocate_quantum_medium_log2)) return false;
            }

            usword_t sd = *sdq;
            if ((sd & start_bit) != start_bit || is_eldest((sd & age_ref_mask) >> age_ref_mask_log2)) return false;
            if (test_set_mark(q)) return false;

            layout = (sd & layout_mask);
            return true;
        }
        

        //
        // start
        //
        // Return the start quantum for the given quantum/address.
        //
        inline usword_t start(usword_t q) const {
            for ( ; 0 < q; q--) {
                if (is_start_lite(q)) break;
            } 
            return q;
        }
        inline usword_t start(void *address)        const { return start(quantum_index(address)); }
        
        
        //
        // next_quantum
        //
        // Returns the next q for block or free node.
        //
        inline usword_t next_quantum(usword_t q = 0) const {
            usword_t nq;
            if (is_start_lite(q)) {
                nq = q + length(q);
            } else {
                // FIXME:  accessing the free list without holding the allocation lock is a race condition.
                // SpinLock lock(_admin->lock());
                // FreeListNode *node = (FreeListNode *)quantum_address(q);
                // q = quantum_index(node->next_block());
                // Instead, we simply search for the next block start. Note, this means we no longer
                // return quanta for free blocks.
                usword_t n = allocation_limit();
                nq = q + 1;
                while (nq < n && !is_start_lite(nq)) ++nq;
            }
            ASSERTION(nq > q);
            return nq;
        }

        inline usword_t next_quantum(usword_t q, MemoryReader & reader) const {
            return next_quantum(q);
        }
        

        //
        // block_start
        //
        // Return the start address for the given address.
        // All clients must (and do) check for NULL return.
        //
        inline void * block_start(void *address) const {
            usword_t q = quantum_index(address), s = q;
            do {
                if (is_start_lite(s)) {
                    usword_t n = length(s);
                    // make sure q is in range
                    return ((q - s) < n) ? quantum_address(s) : NULL;
                }
            } while (s--);
            return NULL;
        }
        
        
        //
        // allocate
        //
        // Initialize side data for a new block.
        //
        inline void allocate(usword_t q, const usword_t n, const usword_t layout, const bool refcount_is_one) {
            bool size_continued = n != 1;
            ASSERTION(n <= maximum_quanta);
            _side_data[q] = start_bit |
                            (size_continued ? size_bit : 0) |
                            ((refcount_is_one ? r1_a5 : r0_a5) << age_ref_mask_log2) |
                            layout;
            if (size_continued) {
                _side_data[q + 1] = n - 1; // size is continued in low 6 bits of next byte (less 1)
                if (n > 2) _side_data[q + n - 1] = end_block_mark;
            }
        }

        
        //
        // deallocate
        //
        // Clear side data for an existing block.
        //
        inline void deallocate(usword_t q) {
            if (_side_data[q] & size_bit) {
                usword_t n = _side_data[q + 1] + 1; // size is continued in low 6 bits of next byte (less 1)
                ASSERTION(n <= maximum_quanta);
                _side_data[q + 1] = 0;
                if (n > 2) _side_data[q + n - 1] = 0;
            }
            _side_data[q] = 0;
        }
        inline void deallocate(usword_t q, usword_t n) {
            if (n > 1) {
                ASSERTION(n <= maximum_quanta);
                _side_data[q + 1] = 0;
                if (n > 2) _side_data[q + n - 1] = 0;
            }
            _side_data[q] = 0;
        }


        //
        // write_barrier
        //
        // Returns accessor for this subzone's write barrier.
        //
        inline WriteBarrier& write_barrier() {
            return _write_barrier;
        }
    };
    
    
    //----- SubzoneRangeIterator -----//
    
    //
    // Iterate over a range of memory
    //
    
    class SubzoneRangeIterator : public Range {

      public:
        
        //
        // Constructors
        //
        SubzoneRangeIterator(void *address, const usword_t size)
        : Range(address, size)
        {}
        
        SubzoneRangeIterator(void *address, void *end)
        : Range(address, end)
        {}
        
        SubzoneRangeIterator(Range range)
        : Range(range)
        {}
        
        
        //
        // next
        //
        // Returns next subzone in the range or NULL if no more entries available.
        //
        inline Subzone *next() {
            // if cursor is still in range
            if (address() < end()) {
                // capture cursor position
                Subzone *_next = (Subzone *)address();
                // advance for next call
                set_address(displace(_next, subzone_quantum));
                // return captured cursor position
                return _next;
            }
            
            // at end
            return NULL;
        }
        
    };
};


#endif // __AUTO_SUBZONE__

