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
#ifndef __AUTO_INTERFACE__
#define __AUTO_INTERFACE__

#include <sys/types.h>
#include <malloc/malloc.h>

//
// Interface from auto to agc
//

#if defined(__cplusplus)
extern "C" {
#endif




//
// Legacy Reference Tracing adapter API.
// referrer_base[referrer_offset]  ->  referent
typedef struct 
{
    vm_address_t referent;
    vm_address_t referrer_base;
    intptr_t     referrer_offset;
} agc_reference_t;

typedef void (*agc_reference_recorder_t) (void *ctx, agc_reference_t reference);

extern void agc_enumerate_references(azone_t *zone, void *referent, 
                                     agc_reference_recorder_t callback, 
                                     void *stack_bottom, void *ctx);

#if defined(__cplusplus)
};
#endif

#endif // __AUTO_INTERFACE__

