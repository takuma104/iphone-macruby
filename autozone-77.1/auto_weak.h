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

#import "auto_impl_utilities.h"

__BEGIN_DECLS

typedef struct {
    void **referrer;    // clear this address
    auto_weak_callback_block_t *block;
} weak_referrer_t;

extern void weak_call_callbacks(auto_weak_callback_block_t *block);

typedef struct weak_referrer_array_t {
    weak_referrer_t 	*refs;
    unsigned		num_refs;
    unsigned		num_allocated;
    unsigned        max_hash_displacement;
} weak_referrer_array_t;

typedef struct weak_entry_t {
    const void *referent;
    weak_referrer_array_t referrers;
} weak_entry_t;

// clear references to garbage
extern auto_weak_callback_block_t *weak_clear_references(azone_t *azone, size_t garbage_count, vm_address_t *garbage, uintptr_t *weak_referents_count, uintptr_t *weak_refs_count);

// register a new weak reference
extern void weak_register(azone_t *azone, const void *referent, void **referrer, auto_weak_callback_block_t *block);

// unregister an existing weak reference
extern void weak_unregister(azone_t *azone, const void *referent, void **referrer);

// unregister all weak references from a block.
extern void weak_unregister_with_layout(azone_t *azone, void *block[], const unsigned char *map);

__END_DECLS
