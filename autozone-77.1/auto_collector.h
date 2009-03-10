/*
 * Copyright (c) 2002-2008 Apple Inc. All rights reserved.
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

#ifndef __AUTO_COLLECTOR__
#define __AUTO_COLLECTOR__

#import "auto_zone.h"
#import "auto_impl_utilities.h"

__BEGIN_DECLS

extern signed auto_collection_gc(azone_t *azone, size_t *garbage_count, vm_address_t **garbage, boolean_t generational, void *collection_context);
    // garbage_bins is filled with objects to be finalized and reclaimed
    // returns AUTO_COLLECTION_STATUS_ERROR     on error
    //         AUTO_COLLECTION_STATUS_INTERRUPT if interrupted due to user activity
    //         AUTO_COLLECTION_STATUS_OK        otherwise

extern vm_address_t auto_collect_stack_bottom;

extern boolean_t auto_collection_full_gc(azone_t *azone, size_t *garbage_count, vm_address_t **garbage, void *collection_context);

extern void auto_zone_resurrection_error(azone_t *azone, const void *new_value);

__END_DECLS

#endif /* __AUTO_COLLECTOR__ */
