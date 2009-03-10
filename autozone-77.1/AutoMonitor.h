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
#ifndef __AUTO_MONITOR__
#define __AUTO_MONITOR__

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <CoreFoundation/CFMessagePort.h>
#include <CoreFoundation/CFData.h>

#include "AutoDefs.h"

extern "C" {
    struct ptr_set;
}

namespace Auto {

    // Forward declarations.
    class Admin;
    class Large;
    class Subzone;
    class Zone;
    
    //----- Monitor -----//
    
    //
    // Handles requests from an external memory debugging tool.
    //

    class Monitor : public AuxAllocated {
    
      private:
      
        enum {
            request_size    = 256,                          // size of the request line buffer
            max_args        = 16,                           // maximum number of request args
        };
      
        static Monitor *_monitor;                           // current monitor
                                                            // non-NULL if the monitor has been initialized

        static int (*_class_list)(void **buffer, int count);// class lister.
        static ptr_set *_class_set;                         // known classes.
        static int _class_count;                            // known class count.

        char      _request[request_size];                   // current request
        CFMessagePortRef _request_port;                     // message port for requests.
        CFMutableDataRef _response_buffer;                  // message port response buffer.
        int       _argc;                                    // argument count
        char      *_args[max_args];                         // array of argument strings
        void *_stack_bottom;                                // monitor stack bottom.
       
      public:
      
        //
        // monitor
        //
        // Get monitor.  Start if not running.
        //
        static Monitor *monitor();

        static void set_class_list(int (*class_list)(void **buffer, int count));
        static bool is_object(void *ptr, long size);
        
        //
        // Constructor
        //
        Monitor();
        
        
        //
        // open_mach_port()
        //
        // Opens the mach communication port.
        //
        void open_mach_port();
        

        //
        // print
        //
        // Formatted print to response buffer.
        //
        void print(const char *fmt, ...);
        
        
        //
        // tokenize_args
        //
        // Scan the request generating an array of arguments.
        // 
        void tokenize_args();
        
        
        //
        // process_request
        //
        // Generate a report based on the supplied request.
        // XXX_JML number of commands is currently small and performance is not an issue
        // XXX_JML later might want to be table driven.
        //
        void process_request();

        //
        // receive_request
        //
        // CFMessagePort function for handling monitor requests.
        //
        static CFDataRef receive_request(CFMessagePortRef local, SInt32 msgid, CFDataRef data, void *info);
        
        
        //
        // send_all_blocks
        //
        // Sends all block information to monitor.
        //
        void send_all_blocks();
        
        //
        // send_root_blocks
        //
        // Send all blocks that are scanned, have refcount > 0 or are referenced by the GC's internal
        // root table.
        // 
        void send_root_blocks();
        
        //
        // send_block_info
        //
        // Send specific information about block
        //
        void send_block_info(Zone *zone, void *block);
        void send_block_info(Zone *zone, Subzone *subzone, usword_t q, void *block);
        void send_block_info(Zone *zone, Large *large, void *block);
        
        //
        // send_malloc_block_info
        //
        // Send fake information about a malloc block.
        //
        void send_malloc_block_info(void *block, size_t size);
        
        
        //
        // send_block_content
        //
        // Send the content of a block to monitor.
        //
        void send_block_content();

        //
        // send_block_description
        //
        // Send a low-level description of the block, if it's owned by our zone, and known to be
        // an object. If so, then we call CFCopyDescription() on the object, escape the resulting
        // string so it can be represented in an XML-plist, and return it.
        //
        void send_block_description();

        
        //
        // send_block
        //
        // Send details of a block
        //
        void send_block(Zone *zone, Subzone *subzone, usword_t q, void *block);
        void send_block(Zone *zone, Large *large, void *block);
        
        
        //
        // send_all_zones
        //
        // Send addresses of all zones.
        //
        void send_all_zones();
        
        
        //
        // send_leaks, scan_leaks
        //
        // Send all blocks that are unreferenced but have a retain count.
        //
        void send_leaks();
        
        
        //
        // send_references
        //
        // Send all reference information for the specified block.
        //
        void send_references();


        //
        // send_roots
        //
        // Send all root information for the specified block.
        //
        void send_roots();
        
        
        //
        // send_zone_samples
        //
        // Send sample of statistics for the specified zone.
        //
        void send_zone_samples();
        
        
        //
        // send_process_samples
        //
        // Send sample of statistics for the entire process.
        //
        void send_process_samples();
    };

};

#endif // __AUTO_MONITOR__

