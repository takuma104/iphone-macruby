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
#ifndef __AUTO_LISTTYPES__
#define __AUTO_LISTTYPES__


#include "AutoDefs.h"
#include "AutoHashList.h"
#include "AutoLarge.h"
#include "AutoList.h"
#include "AutoRange.h"


namespace Auto {

    //
    // Forward declarations
    //
    class Region;
    class Zone;
    
    
    //
    // List Types
    //
    typedef List<Range>              RangeList;
    typedef HashList<Range>          RangeHashList;
    typedef List<Region *>           RegionList;
    typedef List<vm_address_t>       VMAddressList;
    typedef List<Zone *>             ZoneList;
    
};

#endif // __AUTO_LISTTYPES__

