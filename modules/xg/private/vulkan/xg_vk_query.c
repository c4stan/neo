#include "xg_vk_query.h"

#include <std_atomic.h>

#include "vulkan/xg_vk_device.h"

#if 0
static xg_vk_query_pool_state_t* xg_vk_query_pool_state;

void xg_vk_query_pool_load ( xg_vk_query_pool_state_t* state ) {
    xg_vk_query_pool_state = state;

    std_alloc_t alloc = std_virtual_heap_alloc_array_m ( xg_vk_query_pool_t, xg_vk_max_query_pools_m );
    state->pools_handle = alloc.handle;
    state->pools_array = ( xg_vk_query_pool_t* ) alloc.buffer.base;
    state->pools_freelist = std_freelist ( alloc.buffer, sizeof ( xg_vk_query_pool_t ) );
    std_mutex_init ( &state->pools_mutex );
}

void xg_vk_query_pool_reload ( xg_vk_query_pool_state_t* state ) {
    xg_vk_query_pool_state = state;
}

void xg_vk_query_pool_unload ( void ) {
    // TODO free in-use pools
    std_virtual_heap_free ( xg_vk_query_pool_state->pools_handle );
    std_mutex_deinit ( &xg_vk_query_pool_state->pools_mutex );
}

xg_query_pool_h xg_vk_query_pool_create ( const xg_query_pool_params_t* params ) {
    std_mutex_lock ( &xg_vk_query_pool_state->pools_mutex );
    xg_vk_query_pool_t* pool = std_list_pop_m ( &xg_vk_query_pool_state->pools_freelist );
    std_mutex_unlock ( &xg_vk_query_pool_state->pools_mutex );

    uint64_t capacity = params->capacity;
    std_assert_m ( capacity > 0 );

    VkQueryPoolCreateInfo pool_info;

    switch ( params->type ) {
        case xg_query_pool_type_timestamp_m:
            pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            break;
    }

    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = 0;
    pool_info.queryCount = capacity;
    pool_info.pipelineStatistics = 0;
    vkCreateQueryPool ( xg_vk_device_get ( device_handle )->vk_handle, &pool_info, NULL, &pool->vk_pool );

    pool->device = device_handle;
    pool->count = 0;
    pool->queries_count = 0;

    // TODO
    std_alloc_t alloc = std_virtual_heap_alloc_array_m ( xg_vk_query_t, capacity );
    pool->queries_handle = alloc.handle;
    pool->queries = ( xg_vk_query_t* ) alloc.buffer.base;

    xg_query_pool_h handle = ( xg_query_pool_h ) ( pool - xg_vk_query_pool_state->pools_array );
    return handle;
}

void xg_vk_query_pool_destroy ( xg_query_pool_h handle ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_pool_state->pools_array[handle];
    vkDestroyQueryPool ( xg_vk_device_get ( pool->device )->vk_handle, pool->vk_pool, NULL );
    std_list_push ( &xg_vk_query_pool_state->pools_freelist, pool );
}

xg_query_h xg_vk_query_reserve ( xg_query_pool_h pool ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_pool_state->pools_array[handle];
    uint32_t idx = std_atomic_increment_u32 ( &pool->queries_count ) - 1;
    return ( xg_query_h ) idx;
}

uint32_t xg_vk_query_alloc ( xg_query_pool_h pool_handle, xg_query_h query_handle ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_pool_state->pools_array[pool_handle];
    xg_vk_query_t* query = pool->queries[query_handle];
    uint32_t idx = std_atomic_increment_u32 ( &pool->count ) - 1;
    query->idx = idx;
    return idx;
}

void xg_vk_query_readback ( xg_query_pool_h pool, xg_query_h* queries, uint32_t queries_count, std_buffer_t readback_buffer ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_pool_state->pools_array[handle];
    uint32_t count = pool->count;
    std_assert_m ( buffer.size >= count * sizeof ( uint64_t ) );

    std_alloc_t alloc = std_virtual_heap_alloc_array_m ( uint64_t, count );
    uint64_t* results = ( uint64_t* ) alloc.buffer.base;

    const xg_vk_device_t* device = xg_vk_device_get ( pool->device );
    VkResult result = vkGetQueryPoolResults ( device->vk_handle, pool->vk_pool, 0, pool->count, alloc.buffer.size, alloc.buffer.base, sizeof ( uint64_t ), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
    std_assert_m ( result == VK_SUCCESS );

    uint64_t* readback = ( uint64_t* ) readback_buffer.base;
    xg_vk_query_t* queries = pool->queries;

    for ( uint32_t i = 0; i < count; ++i ) {
        readback[i] = results[queries[i].idx];
    }

    std_virtual_heap_free ( alloc.handle );
}
#endif
