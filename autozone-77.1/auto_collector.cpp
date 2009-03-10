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

#include "auto_collector.h"
#include "auto_weak.h"
#include "agc_interface.h"
#include "AutoZone.h"

// reference tracing hooks
vm_address_t auto_collect_stack_bottom;

unsigned long long num_precise_traces = 0;
unsigned long long num_has_layout = 0;
unsigned long long num_words_precise_traces = 0;
unsigned long long num_words_actually_fetched = 0;
unsigned long long num_words_non_trivial_ptrs = 0;

void auto_collect_print_trace_stats(void) {
    printf("num_precise_traces = %d\n", (unsigned)num_precise_traces);
    printf("num_has_layout = %d (%d%%)\n", (unsigned)num_has_layout, (unsigned)(num_has_layout * 100 / ((num_precise_traces) ? num_precise_traces : 1)));
    printf("num_words_precise_traces = %d (%dx)\n", (unsigned)num_words_precise_traces, (unsigned)(num_words_precise_traces/((num_precise_traces) ? num_precise_traces : 1)));
    printf("num_words_actually_fetched = %d (%d%%)\n", (unsigned)num_words_actually_fetched, (unsigned)(num_words_actually_fetched * 100 / ((num_words_precise_traces) ? num_words_precise_traces : 1)));
    printf("num_words_non_trivial_ptrs = %d (%d%%)\n", (unsigned)num_words_non_trivial_ptrs, (unsigned)(num_words_non_trivial_ptrs * 100 / ((num_words_actually_fetched) ? num_words_actually_fetched : 1)));
}

// GrP called directly by reference tracer
__private_extern__ boolean_t auto_collection_full_gc(azone_t *azone, size_t *garbage_count, vm_address_t **garbage, void *collection_context) {
    return agc_zone_collect(azone, false, garbage_count, garbage, (void *)auto_collect_stack_bottom, (void*)azone->control.collection_should_interrupt);
}

static boolean_t auto_collection_generation_gc(azone_t *azone, size_t *garbage_count, vm_address_t **garbage, void *collection_context) {
    return agc_zone_collect(azone, true, garbage_count, garbage, (void *)auto_collect_stack_bottom, (void*)azone->control.collection_should_interrupt);
}


// Paranoid generational - run generational gc AND full gc
// and make sure generational garbage is a subset of full gc garbage
static boolean_t auto_collection_paranoid_generation_gc(azone_t *azone, size_t *garbage_count, vm_address_t **garbage, void *collection_context) 
{
    boolean_t ok;
     
    size_t fullCount, generationalCount;
    vm_address_t *fullGarbage, *generationalGarbage;
    size_t i, j;
    
    *garbage_count = 0;
    *garbage = NULL;
    
    ok = auto_collection_generation_gc(azone, &generationalCount, &generationalGarbage, collection_context);
    
    if (!ok) {
        // generational gc failed
        return false;
    }

    // Skip full gc if there is no generational garbage
    if (generationalCount == 0) {
        return true;
    }
    
    // create a snapshot of the generational garbage list
    Auto::PointerList generationalSnapshot;
    generationalSnapshot.grow(generationalCount);
    vm_copy(mach_task_self(), (vm_address_t)generationalGarbage, generationalSnapshot.size(), (vm_address_t)generationalSnapshot.buffer());
    generationalGarbage = generationalSnapshot.buffer();

    // need to clear marks before next gc
    
    agc_zone_collection_cleanup(azone);
    
    boolean_t paranoid_failure = false, released_locks = false;
    ok = auto_collection_full_gc(azone, &fullCount, &fullGarbage, collection_context);
    if (!ok) {
        // full gc failed - leave generational garbage intact
        // fixme is there more cleanup to do here?
        *garbage_count = fullCount;
        *garbage = fullGarbage;
        return true;
    }
    
    malloc_printf("%s: paranoid generational: %d gen garbage, %d full garbage\n", auto_prelude(), generationalCount, fullCount);
    
    for (i = 0; i < generationalCount; i++) {
         vm_address_t gaddress = generationalGarbage[i];
         for (j = 0; j < fullCount; j++) {
            if (fullGarbage[j] == gaddress) break;
         }
         if (j >= fullCount) {
             if (!released_locks) {
                 // release all relevant locks so debugging will work better.
                 auto_unlock(azone);
                 // FIXME:  pthread_mutex_unlock(&azone->gc_lock);
                 released_locks = true;
             }
             if (azone->control.name_for_address) {
                 char *gaddress_name = azone->control.name_for_address((auto_zone_t *)azone, gaddress, 0);
                 malloc_printf("%s: PARANOID FAILURE: %s(%p) (index %d)\n", auto_prelude(), gaddress_name, gaddress, i);
                 free(gaddress_name);
             } else {
                 malloc_printf("%s: PARANOID FAILURE: address %p (index %d)\n", auto_prelude(), gaddress, i);
             }
             paranoid_failure = true;
         }
    }

    // Stop for debugging if something was amiss
#ifdef __ppc__
    if (paranoid_failure) asm("trap");
#endif
    
    if (paranoid_failure) {
        // reaquire locks after debugging.
        // FIXME:  pthread_mutex_lock(&azone->gc_lock);
        auto_unlock(azone);
        
        return false;
    }
    
    // otherwise, return the full garbage list.
    
    *garbage_count = fullCount;
    *garbage = fullGarbage;
    
    return true;
}

signed auto_collection_gc(azone_t *azone, size_t *garbage_count, vm_address_t **garbage, boolean_t generational, void *collection_context) {
    boolean_t ok;

    // Find the garbage
    if (!generational) {
        ok = auto_collection_full_gc(azone, garbage_count, garbage, collection_context);
    } else if (!azone->control.paranoid_generational) {
        ok = auto_collection_generation_gc(azone, garbage_count, garbage, collection_context);
    } else {
        ok = auto_collection_paranoid_generation_gc(azone, garbage_count, garbage, collection_context);
    }

    if (ok) return AUTO_COLLECTION_STATUS_OK;
    else    return AUTO_COLLECTION_STATUS_INTERRUPT;
}

