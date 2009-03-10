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
#import "auto_zone.h"
#import "malloc_test.h"
#import "auto_impl_utilities.h"

#import <stdio.h>
#import <libc.h>
#import <pthread.h>
#import <assert.h>

double now(void) { // in secs
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + 1E-6 * (double)tv.tv_usec;
}

static void healthy_mix(malloc_zone_t *zone, unsigned num) {
    void	**array;
    array = _malloc_test_random_fill(zone, num, _malloc_test_healthy_mix_generator, 0, 0);
    _malloc_test_random_free(zone, array, num, 0);
}

static void number_recorder(task_t task, void *context, unsigned type, vm_range_t *ranges, unsigned num_ranges) {
    unsigned	*number = context;
    // printf("number_recorder got %d ranges\n", num_ranges);
    *number += num_ranges;
}

static unsigned number_in_use(malloc_zone_t *zone) {
    unsigned	number = 0;
    zone->introspect->enumerator(mach_task_self(), &number, MALLOC_PTR_IN_USE_RANGE_TYPE, (vm_address_t)zone, NULL, number_recorder);
    return number;
}

typedef void fun_t(malloc_zone_t *, unsigned);

static void *thread_doer(void *args) {
    malloc_zone_t	*zone = ((void **)args)[0];
    unsigned	num = ((unsigned *)args)[1];
    fun_t	*fun = ((void **)args)[2];
    fun(zone, num);
    // printf("Thread done for %d\n", num);
    return NULL;
}

static pthread_t start_thread(void *args) {
    pthread_attr_t attr;
    pthread_t tid;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&tid, &attr, thread_doer, args);
    pthread_attr_destroy(&attr);
    return tid;
}

static void expect_in_use(malloc_zone_t *zone, unsigned *in_use) {
    unsigned    new_in_use = number_in_use(zone);
    if (new_in_use == *in_use) {
        // zone->introspect->print(auto_zone(), 0);
        return;
    }
    // printf("*** Different number in use now: %d\n", new_in_use);
    if (zone == auto_zone()) {
        // printf("*** Starting a full GC: %d\n", new_in_use);
        auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
        new_in_use = number_in_use(zone);
        if (new_in_use == *in_use) {
            // printf("*** Back to same number in use %d\n", *in_use);
            // zone->introspect->print(auto_zone(), 0);
            return;
        }
        printf("*** After full GC: %d in_use (%d expected)\n", new_in_use, *in_use);
	zone->introspect->print(zone, 0);
    }
    *in_use = new_in_use;
    // zone->introspect->print(auto_zone(), 0);
}

boolean_t logging = 1;

static void do_threads(malloc_zone_t *zone, unsigned num_threads, fun_t fun, unsigned num, int logging) {
    double	start;
    double	done;
    unsigned	*tids = malloc(num_threads * 4);
    unsigned	count = num_threads;
    void	*ret;
    void	*args[3];
    args[0] = zone;
    args[1] = (void *)num;
    args[2] = fun;
    start = now();
    while (count--) {
        tids[count] = (unsigned)start_thread(args);
        // usleep(100);
    }
    count = num_threads;
    while (count--) {
        pthread_join((void *)tids[count], &ret);
        if (logging) printf("Joined %d ", tids[count]); fflush(stdout);
    }
    done = now();
    if (logging) printf("\n");
    free(tids);
    if (logging) printf("==== All %d Threads joined in %dmsecs\n", num_threads, (int)(double)((done - start) * 1e3));
}

static void torture(malloc_zone_t *zone, unsigned total_count) {
    double	start;
    unsigned	in_use = number_in_use(zone);
    printf("At the beginning of torture: %d in use\n", in_use);

    // 1% on basic loop
    unsigned	times = total_count / 100; total_count -= times;
    start = now();
    _malloc_test_basic_loop(zone, times, _malloc_test_uniform_generator, 1500);
    if (logging) printf("==== Done _malloc_test_basic_loop(%d) in %dmsecs\n", times, (int)(double)((now() - start) * 1e3));
    expect_in_use(zone, &in_use);

    // 0.1% on realloc loop with interesting increments
    unsigned	increments[] = {1, 15, 16, 7*16, 8*16-1, 8*16, 4*1024-1, 4*1024, 10*1024, 0};
    unsigned	*pincr = increments;
    start = now();
    while (*pincr) {
        times = total_count / 1000; total_count -= times;
        _malloc_test_increasing_reallocs(zone, times, *pincr);
        pincr++;
    }
    if (logging) printf("==== Done realloc loops in %dmsecs\n", (int)(double)((now() - start) * 1e3));
    expect_in_use(zone, &in_use);

    // 10% on batch loop
    times = total_count / 10; total_count -= times;
    unsigned	usize = 1500;
    unsigned	hsize = 5000;
    start = now();
    _malloc_test_allocate_a_bunch_then_free(zone, times/2, _malloc_test_uniform_generator, usize);    
    _malloc_test_allocate_a_bunch_then_free(zone, times/2, _malloc_test_healthy_mix_generator, hsize);
    if (logging) printf("==== Done batch(%d,%d) in %dmsecs\n", usize, hsize, (int)(double)((now() - start) * 1e3));
    expect_in_use(zone, &in_use);

    // 10% on worst-case-heap
    times = total_count / 10; total_count -= times;
    void	**array;
    size_t	item_size = 4;
    start = now();
    array = _malloc_test_worst_free_heap(zone, times, item_size);
    _malloc_test_random_free(zone, array, times, 0);
    if (logging) printf("==== Done _malloc_test_worst_free_heap %d blocks of size %d in %dmsecs\n", (int)times, (int)item_size, (int)(double)((now() - start) * 1e3));
    expect_in_use(zone, &in_use);
    
    // 50% on generational
    times = total_count / 2; total_count -= times;
    start = now();
    _malloc_test_generational(zone, times, _malloc_test_healthy_mix_generator, 5000);
    if (logging) printf("==== Done _malloc_test_generational(%d) in %dmsecs\n", (int)times, (int)(double)((now() - start) * 1e3));
    expect_in_use(zone, &in_use);
    
    // the rest on thread testing
    unsigned	num_threads = 4;
    times = total_count / num_threads;
    if (logging) printf("==== Doing threads stress %d * %d\n", num_threads, times);
    start = now();
    do_threads(zone, num_threads, healthy_mix, times, logging);
    if (logging) printf("==== Done threads stress(%d) in %dmsecs\n", (int)times, (int)(double)((now() - start) * 1e3));
    expect_in_use(zone, &in_use);



#if 0
    start = now();
    if (logging) printf("==== Starting for small\n");
    num = 10000;
    array = _malloc_test_random_fill(zone, num, _malloc_test_healthy_mix_generator, 0, 0);
    _malloc_test_random_free(zone, array, num, 0);
    if (logging) printf("==== Starting for large\n");
    num = 100;
    array = _malloc_test_random_fill(zone, num, _malloc_test_uniform_generator, 100*1024, 0);
    _malloc_test_random_free(zone, array, num, 0);
    if (logging) printf("==== Starting for huge\n");
    num = 10;
    array = _malloc_test_random_fill(zone, num, _malloc_test_uniform_generator, 10 * (1<<24), 0);
    _malloc_test_random_free(zone, array, num, 0);
    if (logging) printf("==== Starting for uniform distribution\n");
    num = 10000;
    array = _malloc_test_random_fill(zone, num, _malloc_test_uniform_generator, 1000, 0);
    _malloc_test_random_free(zone, array, num, 0);
    if (logging) printf("==== Done small/large/huge in %dmsecs\n", (int)(double)((now() - start) * 1e3));
    if (! zone->introspect->check(zone)) {
	printf("*** Fails to check!\n");
	zone->introspect->print(zone, 0);
    }
#endif

}

static unsigned array_size = 0;
// static unsigned	num_for_cr = 0;

static void array_item_invalidate(auto_zone_t *zone, const void *ptr, void *collection_context) {
    void	**array = collection_context;
    unsigned	ai;
    for (ai = 0; ai < array_size; ai++) {
        // print the index of the corresponding leak
        if (array[ai] == ptr) {
            // printf("[%d]\t", ai); num_for_cr++; 
            // if (!(num_for_cr % 10)) printf("\n");
            array[ai] = NULL;
            return;
        }
    }
    // printf("*** Leak is NOT an array item 0..%d: %p\n", array_size-1, ptr);
}

static void print_array(malloc_zone_t *zone, void **array, unsigned count) {
    unsigned num_for_cr = 0;
    unsigned ai;
    for (ai = 0; ai < count; ai++) {
        if (array[ai]) {
            printf("[%d]=%p (@%d)\t", ai, array[ai], auto_zone_retain_count(zone, array[ai]));
            num_for_cr++; if (!(num_for_cr % 5)) printf("\n");
        }
    }
    printf("\n");
}

static boolean_t initial_refcount = 1;

static void test_refcount(malloc_zone_t *zone, void *ptr, unsigned log_limit) {
    unsigned	refcount = initial_refcount;
    while (refcount < (1 << log_limit)) {
        refcount++; auto_zone_retain(zone, ptr);
        if (auto_zone_retain_count(zone, ptr) != refcount) {
            printf("*** refcount up error %d instead of %d\n", auto_zone_retain_count(zone, ptr), refcount);
            break;
        }
    }
    while (refcount > initial_refcount) {
        refcount--; auto_zone_release(zone, ptr);
        if (auto_zone_retain_count(zone, ptr) != refcount) {
            printf("*** refcount down error %d instead of %d\n", auto_zone_retain_count(zone, ptr), refcount);
        }
    }
}

static void gc_test(unsigned count) {
    malloc_zone_t	*zone;
    unsigned		index;
    void		**array;

    printf("malloc_default_zone = %p\n", malloc_zone_from_ptr(malloc(4)));
    zone = auto_zone();
    //malloc_zone_register(zone);
    printf("\nauto zone = %p\n", zone);
    auto_collector_disable(zone);

    // we fill an array with 1000 items of random size
    void	**auto_array = _malloc_test_random_fill(zone, count, _malloc_test_healthy_mix_generator, 0, 0);
    
    unsigned	log_limit = 12;
    printf("Starting refcount test for 1<<%d retains\n", log_limit);
    test_refcount(zone, auto_array[0], log_limit);
    test_refcount(zone, auto_array[1], log_limit);
    test_refcount(zone, auto_array, log_limit);
    printf("refcount test done!\n");

    // we copy to a malloc array to avoid having an array with all the pointers!
    array = calloc(count, sizeof(void *));
    memcpy(array, auto_array, count * sizeof(void *));
    bzero(auto_array, count * sizeof(void *));
    free(auto_array);
    auto_collector_reenable(zone);
    
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    zone->introspect->print(zone, 0);
    
    printf("== Allocated %d items from [0]=%p to [%d]=%p\n", count, array[0], count-1, array[count-1]);
    index = 0; 
    while (index < count) {
        size_t	size = malloc_size(array[index]); 
        bzero(array[index], size);
        index++;
    }
#if SHOW_SIZES
    printf("== Sizes: "); index = 0; 
    while (index < count) {
        printf("%p[%d] ", array[index], (unsigned)malloc_size(array[index])); 
        index++;
    }
    printf("\n");
#endif

    auto_collector_disable(zone);

    unsigned	start, end;
    start = 2*count/10; end = 4*count/10;
    printf("== Freeing items [%d]=%p to [%d]=%p\n", start, array[start], end-1, array[end-1]);
    for (index = start; index < end; index++) { free(array[index]); array[index] = NULL; }
    
    start = 6*count/10; end = 8*count/10;
    printf("== Increasing refount for [%d]=%p to [%d]=%p\n", start, array[start], end-1, array[end-1]);
    for (index = start; index < end; index++) { auto_zone_retain(zone, array[index]); }
    
    // we arbitrarily make a chain
    start = 7*count/10; end = start + 5; // XXX - this doesn't work with count == 10. will corrupt memory.
    for (index = start; index < end; index++) {
        void	**x = (void **)(array[index]);
        unsigned	dest = (index == end-1) ? 13 : index-1;
        printf("== We make [%d]=%p point to [%d]=%p\n", index, x, dest, array[dest]);
        x[1] = array[dest];
    }
    
    // we arbitrarily make a chain
    start = 7*count/10 + 10; end = start + 7;  // XXX - this doesn't work with count == 10. will corrupt memory.
    for (index = end; index >= start; index--) {
        void	**x = (void **)(array[index]);
        unsigned	dest = (index == start) ? 17 : index-1;
        printf("== We make [%d]=%p point to [%d]=%p\n", index, x, dest, array[dest]);
        x[1] = array[dest];
    }
    
    // if initial_refcount==1 we bring back the refcount to 0
    if (initial_refcount) for (index = 0; index < count; index++) {
        void	*ptr = array[index];
        if (ptr) auto_zone_release(zone, ptr);
    }
    
    auto_collector_reenable(zone);
    printf("Before GC:\n");
    zone->introspect->print(zone, 0);
 
    array_size = count;
    auto_collection_parameters(zone)->invalidate = array_item_invalidate;
    //auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, array);
    auto_collect(zone, AUTO_COLLECTION_GENERATIONAL_COLLECTION, array);
    printf("\n== Array items left:\n");
    print_array(zone, array, count);

    printf("After first GC:\n");
    zone->introspect->print(zone, 0);

    printf("\n== NOW DOING ANOTHER GENERATIONAL GC\n");
    auto_collect(zone, AUTO_COLLECTION_GENERATIONAL_COLLECTION, array);
    // printf("\n== Array items left:\n");
    // print_array(zone, array, count);
    
    printf("\n== NOW DOING A FULL GC\n");
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, array);
    // printf("\n== Array items left:\n");
    // print_array(zone, array, count);
    
    printf("\n== NOW DOING A SECOND FULL GC\n");
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, array);
    printf("\n== Array items left:\n");
    print_array(zone, array, count);
    
}

static void test_zone(unsigned total_count) {
    malloc_zone_t	*zone;
    printf("malloc_default_zone = %p\n", malloc_zone_from_ptr(malloc(4)));
    zone = auto_zone();
    // malloc_zone_register(zone);
    printf("auto zone created = %p\n", zone);
    
    printf("Regular malloc: torture(%d)..\n", total_count);
    torture(malloc_default_zone(), total_count);
    // printf("== Zone after torture: ");
    // zone->introspect->print(zone, 0);
    
    printf("\nAuto malloc...\n");
    printf("Auto malloc: torture(%d)..\n", total_count);
    torture(zone, total_count);
    printf("== Zone after torture: ");
    zone->introspect->print(zone, 0);

#if 0
    unsigned		index;
    void		**array;
    array = calloc(count, sizeof(void *));
    index = count;
    while (index--) {
	size_t	size = index * 10;
	void	*ptr = malloc_zone_malloc(zone, size);
	array[index] = ptr;
	size_t	ss = malloc_size(ptr);
        if (!ss) printf("*** Got %p for %d (malloc_size = %d)\n", ptr, (int)size, (int)ss);
    }
    // after the initial count
    zone->introspect->print(zone, 0);
    malloc_printf("refcount for %p [0]: %d\n", array[0], auto_zone_retain_count(zone, array[0]));
    auto_zone_retain(zone, array[0]);
    auto_zone_retain(zone, array[0]);
    auto_zone_retain(zone, array[0]);
    printf("refcount for %p [3]: %d\n", array[0], auto_zone_retain_count(zone, array[0]));
    auto_zone_release(zone, array[0]);
    printf("refcount for %p [2]: %d\n", array[0], auto_zone_retain_count(zone, array[0]));
    printf("Now freeing...\n");
    index = count;
    while (index--) {
	free(array[index]);
    }
#endif

    zone->introspect->print(zone, 1);
}

void precise_gc_test(malloc_zone_t *zone, unsigned count) {
    void	**array;
    auto_collector_disable(zone);
    array = _malloc_test_create_network(zone, _malloc_test_constant_size, count, 0.10, 16);
    //array = _malloc_test_create_network(zone, _malloc_test_healthy_mix_generator, count, 0.10, 0);
    auto_collector_reenable(zone);
    zone->introspect->print(zone, 0);
    unsigned	index;
    for (index = 0; index < count; index++) {
        if ((random() % 100) > 10) auto_zone_release(zone, array[index]);
    }
    auto_zone_release(zone, array);
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    zone->introspect->print(zone, 0);
    auto_collect_print_trace_stats();
}

#if 0
static void test_bzero_inline_std(char *ptr, unsigned nbytes) {
    bzero(ptr, nbytes);
}

static void test_bzero_trivial(char *ptr, unsigned nbytes) {
    while (nbytes--) *ptr++ = 0; 
}

static void test_bzero_word(char *ptr, unsigned nbytes) {
    unsigned	*words = (void *)ptr;
    unsigned	nwords = nbytes >> 2;
    while (nwords--) *words++ = 0;
}

static void test_bzero_quad(char *ptr, unsigned nbytes) {
    unsigned	*quads = (void *)ptr;
    unsigned	nquads = nbytes >> 4;
    while (nquads--) {
        quads[0] = 0;
        quads[1] = 0;
        quads[2] = 0;
        quads[3] = 0;
        quads += 4;
    }
}

static void test_bzero_32b(char *ptr, unsigned nbytes) {
    unsigned	*quads = (void *)ptr;
    unsigned	nquads = nbytes >> 5;
    while (nquads--) {
        quads[0] = 0;
        quads[1] = 0;
        quads[2] = 0;
        quads[3] = 0;
        quads[4] = 0;
        quads[5] = 0;
        quads[6] = 0;
        quads[7] = 0;
        quads += 8;
    }
}

static void test_bzero_32b_dcbz(char *ptr, unsigned nbytes) {
    unsigned	*quads = (void *)ptr;
    unsigned	nquads = nbytes >> 5;
    while (nquads--) {
#if defined(__ppc__)
    __asm__ volatile("dcbz 0, r3");
#endif
        quads += 8;
    }
}

static double test_cached(unsigned times, unsigned nbytes, void (*fun)(char *, unsigned)) {
    void	*ptr = malloc(nbytes);
    memset(ptr, 0x55, nbytes);
    double	start;
    double	duration;
    start = now();
    fun(ptr, nbytes);
    start = now();
    while (times--) fun(ptr, nbytes);
    duration = now() - start;
    free(ptr);
    //printf("%d msecs\n", (int)(duration * 1000.0));
    return duration;
}

static void test_all_cached() {
    unsigned	nbytes = 32;
    while (nbytes <= 2*1024) {
        unsigned	times = 1000;
        while (test_cached(times, nbytes, test_bzero_inline_std) < 0.01) times *= 10;
        printf("nbytes=%d times=%dK\n", nbytes, times/1000);
        printf("test_bzero_inline_std() => %d msecs\n", (int)(test_cached(times, nbytes, test_bzero_inline_std)*1000.0));
        printf("test_bzero_trivial() => %d msecs\n", (int)(test_cached(times, nbytes, test_bzero_trivial)*1000.0));
        printf("test_bzero_word() => %d msecs\n", (int)(test_cached(times, nbytes, test_bzero_word)*1000.0));
        printf("test_bzero_quad() => %d msecs\n", (int)(test_cached(times, nbytes, test_bzero_quad)*1000.0));
        printf("test_bzero_32b() => %d msecs\n", (int)(test_cached(times, nbytes, test_bzero_32b)*1000.0));
        printf("test_bzero_32b_dcbz() => %d msecs\n", (int)(test_cached(times, nbytes, test_bzero_32b_dcbz)*1000.0));
        printf("\n");
        nbytes *= 2;
    }
}

#endif

/**
 * For testing purposes, made auto_collection_conservatively_trace_range non-static, so we can test it
 * in isolation.
 */
extern void auto_collection_conservatively_trace_range(azone_t *azone, vm_range_t range, unsigned *found,
                                                       const unsigned *filter, const unsigned *regions_filter);

static vm_address_t stack_bottom = NULL;

static void test_conservative_scanning(malloc_zone_t *zone)
{
    vm_address_t stack_top = (vm_address_t) &zone;
    vm_range_t stack_range = { stack_top, stack_bottom - stack_top };
    unsigned found_bitmap[stack_range.size / sizeof(vm_address_t)];
    bzero(found_bitmap, sizeof(found_bitmap));
    
    auto_collection_conservatively_trace_range((azone_t*) zone, stack_range, found_bitmap, NULL, NULL);
    
    unsigned *f = found_bitmap, *limit = found_bitmap + sizeof(found_bitmap) / sizeof(unsigned);
    while (f < limit) {
        unsigned bits = *f++;
        assert(bits == 0);
    }
    
    fprintf(stderr, "NO conservative pointers found on stack.\n");
}

static void test_stats()
{
    malloc_zone_t *zone = auto_zone();
    void* ptr;
    
    const auto_statistics_t *stats = auto_collection_statistics(zone);
    assert(stats->blocks_in_use == 0);
    ptr = malloc_zone_malloc(zone, 1024);
    assert(stats->blocks_in_use == 1);
    free(ptr);
    assert(stats->blocks_in_use == 0);
    
    ptr = malloc_zone_malloc(zone, 1024);
    assert(stats->blocks_in_use == 1);
    auto_zone_release(zone, ptr);
    ptr = NULL;
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    assert(stats->blocks_in_use == 0);

    test_conservative_scanning(zone);

    fprintf(stderr, "test_stats passed.\n");
}

static void test_accounting(unsigned count)
{
    unsigned i, actualInUse;
    void* pointers[count];
    malloc_zone_t *zone = auto_zone();
    
    // allocate specified number of objects.
    const auto_statistics_t *stats = auto_collection_statistics(zone);
    assert(stats->blocks_in_use == 0);
    
    for (i = 0; i < count; ++i) {
        pointers[i] = malloc_zone_malloc(zone, 16 * ((i + 1) % 8));
        assert(pointers[i] != NULL);
    }
    
    assert(stats->blocks_in_use == count);
    actualInUse = number_in_use(zone);
    assert(actualInUse == count);
    
    for (i = 0; i < count; ++i) {
        auto_zone_release(zone, pointers[i]);
        pointers[i] = NULL;
    }

    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    
    actualInUse = number_in_use(zone);
    assert(actualInUse == 0);

    assert(stats->blocks_in_use == 0);
    
    test_conservative_scanning(zone);

    fprintf(stderr, "test_accounting passed.\n");
}

typedef struct {
    malloc_zone_t* zone;
    size_t size;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} Params;

static void* create_garbage(void* arg)
{
    Params* p = arg;
    malloc_zone_t *zone = p->zone;
    auto_zone_register_thread(zone);
    void* garbage = malloc_zone_malloc(zone, 1024);
    if (garbage) {
        auto_zone_release(zone, garbage);
        pthread_mutex_lock(&p->mutex);
        pthread_cond_wait(&p->cond, &p->mutex);
        pthread_mutex_unlock(&p->mutex);
    }
    // free(garbage);
    auto_zone_unregister_thread(zone);
    return NULL;
}

static pthread_t create_joinable_thread(void* arg, void* (*func) (void* arg))
{
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&thread, &attr, func, arg);
    pthread_attr_destroy(&attr);
    return thread;
}

static void test_threads(malloc_zone_t *zone)
{
    const auto_statistics_t *stats = auto_collection_statistics(zone);
    assert(stats->blocks_in_use == 0);

    Params p = { zone, 1024 };
    pthread_cond_init(&p.cond, NULL);
    pthread_mutex_init(&p.mutex, NULL);
    pthread_t thread = create_joinable_thread(&p, create_garbage);
    while (stats->blocks_in_use == 0);
    assert(stats->blocks_in_use == 1);

    // run a collection, which shouldn't collect anything, since the thread is spinning.
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    assert(stats->blocks_in_use == 1);
    
    // unblock the thread.
    pthread_mutex_lock(&p.mutex);
    pthread_cond_signal(&p.cond);
    pthread_mutex_unlock(&p.mutex);
    pthread_join(thread, NULL);
    
    // run a final collection, which should now collect.
    auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    assert(stats->blocks_in_use == 0);

    fprintf(stderr, "test_threads passed.\n");
}

/**
 * tests whether or not the allocator can be used in the middle of a collection callback.
 */
static void test_reentrancy()
{
    malloc_zone_t *zone = auto_zone();
    auto_collection_control_t *params = auto_collection_parameters(zone);
    void* garbage = malloc_zone_malloc(zone, 1024);
    // params->
    if (garbage) {
        auto_zone_release(zone, garbage);
        garbage = NULL;
        auto_collect(zone, AUTO_COLLECTION_FULL_COLLECTION, NULL);
    }
}

int main (int argc, char *argv[]) {
    int i;
    fprintf(stderr, "AUTO test suite.\n");
    malloc_zone_t *zone = auto_zone();
    
    // 0. initialize the bottom of the stack.
    stack_bottom = (vm_address_t) &zone;

    // 1. test that the number of objects allocated agrees with enumeration, etc.
    test_stats();
    for (i = 0; i < 10; i++) test_accounting(50000);

    // 2. create a pthread that holds an uncollectable pointer.
    test_threads(zone);

    // 3. test finding the base pointer of an object.
    void* ptr = malloc_zone_malloc(zone, 32);
    assert(ptr == auto_zone_base_pointer(zone, (void*)(16 + (vm_address_t)ptr)));
    malloc_zone_free(zone, ptr);
    ptr = malloc_zone_malloc(zone, 32768);
    assert(ptr == auto_zone_base_pointer(zone, (void*)(2048 + (vm_address_t)ptr)));
    malloc_zone_free(zone, ptr);
    
    // 4. test write barrier copying.
    void* src = malloc_zone_malloc(zone, 1024);
    void* dst = malloc_zone_malloc(zone, 1024);
    auto_zone_write_barrier_memmove(zone, (char *)dst + 16, (const char *)src + 16, 16);
    free(src);
    free(dst);
    
    exit(0);
    
#if 0
    unsigned	count = 10000; 
    precise_gc_test(zone, count);
    exit(0);
#endif
#if 0
    double	start;
    start = now();
    double	end = now();
#endif
#if 0
    printf("STATS BEFORE: ");
    zone->introspect->print(zone, 0);
    malloc_zone_free(zone, malloc_zone_realloc(zone, malloc_zone_malloc(zone, 128), 256));
    printf("STATS AFTER: ");
    zone->introspect->print(zone, 0);
    exit(1);
#endif
    test_zone(200000);
    zone->introspect->print(auto_zone(), 0);
    gc_test(500);
    zone->introspect->print(auto_zone(), 0);
    zone->introspect->print(zone, 0);
    auto_collect_print_trace_stats();
    sleep(15);
    return 0;
}
