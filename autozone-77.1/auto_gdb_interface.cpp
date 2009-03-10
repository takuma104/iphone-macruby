/*
 * Copyright (c) 2007-2008 Apple Inc. All rights reserved.
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

#include <vector>
#include "auto_gdb_interface.h"
#include "AutoZone.h"
#include "AutoRootScanner.h"

namespace Auto {
    typedef std::vector<auto_memory_reference_t, AuxAllocator<auto_memory_reference_t> > RefVector;

    class GDBReferenceRecorder : public MemoryScanner {
        void    *_block;                                    // block to find
        Thread  *_thread;                                   // current thread or NULL if not thread
        Range   _thread_range;                              // current thread range
        RefVector _refs;
    
    public:
        GDBReferenceRecorder(Zone *zone, void *block, void *stack_bottom)
            : MemoryScanner(zone, stack_bottom, false, true), _block(block), _thread(NULL), _thread_range(), _refs()
        {
        }
        
        auto_memory_reference_list_t *copy_refs() {
            uint32_t count = _refs.size();
            auto_memory_reference_list_t *result = (auto_memory_reference_list_t *) aux_malloc(sizeof(auto_memory_reference_list_t) + count * sizeof(auto_memory_reference_t));
            result->count = count;
            std::copy(_refs.begin(), _refs.end(), result->references);
            return result;
        }
        
        void check_block(void **reference, void *block) {
            if (block == _block) {
                auto_memory_reference_t ref = { NULL };
                if (_thread) {
                    ref.address = _thread_range.end();
                    ref.offset = (intptr_t)reference - (intptr_t)_thread_range.end();
                    ref.kind = auto_memory_block_stack;
                } else if (reference) {
                    void *owner = zone()->block_start((void*)reference);
                    if (owner) {
                        ref.address = owner;
                        ref.offset = (intptr_t)reference - (intptr_t)owner;
                        int refcount, layout;
                        _zone->block_refcount_and_layout(block, &refcount, &layout);
                        ref.kind = (layout & AUTO_OBJECT) ? auto_memory_block_object : auto_memory_block_bytes;
                        ref.retainCount = refcount;
                    } else if (_zone->is_root((void*)reference)) {
                        ref.address = (void*)reference;
                        ref.offset = 0;
                        ref.kind = auto_memory_block_global;
                    }
                } else {
                    // why would reference ever be NULL? when block itself is a root?
                }
                if (ref.address) _refs.push_back(ref);
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
            _thread_range = range;
            MemoryScanner::scan_range_from_registers(range, thread, first_register);
            _thread = NULL;
        }
    };
    
    class GDBRootScanner : public RootScanner {
    public:
        GDBRootScanner(Zone *zone, void *block, void* stack_bottom)
            : RootScanner(zone, block, stack_bottom)
        {
        }
        
        auto_root_list_t *copy_roots(void *block) {
            auto_root_list_t *result = NULL;
            usword_t count = _graph._nodes.length();
            std::vector<RefVector, AuxAllocator<RefVector> > paths;
            for (usword_t i = 0; i < count; ++i) {
                ReferenceNode& node = _graph._nodes[i];
                void *address = node.address();
                if (node._kind == ReferenceNode::STACK || node._kind == ReferenceNode::ROOT || (_zone->is_block(address) && _zone->block_refcount(address) > 0)) {
                    List<ReferenceNode*> path;
                    if (_graph.findPath(node.address(), block, path)) {
                        usword_t last = paths.size();
                        paths.resize(last + 1);
                        RefVector &refs = paths[last];
                        usword_t length = path.length();
                        for (usword_t j = 1; j <= length; ++j) {
                            ReferenceNode* currentNode = path[length - j];
                            ReferenceNode* nextNode = (j < length ? path[length - j - 1] : NULL);
                            auto_memory_reference_t ref = { NULL };
                            switch (currentNode->_kind) {
                            case ReferenceNode::HEAP:
                                ref.address = currentNode->address();
                                ref.offset = currentNode->offsetOf(nextNode);
                                int refcount, layout;
                                _zone->block_refcount_and_layout(currentNode->address(), &refcount, &layout);
                                ref.kind = (layout & AUTO_OBJECT) ? auto_memory_block_object : auto_memory_block_bytes;
                                ref.retainCount = refcount;
                                break;
                            case ReferenceNode::ROOT:
                                ref.address = currentNode->address();
                                ref.offset = 0;
                                ref.kind = auto_memory_block_global;
                                break;
                            case ReferenceNode::STACK:
                                ref.address = currentNode->end();
                                ref.offset = -(intptr_t)currentNode->size();
                                ref.kind = auto_memory_block_stack;
                                break;
                            }
                            if (ref.address) refs.push_back(ref);
                        }
                    }
                    _graph.resetNodes();
                }
            }
            count = paths.size();
            size_t list_size = sizeof(auto_root_list_t) + count * sizeof(auto_memory_reference_list_t);
            for (usword_t i = 0; i < count; i++) list_size += paths[i].size() * sizeof(auto_memory_reference_t);
            result = (auto_root_list_t *)aux_malloc(list_size);
            result->count = count;
            auto_memory_reference_list_t *list = result->roots;
            for (usword_t i = 0; i < count; i++) {
                const RefVector &refs = paths[i];
                list->count = refs.size();
                std::copy(refs.begin(), refs.end(), list->references);
                list = (auto_memory_reference_list_t *)displace(list, sizeof(auto_root_list_t) + list->count * sizeof(auto_memory_reference_t));
            }
            return result;
        }
    };
};

auto_memory_reference_list_t *auto_gdb_enumerate_references(auto_zone_t *zone, void *address, void *stack_base) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    azone->block_collector();
    auto_memory_reference_list_t *result = NULL;
    {
        GDBReferenceRecorder recorder(azone, address, stack_base);
        recorder.scan();
        azone->reset_all_marks_and_pending();
        result = recorder.copy_refs();
    }
    azone->unblock_collector();
    return result;
}

auto_root_list_t *auto_gdb_enumerate_roots(auto_zone_t *zone, void *address, void *stack_base) {
    using namespace Auto;
    Zone *azone = (Zone *)zone;
    azone->block_collector();
    auto_root_list_t *result = NULL;
    {
        // the scan stack is broken for root scanning, because we aren't correctly tracking which thread
        // is being scanned. This happens because of pushing deferred stack ranges on the scan stack, which
        // get examined well after scan_range_from_thread() is called.
        GDBRootScanner scanner(azone, address, stack_base);
        // try to use the scanning stack for speed.
        azone->clear_use_pending();
        ScanStack &scan_stack = azone->scan_stack();
        do {
            // scan for references
            scanner.scan();
            // clear marks
            azone->reset_all_marks();
        } while (scanner.has_pending_blocks());
        azone->set_use_pending();
        bool stack_overflow = scan_stack.is_overflow();
        scan_stack.reset();
        if (!stack_overflow) result = scanner.copy_roots(address);
    }
    azone->unblock_collector();
    return result;
}
