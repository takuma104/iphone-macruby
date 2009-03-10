// Stubs for non-open-source libauto functions

#include "auto_zone.h"

void auto_collect(auto_zone_t *zone, auto_collection_mode_t mode, void *collection_context)
{
	NSLog(@"[auto_zone] auto_collect");
}

auto_collection_control_t *auto_collection_parameters(auto_zone_t *zone)
{
	NSLog(@"[auto_zone] auto_collection_parameters");
    return NULL;
}

const auto_statistics_t *auto_collection_statistics(auto_zone_t *zone)
{
	NSLog(@"[auto_zone] auto_collection_statistics");
    return NULL;
}

void auto_enumerate_references(auto_zone_t *zone, void *referent, 
							   auto_reference_recorder_t callback, 
							   void *stack_bottom, void *ctx)
{
	NSLog(@"[auto_zone] auto_enumerate_references");
}

void auto_enumerate_references_no_lock(auto_zone_t *zone, void *referent, auto_reference_recorder_t callback, void *stack_bottom, void *ctx)
{
	NSLog(@"[auto_zone] auto_enumerate_references_no_lock");
}

auto_zone_t *auto_zone(void)
{
	NSLog(@"[auto_zone] auto_zone");
    return NULL;
}

void* auto_zone_allocate_object(auto_zone_t *zone, size_t size, auto_memory_type_t type, boolean_t initial_refcount_to_one, boolean_t clear)
{
	NSLog(@"[auto_zone] auto_zone_allocate_object(%d)", size);
    return malloc(size);
}

const void *auto_zone_base_pointer(auto_zone_t *zone, const void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_base_pointer");
    return NULL;
}

auto_memory_type_t auto_zone_get_layout_type(auto_zone_t *zone, void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_get_layout_type");
    return 0;
}

auto_memory_type_t auto_zone_get_layout_type_no_lock(auto_zone_t *zone, void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_get_layout_type_no_lock");
    return 0;
}

boolean_t auto_zone_is_finalized(auto_zone_t *zone, const void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_is_finalized");
    return NO;
}

boolean_t auto_zone_is_valid_pointer(auto_zone_t *zone, const void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_is_valid_pointer");
    return NO;
}

unsigned int auto_zone_release(auto_zone_t *zone, void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_release");
    return 0;
}

void auto_zone_retain(auto_zone_t *zone, void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_retain");
}

unsigned int auto_zone_retain_count_no_lock(auto_zone_t *zone, const void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_retain_count_no_lock");
    return 0;
}

void auto_zone_set_class_list(int (*get_class_list)(void **buffer, int count))
{
	NSLog(@"[auto_zone] auto_zone_set_class_list");
}

size_t auto_zone_size_no_lock(auto_zone_t *zone, const void *ptr)
{
	NSLog(@"[auto_zone] auto_zone_size_no_lock");
    return 0;
}

void auto_zone_start_monitor(boolean_t force)
{
	NSLog(@"[auto_zone] auto_zone_start_monitor");
}


void *auto_zone_write_barrier_memmove(auto_zone_t *zone, void *dst, const void *src, size_t size)
{
	NSLog(@"[auto_zone] auto_zone_write_barrier_memmove");
    return memmove(dst, src, size);
}


void auto_zone_set_associative_ref(auto_zone_t *zone, void *object, void *key, void *value)
{
	NSLog(@"[auto_zone] auto_zone_set_associative_ref");
}

void *auto_zone_get_associative_ref(auto_zone_t *zone, void *object,  void *key)
{
	NSLog(@"[auto_zone] auto_zone_get_associative_ref");
	return NULL;
}

void auto_zone_register_thread(auto_zone_t *zone)
{
	NSLog(@"[auto_zone] auto_zone_register_thread");
}

void auto_zone_add_root(auto_zone_t *zone, void *address_of_root_ptr, void *value)
{
	NSLog(@"[auto_zone] auto_zone_add_root");
}

void auto_collector_disable(auto_zone_t *zone) 
{
	NSLog(@"[auto_zone] auto_collector_disable");
}

void auto_collector_reenable(auto_zone_t *zone)
{
	NSLog(@"[auto_zone] auto_collector_reenable");
}

void auto_zone_unregister_thread(auto_zone_t *zone)
{
	NSLog(@"[auto_zone] auto_zone_unregister_thread");
}

boolean_t auto_zone_set_write_barrier(auto_zone_t *zone, const void *dest, const void *new_value)
{
	NSLog(@"[auto_zone] auto_zone_set_write_barrier");
	return true;
}

