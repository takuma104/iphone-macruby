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
#ifndef __AUTO_SUBZONEITERATOR__
#define __AUTO_SUBZONEITERATOR__


#include "AutoAdmin.h"
#include "AutoDefs.h"
#include "AutoListTypes.h"
#include "AutoRangeIterator.h"
#include "AutoRegion.h"
#include "AutoSubzone.h"
#include "AutoZone.h"

namespace Auto {

    //----- SubzoneIterator -----//
    
    //
    // Visit all the subzones.
    //
    
    template<class T>class SubzoneIterator {
    
      private:
        
        Zone *_zone;                                        // zone containing subzones
        T    &_visitor;                                     // object visiting subzones
        
      public:
      
        //
        // Constructor
        //
        SubzoneIterator(Zone *zone, T &visitor) : _zone(zone), _visitor(visitor) {}
      
        inline bool visit() {
            // iterate through the regions
            RegionList &regions = _zone->regions();
            
            for (usword_t i = 0; i < regions.length(); i++) {
                // get next region
                Region *region = regions[i];
                
                // iterate through the subzones
                SubzoneRangeIterator iterator(Region->subzone_range());
                while (Subzone *subzone = iterator.next()) {
                    // visit the subzone
                    if (!_visitor.visit(_zone, subzone)) return false;
                }
            }
            
            // hit all subzones
            return true;
        }
        
        
    };    

};

#endif // __AUTO_SUBZONEITERATOR__

