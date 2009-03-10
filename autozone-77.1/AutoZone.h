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
#ifndef __AUTO_ZONE_CORE__
#define __AUTO_ZONE_CORE__

#include "auto_zone.h"
#include "auto_impl_utilities.h"

#include "AutoBitmap.h"
#include "AutoConfiguration.h"
#include "AutoDefs.h"
#include "AutoLarge.h"
#include "AutoListTypes.h"
#include "AutoLock.h"
#include "AutoHashTable.h"
#include "AutoRegion.h"
#include "AutoStatistics.h"
#include "AutoSubzone.h"
#include "AutoThread.h"

#include <algorithm>

// XXX_JML embedded object pointers
// XXX_JML resurrection



namespace Auto {

    //
    // Forward declarations.
    //
    
    
    //
    // PointerList
    // Holds list of pointers, including potential repeats
    // Currently used to hold the list of non-repeating garbage
    // The same list is then also used to hold possibly repeating pointers that need "enlivening"
    // The enlivening set is the set of pointers operated on by other threads while the collector was
    // scanning (and we prefer to not have to do interlocks on the scan bits).

    class PointerList {
        usword_t                        _count;
        usword_t                        _capacity;
        vm_address_t                   *_buffer;
        Statistics                      &_stats;     // from our Zone; track memory use as admin_in_use_size
    public:
        PointerList(Statistics &s) :    _count(0), _capacity(0), _buffer(NULL), _stats(s) {}
        ~PointerList()                  { if (_buffer) deallocate_memory(_buffer, _capacity * sizeof(vm_address_t)); }
        
        usword_t count()          const { return _count; }
        void clear_count()              { _count = 0; }
        void set_count(usword_t n)      { _count = n; }
        vm_address_t *buffer()          { return _buffer; }
        usword_t size()                 { return _capacity * sizeof(vm_address_t); }

        void uncommit()                 { if (_buffer) uncommit_memory(_buffer, _capacity * sizeof(vm_address_t)); }

        void grow() {
            if (!_buffer) {
                // start off with 4 pages.
                _capacity = 4 * page_size / sizeof(vm_address_t);
                _buffer = (vm_address_t*) allocate_memory(page_size * 4);
                _stats.add_admin(page_size * 4);    // worst case, but once in use probably stays hot
             } else {
                // double the capacity.
                vm_size_t old_size = _capacity * sizeof(vm_address_t);
                vm_address_t *new_buffer = (vm_address_t*) allocate_memory(old_size * 2);
                if (!new_buffer) {
                    malloc_printf("PointerList::grow() failed.\n");
                    abort();
                }
                _stats.add_admin(old_size);
                _capacity *= 2;
                // malloc_printf("growing PointerList._buffer old_size = %lu, new_size = %lu\n", old_size, old_size * 2);
                vm_copy(mach_task_self(), (vm_address_t) _buffer, old_size, (vm_address_t)new_buffer);
                deallocate_memory(_buffer, old_size);
                _buffer = new_buffer;
            }
        }
        
        void grow(usword_t count) {
            if (count > _capacity) {
                usword_t old_size = _capacity * sizeof(vm_address_t);
                if (_capacity == 0L) _capacity = 4 * page_size / sizeof(vm_address_t);
                while (count > _capacity) _capacity *= 2;
                vm_address_t *new_buffer = (vm_address_t*) allocate_memory(_capacity * sizeof(vm_address_t));
                if (!new_buffer) {
                    malloc_printf("PointerList::grow(count=%lu) failed.\n", count);
                    abort();
                }
                _stats.add_admin(_capacity * sizeof(vm_address_t) - old_size);
                if (_buffer) {
                    // only copy contents if _count != 0.
                    if (new_buffer && _count) {
                        // malloc_printf("growing PointerList._buffer old_size = %lu, new_size = %lu\n", old_size, old_size * 2);
                        vm_copy(mach_task_self(), (vm_address_t) _buffer, old_size, (vm_address_t)new_buffer);
                    }
                    deallocate_memory(_buffer, old_size);
                }
                _buffer = new_buffer;
            }
        }
        
        void add(vm_address_t addr) {
            if (_count == _capacity) grow();
            _buffer[_count++] = addr;
        }
        
        void add(void *pointer) {
            add((vm_address_t)pointer);
        }
    };
    
    
     //----- ScanStack -----//
    
    class ScanStack {
      
      private:
        void **                _address;                    // base address of stack
        void **                _end;                        // stack last + 1
        void **                _cursor;                     // top of stack + 1
        void **                _highwater;                  // largest stack required
      
      public:
      
        //
        // Constructor
        //
        ScanStack()
        : _address(NULL)
        , _end(NULL)
        , _cursor(NULL)
        , _highwater(NULL)
        {}
        
        
        //
        // set_range
        //
        // Set the stack base address and end.
        //
        inline void set_range(Range range) {
            set_range(range.address(), range.end());
        }
        inline void set_range(void *address, void *end) {
            _address = (void **)address;
            _end = (void **)end;
            _cursor = (void **)address;
            _highwater = (void **)address;
        }
        inline void set_range(void *address, usword_t size) {
            set_range(address, displace(address, size));
        }
        
        
        //
        // reset
        //
        // Resets the stack back to initial state.
        //
        void reset() {
            _cursor = _address;
            _highwater = _address;
        }
        

        //
        // is_allocated
        //
        // Returns true if a stack has been allocated.
        //
        inline bool is_allocated() const {
            return _address != NULL;
        }
        

        //
        // is_empty
        //
        // Returns true if a stack contains no elements.
        //
        inline bool is_empty() const {
            return _cursor == _address;
        }
        

        //
        // is_overflow
        //
        // Returns true if a stack overflow condition has occurred.
        //
        inline bool is_overflow() const {
            return _cursor == _end;
        }
        
        

        //
        // push
        //
        // Push the block onto the scan stack.
        //
        inline void push(void *block) {
            // if overflow then lock at overflow
            if (!is_overflow()) {
                *_cursor++ = block;
                if (_highwater < _cursor) _highwater = _cursor;
            }
        }
        
        
        //
        // top
        //
        // Return the top block in the stack.
        //
        inline void *top() {
            // If non empty return TOS (if overflow then lock at overflow)
            if (!is_empty() && !is_overflow()) return _cursor[-1];
            return NULL;
        }
        
        
        //
        // pop
        //
        // Return the next block to scan.
        //
        inline void *pop() {
            // If non empty return TOS (if overflow then lock at overflow)
            if (!is_empty() && !is_overflow()) return *--_cursor;
            return NULL;
        }
    };

   

    //----- Zone -----//
    
    enum State {
        idle, scanning, enlivening, finalizing, reclaiming
    };

    class Zone : public azone_t {
    friend class Monitor;
    friend class MemoryScanner;
   
      private:
      
        //
        // Shared information
        //
        // watch out for static initialization
        static bool           _is_auto_initialized;         // initialization flag
        static Zone           *_last_created;               // last zone created
        
        //
        // system management
        //
        pthread_key_t          _registered_thread_key;      // key used to access tsd for a thread
        Thread                *_registered_threads;         // linked list of registered threads
        spin_lock_t            _registered_threads_lock;    // protects _registered_threads.
        
        pthread_key_t          _thread_finalizing_key;      // key used to mark a thread currrently involved in finalization.
        
        //
        // memory management
        //
        Bitmap                 _in_subzone;                 // indicates which allocations are used for subzone region
        Bitmap                 _in_large;                   // indicates which allocations are used for large blocks
        Large                 *_large_list;                 // doubly linked list of large allocations
        spin_lock_t            _large_lock;                 // protects _large_list
        PtrHashSet             _roots;                      // hash set of registered roots (globals)
        spin_lock_t            _roots_lock;                 // protects _roots
        PtrHashSet             _zombies;                    // hash set of zombies
        spin_lock_t            _zombies_lock;               // protects _zombies
        Region                *_region_list;                // singly linked list of subzone regions
        spin_lock_t            _region_lock;                // protects _region_list
        PtrIntHashMap          _retains;                    // STL hash map of retain counts
        spin_lock_t            _retains_lock;               // protects _retains
        bool                   _is_partial;                 // true if partial collection
        bool                   _repair_write_barrier;       // true if write barrier needs to be repaired after full collection.
        bool                   _use_pending;                // use pending bits instead of scan stack
        ScanStack              _scan_stack;                 // stack used suring scanning
        bool                   _some_pending;               // indicates some blocks are pending scanning
        Range                  _coverage;                   // range of managed memory
        spin_lock_t            _coverage_lock;              // protects _coverage
        volatile bool          _needs_enlivening;           // true if scanning; blocks need to be added to the envivening queue. 
        PointerList            _enlivening_queue;           // vm_map allocated pages to keep track of objects to scan at end.
        spin_lock_t            _enlivening_lock;            // protects _enlivening_queue
        Statistics             _stats;                      // statistics for this zone
        uint32_t               _bytes_allocated;            // byte allocation counter (reset after each collection).
        Monitor                *_monitor;                   // external debugging monitor
        PointerList            _garbage_list;               // vm_map allocated pages to hold the garbage list.
        PtrAssocHashMap        _associations;               // associative references object -> PtrPtrHashMap.
        spin_lock_t            _associations_lock;          // protects _associations
        bool                   _scanning_associations;      // tells 
        volatile enum State    _state;                      // the state of the collector
        
#if UseArena
        void                    *_arena;                    // the actual 32G space (region low, larges high)
        void                    *_large_start;              // half-way into arena + size of bitmaps needed for region
        Bitmap                  _large_bits;                // bitmap of top half - tracks quanta used for large blocks
        spin_lock_t             _large_bits_lock;           // protects _large_bits
#endif
        
        //
        // thread safe Large deallocation routines.
        //
        void (Zone::*_deallocate_large) (void *block);
        void deallocate_large_normal(void *block);
        void deallocate_large_collecting(void *block);
        
        //
        // allocate_region
        //
        // Allocate and initialize a new subzone region.
        //
        Region *allocate_region();
        
        
        //
        // allocate_large
        //
        // Allocates a large block from the universal pool (directly from vm_memory.)
        //
        void *allocate_large(const size_t size, const unsigned layout, bool clear, bool refcount_is_one);
    
    
        //
        // deallocate_large
        //
        // Release memory allocated for a large block
        //
        void deallocate_large(void *block) {
            SpinLock lock(&_large_lock);
            (this->*_deallocate_large)(block);
        }
        
        //
        // find_large
        //
        // Find a large block in this zone.
        //
        inline Large *find_large(void *block) { return Large::large(block); }


        //
        // allocate_small_medium
        //
        // Allocate a block of memory from a subzone,
        //
        void *allocate_small_medium(const size_t size, const unsigned layout, bool clear, bool refcount_is_one);
        
        
        //
        // deallocate_small_medium
        //
        // Release memory allocated for a small block
        //
        void deallocate_small_medium(void *block);
        

      public:
      
        //
        // raw memory allocation
        //

#if UseArena
        
        // set our one region up
        void *arena_allocate_region(usword_t newsize);
#endif

        // on 32-bit w/o arena, goes directly to vm system
        // w/arena, allocate from the top of the arena
        void *arena_allocate_large(usword_t size);
        
        //
        // raw memory deallocation
        //
        void arena_deallocate(void *, size_t size);
        
        //
        // admin_offset
        //
        // Return the number of bytes to the beginning of the first admin data item.
        //
        static inline const usword_t admin_offset() { return align(sizeof(Zone), page_size); }
        

        //
        // bytes_needed
        // 
        // Calculate the number of bytes needed for zone data
        //
        static inline const usword_t bytes_needed() {
            usword_t in_subzone_size = Bitmap::bytes_needed(subzone_quantum_max);
            usword_t in_large_size = Bitmap::bytes_needed(allocate_quantum_large_max);
#if UseArena
            usword_t arena_size = Bitmap::bytes_needed(allocate_quantum_large_max);
#else
            usword_t arena_size = 0;
#endif
            return admin_offset() + in_subzone_size + in_large_size + arena_size;
        }


        //
        // allocator
        //
        inline void *operator new(const size_t size) {
            // allocate zone data
            void *allocation_address = allocate_guarded_memory(bytes_needed());
        
            if (!allocation_address) error("Can not allocate zone");
            
            return allocation_address;

        }


        //
        // deallocator
        //
        inline void operator delete(void *zone) {
            // release zone data
            if (zone) deallocate_guarded_memory(zone, bytes_needed());
        }
       
      
        //
        // setup_shared
        //
        // Initialize information used by all zones.
        //
        static void setup_shared();
        
        
        //
        // Constructors
        //
        Zone();
        
        
        //
        // Destructor
        //
        ~Zone();
        

        //
        // zone
        //
        // Returns the last zone created - for debugging purposes only (no locks.)
        //
        static inline Zone *zone() { return _last_created; }


        //
        // Accessors
        //
        inline Thread         *threads()                    { return _registered_threads; }
        inline spin_lock_t    *threads_lock()               { return &_registered_threads_lock; }
        inline Region         *region_list()                { return _region_list; }
        inline Large          *large_list()                 { return _large_list; }
        inline spin_lock_t    *large_lock()                 { return &_large_lock; }
        inline pthread_key_t  registered_thread_key() const { return _registered_thread_key; }
        inline Statistics     &statistics()                 { return _stats; }
        inline Range          &coverage()                   { return _coverage; }
        inline Monitor        *monitor()                    { return _monitor; }
        inline void           set_monitor(Monitor *monitor) { _monitor = monitor; }
        inline PointerList    &garbage_list()               { return _garbage_list; }
        inline bool volatile  *needs_enlivening()           { return &_needs_enlivening; }
        inline spin_lock_t    *enlivening_lock()            { return &_enlivening_lock; }
        inline PointerList    &enlivening_queue()           { return _enlivening_queue; }
        inline ScanStack      &scan_stack()                 { return _scan_stack; }
        inline void           set_state(enum State ns)      { _state = ns; }
        inline bool           is_state(enum State ns)       { return _state == ns; }
        
        inline spin_lock_t   *associations_lock()           { return &_associations_lock; }

#if UseArena
        inline void *         arena()                       { return _arena; }
#else
        inline void *         arena()                       { return (void *)0; }
#endif
                
        inline uint32_t       bytes_allocated() const       { return _bytes_allocated; }
        inline void           clear_bytes_allocated()       { _bytes_allocated = 0; }
        inline void           add_allocated_bytes(usword_t n)   { _bytes_allocated += n; }
        
        //
        // subzone_index
        //
        // Returns a subzone index for an arbitrary pointer.  Note that this is relative to absolute memory.  subzone_index in
        // Region is relative memory. 
        //
        static inline const usword_t subzone_index(void *address) { return (((usword_t)address & mask(arena_size_log2)) >> subzone_quantum_log2); }
        
        
        //
        // subzone_count
        //
        // Returns a number of subzone quantum for a given size.
        //
        static inline const usword_t subzone_count(const size_t size) { return partition2(size, subzone_quantum_log2); }


        //
        // activate_subzone
        //
        // Marks the subzone as being active.
        //
        inline void activate_subzone(Subzone *subzone) { _in_subzone.set_bit_atomic(subzone_index(subzone)); }
        
        
        //
        // address_in_arena
        //
        // Given arbitrary address, is it in the arena of GC allocated memory
        //
        inline bool address_in_arena(void *address) const {
#if UseArena
            //return (((usword_t)address) >> arena_size_log2) == (((usword_t)_arena) >> arena_size_log2);
            return ((usword_t)address & ~mask(arena_size_log2)) == (usword_t)_arena;
#else
            return true;
#endif
        }
        
        
        //
        // in_subzone_memory
        //
        // Returns true if address is in auto managed memory.
        //
        inline const bool in_subzone_memory(void *address) const { return address_in_arena(address) && (bool)_in_subzone.bit(subzone_index(address)); }
        
        
        //
        // in_large_memory
        //
        // Returns true if address is in auto managed memory.  Since side data is smaller than a large quantum we'll not
        // concern ourselves with rounding.
        //
        inline const bool in_large_memory(void *address) const { return address_in_arena(address) && (bool)_in_large.bit(Large::quantum_index(address)); }
        
        
        //
        // in_zone_memory
        //
        // Returns true in address is in auto managed memory.
        //
        inline const bool in_zone_memory(void *address) const { return in_subzone_memory(address) || in_large_memory(address); }
        
        
        //
        // good_block_size
        //
        // Return a block size which maximizes memory usage (no slop.)
        //
        static inline const usword_t good_block_size(usword_t size) {
            if (size <= allocate_quantum_large)  return align2(size, allocate_quantum_medium_log2);
            return align2(size, allocate_quantum_small_log2);
        }
        
        
        //
        // is_block
        //
        // Determines if the specfied address is a block in this zone.
        //
        inline bool is_block(void *address) {
            return _coverage.in_range(address) && block_is_start(address);
        }
        
        
        //
        // block_allocate
        //
        // Allocate a block of memory from the zone.  layout indicates whether the block is an
        // object or not and whether it is scanned or not.
        //
        void *block_allocate(const size_t size, const unsigned layout, const bool clear, bool refcount_is_one);

        
        //
        // block_deallocate
        //
        // Release a block of memory from the zone, lazily while scanning.
        // 
        void block_deallocate(void *block);

        //
        // block_deallocate_internal
        //
        // Release a block memory from the zone. Only to be called by the collector itself.
        //
        void block_deallocate_internal(void *block);
         

        //
        // block_is_start
        //
        // Return if the arbitrary address is the start of a block.
        // Broken down because of high frequency of use.
        //
        inline bool block_is_start(void *address) {
            if (in_subzone_memory(address)) {
                return Subzone::subzone(address)->is_start(address);
            } else if (in_large_memory(address)) {
                return Large::is_start(address);
            }
            
            return false;
        }
        

        //
        // block_start_large
        // 
        // Return the start of a large block.
        //
        void *block_start_large(void *address);
        
        
        //
        // block_start
        //
        // Return the base block address of an arbitrary address.
        // Broken down because of high frequency of use.
        //
        void *block_start(void *address);


        //
        // block_size
        //
        // Return the size of a specified block.
        //
        usword_t block_size(void *block);


        //
        // block_layout
        //
        // Return the layout of a block.
        //
        int block_layout(void *block);


        //
        // block_set_layout
        //
        // Set the layout of a block.
        //
        void block_set_layout(void *block, int layout);


      private:
        //
        // get_refcount_small_medium
        //
        // Return the refcount of a small/medium block.
        //
        int get_refcount_small_medium(Subzone *subzone, void *block);
            
        
        //
        // inc_refcount_small_medium
        //
        // Increments the refcount of a small/medium block, returning the new value.
        // Requires subzone->admin()->lock() to be held, to protect side data.
        //
        int inc_refcount_small_medium(Subzone *subzone, void *block);
        
        
        //
        // dec_refcount_small_medium
        //
        // Decrements the refcount of a small/medium block, returning the new value.
        // Requires subzone->admin()->lock() to be held, to protect side data.
        //
        int dec_refcount_small_medium(Subzone *subzone, void *block);
        
        //
        // close_locks
        //
        // acquires all locks for critical sections whose behavior changes during scanning
        // enlivening_lock is and must already be held; all other critical sections must
        // order their locks with enlivening_lock acquired first
        //
        inline void close_locks() {
                // acquire all locks for sections that have predicated enlivening work
            // (These locks are in an arbitary order)

            spin_lock(&_region_lock);
            for (Region *region = _region_list; region != NULL; region = region->next()) {
                region->lock();
            }
            spin_lock(&_large_lock);            // large allocations
            
            // Eventually we'll acquire these as well
            //spin_lock(&_retains_lock);          // retain/release
            //spin_lock(&weak_refs_table_lock);   // weak references
            //spin_lock(&_associations_lock);     // associative references
            //spin_lock(&_roots_lock);            // global roots
            
            spin_lock(&_enlivening_lock);
        }
        
        inline void open_locks() {
            spin_unlock(&_enlivening_lock);
            
            //spin_unlock(&_roots_lock);
            //spin_unlock(&_associations_lock);
            //spin_unlock(&weak_refs_table_lock);
            //spin_unlock(&_retains_lock);
            spin_unlock(&_large_lock);
            for (Region *region = _region_list; region != NULL; region = region->next()) {
                region->unlock();
            }
            spin_unlock(&_region_lock);
         }
        
      public:
        //
        // block_refcount
        //
        // Returns the reference count of the specified block.
        //
        int block_refcount(void *block); 


        //
        // block_increment_refcount
        //
        // Increment the reference count of the specified block.
        //
        int block_increment_refcount(void *block); 


        //
        // block_decrement_refcount
        //
        // Decrement the reference count of the specified block.
        //
        int block_decrement_refcount(void *block);
        
        //
        // block_refcount_and_layout
        //
        // Accesses the reference count and layout of the specified block.
        //
        void block_refcount_and_layout(void *block, int *refcount, int *layout);
        
        //
        // block_is_new
        //
        // Returns true if the block was recently created.
        //
        inline bool block_is_new(void *block) {
            if (in_subzone_memory(block)) {
                return Subzone::subzone(block)->is_new(block);
            } else if (in_large_memory(block)) {
                return Large::is_new(block);
            }
            return false;
        }
        
        
        //
        // block_is_garbage
        //
        // Returns true if the specified block is flagged as garbage.  Only valid 
        // during finalization.
        //
        inline bool block_is_garbage(void *block) {
            if (in_subzone_memory(block)) {
                Subzone *subzone = Subzone::subzone(block);
                return !subzone->is_marked(block) && !subzone->is_newest(block);
            } else if (in_large_memory(block)) {
                Large *large = Large::large(block);
                return !large->is_marked() && !large->is_newest();
            }

            return false;
        }
        
        //
        // block_is_marked
        //
        // if the block is already marked, say so.
        // Blocks are marked only by the collector thread in an unsynchronized way, so this may return false
        // if the collector's memmory write hasn't propogated to the caller's cpu.
        inline bool block_is_marked(void *block) {
            if (in_subzone_memory(block)) {
                Subzone *subzone = Subzone::subzone(block);
                return subzone->is_marked(block);
            } else if (in_large_memory(block)) {
                Large *large = Large::large(block);
                return large->is_marked();
            }
            return false;
        }


        //
        // set_associative_ref
        //
        // Creates an association between a given block, a unique pointer-sized key, and a pointer value.
        //
        void set_associative_ref(void *block, void *key, void *value) {
            if (value) {
                // Creating associations must enliven objects that may become garbage otherwise.
                UnconditionalBarrier barrier(&_needs_enlivening, &_enlivening_lock);
                SpinLock lock(&_associations_lock);
                _associations[block][key] = value;
                if (barrier) _enlivening_queue.add(value);
            } else {
                // setting the association to NULL breaks the association.
                SpinLock lock(&_associations_lock);
                PtrPtrHashMap &refs = _associations[block];
                PtrPtrHashMap::iterator i = refs.find(key);
                if (i != refs.end()) refs.erase(i);
            }
        }
        
        //
        // get_associative_ref
        //
        // Returns the associated pointer value for a given block and key.
        //
        void *get_associative_ref(void *block, void *key) {
            SpinLock lock(&_associations_lock);
            PtrAssocHashMap::iterator i = _associations.find(block);
            if (i != _associations.end()) {
                PtrPtrHashMap &refs = i->second;
                PtrPtrHashMap::iterator j = refs.find(key);
                if (j != refs.end()) return j->second;
            }
            return NULL;
        }
        
        // called by memory scanners to enliven associative references.
        void scan_associations(MemoryScanner &scanner);

        void pend_associations(void *block) {
            PtrAssocHashMap::iterator i = _associations.find(block);
            if (i != _associations.end()) {
                PtrPtrHashMap &refs = i->second;
                for (PtrPtrHashMap::iterator j = refs.begin(); j != refs.end(); j++) {
                    set_pending(j->second);
                }
            }
        }
        
        //
        // erase_assocations
        //
        // Removes all associations for a given block. Used to
        // clear associations for explicitly deallocated blocks.
        // When the collector frees blocks, it uses a different code
        // path, to minimize locking overhead. See free_garbage().
        //
        void erase_associations(void *block) {
            SpinLock lock(&_associations_lock);
            if (_associations.size() == 0) return;
            PtrAssocHashMap::iterator iter = _associations.find(block);
            if (iter != _associations.end()) _associations.erase(iter);
        }
        
        //
        // add_root
        //
        // Adds the address as a known root.
        //
        inline void add_root(void *root, void *value) {
            UnconditionalBarrier barrier(&_needs_enlivening, &_enlivening_lock);
            SpinLock lock(&_roots_lock);
            if (_roots.find(root) == _roots.end()) {
                _roots.insert(root);
            }
            // whether new or old, make sure it gets scanned
            // if new, well, that's obvious, but if old the scanner may already have scanned
            // this root and we'll never see this value otherwise
            if (barrier && !block_is_marked(value)) _enlivening_queue.add(value);
            *(void **)root = value;
        }
        
        
        //
        // add_root_no_barrier
        //
        // Adds the address as a known root.
        //
        inline void add_root_no_barrier(void *root) {
            SpinLock lock(&_roots_lock);
            if (_roots.find(root) == _roots.end()) {
                _roots.insert(root);
            }
        }
        
        
        // copy_roots
        //
        // Takes a snapshot of the registered roots during scanning.
        //
        inline void copy_roots(PointerList &list) {
            SpinLock lock(&_roots_lock);
            usword_t count = _roots.size();
            list.clear_count();
            list.grow(count);
            list.set_count(count);
            std::copy(_roots.begin(), _roots.end(), (void**)list.buffer());
        }
        

        // remove_root
        //
        // Removes the address from the known roots.
        //
        inline void remove_root(void *root) {
            SpinLock lock(&_roots_lock);
            PtrHashSet::iterator iter = _roots.find(root);
            if (iter != _roots.end()) {
                _roots.erase(iter);
            }
        }
        
        
        //
        // is_root
        //
        // Returns whether or not the address range is a known root range.
        //
        inline bool is_root(void *address) {
            SpinLock lock(&_roots_lock);
            PtrHashSet::iterator iter = _roots.find(address);
            return (iter != _roots.end());
        }
        

        //
        // add_zombie
        //
        // Adds address to the zombie set.
        //
        inline void add_zombie(void *address) {
            SpinLock lock(&_zombies_lock);
            if (_zombies.find(address) == _zombies.end()) {
                _zombies.insert(address);
            }
        }

        
        //
        // is_zombie
        //
        // Returns whether or not the address is in the zombie set.
        //
        inline bool is_zombie(void *address) {
            SpinLock lock(&_zombies_lock);
            PtrHashSet::iterator iter = _zombies.find(address);
            return (iter != _zombies.end());
        }
        
        //
        // clear_zombies
        //
        inline void clear_zombies() {
            SpinLock lock(&_zombies_lock);
            _zombies.clear();
        }
        

        //
        // set_pending
        //
        // Sets a block as pending during scanning.
        //
        bool set_pending(void *block);
        
        
        //
        // repend
        //
        // Force a block to be rescanned.
        //
        void repend(void *address);
        
        
        //
        // set_write_barrier
        //
        // Set the write barrier byte corresponding to the specified address.
        // If scanning is going on then the value is marked pending.
        // If address is within an allocated block the value is set there and will return true.
        //
        bool set_write_barrier(void *address, void *value);
        
        
        //
        // set_write_barrier_range
        //
        // Set the write barrier bytes corresponding to the specified address & length.
        // Returns if the address is within an allocated block (and barrier set)
        //
        bool set_write_barrier_range(void *address, const usword_t size);
        
        
        //
        // set_write_barrier
        //
        // Set the write barrier byte corresponding to the specified address.
        // Returns if the address is within an allocated block (and barrier set)
        //
        bool set_write_barrier(void *address);
        
        
        //
        // write_barrier_scan_unmarked_content
        //
        // Scan ranges in block that are marked in the write barrier.
        //
        void write_barrier_scan_unmarked_content(void *block, const usword_t size, MemoryScanner &scanner);


        //
        // mark_write_barriers_untouched
        //
        // iterate through all the write barriers and mark the live cards as provisionally untouched.
        //
        void mark_write_barriers_untouched();


        //
        // clear_untouched_write_barriers
        //
        // iterate through all the write barriers and clear all the cards still marked as untouched.
        //
        void clear_untouched_write_barriers();


        //
        // clear_all_write_barriers
        //
        // iterate through all the write barriers and clear all the marks.
        //
        void clear_all_write_barriers();


        //
        // reset_all_marks
        //
        // Clears the mark flags on all blocks
        //
        void reset_all_marks();
       
        
        //
        // reset_all_marks_and_pending
        //
        // Clears the mark and ending flags on all blocks
        //
        void reset_all_marks_and_pending();
       
        
        //
        // statistics
        //
        // Returns the statistics for this zone.
        //
        void statistics(Statistics &stats);
        
        
        //
        //
        // scan_stack_push_block
        //
        // Push the block onto the scan stack.
        //
        void scan_stack_push_block(void *block) {
            _scan_stack.push(block);
        }


        //
        // scan_stack_push_range
        //
        // Push the range onto the scan stack.
        //
        void scan_stack_push_range(Range &range) {
            _scan_stack.push(range.end());
            _scan_stack.push(displace(range.address(), 1));
        }
        
        
        //
        // scan_stack_is_empty
        //
        // Returns true if the stack is empty.
        //
        bool scan_stack_is_empty() { return _scan_stack.is_empty() || _scan_stack.is_overflow(); }
        
        
        //
        // scan_stack_is_range
        //
        // Returns true if the top of the scan stack is a range.
        //
        bool scan_stack_is_range() {
            void *block = _scan_stack.top();
            return !is_bit_aligned(block, 1);
        }
        
        
        //
        // scan_stack_pop_block
        //
        // Return the next block to scan.
        //
        void *scan_stack_pop_block() {
            return _scan_stack.pop();
        }


        //
        // scan_stack_pop_range
        //
        // Return the next block to scan.
        //
        Range scan_stack_pop_range() {
            void *block1 = _scan_stack.pop();
            void *block2 = _scan_stack.pop();
            return Range(displace(block1, -1), block2);
        }

        inline bool repair_write_barrier() const { return _repair_write_barrier; }

        //
        // Accessors for _some_pending.
        //
        inline bool is_some_pending     () const { return _some_pending; }
        inline void set_some_pending    ()       { _some_pending = true; }
        inline void clear_some_pending  ()       { _some_pending = false; }
        
        
        //
        // Accessors for _use_pending.
        //
        inline bool use_pending         () const { return _use_pending; }
        inline void set_use_pending     ()       { _use_pending = true; }
        inline void clear_use_pending   ()       { _use_pending = false; }
        
        
        //
        // set_needs_enlivening
        //
        // Informs the write-barrier that blocks need repending.
        //
        inline void set_needs_enlivening() {
            close_locks();
            _needs_enlivening = true;
            open_locks();
        }
        
        //
        // clear_needs_enlivening
        //
        // Write barriers no longer need to repend blocks.
        //
        inline void  clear_needs_enlivening() {
            _needs_enlivening = false;
        }
        
        
        //
        // collect_begin
        //
        // Indicate the beginning of the collection period.  New blocks allocated during the time will
        // automatically marked and not treated as garbage.
        //
        inline void  collect_begin(bool is_partial) {
            SpinLock lock(&_large_lock);
            _deallocate_large = &Zone::deallocate_large_collecting;
            _is_partial = is_partial;
        }
        
        
        //
        // collect_end
        //
        // Indicate the end of the collection period.  New blocks will no longer be marked automatically and will
        // be collected normally.
        //
        inline void  collect_end() {
            reset_all_marks();
        
            _is_partial = false;
            
            // deallocate all Large blocks marked for deletion during collection.
            SpinLock lock(&_large_lock);
            Large *large = _large_list;
            while (large != NULL) {
                Large *next = large->next();
                if (large->is_freed()) deallocate_large_normal(large->address());
                large = next;
            }
            _deallocate_large = &Zone::deallocate_large_normal;
            
            _garbage_list.uncommit();
        }
        
        //
        // block_collector
        //
        // Called by the monitor to prevent collections. Also suspends all registered threads
        // to avoid potential heap inconsistency.
        //
        void block_collector();
        
        //
        // unblock_collector
        //
        // Called by the monitor to enable collections.  Also resumes all registered threads.
        //
        void unblock_collector();
        
        //
        // collect
        //
        // Performs the collection process.
        //
        void collect(bool is_partial, void *current_stack_bottom, auto_date_t *scan_end);
        
        
        //
        // scavenge_blocks
        //
        // Constructs a list of all blocks that are to be garbaged
        //
        void scavenge_blocks();
        
        
        //
        // release_pages
        //
        // Release any pages that are not in use.
        //
        void release_pages() {
        }
        
        
        //
        // register_thread
        //
        // Add the current thread as a thread to be scanned during gc.
        //
        void register_thread();


        //
        // unregister_thread
        //
        // Remove the current thread as a thread to be scanned during gc.
        //
        void unregister_thread();
        

        //
        // suspend_all_registered_threads
        //
        // Suspend all registered threads. Only used by the monitor for heap snapshots.
        // Acquires _registered_threads_lock.
        //
        void suspend_all_registered_threads();


        //
        // resume_all_registered_threads
        //
        // Resumes all suspended registered threads.  Only used by the monitor for heap snapshots.
        //
        void resume_all_registered_threads();
        
        //
        // set_thread_finalizing
        //
        // Marks a thread as currently finalizing. 
        //
        void set_thread_finalizing(bool is_finalizing) {
            pthread_setspecific(_thread_finalizing_key, (void*)is_finalizing);
        }
        
        //
        // is_thread_finalizing
        //
        // Tests whether the current thread is finalizing.
        //
        bool is_thread_finalizing() { return is_state(finalizing) && (bool)pthread_getspecific(_thread_finalizing_key); }
        
        //
        // weak references.
        //
        unsigned has_weak_references() { return (num_weak_refs != 0); }

        //
        // layout_map_for_block.
        //
        // Used for precise (non-conservative) block scanning.
        //
        const unsigned char *layout_map_for_block(void *block) {
            // FIXME:  for speed, change this to a hard coded offset from the block's word0 field.
            return control.layout_for_address ? control.layout_for_address((auto_zone_t *)this, block) : NULL;
        }
        
        //
        // weak_layout_map_for_block.
        //
        // Used for conservative block with weak references scanning.
        //
        const unsigned char *weak_layout_map_for_block(void *block) {
            // FIXME:  for speed, change this to a hard coded offset from the block's word0 field.
            return control.weak_layout_for_address ? control.weak_layout_for_address((auto_zone_t *)this, block) : NULL;
        }

        //
        // print_all_blocks
        //
        // Prints all allocated blocks.
        //
        void print_all_blocks();
        
        
        //
        // print block
        //
        // Print the details of a block
        //
        void print_block(void *block);
        void print_block(void *block, const char *tag);
        
        
   };

};


#endif // __AUTO_ZONE_CORE__
