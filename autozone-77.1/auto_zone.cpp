/*
 * Copyright (c) 2002-2008 Apple Inc. All rights reserved.
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

#include "auto_zone.h"
#include "auto_impl_utilities.h"
#include "auto_weak.h"
#include "agc_interface.h"
#include "auto_trace.h"
#include "AutoZone.h"
#include "AutoLock.h"
#include "AutoMonitor.h"
#include "AutoInUseEnumerator.h"

#include <stdlib.h>
#include <libc.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

#define USE_INTERPOSING 0
#define LOG_TIMINGS 0

#if USE_INTERPOSING
#include <mach-o/dyld-interposing.h>
#endif

/*********  Globals     ************/

// record stack traces when refcounts change?
static bool AUTO_RECORD_REFCOUNT_STACKS = false;

/*********  Parameters  ************/

#define VM_COPY_THRESHOLD       (40 * 1024)

/*********  Forward references  ************/

static void auto_really_free(Auto::Zone *zone, void *ptr);

#if LOG_TIMINGS

#define LOG_ALLOCATION_THRESHOLD 64*1024

static void log_allocation_threshold(auto_date_t time, size_t allocated, size_t finger);
static void log_collection_begin(auto_date_t time, size_t allocated, size_t finger, bool isFull);
static void log_collection_end(auto_date_t time, size_t allocated, size_t finger, size_t recovered);

#endif LOG_TIMINGS

/*********  Allocation Meter ************/

#if defined(AUTO_ALLOCATION_METER)

static bool allocate_meter_inited = false;
static bool allocate_meter = false;
static double allocate_meter_interval = 1.0;
static double allocate_meter_start_time = 0.0;
static double allocate_meter_report_time = 0.0;
static double allocate_meter_count = 0;
static double allocate_meter_total_time = 0.0;

static double nano_time() {
    static mach_timebase_info_data_t timebase_info;
    static double scale = 1.0;
    static unsigned long long delta; 
    if (!timebase_info.denom) {
        mach_timebase_info(&timebase_info);
        scale = ((double)timebase_info.numer / (double)timebase_info.denom) * 1.0E-9;
        delta = mach_absolute_time();
    }
    return (double)(mach_absolute_time() - delta) * scale;
}

static void allocate_meter_init() {
  if (!allocate_meter_inited) {
    const char *env_str = getenv("AUTO_ALLOCATION_METER");
    allocate_meter = env_str != NULL;
    allocate_meter_interval = allocate_meter ? atof(env_str) : 1.0;
    if (allocate_meter_interval <= 0.0) allocate_meter_interval = 1.0;
    allocate_meter_inited = true;
  }
}

static unsigned long long allocate_meter_average() {
  double daverage = allocate_meter_total_time / allocate_meter_count;
  unsigned long long iaverage = (unsigned long long)(daverage * 1000000000.0);
  allocate_meter_count = 1;
  allocate_meter_total_time = daverage;
  return iaverage;
}

static void allocate_meter_start() {
  allocate_meter_start_time = nano_time();
  if (allocate_meter_count == 0.0)
    allocate_meter_report_time = allocate_meter_start_time + allocate_meter_interval;
}

static void allocate_meter_stop() {
  double stoptime = nano_time();
  allocate_meter_count++;
  allocate_meter_total_time += stoptime - allocate_meter_start_time;
  if (stoptime > allocate_meter_report_time) {
    malloc_printf("%u nanosecs/alloc\n", (unsigned)allocate_meter_average());
    allocate_meter_report_time = stoptime + allocate_meter_interval;
  }
}

#endif

/*********  Zone callbacks  ************/

struct auto_zone_cursor {
    auto_zone_t *zone;
    size_t garbage_count;
    const vm_address_t *garbage;
    volatile unsigned index;
    size_t block_count;
    size_t byte_count;
};

#if DEBUG
extern void* WatchPoint;
#endif

static void foreach_block_do(auto_zone_cursor_t cursor, void (*op) (void *ptr, void *data), void *data) {
    Auto::Zone *azone = (Auto::Zone *)cursor->zone;
    azone->set_thread_finalizing(true);
    while (cursor->index < cursor->garbage_count) {
        void *ptr = (void *)cursor->garbage[cursor->index++];
        auto_memory_type_t type = auto_zone_get_layout_type((auto_zone_t *)azone, ptr);
        if (type & AUTO_OBJECT) {
#if DEBUG
            if (ptr == WatchPoint) {
                malloc_printf("auto_zone invalidating watchpoint: %p\n", WatchPoint);
            }
#endif
            op(ptr, data);
            cursor->block_count++;
            cursor->byte_count += azone->block_size(ptr);
        }
    }
    azone->set_thread_finalizing(false);
}


static void invalidate_garbage(Auto::Zone *azone, boolean_t generational, const size_t garbage_count, const vm_address_t *garbage, void *collection_context) {
    // begin AUTO_TRACE_FINALIZING_PHASE
    auto_trace_phase_begin((auto_zone_t*)azone, generational, AUTO_TRACE_FINALIZING_PHASE);
    
#if DEBUG
    // when debugging, sanity check the garbage list in various ways.
    for (size_t index = 0; index < garbage_count; index++) {
        void *ptr = (void *)garbage[index];
        int rc = azone->block_refcount(ptr);
        if (rc > 0)
            malloc_printf("invalidate_garbage: garbage ptr = %p, has non-zero refcount = %d\n", ptr, rc);
    }
#endif
    
    struct auto_zone_cursor cursor = { (auto_zone_t *)azone, garbage_count, garbage, 0, 0, 0 };
    if (azone->control.batch_invalidate) {
        azone->control.batch_invalidate((auto_zone_t *)azone, foreach_block_do, &cursor, sizeof(cursor));
    }    
    // end AUTO_TRACE_FINALIZING_PHASE
    auto_trace_phase_end((auto_zone_t*)azone, generational, AUTO_TRACE_FINALIZING_PHASE, cursor.block_count, cursor.byte_count);
}

static inline void zombify(Auto::Zone *azone, void *ptr) {
    // callback to morph the object into a zombie.
    if (azone->control.resurrect) azone->control.resurrect((auto_zone_t*)azone, ptr);
    azone->block_set_layout(ptr, AUTO_OBJECT_UNSCANNED);
}

static unsigned free_garbage(Auto::Zone *zone, boolean_t generational, const size_t garbage_count, vm_address_t *garbage) {
    size_t index;
    size_t blocks_freed = 0, bytes_freed = 0;

    auto_trace_phase_begin((auto_zone_t*)zone, generational, AUTO_TRACE_SCAVENGING_PHASE);

    // NOTE:  Zone::block_deallocate_internal() now breaks associative references assuming the associations_lock has been aquired.
    using namespace Auto;
    SpinLock lock(zone->associations_lock());
    
    for (index = 0; index < garbage_count; index++) {
        void *ptr = (void *)garbage[index];
        int rc = zone->block_refcount(ptr);
        if (rc == 0) {
            if ((zone->block_layout(ptr) & AUTO_OBJECT) && zone->control.weak_layout_for_address) {
                const unsigned char* weak_layout = zone->control.weak_layout_for_address((auto_zone_t*)zone, ptr);
                if (weak_layout) weak_unregister_with_layout(zone, (void**)ptr, weak_layout);
            }
            blocks_freed++;
            bytes_freed += zone->block_size(ptr);
            if (malloc_logger) malloc_logger(MALLOC_LOG_TYPE_DEALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE, uintptr_t(zone), uintptr_t(ptr), 0, 0, 0);
            zone->block_deallocate_internal(ptr);
        } else if (zone->is_zombie(ptr)) {
            zombify(zone, ptr);
            zone->block_decrement_refcount(ptr);
        } else {
            malloc_printf("free_garbage: garbage ptr = %p, has non-zero refcount = %d\n", ptr, rc);
        }
    }

    auto_trace_phase_end((auto_zone_t*)zone, generational, AUTO_TRACE_SCAVENGING_PHASE, blocks_freed, bytes_freed);
    
    return bytes_freed;
}

boolean_t auto_zone_is_finalized(auto_zone_t *zone, const void *ptr) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    // detects if the specified pointer is about to become garbage
    // only check if azone->collection_garbage is set
    return (ptr && azone->is_thread_finalizing() && azone->block_is_garbage((void *)ptr));
}

static void auto_collect_internal(Auto::Zone *zone, boolean_t generational) {
    size_t garbage_count;
    vm_address_t *garbage;

    // Avoid simultaneous collections.
    if (!OSAtomicCompareAndSwap32(0, 1, &zone->collector_disable_count)) return;

    using namespace Auto;
    zone->clear_bytes_allocated(); // clear threshold.  Till now we might back off & miss a needed collection.
     
    auto_date_t start = auto_date_now();
    
    // bound the bottom of the stack.
    vm_address_t stack_bottom = auto_get_sp();
    if (zone->control.disable_generational) generational = false;
    auto_trace_collection_begin((auto_zone_t*)zone, generational);
#if LOG_TIMINGS
    log_collection_begin(start, zone->statistics().size(), zone->statistics().allocated(), generational);
#endif
    zone->set_state(scanning);
    
    zone->collect_begin(generational);

    auto_date_t scan_end;
    zone->collect((bool)generational, (void *)stack_bottom, &scan_end);

    PointerList &list = zone->garbage_list();
    garbage_count = list.count();
    garbage = list.buffer();

    // now we reset the generation bit and the write_barrier_bitmap
    auto_date_t enlivening_end = auto_date_now();
    auto_date_t finalize_end;
    unsigned bytes_freed = 0;

    // note the garbage so the write-barrier can detect resurrection
    zone->set_state(finalizing);
    if (zone->control.batch_invalidate) invalidate_garbage(zone, generational, garbage_count, garbage, NULL);
    zone->set_state(reclaiming);
    finalize_end = auto_date_now();
    bytes_freed = free_garbage(zone, generational, garbage_count, garbage);
    zone->clear_zombies();

    zone->collect_end();
    intptr_t after_in_use = zone->statistics().size();
    intptr_t after_allocated = after_in_use + zone->statistics().unused();
    auto_date_t collect_end = auto_date_now();
    
    Statistics &zone_stats = zone->statistics();
    auto_trace_collection_end((auto_zone_t*)zone, generational, garbage_count, bytes_freed, zone_stats.count(), zone_stats.size());
#if LOG_TIMINGS
    log_collection_end(collect_end, after_in_use, after_allocated, bytes_freed);
#endif

    zone->set_state(idle);
    auto_collector_reenable((auto_zone_t *)zone);

    // update collection part of statistics
    auto_stats_lock(zone);
    auto_statistics_t   *stats = &zone->stats;
    
    int which = generational ? 1 : 0;
    stats->num_collections[which]++;
    stats->bytes_in_use_after_last_collection[which] = after_in_use;
    stats->bytes_allocated_after_last_collection[which] = after_allocated;
    stats->bytes_freed_during_last_collection[which] = bytes_freed;
    stats->last_collection_was_generational = generational;
    
    auto_collection_durations_t *max = &stats->maximum[which];
    auto_collection_durations_t *last = &stats->last[which];
    auto_collection_durations_t *total = &stats->total[which];

    last->scan_duration = scan_end - start;
    last->enlivening_duration = enlivening_end - scan_end;
    last->finalize_duration = finalize_end - enlivening_end;;
    last->reclaim_duration = collect_end - finalize_end;
    last->total_duration = collect_end - start;
    
    // compute max individually (they won't add up, but we'll get max scan & max finalize split out
    if (max->scan_duration < last->scan_duration) max->scan_duration = last->scan_duration;
    if (max->enlivening_duration < last->enlivening_duration) max->enlivening_duration = last->enlivening_duration;
    if (max->finalize_duration < last->finalize_duration) max->finalize_duration = last->finalize_duration;
    if (max->reclaim_duration < last->reclaim_duration) max->reclaim_duration = last->reclaim_duration;
    if (max->total_duration < last->total_duration) max->total_duration = last->total_duration;

    total->scan_duration += last->scan_duration;
    total->enlivening_duration += last->enlivening_duration;
    total->finalize_duration += last->finalize_duration;
    total->reclaim_duration += last->reclaim_duration;
    total->total_duration += last->total_duration;

    auto_stats_unlock(zone);
    if (zone->control.log & AUTO_LOG_COLLECTIONS)
        malloc_printf("%s: %s GC collected %u objects (%u bytes) in %d usec "
                      "(%d + %d + %d + %d [scan + freeze + finalize + reclaim])\n", 
                      auto_prelude(), (generational ? "gen." : "full"),
                      garbage_count, bytes_freed,
                      (int)(collect_end - start), // total
                      (int)(scan_end - start), 
                      (int)(enlivening_end - scan_end), 
                      (int)(finalize_end - enlivening_end), 
                      (int)(collect_end - finalize_end));
}

extern "C" void auto_zone_stats(void);

static void auto_collect_with_mode(Auto::Zone *zone, auto_collection_mode_t mode) {
    using namespace Auto;
    if (mode & AUTO_COLLECT_IF_NEEDED) {
        if (zone->bytes_allocated() < zone->control.collection_threshold)
            return;
    }
    bool generational = true, exhaustive = false;
    switch (mode & 0x3) {
      case AUTO_COLLECT_RATIO_COLLECTION:
        // enforce the collection ratio to keep the heap from getting too big.
        if (zone->collection_count++ == zone->control.full_vs_gen_frequency) {
            zone->collection_count = 0;
            generational = false;
        }
        break;
      case AUTO_COLLECT_GENERATIONAL_COLLECTION:
        generational = true;
        break;
      case AUTO_COLLECT_FULL_COLLECTION:
        generational = false;
        break;
      case AUTO_COLLECT_EXHAUSTIVE_COLLECTION:
        exhaustive = true;
    }
    if (exhaustive) {
         // run collections until objects are no longer reclaimed.
        Statistics &stats = zone->statistics();
        usword_t count;
        //if (zone->control.log & AUTO_LOG_COLLECTIONS) malloc_printf("beginning exhaustive collections\n");
        do {
            count = stats.count();
            auto_collect_internal(zone, false);
        } while (stats.count() < count);
        //if (zone->control.log & AUTO_LOG_COLLECTIONS) malloc_printf("ending exhaustive collections\n");
    } else {
        auto_collect_internal(zone, generational);
    }
}

static void *auto_collection_thread(void *arg) {
    using namespace Auto;
    Zone *zone = (Zone *)arg;
    if (zone->control.log & AUTO_LOG_COLLECTIONS) auto_zone_stats();
    pthread_mutex_lock(&zone->collection_mutex);
    for (;;) {
        uint32_t mode_flags;
        while ((mode_flags = zone->collection_requested_mode) == 0) {
            // block until explicity requested to collect.
            pthread_cond_wait(&zone->collection_requested, &zone->collection_mutex);
        }

        // inform other threads that collection has started.
        zone->collection_status_state = 1;
        // no clients
        // pthread_cond_broadcast(&zone->collection_status);
        pthread_mutex_unlock(&zone->collection_mutex);

        auto_collect_with_mode(zone, zone->collection_requested_mode);
        
        // inform blocked threads that collection has finished.
        pthread_mutex_lock(&zone->collection_mutex);
        zone->collection_requested_mode = 0;
        zone->collection_status_state = 0;
        pthread_cond_broadcast(&zone->collection_status);
    }
    
    return NULL;
}

void auto_collect(auto_zone_t *zone, auto_collection_mode_t mode, void *collection_context) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;

    if (azone->collector_disable_count) return;
    if (mode & AUTO_COLLECT_IF_NEEDED) {
        if (azone->bytes_allocated() < azone->control.collection_threshold)
            return;
    }
    if (azone->multithreaded) {
        // request a collection by setting the requested flags, and signaling the collector thread.
        pthread_mutex_lock(&azone->collection_mutex);
        if (azone->collection_requested_mode) {
            // request already in progress
        }
        else {
            azone->collection_requested_mode = mode | 0x1000;         // force non-zero value
            // wake up the collector, telling it to begin a collection.
            pthread_cond_signal(&azone->collection_requested);
        }
        if (mode & AUTO_COLLECT_SYNCHRONOUS) {
            // wait for the collector to finish the current collection. wait at most 1 second, to avoid deadlocks.
            const struct timespec one_second = { 1, 0 };
            pthread_cond_timedwait_relative_np(&azone->collection_status, &azone->collection_mutex, &one_second);
        }
        pthread_mutex_unlock(&azone->collection_mutex);
    }
    else {
        auto_collect_with_mode(azone, mode);
    }
}



size_t auto_size_no_lock(Auto::Zone *azone, const void *ptr) {
    return azone->is_block((void *)ptr) ? azone->block_size((void *)ptr) : 0L;
}

static inline size_t auto_size(auto_zone_t *zone, const void *ptr) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    return azone->is_block((void *)ptr) ? azone->block_size((void *)ptr) : 0L;
}

boolean_t auto_zone_is_valid_pointer(auto_zone_t *zone, const void *ptr) {
    Auto::Zone* azone = (Auto::Zone *)zone;
    boolean_t result;
    result = azone->is_block((void *)ptr);
    return result;
}

size_t auto_zone_size(auto_zone_t *zone, const void *ptr) {
    return auto_size(zone, ptr);
}

size_t auto_zone_size_no_lock(auto_zone_t *zone, const void *ptr) {
    return auto_size_no_lock((Auto::Zone *)zone, ptr);
}

const void *auto_zone_base_pointer(auto_zone_t *zone, const void *ptr) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    const void *base = (const void *)azone->block_start((void *)ptr);
    return base;
}

#if DEBUG
void *WatchPoint = (void*)0xFFFFFFFF;
void blainer() {
    sleep(0);
}
#endif

static void *auto_malloc_internal(Auto::Zone *azone, size_t size, auto_memory_type_t type, boolean_t initial_refcount_to_one, boolean_t clear) {
    void    *ptr = NULL;

    ptr = azone->block_allocate(size, type, clear, initial_refcount_to_one);
    //if (azone->control.log & AUTO_LOG_COLLECTIONS) auto_zone_stats();
    if (!ptr) {
        return NULL;
    }
    if (azone->multithreaded) {
        auto_collect((auto_zone_t *)azone, AUTO_COLLECT_IF_NEEDED, NULL);
    }
    if (AUTO_RECORD_REFCOUNT_STACKS) {
        auto_record_refcount_stack(azone, ptr, 0);
    }
#if LOG_TIMINGS
    size_t allocated = azone->statistics().size();
    if ((allocated & ~(LOG_ALLOCATION_THRESHOLD-1)) != ((allocated - size) & ~(LOG_ALLOCATION_THRESHOLD-1)))
        log_allocation_threshold(auto_date_now(), azone->statistics().size(), azone->statistics().allocated());
#endif
    return ptr;
}


static inline void *auto_malloc(auto_zone_t *zone, size_t size) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    void *result = auto_malloc_internal(azone, size, AUTO_MEMORY_UNSCANNED, azone->initial_refcount_to_one, 0);
    return result;
}

static void auto_really_free(Auto::Zone *zone, void *ptr) {
    // updates deltas
    zone->block_deallocate(ptr);
}

static void auto_free(auto_zone_t *azone, void *ptr) {
    if (ptr == NULL) return; // XXX_PCB don't mess with NULL pointers.
    using namespace Auto;
    Zone *zone = (Zone *)azone;
    unsigned    refcount = zone->block_refcount(ptr);
    if (refcount || zone->initial_refcount_to_one) {
        if (refcount != 1)
            malloc_printf("*** free() called with %p with refcount %d\n", ptr, refcount);
    }
    auto_really_free(zone, ptr);
}

static void *auto_calloc(auto_zone_t *zone, size_t size1, size_t size2) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    size1 *= size2;
    void *ptr;
    ptr = auto_malloc_internal(azone, size1, AUTO_MEMORY_UNSCANNED, azone->initial_refcount_to_one, 1);
    return ptr;
}

static void *auto_valloc(auto_zone_t *zone, size_t size) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    void *result = auto_malloc_internal(azone, auto_round_page(size), AUTO_MEMORY_UNSCANNED, azone->initial_refcount_to_one, 1);
    return result;
}

static boolean_t get_type_and_retain_count(Auto::Zone *zone, void *ptr, auto_memory_type_t *type, int *rc) {
    boolean_t is_block = zone->is_block(ptr);
    if (is_block) zone->block_refcount_and_layout(ptr, rc, type);
    return is_block;
}

static void *auto_realloc(auto_zone_t *zone, void *ptr, size_t size) {
    using namespace Auto;
    Zone *azone = (Zone*)zone;
    if (!ptr) return auto_malloc(zone, size);
    size_t old_size = auto_size(zone, ptr);

    // preserve the layout type, and retain count of the realloc'd object.
    auto_memory_type_t type; int rc = 0;
    if (!get_type_and_retain_count(azone, ptr, &type, &rc)) {
        auto_error(azone, "auto_realloc: can't get type or retain count, ptr (%p) from ordinary malloc zone?", ptr);
        // If we're here because someone used the wrong zone we should let them have what they intended.
        return malloc_zone_realloc(malloc_zone_from_ptr(ptr), ptr, size);
    }
    
    // malloc man page says to allocate a "minimum sized" object if size==0
    if (size == 0) size = allocate_quantum_small;
    
    if (old_size > size) {
        size_t delta = old_size - size;
        // When reducing the size check if the reduction would result in a smaller block being used. If not, reuse the same block.
        // We can reuse the same block if any of these are true:
        // 1) original is a small block, reduced by less than small quanta
        // 2) original is a medium block, new size is still medium, and reduced by less than medium quanta
        // 3) original is a large block, new size is still large, and block occupies the same number of pages
        if ((old_size <= allocate_quantum_medium && delta < allocate_quantum_small) ||
            (old_size <= allocate_quantum_large && size >= allocate_quantum_medium && delta < allocate_quantum_medium) ||
            (size > allocate_quantum_large && auto_round_page(old_size) == auto_round_page(size))) {
            // if the block is scanned, resizing smaller should clear the extra space
            if (type == AUTO_MEMORY_SCANNED)
                bzero(displace(ptr,size), old_size-size);
            return ptr;
        }
    }
    
    // We could here optimize realloc by adding a primitive for small blocks to try to grow in place
    // But given that this allocator is intended for objects, this is not necessary
    void *new_ptr = auto_malloc_internal(azone, size, type, (rc != 0), (type & AUTO_UNSCANNED) != AUTO_UNSCANNED);
    auto_zone_write_barrier_memmove((auto_zone_t *)azone, new_ptr, ptr, MIN(size, old_size));
    
    switch (rc) {
    case 0:
        // object is collectable.
        break;
    case 1:
        // object can be freed eagerly.
        auto_really_free(azone, ptr);
        break;
    default:
        auto_error(azone, "auto_realloc: retain count > 1", ptr);
        break;
    }
    return new_ptr;
}

static void auto_zone_destroy(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone*)zone;
    auto_error(azone, "auto_zone_destroy:  %p", zone);
}

static kern_return_t auto_default_reader(task_t task, vm_address_t address, vm_size_t size, void **ptr) {
    *ptr = (void *)address;
    return KERN_SUCCESS;
}

static kern_return_t auto_in_use_enumerator(task_t task, void *context, unsigned type_mask, vm_address_t zone_address, memory_reader_t reader, vm_range_recorder_t recorder) {
    kern_return_t  err;

    if (!reader) reader = auto_default_reader;
    
    // make sure the zone version numbers match.
    union {
        unsigned *version;
        void *voidStarVersion;
    } u;
    err = reader(task, zone_address + offsetof(malloc_zone_t, version), sizeof(unsigned), &u.voidStarVersion);
    if (err != KERN_SUCCESS || *u.version != AUTO_ZONE_VERSION) return KERN_FAILURE;
        
    Auto::InUseEnumerator enumerator(task, context, type_mask, zone_address, reader, recorder);
    err = enumerator.scan();

    return err;
}

static size_t auto_good_size(malloc_zone_t *azone, size_t size) {
    return ((Auto::Zone *)azone)->good_block_size(size);
}

unsigned auto_check_counter = 0;
unsigned auto_check_start = 0;
unsigned auto_check_modulo = 1;

static boolean_t auto_check(malloc_zone_t *zone) {
    if (! (++auto_check_counter % 10000)) {
        malloc_printf("%s: At auto_check counter=%d\n", auto_prelude(), auto_check_counter);
    }
    if (auto_check_counter < auto_check_start) return 1;
    if (auto_check_counter % auto_check_modulo) return 1;
    return 1;
}

static char *b2s(int bytes, char *buf) {
    if (bytes < 10*1024) {
        sprintf(buf, "%dbytes", bytes);
    } else if (bytes < 10*1024*1024) {
        sprintf(buf, "%dKB", bytes / 1024);
    } else {
        sprintf(buf, "%dMB", bytes / (1024*1024));
    }
    return buf;
}

static void auto_zone_print(malloc_zone_t *zone, boolean_t verbose) {
    char    buf1[256];
    char    buf2[256];
    Auto::Zone             *azone = (Auto::Zone *)zone;
    auto_statistics_t   *stats = &azone->stats;
    printf("auto zone %p: in_use=%u  used=%s allocated=%s\n", azone, stats->malloc_statistics.blocks_in_use, b2s(stats->malloc_statistics.size_in_use, buf1), b2s(stats->malloc_statistics.size_allocated, buf2));
    if (verbose) azone->print_all_blocks();
}

static void auto_zone_log(malloc_zone_t *zone, void *log_address) {
}

// these force_lock() calls get called when a process calls fork(). we need to be careful not to be in the collector when this happens.

static void auto_zone_force_lock(malloc_zone_t *zone) {
    // if (azone->control.log & AUTO_LOG_UNUSUAL) malloc_printf("%s: auto_zone_force_lock\n", auto_prelude());
    // need to grab the allocation locks in each Admin in each Region
    // After we fork, need to zero out the thread list.
#warning forking needs allocation locks held
}

static void auto_zone_force_unlock(malloc_zone_t *zone) {
    // if (azone->control.log & AUTO_LOG_UNUSUAL) malloc_printf("%s: auto_zone_force_unlock\n", auto_prelude());
}

// copy, from the internals, the malloc_zone_statistics data wanted from the malloc_introspection API
static void auto_malloc_statistics(malloc_zone_t *zone, malloc_statistics_t *stats) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    
    auto_stats_lock(azone);
    using namespace Auto;
    Statistics &statistics = azone->statistics();
    stats->blocks_in_use = statistics.count();
    stats->size_in_use = statistics.size();
    stats->max_size_in_use = statistics.dirty_size();  // + aux_zone max_size_in_use ??
    stats->size_allocated = statistics.allocated();    // + aux_zone size_allocated ??
    auto_stats_unlock(azone);
}


/*********  Entry points    ************/

static struct malloc_introspection_t auto_zone_introspect = {
    auto_in_use_enumerator,
    auto_good_size,
    auto_check,
    auto_zone_print,
    auto_zone_log, 
    auto_zone_force_lock, 
    auto_zone_force_unlock,
    auto_malloc_statistics
};

struct malloc_introspection_t auto_zone_introspection() {
    return auto_zone_introspect;
}

static auto_zone_t *gc_zone = NULL;

// DEPRECATED
auto_zone_t *auto_zone(void) {
    return gc_zone;
}

static void *auto_collection_thread(void *arg);


static void willgrow(auto_zone_t *collector, auto_heap_growth_info_t info) {  }

static void getenv_ulong(const char *name, unsigned long *dest) {
    const char *str = getenv(name);
    if (str) *dest = strtoul(str, NULL, 0);
}

static boolean_t getenv_bool(const char *name) {
    const char *str = getenv(name);
    return str && !strcmp(str, "YES");
}

// there can be several autonomous auto_zone's running, in theory at least.
auto_zone_t *auto_zone_create(const char *name) {
    aux_init();
#if defined(AUTO_ALLOCATION_METER)
    allocate_meter_init();
#endif
    Auto::Zone  *azone = new Auto::Zone();
    azone->basic_zone.size = auto_size;
    azone->basic_zone.malloc = auto_malloc;
    azone->basic_zone.free = auto_free;
    azone->basic_zone.calloc = auto_calloc;
    azone->basic_zone.valloc = auto_valloc;
    azone->basic_zone.realloc = auto_realloc;
    azone->basic_zone.destroy = auto_zone_destroy;
    azone->basic_zone.zone_name = name; // ;
    azone->basic_zone.introspect = &auto_zone_introspect;
    azone->basic_zone.version = AUTO_ZONE_VERSION;
    azone->initial_refcount_to_one = 1;  // for CF & regular malloc/free use
    azone->control.disable_generational = getenv_bool("AUTO_DISABLE_GENERATIONAL");
    azone->control.malloc_stack_logging = (getenv("MallocStackLogging") != NULL  ||  getenv("MallocStackLoggingNoCompact") != NULL);
    azone->control.log = AUTO_LOG_NONE;
    if (getenv_bool("AUTO_LOG_NOISY"))       azone->control.log |= AUTO_LOG_COLLECTIONS;
    if (getenv_bool("AUTO_LOG_ALL"))         azone->control.log |= AUTO_LOG_ALL;
    if (getenv_bool("AUTO_LOG_COLLECTIONS")) azone->control.log |= AUTO_LOG_COLLECTIONS;
    if (getenv_bool("AUTO_LOG_REGIONS"))     azone->control.log |= AUTO_LOG_REGIONS;
    if (getenv_bool("AUTO_LOG_UNUSUAL"))     azone->control.log |= AUTO_LOG_UNUSUAL;
    if (getenv_bool("AUTO_LOG_WEAK"))        azone->control.log |= AUTO_LOG_WEAK;

    azone->control.collection_threshold = 1024L * 1024L;
    getenv_ulong("AUTO_COLLECTION_THRESHOLD", &azone->control.collection_threshold);
    azone->control.full_vs_gen_frequency = 10;
    getenv_ulong("AUTO_COLLECTION_RATIO", &azone->control.full_vs_gen_frequency);
    azone->control.will_grow = willgrow;

    malloc_zone_register((auto_zone_t*)azone);

    AUTO_RECORD_REFCOUNT_STACKS = (getenv("AUTO_RECORD_REFCOUNT_STACKS") != NULL);

    pthread_mutex_init(&azone->collection_mutex, NULL);
    pthread_cond_init(&azone->collection_requested, NULL);
    azone->collection_requested_mode = 0;
    pthread_cond_init(&azone->collection_status, NULL);
    azone->collection_status_state = 0;
    azone->collection_thread = pthread_self();
    
    if (!gc_zone) gc_zone = (auto_zone_t *)azone;   // cache first one for debugging, monitoring
    return (auto_zone_t*)azone;
}

static void *auto_monitor_thread(void *unused) {
    using namespace Auto;
    Monitor *monitor = Monitor::monitor();
    if (monitor) monitor->open_mach_port();
    return NULL;
}

static void agc_zone_monitor_open_port() {
    pthread_t monitor_thread;
    pthread_create(&monitor_thread, NULL, auto_monitor_thread, NULL);
}

void auto_zone_start_monitor(boolean_t force) {
    // starts the zone monitoring thread.
    if (force && getenv("AUTO_ENABLE_MONITOR") == NULL) {
        putenv("AUTO_ENABLE_MONITOR=YES");
    }
    using namespace Auto;
    Zone::setup_shared();
    // XXX_PCB: in case this was called AFTER the Zone was created, need to set the Zone's monitor.
    Monitor *monitor = Monitor::monitor();
    Zone *zone = Zone::zone();
    if (monitor && zone) {
        if (zone->monitor() != monitor)
            zone->set_monitor(monitor);
    }
    if (force) {
        static pthread_once_t control = PTHREAD_ONCE_INIT;
        pthread_once(&control, agc_zone_monitor_open_port);
    }
}

void auto_zone_set_class_list(int (*class_list)(void **buffer, int count)) {
    Auto::Monitor::set_class_list(class_list);
}

/*********  Reference counting  ************/

void auto_zone_retain(auto_zone_t *zone, void *ptr) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
#if DEBUG
    if (ptr == WatchPoint) {
        malloc_printf("auto_zone_retain watchpoint: %p\n", WatchPoint);
        blainer();
    }
#endif

#if 0
    if (auto_zone_is_finalized(zone, ptr)) {
        malloc_printf("auto_zone_retain retaining finalized pointer: %p\n", ptr);
    }
#endif

    if (AUTO_RECORD_REFCOUNT_STACKS) {
        auto_record_refcount_stack(azone, ptr, +1);
    }

    azone->block_increment_refcount(ptr);
} 

unsigned int auto_zone_release(auto_zone_t *zone, void *ptr) {
    Auto::Zone *azone = (Auto::Zone *)zone;

#if DEBUG
    if (ptr == WatchPoint) {
        malloc_printf("auto_zone_release watchpoint: %p\n", WatchPoint);
        blainer();
    }
#endif

    if (AUTO_RECORD_REFCOUNT_STACKS) {
        auto_record_refcount_stack(azone, ptr, -1);
    }

    return azone->block_decrement_refcount(ptr);
}


unsigned int auto_zone_retain_count(auto_zone_t *zone, const void *ptr) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    return azone->block_refcount((void *)ptr);
}

unsigned int auto_zone_retain_count_no_lock(auto_zone_t *zone, const void *ptr) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    return azone->block_refcount((void *)ptr);
}

/*********  Write-barrier   ************/


void __attribute__ ((noinline)) auto_zone_resurrection(Auto::Zone *azone, const void *new_value) {
    auto_error(azone, "pointer in garbage list being stored into reachable memory, break on auto_zone_resurrection_error to debug", new_value);
    auto_zone_resurrection_error();
}

static void check_resurrection(Auto::Zone *azone, void *recipient, const void *new_value, size_t offset) {
    if (new_value
        && azone->is_block((void *)new_value)
        && azone->block_is_garbage((void *)new_value)
        && !azone->block_is_garbage(recipient)) {
        auto_memory_type_t recipient_type = (auto_memory_type_t) azone->block_layout((void*)recipient);
        if ((recipient_type & AUTO_UNSCANNED) != AUTO_UNSCANNED) {
            auto_memory_type_t new_value_type = (auto_memory_type_t) azone->block_layout((void*)new_value);
            if (new_value_type == AUTO_OBJECT_SCANNED) {
                // mark the object for zombiehood.
                azone->block_increment_refcount((void*)new_value); // mark the object ineligible for freeing this time around.
                azone->add_zombie((void*)new_value);
                if (azone->control.name_for_address) {
                    // note, the auto lock is held until the callback has had a chance to examine each block.
                    char *recipient_name = azone->control.name_for_address((auto_zone_t *)azone, (vm_address_t)recipient, offset);
                    char *new_value_name = azone->control.name_for_address((auto_zone_t *)azone, (vm_address_t)new_value, 0);
                    malloc_printf("*** resurrection error for object %p: auto_zone_write_barrier: %s(%p)[%d] = %s(%p)\n",
                                  new_value, recipient_name, recipient, offset, new_value_name, new_value);
                    free(recipient_name);
                    free(new_value_name);
                }
            }
            auto_zone_resurrection(azone, new_value);
        }
    }
}


boolean_t auto_zone_set_write_barrier(auto_zone_t *zone, const void *dest, const void *new_value) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    if (azone->is_thread_finalizing()) {
        const void *recipient = auto_zone_base_pointer(zone, dest);
        if (!recipient) return false;
        size_t offset_in_bytes = (char *)dest - (char *)recipient;
        check_resurrection(azone, (void *)recipient, new_value, offset_in_bytes);
    }
    return azone->set_write_barrier((void *)dest, (void *)new_value);
}

void auto_zone_write_barrier(auto_zone_t *zone, void *recipient, const unsigned long offset_in_bytes, const void *new_value) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    // FIXME:  can only do this check if we're in a finalizing thread.
    if (azone->is_thread_finalizing()) check_resurrection(azone, recipient, new_value, offset_in_bytes);
    azone->set_write_barrier((char*)recipient + offset_in_bytes, (void *)new_value);
}

void auto_zone_write_barrier_range(auto_zone_t *zone, void *address, size_t size) {
    // This is a bogus entry point that does not work if the collector starts up just after this check
    // THIS IS DEPRECATED UGLY SHOULD NOT BE USED
}

void *auto_zone_write_barrier_memmove(auto_zone_t *zone, void *dst, const void *src, size_t size) {
    if (size && dst != src) {
        // speculatively determine the object pointer for the destination
        void *base = (void *)auto_zone_base_pointer(zone, dst);
        // if the destination is an object then mark the write barrier
        if (base) {
            // range check for extra safety.
            Auto::Zone *azone = (Auto::Zone *)zone;
            size_t block_size = auto_zone_size(zone, base);
            if ((uintptr_t(dst) + size) > (uintptr_t(base) + block_size)) {
                auto_error(azone, "auto_zone_write_barrier_memmove: range check failed", dst);
                // FIXME:  set __crashreporter_info__
                __builtin_trap();
            }
            if (azone->is_thread_finalizing()) {
                auto_memory_type_t type = auto_zone_get_layout_type(zone, base);
                if ((type == AUTO_OBJECT_SCANNED || type == AUTO_MEMORY_SCANNED) && !auto_zone_is_finalized(zone, base)) {
                    void **src_ptr = (void **)src;
                    int count = size / sizeof(void *);
                    int i;
                    for (i = 0; i < count; i++) {
                        if (auto_zone_is_finalized(zone, src_ptr[i])) {
                            auto_error(azone, "auto_zone_write_barrier_memmove: resurrecting collected object", src_ptr[i]);
                            //  make object immortal
                            azone->block_increment_refcount(src_ptr[i]);
                            auto_zone_resurrection(azone, src_ptr[i]);
                        }
                    }
                }
            }
            if (azone->set_write_barrier_range(dst, size)) {
                // must hold enlivening lock for duration of the move; otherwise if we get scheduled out during the move
                // and GC starts and scans our destination before we finish filling it with unique values we lose them
                Auto::UnconditionalBarrier condition(azone->needs_enlivening(), azone->enlivening_lock());
                if (condition) {
                    // add all values in the range.
                    // We could/should only register those that are as yet unmarked.
                    // We also only add values that are objects.
                    void **start = (void **)src;
                    void **end = start + size/sizeof(void *);
                    while (start < end) {
                        void *candidate = *start;
                        if (azone->is_block(candidate) && !azone->block_is_marked(candidate)) azone->enlivening_queue().add(candidate);
                        start++;
                    }
                    return memmove(dst, src, size);
                }
            }
        }
    }
    // perform the copy
    return memmove(dst, src, size);
}

/*********  Layout  ************/

void* auto_zone_allocate_object(auto_zone_t *zone, size_t size, auto_memory_type_t type, boolean_t initial_refcount_to_one, boolean_t clear) {
    void *ptr;
//    if (allocate_meter) allocate_meter_start();
    Auto::Zone *azone = (Auto::Zone *)zone;
    // ALWAYS clear if scanned memory <rdar://problem/5341463>.
    ptr = auto_malloc_internal(azone, size, type, initial_refcount_to_one, clear || (type & AUTO_UNSCANNED) != AUTO_UNSCANNED);
    if (ptr && malloc_logger) malloc_logger(MALLOC_LOG_TYPE_ALLOCATE | MALLOC_LOG_TYPE_HAS_ZONE | (clear ? MALLOC_LOG_TYPE_CLEARED : 0), uintptr_t(zone), size, 0, uintptr_t(ptr), 0);
//    if (allocate_meter) allocate_meter_stop();
    return ptr;
}

extern "C" void *auto_zone_create_copy(auto_zone_t *zone, void *ptr) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    auto_memory_type_t type; int rc = 0;
    if (!get_type_and_retain_count(azone, ptr, &type, &rc)) {
        auto_error(azone, "auto_zone_copy_memory: can't get type or retain count, ptr (%p) from ordinary malloc zone?", ptr);
        return (void *)0;
    }
    if (rc > 1) {
        auto_error(azone, "auto_zone_copy_memory: retain count too large for ptr (%p)", ptr);
        return (void *)0;
    }
    if (type == AUTO_OBJECT_SCANNED || type == AUTO_OBJECT_UNSCANNED) {
        auto_error(azone, "auto_zone_copy_memory called on object %p\n", ptr);
        return (void *)0;
    }
    size_t size = auto_size(zone, ptr);
    void *result = auto_zone_allocate_object(zone, size, type, (rc == 1), false);
    if (type == AUTO_OBJECT_SCANNED)
        auto_zone_write_barrier_memmove(zone, result, ptr, size);
    else
        memmove(result, ptr, size);
    return result;
}


void auto_zone_set_layout_type(auto_zone_t *zone, void *ptr, auto_memory_type_t type) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    azone->block_set_layout(ptr, type);
}


auto_memory_type_t auto_zone_get_layout_type(auto_zone_t *zone, void *ptr) {
    return (auto_memory_type_t) ((Auto::Zone *)zone)->block_layout(ptr);
}


auto_memory_type_t auto_zone_get_layout_type_no_lock(auto_zone_t *zone, void *ptr) {
    return (auto_memory_type_t) ((Auto::Zone *)zone)->block_layout(ptr);
}


void auto_zone_register_thread(auto_zone_t *zone) {
    static pthread_once_t control = PTHREAD_ONCE_INIT;
    pthread_once(&control, agc_zone_monitor_open_port);
    ((Auto::Zone *)zone)->register_thread();
}


void auto_zone_unregister_thread(auto_zone_t *zone) {
    ((Auto::Zone *)zone)->unregister_thread();
}


/**
 * Computes a conservative estimate of the amount of memory touched by the collector. Examines each
 * small region, determining the high watermark of used blocks, and subtracts out the unused block sizes
 * (to the nearest page boundary). Assumes all of the book keeping bitmaps have been touched. Also subtracts
 * out the sizes of the allocate big entries, since these aren't touched by the allocator itself.
 */
unsigned auto_zone_touched_size(auto_zone_t *zone) {
    using namespace Auto;
    Statistics stats;
    ((Zone *)zone)->statistics(stats);
    return stats.size();
}

double auto_zone_utilization(auto_zone_t *zone) {
    using namespace Auto;
    Statistics stats;
    ((Zone *)zone)->statistics(stats);
    return (double)stats.small_medium_size() / (double)(stats.small_medium_size() + stats.unused());
}

/*********  Garbage Collection and Compaction   ************/

auto_collection_control_t *auto_collection_parameters(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    return &azone->control;
}


// DEPRECATED ENTRY POINT
const auto_statistics_t *auto_collection_statistics(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    Auto::Statistics &statistics = azone->statistics();
    
    auto_stats_lock(azone);
    azone->stats.malloc_statistics.blocks_in_use = statistics.count();
    azone->stats.malloc_statistics.size_in_use = statistics.size();
    azone->stats.malloc_statistics.max_size_in_use = statistics.dirty_size();
    azone->stats.malloc_statistics.size_allocated = statistics.allocated();
    auto_stats_unlock(azone);
    
    return (const auto_statistics_t *)&azone->stats;
}

// public entry point.
void auto_zone_statistics(auto_zone_t *zone, auto_statistics_t *stats) {
    if (!stats || stats->version != 0) return;
    Auto::Zone *azone = (Auto::Zone *)zone;
    Auto::Statistics &statistics = azone->statistics();
    
    auto_stats_lock(azone);
    azone->stats.malloc_statistics.blocks_in_use = statistics.count();
    azone->stats.malloc_statistics.size_in_use = statistics.size();
    azone->stats.malloc_statistics.max_size_in_use = statistics.dirty_size();
    azone->stats.malloc_statistics.size_allocated = statistics.allocated() + statistics.admin_size();
    // now copy the whole thing over
    *stats = azone->stats;
    auto_stats_unlock(azone);
}

// work in progress
typedef struct {
    FILE *f;
    char *buff;
    size_t buff_size;
    size_t buff_pos;
} AutoZonePrintInfo;

static void _auto_zone_stats_printf(AutoZonePrintInfo *info, const char *fmt, ...)
{
    if (info->f) {
        va_list valist;
        va_start(valist, fmt);
        vfprintf(info->f, fmt, valist);
        va_end(valist);
    }
    if (info->buff) {
        if (info->buff_pos < info->buff_size) {
            va_list valist;
            va_start(valist, fmt);
            info->buff_pos += vsnprintf(&info->buff[info->buff_pos], info->buff_size - info->buff_pos, fmt, valist);
            va_end(valist);
        }
    }
}

static void print_zone_stats(AutoZonePrintInfo *info, malloc_statistics_t &stats, char *message) {
    _auto_zone_stats_printf(info, "%s %10lu %10u %10lu %10lu        %0.2f\n", message,
        stats.size_in_use, stats.blocks_in_use, stats.max_size_in_use, stats.size_allocated,
        ((float)stats.size_in_use)/stats.max_size_in_use);
}

__private_extern__ malloc_zone_t *aux_zone;

static void _auto_zone_stats(AutoZonePrintInfo *info) {
    // Memory first
    malloc_statistics_t mstats;
    _auto_zone_stats_printf(info, "\n            bytes     blocks      dirty     vm     bytes/dirty\n");
    if (gc_zone) {
        malloc_zone_statistics(gc_zone, &mstats);
        print_zone_stats(info, mstats, "auto  ");
        malloc_zone_statistics(aux_zone, &mstats);
        print_zone_stats(info, mstats, "aux   ");
    }
    malloc_zone_statistics(malloc_default_zone(), &mstats);
    print_zone_stats(info, mstats, "malloc");
    malloc_zone_statistics(NULL, &mstats);
    print_zone_stats(info, mstats, "total ");
    if (!gc_zone) return;
    
    Auto::Statistics &statistics = ((Auto::Zone *)gc_zone)->statistics();
    Auto::Zone *azone = (Auto::Zone *)gc_zone;
    
    _auto_zone_stats_printf(info, "Regions In Use: %ld\nSubzones In Use: %ld\n", statistics.regions_in_use(), statistics.subzones_in_use());
    
    
    auto_statistics_t *stats = &azone->stats;
    // CPU
//    _auto_zone_stats_printf(info, "\ncpu (microseconds):\n\ntotal %lld usecs = scan %lld + finalize %lld + reclaim %lld\n",
    _auto_zone_stats_printf(info, "\n%ld generational\n%ld full\ncpu (microseconds):\n               total =     scan   + freeze + finalize  + reclaim\nfull+gen  %10lld %10lld %10lld %10lld %10lld\n", statistics.partial_gc_count(), statistics.full_gc_count(),
        // full + gen
        stats->total[0].total_duration + stats->total[1].total_duration,
        stats->total[0].scan_duration + stats->total[1].scan_duration,
        stats->total[0].enlivening_duration + stats->total[1].enlivening_duration,
        stats->total[0].finalize_duration + stats->total[1].finalize_duration,
        stats->total[0].reclaim_duration + stats->total[1].reclaim_duration);
    _auto_zone_stats_printf(info, "gen. max  %10lld %10lld %10lld %10lld %10lld\n",
        stats->maximum[1].total_duration, 
        stats->maximum[1].scan_duration,
        stats->maximum[1].enlivening_duration,
        stats->maximum[1].finalize_duration,
        stats->maximum[1].reclaim_duration);
    _auto_zone_stats_printf(info, "full max  %10lld %10lld %10lld %10lld %10lld\n\n",
        stats->maximum[0].total_duration, 
        stats->maximum[0].scan_duration,
        stats->maximum[0].enlivening_duration,
        stats->maximum[0].finalize_duration,
        stats->maximum[0].reclaim_duration);
    long count = statistics.partial_gc_count();
    if (!count) count = 1;
    _auto_zone_stats_printf(info, "gen. avg  %10lld %10lld %10lld %10lld %10lld\n",
        stats->total[1].total_duration/count, 
        stats->total[1].scan_duration/count,
        stats->total[1].enlivening_duration/count,
        stats->total[1].finalize_duration/count,
        stats->total[1].reclaim_duration/count);
    count = statistics.full_gc_count();
    if (!count) count = 1;
    _auto_zone_stats_printf(info, "full avg  %10lld %10lld %10lld %10lld %10lld\n\n",
        stats->total[0].total_duration/count, 
        stats->total[0].scan_duration/count,
        stats->total[0].enlivening_duration/count,
        stats->total[0].finalize_duration/count,
        stats->total[0].reclaim_duration/count);
}

void auto_zone_write_stats(FILE *f) {
    AutoZonePrintInfo info;
    info.f = f;
    info.buff = NULL;
    _auto_zone_stats(&info);
}

void auto_zone_stats() {
    auto_zone_write_stats(stdout);
}

char *auto_zone_stats_string()
{
    AutoZonePrintInfo info;
    info.f = NULL;
    info.buff = NULL;
    info.buff_size = 0;
    do {
        info.buff_size += 2048;
        if (info.buff) free(info.buff);
        info.buff = (char *)malloc(info.buff_size);
        info.buff_pos = 0;
        _auto_zone_stats(&info);
    } while (info.buff_pos > info.buff_size);
    return info.buff;
}

void auto_collector_reenable(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    // although imperfect, try to avoid dropping below zero
    if (azone->collector_disable_count == 0) return;
    OSAtomicDecrement32(&azone->collector_disable_count);
}

void auto_collector_disable(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    OSAtomicIncrement32(&azone->collector_disable_count);
}

boolean_t auto_zone_is_enabled(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    return azone->collector_disable_count == 0;
}

boolean_t auto_zone_is_collecting(auto_zone_t *zone) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    // FIXME: the result of this function only valid on the collector thread (main for now).
    return !azone->is_state(idle);
}

void auto_collect_multithreaded(auto_zone_t *zone) {
    Auto::Zone *azone = (Auto::Zone *)zone;
    if (! azone->multithreaded) {
        if (azone->control.log & AUTO_LOG_COLLECTIONS) malloc_printf("starting dedicated collection thread\n");
        azone->multithreaded = true;
        pthread_create(&azone->collection_thread, NULL, auto_collection_thread, azone);
    }
}


struct __auto_reference_context {
    auto_zone_t *zone;
    auto_reference_recorder_t callback;
    void *ctx;
};

static void agc_reference_recorder(void *ctx, agc_reference_t reference)
{
    struct __auto_reference_context *context = (struct __auto_reference_context *)ctx;
    auto_reference_t ref = { reference.referent, reference.referrer_base, reference.referrer_offset };
    context->callback(context->zone, context->ctx, ref);
}

void auto_enumerate_references(auto_zone_t *zone, void *referent, 
                               auto_reference_recorder_t callback, 
                               void *stack_bottom, void *ctx)
{
    Auto::Zone *azone = (Auto::Zone *)zone;
    struct __auto_reference_context context = { zone, callback, ctx };
    agc_enumerate_references(azone, referent, &agc_reference_recorder, stack_bottom, &context);
}

void auto_enumerate_references_no_lock(auto_zone_t *zone, void *referent, 
                               auto_reference_recorder_t callback, 
                               void *stack_bottom, void *ctx)
{
    Auto::Zone *azone = (Auto::Zone *)zone;
    struct __auto_reference_context context = { zone, callback, ctx };
    agc_enumerate_references(azone, referent, &agc_reference_recorder, stack_bottom, &context);
}

/********* Weak References ************/


// auto_assign_weak
// The new and improved one-stop entry point to the weak system
// Atomically assign value to *location and track it for zero'ing purposes.
// Assign a value of NULL to deregister from the system.
void auto_assign_weak_reference(auto_zone_t *zone, const void *value, void *const*location, auto_weak_callback_block_t *block) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    if (azone->is_thread_finalizing()) {
        const void *base = auto_zone_base_pointer(zone, (const void*)location);
        if (!base) base = location;
        size_t offset = uintptr_t(location) - uintptr_t(base);
        check_resurrection(azone, (void*)base, value, offset);
    }
    weak_register(azone, value, (void **)location, block);
}

void *auto_read_weak_reference(auto_zone_t *zone, void **referrer) {
    void *result = *referrer;
    if (result != NULL) {
        // We grab the condition barrier.  Missing the transition is not a real issue.
        // For a missed transition to be problematic the collector would have had to mark
        // the transition before we entered this routine, scanned this thread (not seeing the
        // enlivened read), scanned the heap, and scanned this thread exhaustively before we
        // load *referrer
        using namespace Auto;
        Zone *azone = (Zone*)zone;
        ConditionBarrier barrier(azone->needs_enlivening(), azone->enlivening_lock());
        if (barrier) {
            // need to tell the collector this block should be scanned.
            result = *referrer;
            if (result && !azone->block_is_marked(result)) azone->enlivening_queue().add(result);
        } else {
            result = *referrer;
        }
    }
    return result;
}

/********* Associative References ************/

void auto_zone_set_associative_ref(auto_zone_t *zone, void *object, void *key, void *value) {
    using namespace Auto;
    Zone *azone = (Zone*)zone;
    if (azone->is_thread_finalizing()) check_resurrection(azone, object, value, 0);
    azone->set_associative_ref(object, key, value);
}

void *auto_zone_get_associative_ref(auto_zone_t *zone, void *object, void *key) {
    using namespace Auto;
    Zone *azone = (Zone*)zone;
    return azone->get_associative_ref(object, key);
}

/********* Root References ************/

void auto_zone_add_root(auto_zone_t *zone, void *root, void *value)
{
    ((Auto::Zone *)zone)->add_root(root, value);
}

extern void auto_zone_root_write_barrier(auto_zone_t *auto_zone, void *address_of_possible_root_ptr, void *value) {
    if (!value) {
        *(void **)address_of_possible_root_ptr = NULL;
        return;
    }
    using namespace Auto;
    Zone *azone = (Zone *)auto_zone;
    if (azone->is_root(address_of_possible_root_ptr)) {
        UnconditionalBarrier barrier(azone->needs_enlivening(), azone->enlivening_lock());
        // might need to tell the collector this block should be scanned.
        if (barrier && !azone->block_is_marked(value)) azone->enlivening_queue().add(value);
        *(void **)address_of_possible_root_ptr = value;
    }
    else {
        // always write
        *(void **)address_of_possible_root_ptr = value;
    }
}


void auto_zone_print_roots(auto_zone_t *zone) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    Statistics junk;
    PointerList roots(junk);
    azone->copy_roots(roots);
    usword_t count = roots.count();
    printf("### %lu roots. ###\n", count);
    void ***buffer = (void ***)roots.buffer();
    for (usword_t i = 0; i < count; ++i) {
        void **root = buffer[i];
        printf("%p -> %p\n", root, *root);
    }
}

/********** Atomic operations *********************/

boolean_t auto_zone_atomicCompareAndSwap(auto_zone_t *zone, void *existingValue, void *newValue, void *volatile *location, boolean_t isGlobal, boolean_t issueBarrier) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    if (azone->is_thread_finalizing()) check_resurrection(azone, (void *)location, newValue, 0);
    if (isGlobal) {
        azone->add_root_no_barrier((void *)location);
    }
    UnconditionalBarrier barrier(azone->needs_enlivening(), azone->enlivening_lock());
    boolean_t result;
    if (issueBarrier)
        result = OSAtomicCompareAndSwapPtrBarrier(existingValue, newValue, location);
    else
        result = OSAtomicCompareAndSwapPtr(existingValue, newValue, location);
    if (!isGlobal) {
        // mark write-barrier w/o storing
        azone->set_write_barrier((char*)location);
    }
    if (result && barrier && !azone->block_is_marked(newValue)) azone->enlivening_queue().add(newValue);
    return result;
}
 

/************ Miscellany **************************/


#if 0
// Watching

#define WatchLimit 16
static const void *WatchPoints[WatchLimit];

void auto_zone_watch(const void *ptr) {
    for (int i = 0; i < WatchLimit; ++i) {
        if (WatchPoints[i]) 
            if (WatchPoints[i] == ptr) return;
            else
                continue;
        WatchPoints[i] = ptr;
        return;
    }
    printf("too many watchpoints already, skipping %p\n", ptr);
}

void auto_zone_watch_free(const void *ptr, const char *msg) {
    for (int i = 0; i < WatchLimit; ++i) {
        if (WatchPoints[i] == NULL) return;
        if (WatchPoints[i] == ptr) {
            printf(msg, ptr);
            while(++i < WatchLimit)
                WatchPoints[i-1] = WatchPoints[i];
            WatchPoints[WatchLimit-1] = NULL;
            return;
        }
    }
}

boolean_t auto_zone_watch_msg(void *ptr, const char *format,  void *extra) {
    for (int i = 0; i < WatchLimit; ++i) {
        if (WatchPoints[i] == NULL) return false;
        if (WatchPoints[i] == ptr) {
            printf(format, ptr, extra);
            return true;
        }
    }
    return false;
}
#endif


////////////////// SmashMonitor ///////////////////

static void range_check(void *pointer, size_t size) {
    Auto::Zone *azone = (Auto::Zone *)gc_zone;
    if (azone) {
        void *base_pointer = azone->block_start(pointer);
        if (base_pointer) {
            size_t block_size = azone->block_size(base_pointer);
            if ((uintptr_t(pointer) + size) > (uintptr_t(base_pointer) + block_size)) {
                malloc_printf("SmashMonitor: range check violation for pointer = %p, size = %lu", pointer, size);
                __builtin_trap();
            }
        }
    }
}

void *SmashMonitor_memcpy(void *dst, const void* src, size_t size) {
    // add some range checking code for auto allocated blocks.
    range_check(dst, size);
    return memcpy(dst, src, size);
}

void *SmashMonitor_memmove(void *dst, const void* src, size_t size) {
    // add some range checking code for auto allocated blocks.
    range_check(dst, size);
    return memmove(dst, src, size);
}

void *SmashMonitor_memset(void *pointer, int value, size_t size) {
    // add some range checking code for auto allocated blocks.
    range_check(pointer, size);
    return memset(pointer, value, size);
}

void SmashMonitor_bzero(void *pointer, size_t size) {
    // add some range checking code for auto allocated blocks.
    range_check(pointer, size);
    bzero(pointer, size);
}


#if USE_INTERPOSING
DYLD_INTERPOSE(SmashMonitor_memcpy, memcpy)
DYLD_INTERPOSE(SmashMonitor_memmove, memmove)
DYLD_INTERPOSE(SmashMonitor_memset, memset)
DYLD_INTERPOSE(SmashMonitor_bzero, bzero)
#endif


#if LOG_TIMINGS
// allocation & collection rate logging

typedef struct {
    auto_date_t stamp;
    size_t  allocated;
    size_t  finger;
    size_t  recovered;
    char    purpose;    // G or F - start GC; E - end GC; A - allocation threshold
} log_record_t;

#define NRECORDS 2048

log_record_t AutoRecords[NRECORDS];
int AutoRecordsIndex = 0;


static void dumpRecords() {
    int fd = open("/tmp/records", O_CREAT|O_APPEND, 0666);
    int howmany = AutoRecordsIndex - 1;
    write(fd, &AutoRecords[0], howmany*sizeof(log_record_t));
    close(fd);
    AutoRecordsIndex = 0;
}
static log_record_t *getRecord(int dump) {
    for (;;) {
        int index = OSAtomicIncrement32(&AutoRecordsIndex);
        if (index == NRECORDS || dump) {
            dumpRecords();
        }
        else if (index < NRECORDS) return &AutoRecords[index];
    }
}
static void log_allocation_threshold(auto_date_t time, size_t allocated, size_t finger) {
    log_record_t *record = getRecord(0);
    record->stamp = time;
    record->allocated = allocated;
    record->finger = finger;
    record->purpose = 'A';
}
static void log_collection_begin(auto_date_t time, size_t allocated, size_t finger, bool isGen) {
    log_record_t *record = getRecord(0);
    record->stamp = time;
    record->allocated = allocated;
    record->finger = finger;
    record->purpose = isGen ? 'G' : 'F';
}

static void log_collection_end(auto_date_t time, size_t allocated, size_t finger, size_t recovered) {
    log_record_t *record = getRecord(0);
    record->stamp = time;
    record->allocated = allocated;
    record->finger = finger;
    record->recovered = recovered;
    record->purpose = 'E';
}

static double rateps(size_t quant, auto_date_t interval) {
    double quantity = quant;
    return (quantity/interval);
}

void log_analysis() {
    int lastAllocation = -1;
    int collectionBegin = -1;
    for (int index = 0; index < AutoRecordsIndex; ++index) {
        if (AutoRecords[index].purpose == 'A') {
            if (lastAllocation == -1) { lastAllocation = index; continue; }
            auto_date_t interval = AutoRecords[index].stamp - AutoRecords[lastAllocation].stamp;
            size_t quantity = AutoRecords[index].allocated - AutoRecords[lastAllocation].allocated;
            printf("%ld bytes in %lld microseconds, %gmegs/sec allocation rate\n", quantity, interval, rateps(quantity, interval));
            lastAllocation = index;
        }
        else if (AutoRecords[index].purpose == 'G' || AutoRecords[index].purpose == 'F') {
            collectionBegin = index;
            printf("begining %c collection\n", AutoRecords[index].purpose);
        }
        else if (AutoRecords[index].purpose == 'E') {
            auto_date_t interval = AutoRecords[index].stamp - AutoRecords[collectionBegin].stamp;
            size_t quantity = AutoRecords[index].allocated - AutoRecords[collectionBegin].allocated;
            size_t recovered  = AutoRecords[index].recovered;
            quantity += recovered;
            printf("%ld bytes in %lld microseconds, %gmegs/sec rate during collection\n", quantity, interval, rateps(quantity, interval));
            printf("%ld bytes %lld microseconds, %gmegs/sec recovery rate\n", recovered, interval, rateps(recovered, interval));
        }
    }
}

#endif LOG_TIMINGS
