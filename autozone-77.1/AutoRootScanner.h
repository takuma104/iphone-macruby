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

#include "AutoMemoryScanner.h"
#include "AutoZone.h"

namespace Auto {
    struct ReferenceNode : Range {
        RangeList _incoming;
        RangeList _outgoing;
        
        enum Kind { HEAP, ROOT, STACK };
        Kind _kind;
        
        // used by shortest path algorithm, ReferenceGraph::findPath() below.
        bool _visited;
        ReferenceNode* _parent;
        ReferenceNode* _next;
        
        ReferenceNode() : _kind(HEAP), _visited(false), _parent(NULL), _next(NULL) {}
        
        void pointsFrom(void *address, usword_t offset) {
            _incoming.add(Range(address, offset));
        }
        
        void pointsTo(void *address, usword_t offset) {
            _outgoing.add(Range(address, offset));
        }
        
        usword_t offsetOf(ReferenceNode *node) {
            if (node != NULL) {
                usword_t count = _outgoing.length();
                for (usword_t i = 0; i < count; ++i) {
                    if (node->address() == _outgoing[i].address())
                        return _outgoing[i].size();
                }
            }
            return 0;
        }
    };
    
    struct ReferenceNodeQueue {
        ReferenceNode *_head;
        ReferenceNode *_tail;
        
        ReferenceNodeQueue() : _head(NULL), _tail(NULL) {}
        
        void enqueue(ReferenceNode *node) {
            node->_next = NULL;
            if (_tail == NULL)
                _head = _tail = node;
            else {
                _tail->_next = node;
                _tail = node;
            }
        }
        
        ReferenceNode *deque() {
            ReferenceNode *node = _head;
            if (_head != NULL) {
                _head = _head->_next;
                if (_head == NULL)
                    _tail = NULL;
            }
            return node;
        }
        
        bool empty() {
            return _head == NULL;
        }
    };
    
    struct ReferenceGraph {
        HashList<ReferenceNode> _nodes;
        
        ReferenceGraph() : _nodes() {}
        
        bool contains(void *block) {
            return _nodes.find(block) != NULL;
        }
        
        ReferenceNode *addNode(const Range& range) {
            return _nodes.addRange(range);
        }
        
        ReferenceNode* addNode(void *block, usword_t size) {
            return _nodes.addRange(Range(block, size));
        }
        
        void removeNode(ReferenceNode *node) {
            _nodes.remove(node);
        }
        
        ReferenceNode *node(void *block) {
            return _nodes.find(block);
        }
        
        // performs a bread-first-search traversal starting at the from node, until the to node is reached, and returns the path.
        bool findPath(void *from, void *to, List<ReferenceNode*>& path) {
            ReferenceNodeQueue queue;
            ReferenceNode *node = _nodes.find(from);
            node->_visited = true;
            queue.enqueue(node);
            while (!queue.empty()) {
                node = queue.deque();
                usword_t count = node->_outgoing.length();
                for (usword_t i = 0; i < count; ++i) {
                    ReferenceNode *child = _nodes.find(node->_outgoing[i].address());
                    if (!child->_visited) {
                        child->_visited = true;
                        child->_parent = node;
                        if (child->address() == to) {
                            while (child != NULL) {
                                path.add(child);
                                child = child->_parent;
                            }
                            return true;
                        }
                        queue.enqueue(child);
                    }
                }
            }
            return false;
        }
        
        void resetNodes() {
            usword_t count = _nodes.length();
            for (usword_t i = 0; i < count; ++i) {
                ReferenceNode& node = _nodes[i];
                node._visited = false;
                node._parent = node._next = NULL;
            }
        }
    };

    class RootScanner : public MemoryScanner {
    protected:
        void    *_block;                                    // current block to find
        int     _first_register;                            // current first register or -1 if not registers
        RangeList _thread_ranges;                           // thread stacks we're scanning.
        ReferenceGraph _graph;                              // graph we are building.
        List<void*> _block_stack;                           // blocks whose successors we still need to find.
    
    public:
        RootScanner(Zone *zone, void *block, void* stack_bottom)
            : MemoryScanner(zone, stack_bottom, false, true),
              _block(block), _first_register(-1), _thread_ranges(), _graph(), _block_stack()
        {
            _graph.addNode(block, zone->block_size(block));
        }
        
        bool on_thread_stack(void *address, Range &range) {
            usword_t count = _thread_ranges.length();
            for (usword_t i = 0; i < count; ++i) {
                if (_thread_ranges[i].in_range(address)) {
                    range = _thread_ranges[i];
                    return true;
                }
            }
            return false;
        }
        
        virtual void check_block(void **reference, void *block) {
            MemoryScanner::check_block(reference, block);
            
            if (block == _block && reference != NULL) {
                Range thread_range;
                if (!on_thread_stack(reference, thread_range)) {
                    // heap scan in progress.
                    void *owner = _zone->block_start((void*)reference);
                    if (owner) {
                        intptr_t offset = (intptr_t)reference - (intptr_t)owner;
                        if (!_graph.contains(owner)) {
                            ReferenceNode *ownerNode = _graph.addNode(owner, _zone->block_size(owner));
                            ownerNode->pointsTo(block, offset);
                            _block_stack.push(owner);
                            ReferenceNode *blockNode = _graph.node(block);
                            blockNode->pointsFrom(owner, offset);
                        }
                    } else if (_zone->is_root(reference)) {
                        if (!_graph.contains(reference)) {
                            ReferenceNode *referenceNode = _graph.addNode(reference, sizeof(void**));
                            referenceNode->_kind = ReferenceNode::ROOT;
                            referenceNode->pointsTo(block, 0);
                            // note the root reference in the graph.
                            ReferenceNode *blockNode = _graph.node(block);
                            blockNode->pointsFrom(reference, 0);
                        }
                    }
                } else if (!_graph.contains(reference)) {
                    // thread scan in progress.
                    ReferenceNode *referenceNode = _graph.addNode(Range(reference, thread_range.end()));
                    referenceNode->_kind = ReferenceNode::STACK;
                    referenceNode->pointsTo(block, 0);
                    ReferenceNode *blockNode = _graph.node(block);
                    blockNode->pointsFrom(referenceNode->address(), 0); // really offset
                }
            }
        }
        
        
        void scan_range_from_thread(Range &range, Thread *thread) {
            _thread_ranges.add(range);
            MemoryScanner::scan_range_from_thread(range, thread);
        }
        
        
        void scan_range_from_registers(Range &range, Thread *thread, int first_register) {
            _first_register = first_register;
            MemoryScanner::scan_range_from_registers(range, thread, first_register);
            _first_register = -1;
        }
        
        bool has_pending_blocks() {
            if (!_block_stack.is_empty()) {
                _block = _block_stack.pop();
                return true;
            }
            return false;
        }
    };
}
