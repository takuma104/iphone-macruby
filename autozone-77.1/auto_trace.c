/*
 * Copyright (c) 2006-2008 Apple Inc. All rights reserved.
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

#include "auto_trace.h"

static auto_trace_collection_callouts callouts = { sizeof(auto_trace_collection_callouts), NULL, NULL };

void auto_trace_collection_set_callouts(auto_trace_collection_callouts *new_callouts)
{
    if (new_callouts->size == sizeof(auto_trace_collection_callouts)) {
        callouts = *new_callouts;
    } else {
        malloc_printf("auto_trace_collection_set_callouts() called with incompatible size (ignored)\n");
    }
}

void auto_trace_collection_begin(auto_zone_t *zone, boolean_t generational) 
{
    if (callouts.auto_trace_collection_begin)
        callouts.auto_trace_collection_begin(zone, generational);
}

void auto_trace_phase_begin(auto_zone_t *zone, boolean_t generational, auto_collection_phase_t phase) {}
void auto_trace_phase_end(auto_zone_t *zone, boolean_t generational, auto_collection_phase_t phase, size_t objectsReclaimed, size_t bytesReclaimed) {}

void auto_trace_collection_end(auto_zone_t *zone, boolean_t generational, size_t objectsReclaimed, size_t bytesReclaimed, size_t totalObjectsInUse, size_t totalBytesInUse) 
{
    if (callouts.auto_trace_collection_end)
        callouts.auto_trace_collection_end(zone, generational, objectsReclaimed, bytesReclaimed, totalObjectsInUse, totalBytesInUse);
}
