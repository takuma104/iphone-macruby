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

#include <malloc/malloc.h>

#include "AutoMonitor.h"
#include "AutoZone.h"
#include "AutoInUseEnumerator.h"
#include "AutoMemoryScanner.h"

#include "agc_interface.h"

namespace Auto {
    struct ReferenceRecorder : public MemoryScanner {
        agc_reference_recorder_t _callback;                 // callback to notify
        void    *_callback_ctx;                             // context for callback
        void    *_block;                                    // block to find
        Thread  *_thread;                                   // current thread or NULL if not thread
        int     _first_register;                            // current first register or -1 if not registers
        Range   _thread_range;                              // current thread range
        
        ReferenceRecorder(Zone *zone, void *block, agc_reference_recorder_t callback, void *stack_bottom, void *ctx)
            : MemoryScanner(zone, stack_bottom, false, true)
            , _callback(callback)
            , _callback_ctx(ctx)
            , _block(block)
            , _thread(NULL)
            , _first_register(-1)
            , _thread_range()
        {}
        
        void check_block(void **reference, void *block) {
            if (block == _block) {
                if (_thread) {
                    intptr_t offset = (intptr_t)reference - (intptr_t)_thread_range.end();
                    agc_reference_t ref = { (vm_address_t)block, (vm_address_t)_thread_range.end(), offset };
                    _callback(_callback_ctx, ref);
                } else if (!reference) {
                    // ???
                } else {
                    void *owner = zone()->block_start((void*)reference);
                    if (owner) {
                        intptr_t offset = (intptr_t)reference - (intptr_t)owner;
                        agc_reference_t ref = { (vm_address_t)block, (vm_address_t)owner, offset };
                        _callback(_callback_ctx, ref);
                    } else {
                        agc_reference_t ref = { (vm_address_t)block, (vm_address_t)reference, 0 };
                        _callback(_callback_ctx, ref);
                    }
                }
            }
            
            MemoryScanner::check_block(reference, block);
        }
        
        
        void scan_range_from_thread(Range &range, Thread *thread) {
            _thread = thread;
            _thread_range = range;
            MemoryScanner::scan_range_from_thread(range, thread);
            _thread = NULL;
        }
        
        
        void scan_range_from_registers(Range &range, Thread *thread, int first_register) {
            _thread = thread;
            _first_register = first_register;
            _thread_range = range;
            MemoryScanner::scan_range_from_registers(range, thread, first_register);
            _thread = NULL;
            _first_register = -1;
        }
    };
};

void agc_enumerate_references(azone_t *zone, void *referent, 
                              agc_reference_recorder_t callback, 
                              void *stack_bottom, void *ctx)
{
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    azone->block_collector();
    {
        ReferenceRecorder recorder(azone, referent, callback, stack_bottom, ctx);
        recorder.scan();
        azone->reset_all_marks_and_pending();
    }
    azone->unblock_collector();
}
