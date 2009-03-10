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
#ifndef __AUTO_ADMINITERATOR__
#define __AUTO_ADMINITERATOR__


#include "AutoAdmin.h"
#include "AutoDefs.h"
#include "AutoListTypes.h"
#include "AutoRegion.h"
#include "AutoZone.h"


namespace Auto {

    //----- AdminIterator -----//
    
    //
    // Visit all the admins.
    //
    
    template<class T>class AdminIterator {
    
      private:
        
        Zone *_zone;                                        // zone containing admins
        T    &_visitor;                                     // object visiting admins
        
      public:
      
        //
        // Constructor
        //
        AdminIterator(Zone *zone, T &visitor) : _zone(zone), _visitor(visitor) {}
      
        inline bool visit() {
            // iterate through the regions first
            RegionList &regions = _zone->regions();
            
            for (usword_t i = 0; i < regions.length(); i++) {
                // get next region
                Region *region = regions[i];
                
                // check the small admin
                if (!_visitor.visit(_zone, region, region->small_admin())) return false;
 
                // check the medium admin
                if (!_visitor.visit(_zone, region, region->medium_admin())) return false;
            }

            // check the large admin if there are large blocks
            if (!_visitor.visit(_zone, NULL, _zone->large_admin())) return false;
            
            return true;
        }
        
        
    };


};

#endif // __AUTO_ADMINITERATOR__

