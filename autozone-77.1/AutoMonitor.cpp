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

#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdarg>
#include <cerrno>
#include <cstdlib>
#include <malloc/malloc.h>
#include "CFSoftLinking.h"

#include "AutoBlockIterator.h"
#include "AutoDefs.h"
#include "AutoEnvironment.h"
#include "AutoMonitor.h"
#include "AutoMemoryScanner.h"
#include "AutoRootScanner.h"
#include "AutoZone.h"
#include "auto_zone.h"

namespace Auto {

    //----- <plist> constants.
    
    const char* kPropertyListXMLHeader = "<plist version=\"1.0\">\n<dict>\n";
    const char* kPropertyListXMLFooter = "</dict>\n</plist>\n";
    
    //----- Monitor -----//
    
    Monitor *Monitor::_monitor;                             // current monitor

    //
    // monitor
    //
    // Get monitor.  Start if not running.
    //
    Monitor *Monitor::monitor() {
        if (Environment::_agc_env._enable_monitor && !_monitor) {
            _monitor = new Monitor();
        }
        
        return _monitor;
    }

    int (*Monitor::_class_list)(void **buffer, int count);
    ptr_set *Monitor::_class_set;
    int Monitor::_class_count;
    
    void Monitor::set_class_list(int (*class_list)(void **buffer, int count)) {
        _class_list = class_list;
        _class_set = ptr_set_new();
        _class_count = 0;
    }
    
    // XXX_PCB:  lifted from <objc/objc-class.h>:
    struct objc_class_header {			
        struct objc_class *isa;	
        struct objc_class *super_class;	
        const char *name;		
        long version;
        long info;
        long instance_size;
    };
    
    bool Monitor::is_object(void *ptr, long size) {
        if (_class_list) {
            int count = _class_list(NULL, 0);
            if (count > _class_count) {
                void **buffer = (void**) aux_malloc(count * sizeof(void*));
                int new_count = _class_list(buffer, count);
                while (new_count > count) {
                    count = new_count;
                    buffer = (void**) aux_realloc(buffer, count * sizeof(void*));
                    new_count = _class_list(buffer, count);
                }
                _class_count = count;
                for (int i = 0; i < count; i++) ptr_set_add(_class_set, buffer[i]);
                aux_free(buffer);
            }
            // XXX_PCB shouldn't be hard coding this!
            objc_class_header *isa = *(objc_class_header**)ptr;
            return isa && ptr_set_is_member(_class_set, isa) && (size >= isa->instance_size);
        }
        return false;
    }
    
    //
    // Constructor
    //
    Monitor::Monitor() : _argc(0) {
        // prime the timer
        nano_time();
    }
    
    //
    // open_mach_port()
    //
    // Opens the mach communication port.
    //
    void Monitor::open_mach_port() {
        // XXX_PCB:  this is quite skanky, using CF from here. we expect to be called at the earliest when a GC thread is registered.
        CFStringRef format = CFStringCreateWithCString(NULL, "com.apple.auto.%d", kCFStringEncodingUTF8);
        CFStringRef name = CFStringCreateWithFormat(NULL, NULL, format, getpid());
        CFRelease(format);
        CFMessagePortContext context = { 0, this, NULL, NULL, NULL };
        _request_port = CFMessagePortCreateLocal(NULL, name, receive_request, &context, NULL);
        CFRelease(name);
        CFRunLoopSourceRef source = CFMessagePortCreateRunLoopSource(NULL, _request_port, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopCommonModes);
        CFRelease(source);
        CFRunLoopRun();
    }
    
    //
    // print
    //
    // Formatted print to response pipe.
    //
    void Monitor::print(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        //XXX_JML needs a lock
        // XXX_PCB not if it's run off the main runloop.
        char *buffer;
        int length = vasprintf(&buffer, fmt, ap);
        if (buffer) {
            CFDataAppendBytes(_response_buffer, (const UInt8*)buffer, length);
            free(buffer);
        }
        va_end(ap);
    }


    //
    // tokenize_args
    //
    // Scan the request generating an array of arguments.
    // 
    void Monitor::tokenize_args() {
        char *cursor = _request;
        
        for (_argc = 0; *cursor && _argc < max_args; _argc++) {
            // skip spaces
            while (*cursor <= ' ' && *cursor != '\0')  *cursor++ = '\0';
            // exit if no args
            if (*cursor == '\0') break;
            
            // scan over arg characters
            if (*cursor == '\"') {
                cursor++;
                _args[_argc] = cursor;
                while (*cursor >= ' ') {
                    if (*cursor == '\"') {
                        *cursor++ = '\0';
                        break;
                    }
                    
                    cursor++;
                }
            } else {
                // record arg position
                _args[_argc] = cursor;
                while (*cursor > ' ') cursor++;
            }
        }
    }
    
    
    //
    // process_request
    //
    // Generate a report based on the supplied request.
    // XXX_JML number of commands is currently small and performance is not an issue
    // XXX_JML later might want to be table driven.
    // XXX_JML request's firs arg is object to respond.
    //
    void Monitor::process_request() {
        if (_argc > 0) {
            // get the command
            char *command = _args[0];
            if (is_equal(command, "blocks"))     { send_all_blocks();           return; }
            if (is_equal(command, "rootblocks")) { send_root_blocks();          return; }
            if (is_equal(command, "content"))    { send_block_content();        return; }
            if (is_equal(command, "describe"))   { send_block_description();    return; }
            if (is_equal(command, "leaks"))      { send_leaks();                return; }
            if (is_equal(command, "references")) { send_references();           return; }
            if (is_equal(command, "roots"))      { send_roots();                return; }
            if (is_equal(command, "samples"))    { send_zone_samples();         return; }
            if (is_equal(command, "samplesAll")) { send_process_samples();      return; }
            if (is_equal(command, "zones"))      { send_all_zones();            return; }
            print("Unknown command %s\n", command);
        }
    }
    
    
    CFDataRef Monitor::receive_request(CFMessagePortRef local, SInt32 msgid, CFDataRef data, void *info) {
        Monitor *monitor = (Monitor *)info;
        CFIndex length = CFDataGetLength(data);
        memcpy(monitor->_request, CFDataGetBytePtr(data), length);

        CFDataRef reply = monitor->_response_buffer = CFDataCreateMutable(kCFAllocatorMallocZone, 0);
        
        // sample the stack.
        monitor->_stack_bottom = (void*) auto_get_sp();
        
        // scan args
        monitor->tokenize_args();
        
        // prevent the collector from collecting until we've processed the request.
        Zone *zone = Zone::zone();
        if (zone) zone->block_collector();

        // process request generating report
        monitor->process_request();
        
        // unblock the collector.
        if (zone) zone->unblock_collector();

        // return the response.
        CFDataAppendBytes(monitor->_response_buffer, (const UInt8*)"\0", 1);
        monitor->_response_buffer = NULL;
        return reply;
    }


    //
    // send_all_blocks
    //
    // Sends all block information to monitor.
    //
    struct send_all_blocks_visitor {
        Monitor *_monitor;
        
        // Constructor
        send_all_blocks_visitor(Monitor *monitor) : _monitor(monitor) {}
        
        // visitor function for subzone
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q) {
            // send single block information
            _monitor->send_block(zone, subzone, q, subzone->quantum_address(q));
            // always continue
            return true;
        }
        
        // visitor function for large
        inline bool visit(Zone *zone, Large *large) {
            // send single block information
            _monitor->send_block(zone, large, large->address());
            // always continue
            return true;
        }
    };
    
    static void malloc_block_recorder(task_t task, void *context, unsigned type, vm_range_t *range, unsigned count) {
        Monitor *monitor = reinterpret_cast<Monitor*>(context);
        for (unsigned i = 0; i < count; i++, range++) {
            monitor->print("block");
            monitor->send_malloc_block_info((void*)range->address, range->size);
            monitor->print("\n");
        }
    }
    
    void Monitor::send_all_blocks() {
        malloc_zone_t *zone = (malloc_zone_t *)strtoul(_args[2], NULL, 0);
        print("blocks %s\n", _args[1]);
        if (zone == (malloc_zone_t *)Zone::zone()) {
            send_all_blocks_visitor visitor(this);
            visitAllocatedBlocks(reinterpret_cast<Zone*>(zone), visitor);
        } else {
            // use the malloc_zone API for enumerating blocks.
            zone->introspect->enumerator(mach_task_self(), this, MALLOC_PTR_IN_USE_RANGE_TYPE, (vm_address_t) zone, NULL, malloc_block_recorder);
        }
        print("\\blocks\n");
    }
    
    //
    // send_block_info
    //
    // Send specific information about block
    //
    void Monitor::send_block_info(Zone *zone, void *block) {
        if (zone->in_subzone_memory(block)) {
            Subzone *subzone = Subzone::subzone(block);
            return send_block_info(zone, subzone, subzone->quantum_index(block), block);
        } else if (zone->in_large_memory(block)) {
            return send_block_info(zone, Large::large(block), block);
        } else {
            ASSERTION(0 && "not a block");
        }
    }
    void Monitor::send_block_info(Zone *zone, Subzone *subzone, usword_t q, void *block) {
        // print block info
        int rc = zone->block_refcount(block);
        int layout = subzone->layout(q);
        bool is_unscanned = (layout & AUTO_UNSCANNED) != 0;
        bool is_object = (layout & AUTO_OBJECT) != 0;
        bool is_new = subzone->is_new(q);
        bool is_marked = subzone->is_marked(q);
        char *class_name = NULL;
        if (is_object) {
            class_name = zone->control.name_for_address((auto_zone_t *)zone, (vm_address_t)block, 0);
        }
        print(" %p %lu %d %s%s%s%s %s",
                           block, (unsigned long)subzone->size(q),
                           rc,
                           is_unscanned ? "u" : "s",
                           is_object    ? "o" : "m",
                           is_new       ? "n" : "o",
                           is_marked    ? "m" : "u",
                           class_name   ? class_name : "");
        if (class_name) free(class_name);
    }
    void Monitor::send_block_info(Zone *zone, Large *large, void *block) {
        // print block info
        int rc = zone->block_refcount(block);
        int layout = large->layout();
        bool is_unscanned = (layout & AUTO_UNSCANNED) != 0;
        bool is_object = (layout & AUTO_OBJECT) != 0;
        bool is_new = large->is_new();
        bool is_marked = large->is_marked();
        char *class_name = NULL;
        if (is_object) {
            class_name = zone->control.name_for_address((auto_zone_t *)zone, (vm_address_t)block, 0);
        }
        print(" %p %lu %d %s%s%s%s %s",
                           block, (unsigned long)large->size(),
                           rc,
                           is_unscanned ? "u" : "s",
                           is_object    ? "o" : "m",
                           is_new       ? "n" : "o",
                           is_marked    ? "m" : "u",
                           class_name   ? class_name : "");
        if (class_name) free(class_name);
    }
    
    //
    // send_malloc_block_info
    //
    // Send fake information about a malloc block.
    //
    void Monitor::send_malloc_block_info(void *block, size_t size) {
        int rc = 1;
        int layout = AUTO_MEMORY_UNSCANNED;
        bool is_unscanned = (layout & AUTO_UNSCANNED) != 0;
        bool is_new = false;
        bool is_marked = false;
        const char *class_name = "";
        bool is_object = Monitor::is_object(block, size);
        if (is_object) class_name = (*(objc_class_header**)block)->name;
        print(" %p %lu %d %s%s%s%s %s",
              block, size,
              rc,
              is_unscanned ? "u" : "s",
              is_object    ? "o" : "m",
              is_new       ? "n" : "o",
              is_marked    ? "m" : "u",
              class_name);
    }
    
    //
    // send_block_content
    //
    // Send the content of a block to monitor.
    //
    void Monitor::send_block_content() {
        // get zone
        malloc_zone_t *zone = (malloc_zone_t *)strtoul(_args[2], NULL, 0);
        // get block
        void *block = (void *)strtoul(_args[3], NULL, 0);
        print("content %s\n", _args[1]);
        if (zone == (malloc_zone_t *)Zone::zone()) {
            Zone *azone = (Zone *)zone;
            if (azone->is_block(block)) {
                usword_t size = azone->block_size(block);
                for (usword_t offset = 0; offset < size; offset += sizeof(void *)) {
                    intptr_t *slot = (intptr_t *)displace(block, offset);
                    intptr_t content = *slot;
                    print("slot %p %lu %p", slot, offset, content);
                    if (azone->is_block((void *)content)) {
                        send_block_info(azone, (void *)content);
                    } else {
                        // room for comment
                    }
                    
                    print("\n");
                }
            }
        } else {
            // it's a malloc_zone_t block of some kind.
            size_t size = malloc_size(block);
            if (size != 0) {
                for (usword_t offset = 0; offset < size; offset += sizeof(void *)) {
                    intptr_t *slot = (intptr_t *)displace(block, offset);
                    intptr_t content = *slot;
                    print("slot %p %lu %p", slot, offset, content);
                    size_t content_size = malloc_size((void *)content);
                    if (content_size) {
                        send_malloc_block_info((void *)content, content_size);
                    } else {
                        // room for comment
                    }
                    
                    print("\n");
                }
            }
        }
        print("\\content\n");
    }
    
    //
    // send_block_description
    //
    // Send a low-level description of the block, if it's owned by our zone, and known to be
    // an object. If so, then we call CFCopyDescription() on the object, escape the resulting
    // string so it can be represented in an XML-plist, and return it.
    //
    
    void Monitor::send_block_description() {
        print(kPropertyListXMLHeader);
        // get zone
        malloc_zone_t *zone = (malloc_zone_t *)strtoul(_args[2], NULL, 0);
        // get block
        void *block = (void *)strtoul(_args[3], NULL, 0);
        print("<key>requestor</key><string>%s</string>\n", _args[1]);
        print("<key>block</key><string>%s</string>\n", _args[3]);
        if (zone == (malloc_zone_t *)Zone::zone()) {
            Zone *azone = (Zone *)zone;
            if (azone->is_block(block)) {
                auto_memory_type_t type = auto_zone_get_layout_type(zone, block);
                if ((type & AUTO_OBJECT) == AUTO_OBJECT) {
                    // Use CFCopyDescription() rather than sending the object a -description message, because the latter may
                    // crash when containers contain non-object values.
                    CFStringRef description = CFCopyDescription((CFTypeRef)block);
                    if (description) {
                        CFStringRef escaped = CFXMLCreateStringByEscapingEntities(NULL, description, NULL);
                        if (escaped != description) {
                            CFRelease(description);
                            description = escaped;
                        }
                        char buffer[CFStringGetMaximumSizeForEncoding(CFStringGetLength(description), kCFStringEncodingUTF8)];
                        CFStringGetCString(description, buffer, sizeof(buffer), kCFStringEncodingUTF8);
                        CFRelease(description);
                        print("<key>description</key><string>%s</string>", buffer);
                    }
                }
            }
        }
        print(kPropertyListXMLFooter);
    }

    //
    // send_block
    //
    // Send details of a block
    //
    void Monitor::send_block(Zone *zone, Subzone *subzone, usword_t q, void *block) {
        print("block");
        send_block_info(zone, subzone, q, block);
        print("\n");
    }
    void Monitor::send_block(Zone *zone, Large *large, void *block) {
        print("block");
        send_block_info(zone, large, block);
        print("\n");
    }
    
    
    //
    // send_all_zones
    //
    // Send addresses of all zones.
    //
    void Monitor::send_all_zones() {
        Zone *zone = Zone::zone();
        
        print("zones %s\n", _args[1]);
        if (zone) {
            print("zone %p %p %s\n", zone, zone, malloc_get_zone_name((malloc_zone_t *)zone));
        }
        
        vm_address_t *zone_addresses;
        unsigned count = 0;
        
        malloc_get_all_zones(mach_task_self(), NULL, &zone_addresses, &count);
        
        for (unsigned i = 0; i < count; i++) {
            malloc_zone_t *malloc_zone = (malloc_zone_t *)zone_addresses[i];
            if (malloc_zone != (malloc_zone_t *)zone) print("zone %p 0x00000000 \"%s\"\n", malloc_zone, malloc_get_zone_name(malloc_zone));
        }

        print("\\zones\n");
    }
	
    //
    // send_leaks
    //
    // Send all blocks that are unreferenced but have a retain count.
    //
    struct LeakScanner : public MemoryScanner {
        LeakScanner(Zone *zone, void *stack_bottom)
        : MemoryScanner(zone, stack_bottom, false, false)
        {}
    
        virtual void scan_retained_blocks() {
            // ignored retained blocks for this scan.
        }
    };
    struct send_leaks_visitor {
        Monitor *_monitor;
        
        // Constructor
        send_leaks_visitor(Monitor *monitor) : _monitor(monitor) {}
        
        // visitor function for subzone
        inline bool visit(Zone *zone, Subzone *subzone, usword_t q) {
            // check block
            if (!subzone->is_marked(q) && subzone->has_refcount(q)) {
                _monitor->send_block(zone, subzone, q, subzone->quantum_address(q));
            }
            // always continue
            return true;
        }
        
        // visitor function for large
        inline bool visit(Zone *zone, Large *large) {
            // check block
            if (!large->is_marked() && large->refcount()) {
                _monitor->send_block(zone, large, large->address());
            }
            // always continue
            return true;
        }
    };
    void Monitor::send_leaks() {
        // get zone
        Zone *zone = (Zone *)strtoul(_args[2], NULL, 0);
        // run a FULL collection first.
        // auto_collect((auto_zone_t *)zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
        // scan from threads
        LeakScanner scanner(zone, _stack_bottom);
        scanner.scan();
        // send response
        print("leaks %s\n", _args[1]);
        send_leaks_visitor visitor(this);
        BlockIterator<send_leaks_visitor> iterator(zone, visitor);
        iterator.visit();
        print("\\leaks\n");
        // clear marks
        zone->reset_all_marks_and_pending();
    }
    
    //
    // send_references
    //
    // Send all reference information for the specified block.
    //
    struct ReferenceScanner : public MemoryScanner {
        Monitor *_monitor;                                  // requesting monitor
        void    *_block;                                    // block to find
        Thread  *_thread;                                   // current thread or NULL if not thread
        int     _first_register;                            // current first register or -1 if not registers
        Range   _thread_range;                              // current thread range
        
        ReferenceScanner(Zone *zone, void *block, Monitor *monitor, void* stack_bottom)
        : MemoryScanner(zone, stack_bottom, false, true)
        , _monitor(monitor)
        , _block(block)
        , _thread(NULL)
        , _first_register(-1)
        , _thread_range()
        {
        }
        
        virtual void check_block(void **reference, void *block) {
            MemoryScanner::check_block(reference, block);
            
            if (block == _block) {
                if (_thread) {
                    // use offset BELOW stack bottom.
                    intptr_t offset = (intptr_t)reference - (intptr_t)_thread_range.end();
                    if (_first_register != -1) {
                        // XXX_JML id needs to be platform specific
                        int id = (offset >> 2) + _first_register;
                        _monitor->print("reference %p %d r %d r%d", reference, offset, id, id);
                    } else {
                        _monitor->print("reference %p %d t %p \"thread stack\"", reference, offset, _thread_range.end());
                    }
                } else if (!reference) {
                    _monitor->print("reference 0 0 z 0 \"zone retained\"");
                } else {
                    void *owner = _zone->block_start((void*)reference);
                    
                    if (owner) {
                        intptr_t offset = (intptr_t)reference - (intptr_t)owner;
                        char *referrer_name = _zone->control.name_for_address((auto_zone_t *)_zone, (vm_address_t)owner, (vm_address_t)offset);
                        _monitor->print("reference %p %d b %p %s", reference, offset, owner, referrer_name);
                        free(referrer_name);
                        _monitor->send_block_info(_zone, (void *)owner);
                    } else if (_zone->is_root(reference)) {
                        Dl_info info;
                        if (dladdr(reference, &info) != 0 && info.dli_saddr == reference)
                            _monitor->print("reference %p 0 b 0 \"global variable: %s\"", reference, info.dli_sname);
                        else
                            _monitor->print("reference %p 0 b 0 \"registered root\"", reference);
                    } else {
                        _monitor->print("reference %p 0 b 0 \"unknown container\"", reference);
                    }
                }
                
                _monitor->print("\n");
            }
        }
        
        
        void scan_range_from_thread(Range &range, Thread *thread) {
            _thread = thread;
            _thread_range = range;
            MemoryScanner::scan_range_from_thread(range, thread);
            _thread = NULL;
            _thread_range = Range();
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
    
    void Monitor::send_references() {
        // get zone
        Zone *zone = (Zone *)strtoul(_args[2], NULL, 0);
        // get block
        void *block = (void *)strtoul(_args[3], NULL, 0);
        // scan from roots
        ReferenceScanner scanner(zone, block, this, _stack_bottom);
        // send response
        print("references %s\n", _args[1]);
        // scan for references
        scanner.scan();
        // clear marks
        zone->reset_all_marks_and_pending();
        print("\\references\n");
    }
    
    
    //
    // send_roots
    //
    // Send all root information for the specified block.
    //
    struct MonitorRootScanner : public RootScanner {
        Monitor *_monitor;                                  // requesting monitor
        
        MonitorRootScanner(Zone *zone, void *block, Monitor *monitor, void* stack_bottom)
            : RootScanner(zone, block, stack_bottom), _monitor(monitor)
        {
        }
        
        void print_root(ReferenceNode *node, ReferenceNode *nextNode) {
            void *address = node->address();
            switch (node->_kind) {
            case ReferenceNode::HEAP:
                usword_t offset = node->offsetOf(nextNode);
                char *referrer_name = _zone->control.name_for_address((auto_zone_t *)_zone, (vm_address_t)address, offset);
                _monitor->print("reference %p %u b %p %s", (uintptr_t)address + offset, offset, address, referrer_name);
                free(referrer_name);
                _monitor->send_block_info(_zone, address);
                break;
            case ReferenceNode::ROOT:
                Dl_info info;
                if (dladdr(address, &info) != 0 && info.dli_saddr == address)
                    _monitor->print("reference %p 0 b 0 \"global variable: %s\"", address, info.dli_sname);
                else
                    _monitor->print("reference %p 0 b 0 \"registered root\"", address);
                break;
            case ReferenceNode::STACK:
                _monitor->print("reference %p %ld t %p \"thread stack\"", address, -(intptr_t)node->size(), node->end());
                break;
            }
            _monitor->print("\n");
        }
        
        void print_roots(void *block) {
            // print the roots of the reference graph. this means go through all the nodes, identifying the roots, which are either blocks that
            // have a retain count > 0, or registered roots. From one of these roots, perform a bread-first search.
            usword_t count = _graph._nodes.length();
            for (usword_t i = 0; i < count; ++i) {
                ReferenceNode& node = _graph._nodes[i];
                void *address = node.address();
                if (node._kind == ReferenceNode::STACK || node._kind == ReferenceNode::ROOT || (_zone->is_block(address) && _zone->block_refcount(address) > 0)) {
                    List<ReferenceNode*> path;
                    // if (node._kind == ReferenceNode::STACK) __builtin_trap();
                    if (_graph.findPath(node.address(), block, path)) {
                        // print out the path.
                        usword_t length = path.length();
                        for (usword_t j = 1; j <= length; ++j) {
                            ReferenceNode* currentNode = path[length - j];
                            ReferenceNode* nextNode = (j < length ? path[length - j - 1] : NULL);
                            print_root(currentNode, nextNode);
                        }
                        // generate a separator line.
                        _monitor->print("\n");
                    }
                    _graph.resetNodes();
                }
            }
        }
    };

    void Monitor::send_roots() {
        // get zone
        Zone *zone = (Zone *)strtoul(_args[2], NULL, 0);
        // get block
        void *block = (void *)strtoul(_args[3], NULL, 0);
        // scan from roots
        MonitorRootScanner scanner(zone, block, this, _stack_bottom);
        // try to use the scanning stack for speed.
        zone->clear_use_pending();
        ScanStack &scan_stack = zone->scan_stack();
        do {
            // scan for references
            scanner.scan();
            // clear marks
            zone->reset_all_marks();
        } while (scanner.has_pending_blocks());
        zone->set_use_pending();
        bool stack_overflow = scan_stack.is_overflow();
        scan_stack.reset();
        // send response
        print("roots %s\n", _args[1]);
        if (!stack_overflow) scanner.print_roots(block);
        print("\\roots\n");
    }

    // eventually, use a MemoryScanner sub-class (see RootScanner above for an example). We'll need this, to be able
    // to compute each root's sub-graph size.
    
    struct RootFinder : private MemoryScanner {
        Monitor *_monitor;                                  // requesting monitor
        bool _scanning_roots;
        RangeList _list;
        
        RootFinder(Zone *zone, Monitor *monitor)
            : MemoryScanner(zone, NULL, false, true),
            _monitor(monitor)
        {
        }
        
        void find() {
            _scanning_roots = true;
            scan_root_ranges();             // this scans the roots eagerly.
            _scanning_roots = false;
            scan_retained_blocks();         // this pends the retained blocks for scanning.
            scan_pending_blocks();          // this should call check_block() on all pending blocks.
        }
        
        virtual void check_block(void **reference, void *block) {
            if (_scanning_roots || _zone->block_refcount(block) != 0) {
                // only consider scanned blocks.
                auto_memory_type_t type = (auto_memory_type_t) _zone->block_layout(block);
                if ((type & AUTO_UNSCANNED) != AUTO_UNSCANNED) {
                    _list.add(Range(block, _zone->block_size(block)));
                }
            }
        }
    };
    
    struct BlockScanner : public MemoryScanner {
        Range _blockRange;
        usword_t _bytesReachable;
        usword_t _objectsReachable;

        BlockScanner(Zone *zone, Range block)
            : MemoryScanner(zone, NULL, false, true), _blockRange(block), _bytesReachable(block.size()), _objectsReachable(0)
        {
        }
        
        void scan() {
            scan_range(_blockRange);
            scan_pending_until_done();
        }
        
        virtual void check_block(void **reference, void *block) {
            _bytesReachable += _zone->block_size(block), ++_objectsReachable;
            MemoryScanner::check_block(reference, block); // to get recursive scanning.
        }
    };

    void Monitor::send_root_blocks() {
        Zone *zone = Zone::zone();
        zone->set_use_pending();
        RootFinder roots(zone, this);
        roots.find();
        print(kPropertyListXMLHeader);
        print("<key>requestor</key><string>%s</string>\n", _args[1]);
        print("<key>rootBlocks</key><dict>\n");
        for (usword_t i = 0; i < roots._list.length(); ++i) {
            BlockScanner scanner(zone, roots._list[i]);
            scanner.scan();
            _monitor->print("<key>%p</key><array><integer>%lu</integer><integer>%lu</integer></array>\n",
                            scanner._blockRange.address(), scanner._bytesReachable, scanner._objectsReachable);
        }
        zone->reset_all_marks_and_pending();
        print("</dict>\n");
        print(kPropertyListXMLFooter);
    }
    
    //
    // send_zone_samples
    //
    // Send sample of statistics for the specified zone.
    //
    void Monitor::send_zone_samples() {
        malloc_zone_t *malloc_zone = (malloc_zone_t *)strtoul(_args[2], NULL, 0);
        malloc_statistics_t stats;
        malloc_zone_statistics(malloc_zone, &stats);
        print("samples %s\n", _args[1]);
        print("sample %f %u %zu %zu %zu\n", nano_time(), stats.blocks_in_use, stats.size_in_use, stats.max_size_in_use, stats.size_allocated);
        print("\\samples\n");  
    }
    

    //
    // send_process_samples
    //
    // Send sample of statistics for the entire process.
    //
    void Monitor::send_process_samples() {
        vm_address_t *zone_addresses;
        unsigned count = 0;
        
        malloc_get_all_zones(mach_task_self(), NULL, &zone_addresses, &count);
        malloc_statistics_t stats;
        bzero(&stats, sizeof(malloc_statistics_t));
       
        for (unsigned i = 0; i < count; i++) {
            malloc_zone_t *malloc_zone = (malloc_zone_t *)zone_addresses[i];
            malloc_statistics_t zone_stats;
            malloc_zone_statistics(malloc_zone, &zone_stats);
            stats.blocks_in_use   += zone_stats.blocks_in_use;
            stats.size_in_use     += zone_stats.size_in_use;
            stats.max_size_in_use += zone_stats.max_size_in_use;
            stats.size_allocated  += zone_stats.size_allocated;;
        }

        print("samples %s\n", _args[1]);
        print("sample %f %u %zu %zu %zu\n", nano_time(), stats.blocks_in_use, stats.size_in_use, stats.max_size_in_use, stats.size_allocated);
        print("\\samples\n");  
    }
};
