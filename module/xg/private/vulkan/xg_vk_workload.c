#include "xg_vk_workload.h"

#include <std_atomic.h>

#include "../xg_cmd_buffer.h"
#include "../xg_resource_cmd_buffer.h"

#include "xg_vk_event.h"
#include "xg_vk_allocator.h"
#include "xg_vk_buffer.h"
#include "xg_vk_device.h"

#include <std_list.h>

static xg_vk_workload_state_t* xg_workload_state;

// TODO always check gen, not only when assert is enabled? or avoid writing it entirely when assert is off?
// TODO properly wrap around when incrementing gen? declared as u64, will break once msb reaches top 10 bits
#define xg_workload_handle_idx_bits_m 10
#define xg_workload_handle_gen_bits_m (64 - xg_workload_handle_idx_bits_m)

static xg_vk_workload_t* get_workload ( xg_workload_h workload_handle ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );
    return workload;
}

void xg_vk_workload_activate_device ( xg_device_h device_handle ) {
#if 0
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );

    xg_vk_workload_device_context_t* context = &xg_workload_state->device_contexts[device_idx];

    for ( uint32_t i = 0; i < xg_workload_max_allocated_workloads_m; ++i ) {
        // uniform buffer
        // TODO sub-allocate from a single big buffer instead of allocating multiple
        xg_buffer_h buffer_handle = xg_buffer_create ( & xg_buffer_params_m (
            .allocator = xg_allocator_default ( device_handle, xg_memory_type_gpu_mappable_m ),
            .device = device_handle,
            .size = xg_vk_workload_uniform_buffer_size_m,
            .allowed_usage = xg_buffer_usage_uniform_m,
            .debug_name = "workload uniform buffer"
        ) );

        xg_buffer_info_t uniform_buffer_info;
        xg_buffer_get_info ( &uniform_buffer_info, buffer_handle );

        xg_vk_workload_uniform_buffer_t* uniform_buffer = &context->uniform_buffers[i];
        uniform_buffer->handle = buffer_handle;
        uniform_buffer->alloc = uniform_buffer_info.allocation;
        //uniform_buffer->base = xg_vk_device_map_alloc ( &uniform_buffer_info.allocation );
        uniform_buffer->used_size = 0;
        uniform_buffer->total_size = xg_vk_workload_uniform_buffer_size_m;
    }
#endif
}

void xg_vk_workload_deactivate_device ( xg_device_h device_handle ) {
#if 0
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );

    xg_vk_workload_device_context_t* context = &xg_workload_state->device_contexts[device_idx];

    for ( uint32_t i = 0; i < xg_workload_max_allocated_workloads_m; ++i ) {
        xg_vk_workload_uniform_buffer_t* uniform_buffer = &context->uniform_buffers[i];
        //xg_vk_device_unmap_alloc ( &uniform_buffer->alloc );
        xg_buffer_destroy ( uniform_buffer->handle );
    }

    std_mem_zero_m ( context );
#endif
}

void xg_vk_workload_load ( xg_vk_workload_state_t* state ) {
    xg_workload_state = state;

    state->workloads_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_t, xg_workload_max_allocated_workloads_m );
    state->workloads_freelist = std_freelist_m ( state->workloads_array, xg_workload_max_allocated_workloads_m );
    std_mutex_init ( &state->workloads_mutex );
    state->workloads_uid = 0;

    for ( size_t i = 0; i < xg_workload_max_allocated_workloads_m; ++i ) {
        xg_vk_workload_t* workload = &state->workloads_array[i];
        std_mutex_init ( &workload->mutex );
        workload->gen = 0;
    }

    std_mem_zero_m ( &state->device_contexts );

    std_assert_m ( xg_workload_max_allocated_workloads_m <= ( 1 << xg_workload_handle_idx_bits_m ) );
}

void xg_vk_workload_reload ( xg_vk_workload_state_t* state ) {
    xg_workload_state = state;
}

void xg_vk_workload_unload ( void ) {
    for ( size_t i = 0; i < xg_workload_max_allocated_workloads_m; ++i ) {
        std_mutex_deinit ( &xg_workload_state->workloads_array[i].mutex );
    }

    std_virtual_heap_free ( xg_workload_state->workloads_array );
    std_mutex_deinit ( &xg_workload_state->workloads_mutex );
    // TODO
}

xg_workload_h xg_workload_create ( xg_device_h device_handle ) {
    std_mutex_lock ( &xg_workload_state->workloads_mutex );

    xg_vk_workload_t* workload = std_list_pop_m ( &xg_workload_state->workloads_freelist );
    uint64_t workload_idx = ( uint64_t ) ( workload - xg_workload_state->workloads_array );

    workload->cmd_buffers_count = 0;
    workload->resource_cmd_buffers_count = 0;
    workload->id = xg_workload_state->workloads_uid++;
    workload->device = device_handle;

    // https://github.com/krOoze/Hello_Triangle/blob/master/doc/Schema.pdf
    //workload->execution_complete_gpu_event = xg_gpu_queue_event_create ( device_handle ); // TODO pre-create these?
    workload->execution_complete_gpu_event = xg_null_handle_m;
    workload->execution_complete_cpu_event = xg_cpu_queue_event_create ( device_handle );
    workload->swapchain_texture_acquired_event = xg_null_handle_m;

    workload->stop_debug_capture_on_present = false;

    xg_buffer_h buffer_handle = xg_buffer_create ( & xg_buffer_params_m (
        .allocator = xg_allocator_default ( device_handle, xg_memory_type_gpu_mappable_m ),
        .device = device_handle,
        .size = xg_vk_workload_uniform_buffer_size_m,
        .allowed_usage = xg_buffer_usage_uniform_m,
        .debug_name = "workload uniform buffer"
    ) );

    xg_buffer_info_t uniform_buffer_info;
    xg_buffer_get_info ( &uniform_buffer_info, buffer_handle );

    xg_vk_workload_uniform_buffer_t* uniform_buffer = &workload->uniform_buffer;
    uniform_buffer->handle = buffer_handle;
    uniform_buffer->alloc = uniform_buffer_info.allocation;
    //uniform_buffer->base = xg_vk_device_map_alloc ( &uniform_buffer_info.allocation );
    uniform_buffer->used_size = 0;
    uniform_buffer->total_size = xg_vk_workload_uniform_buffer_size_m;

#if 0
    workload->timestamp_query_pools_count = 0;
#endif

    std_mutex_unlock ( &xg_workload_state->workloads_mutex );

    return workload->gen << xg_workload_handle_idx_bits_m | workload_idx ;
}

xg_buffer_range_t xg_workload_write_uniform ( xg_workload_h workload_handle, void* data, size_t data_size ) {
    xg_vk_workload_t* workload = get_workload ( workload_handle );
    //uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    //uint64_t workload_idx = ( uint64_t ) ( workload - xg_workload_state->workloads_array );
    //xg_vk_workload_uniform_buffer_t* uniform_buffer = &xg_workload_state->device_contexts[device_idx].uniform_buffers[workload_idx];
    xg_vk_workload_uniform_buffer_t* uniform_buffer = &workload->uniform_buffer;

    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
    uint64_t alignment = device->generic_properties.limits.minUniformBufferOffsetAlignment;

    char* base = uniform_buffer->alloc.mapped_address;//uniform_buffer->base;
    std_assert_m ( base != NULL );
    uint64_t offset = 0; // this is relative to the start of the VkBuffer object, not to the underlying Vk memory allocation

    uint64_t used_size;
    uint64_t top;
    uint64_t new_used_size;

    do {
        used_size = uniform_buffer->used_size;
        top = std_align_u64 ( offset + uniform_buffer->used_size, alignment );
        new_used_size = top + data_size - offset;
        //workload->uniform_buffer.used_size = new_used_size;
    } while ( !std_compare_and_swap_u64 ( &uniform_buffer->used_size, &used_size, new_used_size ) );

    std_assert_m ( new_used_size < uniform_buffer->total_size );
    std_mem_copy ( base + top, data, data_size );

    xg_buffer_range_t range;
    range.handle = uniform_buffer->handle;
    range.offset = top;
    range.size = data_size;
    return range;
}

xg_cmd_buffer_h xg_workload_add_cmd_buffer ( xg_workload_h workload_handle ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    xg_cmd_buffer_h cmd_buffer = xg_cmd_buffer_open ( workload_handle );

    workload->cmd_buffers[workload->cmd_buffers_count++] = cmd_buffer;

    return cmd_buffer;
}

xg_resource_cmd_buffer_h xg_workload_add_resource_cmd_buffer ( xg_workload_h workload_handle ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    xg_resource_cmd_buffer_h cmd_buffer = xg_resource_cmd_buffer_open ( workload_handle );

    workload->resource_cmd_buffers[workload->resource_cmd_buffers_count++] = cmd_buffer;

    return cmd_buffer;
}

const xg_vk_workload_t* xg_vk_workload_get ( xg_workload_h workload_handle ) {
    return get_workload ( workload_handle );
}

bool xg_workload_is_complete ( xg_workload_h workload_handle ) {
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    return workload->gen > handle_gen;
}

void xg_workload_destroy ( xg_workload_h workload_handle ) {
    std_mutex_lock ( &xg_workload_state->workloads_mutex );

    xg_vk_workload_t* workload = get_workload ( workload_handle );

    // gen
    ++workload->gen;

    // cmd buffer
    xg_cmd_buffer_discard ( workload->cmd_buffers, workload->cmd_buffers_count );
    xg_resource_cmd_buffer_discard ( workload->resource_cmd_buffers, workload->resource_cmd_buffers_count );

    // events
    //xg_gpu_queue_event_destroy ( workload->execution_complete_gpu_event );
    xg_cpu_queue_event_destroy ( workload->execution_complete_cpu_event );

    if ( workload->swapchain_texture_acquired_event != xg_null_handle_m ) {
        xg_gpu_queue_event_destroy ( workload->swapchain_texture_acquired_event );
    }

    //uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    //uint64_t workload_idx = ( uint64_t ) ( workload - xg_workload_state->workloads_array );
    //xg_vk_workload_uniform_buffer_t* uniform_buffer = &xg_workload_state->device_contexts[device_idx].uniform_buffers[workload_idx];
    //uniform_buffer->used_size = 0;

    xg_buffer_destroy ( workload->uniform_buffer.handle );
    workload->uniform_buffer.handle = xg_null_handle_m;

#if 0

    for ( uint32_t i = 0; i < workload->timestamp_query_pools_count; ++i ) {
        xg_vk_workload_query_pool_t* pool = &workload->timestamp_query_pools[i];
        xg_vk_timestamp_query_pool_readback ( pool->handle, pool->buffer );
    }

#endif

    std_list_push ( &xg_workload_state->workloads_freelist, workload );
    std_mutex_unlock ( &xg_workload_state->workloads_mutex );
}

/*void xg_workload_set_swapchain_texture_acquired_event ( xg_workload_h workload_handle, xg_gpu_queue_event_h event_handle ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = ( xg_vk_workload_t* ) std_pool_at ( &xg_workload_state.workloads_pool, handle_idx );
    std_assert_m ( workload->gen == handle_gen );

    std_unused_m ( event_handle );
    //workload->swapchain_texture_acquired_event = event_handle;
}*/

void xg_workload_set_execution_complete_gpu_event ( xg_workload_h workload_handle, xg_gpu_queue_event_h event ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    workload->execution_complete_gpu_event = event;
}

void xg_workload_init_swapchain_texture_acquired_gpu_event ( xg_workload_h workload_handle ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    workload->swapchain_texture_acquired_event = xg_gpu_queue_event_create ( workload->device );
}

void xg_vk_workload_queue_debug_capture_stop_on_present ( xg_workload_h workload_handle ) {
#if std_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_workload_state->workloads_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    workload->stop_debug_capture_on_present = true;
}

#if 0
void xg_vk_workload_add_timestamp_query_pool ( xg_workload_h workload_handle, xg_timestamp_query_pool_h pool_handle, std_buffer_t buffer ) {
    xg_vk_workload_t* workload = get_workload ( workload_handle );

    uint32_t pool_idx = std_atomic_increment_u32 ( &workload->timestamp_query_pools_count );
    std_assert_m ( pool_idx < xg_workload_max_timestamp_query_pools_per_workload_m );

    xg_vk_workload_query_pool_t* pool = &workload->timestamp_query_pools[pool_idx];
    pool->handle = pool_handle;
    pool->buffer = buffer;
}
#endif
