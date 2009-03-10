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
#ifndef __AUTO_WRITEBARRIERITERATOR__
#define __AUTO_WRITEBARRIERITERATOR__


#include "AutoAdmin.h"
#include "AutoDefs.h"
#include "AutoLarge.h"
#include "AutoListTypes.h"
#include "AutoRangeIterator.h"
#include "AutoRegion.h"
#include "AutoSubzone.h"
#include "AutoWriteBarrier.h"
#include "AutoZone.h"


namespace Auto {

    //----- WriteBarrierIterator -----//
    
    //
    // Visit all the write barriers.
    //

    template <class Visitor> bool visitWriteBarriers(Zone *zone, Visitor &visitor) {
        // iterate through the regions first
        for (Region *region = zone->region_list(); region != NULL; region = region->next()) {
            // iterate through the subzones
            SubzoneRangeIterator iterator(region->subzone_range());
            while (Subzone *subzone = iterator.next()) {
                 // extract the write barrier information
                WriteBarrier& wb = subzone->write_barrier();
                
                // let the visitor visit the write barrier
                if (!visitor.visit(zone, wb)) return false;
            }
        }

        // iterate through the large
        for (Large *large = zone->large_list(); large; large = large->next()) {
            // extract the write barrier information
            WriteBarrier& wb = large->write_barrier();
            
            // let the visitor visit the write barrier
            if (!visitor.visit(zone, wb)) return false;
        }
        
        return true;
    }
    
    template<class T>class WriteBarrierIterator {
    
      private:
        
        Zone *_zone;                                        // zone containing write barriers
        T    &_visitor;                                     // object visiting write barriers
        
      public:
      
        //
        // Constructor
        //
        WriteBarrierIterator(Zone *zone, T &visitor) : _zone(zone), _visitor(visitor) {}
      
        inline bool visit() {
            return visitWriteBarriers(_zone, _visitor);
        }
        
    };
};

#endif // __AUTO_WRITEBARRIERITERATOR__

