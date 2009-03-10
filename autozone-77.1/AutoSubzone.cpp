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

#include "AutoSubzone.h"


namespace Auto {

    //----- Subzone -----//
    
        const unsigned char Subzone::age_map[16] = { 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 4, 5 };
        const unsigned char Subzone::ref_map[16] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1 };
        const unsigned char Subzone::next_age_map[16] = {
            r0_a0, r0_a0, r0_a1, r0_a2,
            r1_a0, r1_a0, r1_a1, r1_a2,
            r2_a0, r2_a0, r2_a1, r2_a2,
            r0_a3, r0_a4,
            r1_a3, r1_a4,
        };
        const unsigned char Subzone::incr_refcount_map[16] = {
            r1_a0, r1_a1, r1_a2, r1_a3, // refcount 0 -> refcount 1
            r2_a0, r2_a1, r2_a2, r2_a3, // refcount 1 -> refcount 2
            0xff, 0xff, 0xff, 0xff,     // not used
            r1_a4, r1_a5,               // refcount 0 -> refcount 1
            r2_a3, r2_a3,               // refcount 1 -> refcount 2 (note (a4,a5)->a3 transition tho)
        };
        const unsigned char Subzone::decr_refcount_map[16] = {
            0xff, 0xff, 0xff, 0xff,     // not used
            r0_a0, r0_a1, r0_a2, r0_a3, // refcount 1 -> refcount 0
            r1_a0, r1_a1, r1_a2, r1_a5, // refcount 2 -> refcount 1  (note r2_a3 -> r1_a5)
            0xff, 0xff,                 // not used
            r0_a4, r0_a5,               // refcount 1 -> refcount 0
        };
};

