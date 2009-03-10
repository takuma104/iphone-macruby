/*
 * Copyright (c) 1999-2008 Apple Computer, Inc. All rights reserved.
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

#import <objc/malloc.h>

/*********	Random generation of sizes	************/

typedef size_t _malloc_test_size_generator_t(unsigned context);

extern size_t _malloc_test_uniform_generator(unsigned context);
    /* Generates sizes uniformly distributed up to context (defaulting to a page) */

extern size_t _malloc_test_healthy_mix_generator(unsigned context);
    /* Generates small sizes, with a distribution typical of apps (lots of small allocations) */

extern size_t _malloc_test_constant_size(unsigned context);
    /* returns context */
    
/*********	Specific tests	************/

extern void _malloc_test_basic_loop(malloc_zone_t *zone, unsigned count, _malloc_test_size_generator_t size_generator, unsigned context);
    /* do malloc/free count times */
    
extern void _malloc_test_increasing_reallocs(malloc_zone_t *zone, unsigned count, unsigned increment);
    /* keeps doing reallocs */

extern void _malloc_test_allocate_a_bunch_then_free(malloc_zone_t *zone, unsigned count, _malloc_test_size_generator_t size_generator, unsigned context);
    /* As its name says */

extern void **_malloc_test_random_fill(malloc_zone_t *zone, size_t array_size, _malloc_test_size_generator_t size_generator, unsigned context, int fd);
    /* Fills an array with randomly generated blocks; Does a step backwards after a few steps forwards, somewhat randomly */
    /* Roughly needs 2 * array_size allocation before array is filled */

extern void **_malloc_test_worst_free_heap(malloc_zone_t *zone, size_t array_size, size_t item_size);
    /* Creates a very fragmented heap alternating in use and freed block */

extern void _malloc_test_random_free(malloc_zone_t *zone, void **array, size_t num, int fd);
    /* Frees any non-NULL item in array */

extern void _malloc_test_generational(malloc_zone_t *zone, unsigned allocation_budget, _malloc_test_size_generator_t size_generator, unsigned context);
    // given a big enough allocation budget (100,000 is a minimum)
    // allocates an array coresponding to a "surviving" generation
    // then, 100 times, 
    //		allocates a new generation
    //		free 95% of the new generation, 5% of the old

/*********	Filling Mallocs: contents of "classes"	************/

#define _TEST_CLASS_ODD_SIZE	0x1357	// used for blocks which size is not multiple of 4
#define _TEST_CLASS_PTR_ARRAY	0x1234	// all pointers except word[0]
#define _TEST_CLASS_32_PTRS	2
#define _TEST_CLASS_PTR_ARRAY_AFTER	(_TEST_CLASS_PTR_ARRAY + _TEST_CLASS_32_PTRS*32*4)
#define _TEST_CLASS_NO_PTR	0x9999	// all slots are non-pointers

extern void **_malloc_test_create_network(malloc_zone_t *zone, _malloc_test_size_generator_t size_generator, unsigned count, float point_to_another, unsigned context);
