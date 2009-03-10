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

#import "malloc_test.h"
#import "auto_zone.h"

#import <libc.h>
#import <stdio.h>
#import <math.h>
#import <mach/mach_init.h>
#import <mach/message.h>

/*********	Random generation of sizes	************/

size_t _malloc_test_uniform_generator(unsigned context) {
    if (!context) context = vm_page_size;
    return random() % context;
}

size_t _malloc_test_healthy_mix_generator(unsigned context) {
    unsigned	rr = random() % 1000;
    if (!context) context = 100 * vm_page_size;
    if (rr < 200) return 16;
    if (rr < 600) return 32;
    if (rr < 900) return 48;
    if (rr < 990) return _malloc_test_uniform_generator(context / 100);
    if (rr < 999) return _malloc_test_uniform_generator(context / 10);
    return random() % context;
}

size_t _malloc_test_constant_size(unsigned context) {
    return context;
}

/*********	Filler & checker	************/

#define MAGIC1	0x12aef356
#define MAGIC2	0xfe235601

#if 1
#define SET_ALL_POINTERS(ptr)	auto_zone_set_layout_type(auto_zone(), ptr, AUTO_MEMORY_SCANNED/*NOSHADOW*/)
#define SET_NO_POINTERS(ptr)	auto_zone_set_layout_type(auto_zone(), ptr, AUTO_MEMORY_UNSCANNED)
#else
#define SET_ALL_POINTERS(ptr)	{}
#define SET_NO_POINTERS(ptr)	{}
#endif

void filler(void *ptr, size_t size, void **ptrs_array, unsigned array_size) {
    // fills ptrs with specific data if if it were of given size
    if (size < 8) return;
    if (size & 3) {
        unsigned	class = _TEST_CLASS_ODD_SIZE;
        unsigned	*words = ptr;
        words[0] = class; 
    } else if (size < _TEST_CLASS_PTR_ARRAY_AFTER - _TEST_CLASS_PTR_ARRAY) {
        unsigned	class = _TEST_CLASS_PTR_ARRAY + size;
        unsigned	*words = ptr;
        SET_ALL_POINTERS(ptr);
        words[0] = class; 
        words++; size -= 4; 
        while (size >= 4) {
            void	*ptr = (ptrs_array) ? ptrs_array[random() % array_size] : NULL;
            words[0] = (unsigned)ptr;
            words++; size -= 4;
        }
    } else if (random() & 1) {
        unsigned	class = _TEST_CLASS_NO_PTR;
        unsigned	*words = ptr;
        SET_NO_POINTERS(ptr);
        words[0] = class; 
        words[1] = MAGIC1;
        words[(size-4)/4] = MAGIC2;
    } else {
        // unspecified
        unsigned	*words = ptr;
        words[1] = random() << 4;
    }
}            
    
/*********	Logger	************/

/*********	Specific tests	************/

void _malloc_test_basic_loop(malloc_zone_t *zone, unsigned count, _malloc_test_size_generator_t size_generator, unsigned context) {
    unsigned	index = count;
    while (index--) {
        size_t	size = size_generator(context);
        void 	*ptr = malloc_zone_malloc(zone, size);
        free(ptr);
    }
}

void _malloc_test_increasing_reallocs(malloc_zone_t *zone, unsigned count, unsigned increment) {
    unsigned	size = 0;
    char	*ptr = NULL;
    while (count--) {
        size += increment;
        ptr = malloc_zone_realloc(zone, ptr, size);
        ptr[0] = 'A';
        ptr[size-1] = 'Z';
    }
    malloc_zone_free(zone, ptr);
}

void _malloc_test_allocate_a_bunch_then_free(malloc_zone_t *zone, unsigned count, _malloc_test_size_generator_t size_generator, unsigned context) {
    void	**array = malloc_zone_calloc(zone, count, sizeof(void *));
    size_t	*sizes = malloc_zone_calloc(zone, count, sizeof(size_t));
    unsigned	index;
    index = 0; while (index < count) {
        sizes[index++] = size_generator(context);
    }
    index = 0; while (index < count) {
        unsigned	size = sizes[index];
        unsigned	msize;
        void	*ptr = malloc_zone_malloc(zone, size);
        array[index] = ptr;
        msize = malloc_size(ptr);
        *((unsigned *)ptr) = msize;
        index++;
    }
    index = count; while (index--) {
        void	*ptr = array[index];
        unsigned	size = malloc_size(ptr);
        if (*((unsigned *)ptr) != size) {
            printf("*** for %p recorded size was %d for requested %d; now malloc_size is %d\n", ptr, *((unsigned *)ptr), (unsigned)sizes[index], size);
        }
        malloc_zone_free(zone, ptr);
    }
    malloc_zone_free(zone, sizes);
    malloc_zone_free(zone, array);
}

static boolean_t check_ptr(unsigned char *ptr) {
#if 0
    unsigned	size;
    unsigned	ix;
    size = ((unsigned *)ptr)[0];
    ix = 4;
    if (size < 1000) while (ix < size) {
        if (ptr[ix] != (ix & 0xff)) return 0;
        ix++;
    }
#endif
    return 1;
}

void **_malloc_test_random_fill(malloc_zone_t *zone, size_t array_size, _malloc_test_size_generator_t size_generator, unsigned context, int fd) {
    void	**array;
    unsigned	num = 0;
    array = malloc_zone_calloc(zone, array_size, 4);
    if (fd) printf("\tvoid	**array = malloc_zone_calloc(zone, %d, 4);\n", (int)array_size);
    while (num < array_size) {
        unsigned	rr = random() % 8;
        unsigned char	*ptr;
        unsigned	size;
        // printf("rr=%d\n", rr);
        if (0) {
            unsigned	index = num;
            while (index--) if (!check_ptr(array[index])) {
                printf("*** Item at %d seems damaged; size=%d\n", index, ((unsigned *)array[index])[0]);
                sleep(3600);
            }
        }
        switch (rr) {
            case 0:
            case 1:
            case 2:
            case 3:
                size = size_generator(context);
                if (size < 4) size = 4;
                if (fd) printf("\tarray[%d] = malloc_zone_malloc(zone, %d);\n", num, size);
                ptr = malloc_zone_malloc(zone, size);
                if (num) filler(ptr, size, array, num);
                // printf("Allocation for %d returned %p\n", size, ptr);
                array[num++] = ptr;
                break;
            case 4:
            case 5:
                if (num) {
                    if (fd) printf("\tmalloc_zone_free(zone, array[%d]);\n", num-1);
                    ptr = array[--num];
                    if (!check_ptr(ptr)) {
                        printf("*** Item at %d seems damaged\n", num+1);
                        sleep(3600);
                    }
                    malloc_zone_free(zone, ptr);
                }
                break;
            case 6: 	// realloc
                if (num) {
                    unsigned	index = random() % num;
                    size = size_generator(context);
                    if (size < 4) size = 4;
                    // printf("Realloc index=%d num=%d\n", index, num); 
                    if (fd) printf("\tarray[%d] = malloc_zone_realloc(zone, array[%d], %d);\n", index, index, size);
                    ptr = malloc_zone_realloc(zone, array[index], size);
                    if (num) filler(ptr, size, array, num);
                    array[index] = ptr;
                }
                break;
            case 7:
                if (num) {
                    unsigned	index = random() % num;
                    malloc_size(array[index]);
                }
                break;
        }
    }
    return array;
}

void _malloc_test_random_free(malloc_zone_t *zone, void **array, size_t num, int fd) {
    while (num--) {
        if (fd) printf("\tmalloc_zone_free(zone, array[%d]);\n", (unsigned)num);
        if (array[num]) malloc_zone_free(zone, array[num]);
    }
    if (fd) printf("\tmalloc_zone_free(zone, array);\n");
    malloc_zone_free(zone, array);
}

void **_malloc_test_worst_free_heap(malloc_zone_t *zone, size_t array_size, size_t item_size) {
    void	**array;
    unsigned	num = 0;
    array = malloc_zone_calloc(zone, array_size, sizeof(void *));
    while (num < array_size) {
        array[num++] = malloc_zone_malloc(zone, item_size);
    }
    num = 0;
    while (num < array_size) {
        malloc_zone_free(zone, array[num]);
        array[num] = NULL;
        num += 2;
    }
    return array;
}

void _malloc_test_generational(malloc_zone_t *zone, unsigned allocation_budget, _malloc_test_size_generator_t size_generator, unsigned context) {
    unsigned	count = allocation_budget / 100;
    void	**old_gen = malloc_zone_calloc(zone, count, sizeof(void *));
    void	**new_gen = malloc_zone_calloc(zone, count, sizeof(void *));
    unsigned	index = 0;
    while (index < count) {
        size_t	size = size_generator(context);
        void	*ptr = malloc_zone_malloc(zone, size);
        filler(ptr, size, old_gen, count);
        old_gen[index] = ptr;
        index++;
    }
    unsigned	gen = 0;
    while (gen < 100) {
        unsigned	index = 0;
        // allocate
        while (index < count) {
            size_t	size = size_generator(context);
            void	*ptr = malloc_zone_malloc(zone, size);
            filler(ptr, size, old_gen, count);
            new_gen[index] = ptr;
            index++;
        }
        // free
        index = 0;
        while (index < count) {
            boolean_t	old = (random() % 100) < 5;
            malloc_zone_free(zone, (old) ? old_gen[index] : new_gen[index]);
            if (old) old_gen[index] = new_gen[index];
            index++;
        }
        gen++;
    }
    index = count; while (index--) {
        malloc_zone_free(zone, old_gen[index]);
    }
    malloc_zone_free(zone, old_gen);
    malloc_zone_free(zone, new_gen);
}

extern void **_malloc_test_create_network(malloc_zone_t *zone, _malloc_test_size_generator_t size_generator, unsigned count, float point_to_another, unsigned context) {
    void	**array = malloc_zone_calloc(zone, count, sizeof(void *));
    unsigned 	index;
    unsigned	point_threshold = round((float)0xffff * point_to_another);
    for (index = 0; index < count; index++) {
        size_t	size = size_generator(context);
        void	*ptr = malloc_zone_calloc(zone, size, 1);
        array[index] = ptr;
        if ((size >= 8) && ((random() & 0xffff) < point_threshold)) {
            void	*other = array[random() % (index+1)];
            ((void **)ptr)[1] = other;
        }
    }
    return array;
}
