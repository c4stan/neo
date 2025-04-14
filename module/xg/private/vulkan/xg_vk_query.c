#include "xg_vk_query.h"

#include <std_atomic.h>

#include "vulkan/xg_vk_device.h"

static xg_vk_query_state_t* xg_vk_query_state;

void xg_vk_query_load ( xg_vk_query_state_t* state ) {
    xg_vk_query_state = state;

    state->pools_array = std_virtual_heap_alloc_array_m ( xg_vk_query_pool_t, xg_vk_max_query_pools_m );
    state->pools_freelist = std_freelist_m ( state->pools_array, xg_vk_max_query_pools_m );
    state->pools_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xg_vk_max_query_pools_m ) );
    std_mem_zero_array_m ( state->pools_bitset, std_bitset_u64_count_m ( xg_vk_max_query_pools_m ) );
    std_mutex_init ( &state->pools_mutex );
}

void xg_vk_query_reload ( xg_vk_query_state_t* state ) {
    xg_vk_query_state = state;
}

void xg_vk_query_unload ( void ) {
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_query_state->pools_bitset, idx, std_bitset_u64_count_m ( xg_vk_max_query_pools_m ) ) ) {
        //xg_vk_query_pool_t* pool = &xg_vk_query_state->pools_array[idx];
        //std_log_info_m ( "Destroying query pool " std_fmt_u64_m ": " std_fmt_str_m, idx, pool->params.debug_name );
        xg_vk_query_pool_destroy ( idx );
        ++idx;
    }

    std_virtual_heap_free ( xg_vk_query_state->pools_array );
    std_virtual_heap_free ( xg_vk_query_state->pools_bitset );

    std_mutex_deinit ( &xg_vk_query_state->pools_mutex );
}

xg_query_pool_h xg_vk_query_pool_create ( const xg_query_pool_params_t* params ) {
    std_mutex_lock ( &xg_vk_query_state->pools_mutex );
    xg_vk_query_pool_t* pool = std_list_pop_m ( &xg_vk_query_state->pools_freelist );
    std_mutex_unlock ( &xg_vk_query_state->pools_mutex );

    uint64_t capacity = params->capacity;
    std_assert_m ( capacity > 0 );

    VkQueryPoolCreateInfo pool_info;

    switch ( params->type ) {
        case xg_query_pool_type_timestamp_m:
            pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            break;
        default:
            std_assert_m ( false );
    }

    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.pNext = NULL;
    pool_info.flags = 0;
    pool_info.queryCount = capacity;
    pool_info.pipelineStatistics = 0;
    vkCreateQueryPool ( xg_vk_device_get ( params->device )->vk_handle, &pool_info, NULL, &pool->vk_handle );
    pool->params = *params;

    xg_query_pool_h handle = ( xg_query_pool_h ) ( pool - xg_vk_query_state->pools_array );
    std_bitset_set ( xg_vk_query_state->pools_bitset, handle );
    return handle;
}

void xg_vk_query_pool_destroy ( xg_query_pool_h handle ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_state->pools_array[handle];
    vkDestroyQueryPool ( xg_vk_device_get ( pool->params.device )->vk_handle, pool->vk_handle, NULL );    
    std_bitset_clear ( xg_vk_query_state->pools_bitset, handle );
    std_list_push ( &xg_vk_query_state->pools_freelist, pool );
}

void xg_vk_query_pool_read ( std_buffer_t dest, xg_query_pool_h pool_handle ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_state->pools_array[pool_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( pool->params.device );
    VkResult result = vkGetQueryPoolResults ( device->vk_handle, pool->vk_handle, 0, pool->params.capacity, dest.size, dest.base, sizeof ( uint64_t ), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
    std_assert_m ( result == VK_SUCCESS );
}

xg_vk_query_pool_t* xg_vk_query_pool_get ( xg_query_pool_h pool_handle ) {
    xg_vk_query_pool_t* pool = &xg_vk_query_state->pools_array[pool_handle];
    return pool;
}
