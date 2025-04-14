#include "xg_vk_workload.h"

#include <std_atomic.h>
#include <std_sort.h>

#include <xg_enum.h>

#include "../xg_cmd_buffer.h"
#include "../xg_resource_cmd_buffer.h"
#include "../xg_debug_capture.h"

#include "xg_vk_event.h"
#include "xg_vk_allocator.h"
#include "xg_vk_buffer.h"
#include "xg_vk_device.h"
#include "xg_vk_pipeline.h"
#include "xg_vk_texture.h"
#include "xg_vk_enum.h"
#include "xg_vk_sampler.h"
#include "xg_vk_instance.h"
#include "xg_vk_raytrace.h"
#include "xg_vk_query.h"

#include <std_list.h>

static xg_vk_workload_state_t* xg_vk_workload_state;

#if defined ( std_compiler_gcc_m )
std_warnings_ignore_m ( "-Wint-to-pointer-cast" )
#endif

void xg_vk_workload_load ( xg_vk_workload_state_t* state ) {
    xg_vk_workload_state = state;

    state->workload_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_t, xg_workload_max_allocated_workloads_m );
    state->workload_freelist = std_freelist_m ( state->workload_array, xg_workload_max_allocated_workloads_m );
    state->workloads_uid = 0;

    for ( size_t i = 0; i < xg_workload_max_allocated_workloads_m; ++i ) {
        xg_vk_workload_t* workload = &state->workload_array[i];
        workload->gen = 0;
    }

    std_mem_zero_m ( &state->device_contexts );
}

void xg_vk_workload_reload ( xg_vk_workload_state_t* state ) {
    xg_vk_workload_state = state;
}

void xg_vk_workload_unload ( void ) {
    xg_workload_wait_all_workload_complete();

    std_virtual_heap_free ( xg_vk_workload_state->workload_array );
    // TODO
}

// ======================================================================================= //
//                                     W O R K L O A D
// ======================================================================================= //
//
//  Workload handle:
//     lsb                      msb
//      --------------------------
//      |  index  |  generation  |
//      --------------------------
//      |    10   |      54      |
//      --------------------------
//
// TODO always check gen, not only when assert is enabled? or avoid writing it entirely when assert is off?
#define xg_workload_handle_idx_bits_m 10
#define xg_workload_handle_gen_bits_m (64 - xg_workload_handle_idx_bits_m)

std_static_assert_m ( xg_workload_max_allocated_workloads_m <= ( 1 << xg_workload_handle_idx_bits_m ) );

static xg_vk_workload_t* xg_vk_workload_edit ( xg_workload_h workload_handle ) {
#if std_log_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64_m ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64_m ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_vk_workload_state->workload_array[handle_idx];
    return workload->gen == handle_gen ? workload : NULL;
}

const xg_vk_workload_t* xg_vk_workload_get ( xg_workload_h workload_handle ) {
    return xg_vk_workload_edit ( workload_handle );
}

static xg_vk_workload_device_context_t* xg_vk_workload_device_context_get ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_workload_device_context_t* context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( context->device_handle != xg_null_handle_m );
    return context;
}

static void xg_vk_desc_allocator_reset_counters ( xg_vk_desc_allocator_t* allocator ) {
    allocator->set_count = xg_vk_max_sets_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_sampler_m] = xg_vk_max_samplers_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_texture_to_sample_m] = xg_vk_max_sampled_texture_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_texture_storage_m] = xg_vk_max_storage_texture_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_buffer_uniform_m] = xg_vk_max_uniform_buffer_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_buffer_storage_m] = xg_vk_max_storage_buffer_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_buffer_texel_uniform_m] = xg_vk_max_uniform_texel_buffer_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_buffer_texel_storage_m] = xg_vk_max_storage_texel_buffer_per_descriptor_pool_m;
    allocator->descriptor_counts[xg_resource_binding_raytrace_world_m] = xg_vk_max_raytrace_world_per_descriptor_pool_m;    
}

static void xg_vk_desc_allocator_init ( xg_vk_desc_allocator_t* allocator, xg_device_h device_handle ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    VkDescriptorPoolSize sizes[xg_resource_binding_count_m] = {
        [xg_resource_binding_sampler_m] = { .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = xg_vk_max_samplers_per_descriptor_pool_m },
        [xg_resource_binding_texture_to_sample_m] = { .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = xg_vk_max_sampled_texture_per_descriptor_pool_m },
        [xg_resource_binding_texture_storage_m] = { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = xg_vk_max_storage_texture_per_descriptor_pool_m },
        [xg_resource_binding_buffer_uniform_m] = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = xg_vk_max_uniform_buffer_per_descriptor_pool_m },
        [xg_resource_binding_buffer_storage_m] = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = xg_vk_max_storage_buffer_per_descriptor_pool_m },
        [xg_resource_binding_buffer_texel_uniform_m] = { .type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, .descriptorCount = xg_vk_max_uniform_texel_buffer_per_descriptor_pool_m },
        [xg_resource_binding_buffer_texel_storage_m] = { .type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, .descriptorCount = xg_vk_max_storage_texel_buffer_per_descriptor_pool_m },
#if xg_enable_raytracing_m
    #if xg_vk_enable_nv_raytracing_ext_m
        [xg_resource_binding_raytrace_world_m] = { .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV,
    #else
        [xg_resource_binding_raytrace_world_m] = { .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
    #endif
        .descriptorCount = xg_vk_max_raytrace_world_per_descriptor_pool_m },
#endif
    };
    VkDescriptorPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .maxSets = xg_vk_max_sets_per_descriptor_pool_m,
    #if xg_enable_raytracing_m
        .poolSizeCount = xg_resource_binding_count_m,
    #else
        .poolSizeCount = xg_resource_binding_count_m - 1,
    #endif
        .pPoolSizes = sizes,
    };

    VkResult result = vkCreateDescriptorPool ( device->vk_handle, &info, NULL, &allocator->vk_desc_pool );
    xg_vk_assert_m ( result );

    xg_vk_desc_allocator_reset_counters ( allocator );
}

static void xg_vk_cmd_allocator_init ( xg_vk_cmd_allocator_t* allocator, xg_device_h device_handle, xg_cmd_queue_e queue ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, // No support for cmd buffer reuse
        .queueFamilyIndex = device->queues[queue].vk_family_idx,
    };
    VkResult result = vkCreateCommandPool ( device->vk_handle, &pool_info, NULL, &allocator->vk_cmd_pool );
    xg_vk_assert_m ( result );

    // cmd buffers
    VkCommandBufferAllocateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = NULL,
        .commandPool = allocator->vk_cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,                               // TODO allocate some primary and some secondary
        .commandBufferCount = xg_vk_workload_cmd_buffers_per_allocator_m,       // TODO preallocate only some and allocate more when we run out?
    };
    result = vkAllocateCommandBuffers ( device->vk_handle, &buffer_info, allocator->vk_cmd_buffers );
    xg_vk_assert_m ( result );

    allocator->cmd_buffers_count = 0;
}

static void xg_vk_cmd_allocator_reset ( xg_vk_cmd_allocator_t* allocator, xg_device_h device_handle) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    vkResetCommandPool ( device->vk_handle, allocator->vk_cmd_pool, 0 ); // VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT
    allocator->cmd_buffers_count = 0;
}

static void xg_vk_workload_context_init ( xg_vk_workload_context_t* context ) {
    context->submit.events_count = 0;
    context->is_submitted = false;
}

#define xg_vk_workload_max_uniform_buffers_m std_div_ceil_m ( xg_workload_max_uniform_size_m, xg_workload_uniform_buffer_size_m )
#define xg_vk_workload_max_staging_buffers_m std_div_ceil_m ( xg_workload_max_staging_size_m, xg_workload_staging_buffer_size_m )

void xg_vk_workload_activate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    std_assert_m ( device_idx < xg_max_active_devices_m );

    xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( !device_context->is_active );
    device_context->is_active = true;

    device_context->device_handle = device_handle;

    device_context->workload_contexts_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_context_t, xg_workload_max_queued_workloads_m );

    device_context->workload_contexts_ring = std_ring ( xg_workload_max_queued_workloads_m );

    for ( size_t i = 0; i <  xg_workload_max_queued_workloads_m; ++i ) {
        xg_vk_workload_context_t* workload_context = &device_context->workload_contexts_array[i];
        workload_context->is_submitted = false;
        workload_context->workload = xg_null_handle_m;

        workload_context->sort.result = NULL;
        workload_context->sort.temp = NULL;
        workload_context->sort.capacity = 0;

        workload_context->chunk.cmd_chunks_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_cmd_chunk_t, 1024 );
        workload_context->chunk.queue_chunks_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_queue_chunk_t, 1024 );
        workload_context->chunk.cmd_chunks_capacity = 1024;
        workload_context->chunk.queue_chunks_capacity = 1024;

        xg_vk_cmd_allocator_t* graphics_cmd_allocator = &workload_context->translate.cmd_allocators[xg_cmd_queue_graphics_m];
        xg_vk_cmd_allocator_t* compute_cmd_allocator = &workload_context->translate.cmd_allocators[xg_cmd_queue_compute_m];
        xg_vk_cmd_allocator_t* copy_cmd_allocator = &workload_context->translate.cmd_allocators[xg_cmd_queue_copy_m];
        xg_vk_cmd_allocator_init ( graphics_cmd_allocator, device_handle, xg_cmd_queue_graphics_m );
        xg_vk_cmd_allocator_init ( compute_cmd_allocator, device_handle, xg_cmd_queue_compute_m );
        xg_vk_cmd_allocator_init ( copy_cmd_allocator, device_handle, xg_cmd_queue_copy_m );
        workload_context->translate.translated_chunks_array = std_virtual_heap_alloc_array_m ( VkCommandBuffer, 1024 );
        workload_context->translate.translated_chunks_capacity = 1024;

        workload_context->submit.events_count = 0;
    }

    device_context->desc_allocators_array = std_virtual_heap_alloc_array_m ( xg_vk_desc_allocator_t, xg_vk_workload_max_desc_allocators_m );
    device_context->desc_allocators_freelist = std_freelist_m ( device_context->desc_allocators_array, xg_vk_workload_max_desc_allocators_m );

    for ( uint32_t i = 0; i < xg_vk_workload_max_desc_allocators_m; ++i ) {
        xg_vk_desc_allocator_init ( &device_context->desc_allocators_array[i], device_handle );
    }

    device_context->uniform_buffers_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_buffer_t, xg_vk_workload_max_uniform_buffers_m );
    device_context->uniform_buffers_freelist = std_freelist_m ( device_context->uniform_buffers_array, xg_vk_workload_max_uniform_buffers_m );
    device_context->staging_buffers_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_buffer_t, xg_vk_workload_max_staging_buffers_m );
    device_context->staging_buffers_freelist = std_freelist_m ( device_context->staging_buffers_array, xg_vk_workload_max_staging_buffers_m );

    // TODO have all workload uniform buffers use a single permanently allocated buffer as backend,
    //      and just offset into that, in order to support dynamic uniform buffers ( just need to 
    //      rebind same descriptor with new offset instead of re-allocating a new descriptor 
    //      and/or writing to it )
    for ( uint32_t i = 0; i < xg_vk_workload_max_uniform_buffers_m; ++i ) {
        xg_buffer_h uniform_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
            .memory_type = xg_memory_type_gpu_mapped_m,
            .device = device_handle,
            .size = xg_workload_uniform_buffer_size_m,
            .allowed_usage = xg_buffer_usage_bit_uniform_m,
            .debug_name = "workload_uniform_buffer"
        ) );
        xg_buffer_info_t uniform_buffer_info;
        xg_buffer_get_info ( &uniform_buffer_info, uniform_buffer_handle );
        xg_vk_workload_buffer_t* buffer = &device_context->uniform_buffers_array[i];
        buffer->handle = uniform_buffer_handle;
        buffer->alloc = uniform_buffer_info.allocation;
        buffer->used_size = 0;
        buffer->total_size = xg_workload_uniform_buffer_size_m;
    }

    for ( uint32_t i = 0; i < xg_vk_workload_max_staging_buffers_m; ++i ) {
        xg_buffer_h staging_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
            .memory_type = xg_memory_type_upload_m,
            .device = device_handle,
            .size = xg_workload_staging_buffer_size_m,
            .allowed_usage = xg_buffer_usage_bit_copy_source_m,
            .debug_name = "workload_staging_buffer",
        ) );
        xg_buffer_info_t staging_buffer_info;
        xg_buffer_get_info ( &staging_buffer_info, staging_buffer_handle );
        xg_vk_workload_buffer_t* buffer = &device_context->staging_buffers_array[i];
        buffer->handle = staging_buffer_handle;
        buffer->alloc = staging_buffer_info.allocation;
        buffer->used_size = 0;
        buffer->total_size = xg_workload_staging_buffer_size_m;
    }
}

void xg_vk_workload_deactivate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( device_context->is_active );
    const xg_vk_device_t* device = xg_vk_device_get ( device_context->device_handle );

    std_virtual_heap_free ( device_context->workload_contexts_array );
    std_virtual_heap_free ( device_context->desc_allocators_array );

    std_virtual_heap_free ( device_context->uniform_buffers_array );
    std_virtual_heap_free ( device_context->staging_buffers_array );

    for ( size_t i = 0; i <  xg_workload_max_queued_workloads_m; ++i ) {
        xg_vk_workload_context_t* workload_context = &device_context->workload_contexts_array[i];

        std_virtual_heap_free ( workload_context->chunk.cmd_chunks_array );
        std_virtual_heap_free ( workload_context->chunk.queue_chunks_array );
        std_virtual_heap_free ( workload_context->translate.translated_chunks_array );
        std_virtual_heap_free ( workload_context->sort.result );
        std_virtual_heap_free ( workload_context->sort.temp );

        //if ( submit_context->is_submitted ) {
        //    const xg_vk_workload_t* workload = xg_vk_workload_get ( submit_context->workload );
        //    const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );
        //    vkWaitForFences ( device->vk_handle, 1, &fence->vk_fence, VK_TRUE, UINT64_MAX );
        //}

        // Call this to process on workload complete events like resource destruction
        //xg_vk_workload_recycle_submission_contexts ( context, UINT64_MAX );

        // TODO
        vkDestroyCommandPool ( device->vk_handle, workload_context->translate.cmd_allocators[xg_cmd_queue_graphics_m].vk_cmd_pool, NULL );
        vkDestroyCommandPool ( device->vk_handle, workload_context->translate.cmd_allocators[xg_cmd_queue_compute_m].vk_cmd_pool, NULL );
        vkDestroyCommandPool ( device->vk_handle, workload_context->translate.cmd_allocators[xg_cmd_queue_copy_m].vk_cmd_pool, NULL );
    }

    for ( uint32_t i = 0; i < xg_vk_workload_max_desc_allocators_m; ++i ) {
        vkDestroyDescriptorPool ( device->vk_handle, device_context->desc_allocators_array[i].vk_desc_pool, NULL );
    }
}

static xg_vk_desc_allocator_t* xg_vk_desc_allocator_pop ( xg_device_h device_handle, xg_workload_h workload_handle ) {
    xg_vk_workload_device_context_t* device_context = xg_vk_workload_device_context_get ( device_handle );
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_desc_allocator_t* desc_allocator = std_list_pop_m ( &device_context->desc_allocators_freelist );
    uint32_t desc_allocator_idx = std_atomic_increment_u32 ( &workload->desc_allocators_count ) - 1;
    workload->desc_allocators_array[desc_allocator_idx] = desc_allocator;
    return desc_allocator;
}

#if 0
static xg_vk_cmd_allocator_t* xg_vk_cmd_allocator_pop ( xg_device_h device_handle, xg_workload_h workload_handle, xg_cmd_queue_e queue ) {
    xg_vk_workload_device_context_t* device_context = xg_vk_workload_device_context_get ( device_handle );
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_cmd_allocator_t* cmd_allocator = std_list_pop_m ( &device_context->cmd_allocators_freelist[queue] );
    uint32_t cmd_allocator_idx = std_atomic_increment_u32 ( &workload->cmd_allocators_count[i] ) - 1;
    workload_cmd_allocators_array[queue][cmd_allocator_idx] = cmd_allocator;
    return cmd_allocator;
}
#endif

static xg_vk_workload_buffer_t* xg_vk_uniform_buffer_pop ( xg_device_h device_handle, xg_workload_h workload_handle ) {
    xg_vk_workload_device_context_t* device_context = xg_vk_workload_device_context_get ( device_handle );
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_workload_buffer_t* buffer = std_list_pop_m ( &device_context->uniform_buffers_freelist );
    std_assert_m ( buffer );
    workload->uniform_buffer = buffer;
    workload->uniform_buffers_array[workload->uniform_buffers_count++] = buffer;
    return buffer;
}

static xg_vk_workload_buffer_t* xg_vk_staging_buffer_pop ( xg_device_h device_handle, xg_workload_h workload_handle ) {
    xg_vk_workload_device_context_t* device_context = xg_vk_workload_device_context_get ( device_handle );
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_workload_buffer_t* buffer = std_list_pop_m ( &device_context->staging_buffers_freelist );
    std_assert_m ( buffer );
    workload->staging_buffer = buffer;
    workload->staging_buffers_array[workload->staging_buffers_count++] = buffer;
    return buffer;
}

xg_workload_h xg_workload_create ( xg_device_h device_handle ) {
    xg_vk_workload_t* workload = std_list_pop_m ( &xg_vk_workload_state->workload_freelist );
    uint64_t workload_idx = ( uint64_t ) ( workload - xg_vk_workload_state->workload_array );
    xg_workload_h workload_handle = workload->gen << xg_workload_handle_idx_bits_m | workload_idx;

    workload->id = xg_vk_workload_state->workloads_uid++;
    workload->device = device_handle;

    workload->cmd_buffers_count = 0;
    workload->resource_cmd_buffers_count = 0;

    workload->desc_allocators_count = 0;
    workload->desc_layouts_count = 0;
    workload->desc_allocator = xg_vk_desc_allocator_pop ( device_handle, workload_handle );

    workload->uniform_buffers_count = 0;
    workload->uniform_buffer = xg_vk_uniform_buffer_pop ( device_handle, workload_handle );
    workload->staging_buffers_count = 0;
    workload->staging_buffer = xg_vk_staging_buffer_pop ( device_handle, workload_handle );

    // https://github.com/krOoze/Hello_Triangle/blob/master/doc/Schema.pdf
    //workload->execution_complete_gpu_event = xg_gpu_queue_event_create ( device_handle ); // TODO pre-create these?
    workload->execution_complete_gpu_event = xg_null_handle_m;
    workload->execution_complete_cpu_event = xg_cpu_queue_event_create ( device_handle );
    workload->swapchain_texture_acquired_event = xg_null_handle_m;

    workload->stop_debug_capture_on_present = false;

    workload->global_bindings = xg_null_handle_m;

    workload->debug_capture = false;
#if 0
    workload->timestamp_query_pools_count = 0;
#endif

    return workload_handle;
}

void xg_vk_workload_enable_debug_capture ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    workload->debug_capture = true;
}

#define xg_vk_resource_group_handle_is_permanent_m( h ) ( std_bit_read_ms_64_m( h, 1 ) == 0 )
#define xg_vk_resource_group_handle_is_workload_m( h ) ( std_bit_read_ms_64_m( h, 1 ) == 1 )
#define xg_vk_resource_group_handle_tag_as_permanent_m( h ) ( std_bit_write_ms_64_m( h, 1, 0 ) )
#define xg_vk_resource_group_handle_tag_as_workload_m( h ) ( std_bit_write_ms_64_m( h, 1, 1 ) )
#define xg_vk_resource_group_handle_remove_tag_m( h ) ( std_bit_clear_ms_64_m( h, 1 ) )

xg_resource_bindings_h xg_workload_create_resource_group ( xg_workload_h workload_handle, xg_resource_bindings_layout_h layout_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    uint32_t idx = std_atomic_increment_u32 ( &workload->desc_layouts_count ) - 1;
    workload->desc_layouts_array[idx] = layout_handle;
    xg_resource_bindings_h bindings_handle = xg_vk_resource_group_handle_tag_as_workload_m ( idx );
    return bindings_handle;
}

void xg_vk_workload_allocate_resource_groups ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );

    uint32_t desc_layouts_count = workload->desc_layouts_count;
    if ( desc_layouts_count == 0 ) {
        return;
    }

    VkDescriptorSetLayout vk_layouts[xg_vk_workload_max_resource_bindings_m];
    for ( uint32_t i = 0; i < desc_layouts_count; ++i ) {
        xg_resource_bindings_layout_h layout_handle = workload->desc_layouts_array[i];
        const xg_vk_resource_bindings_layout_t* layout = xg_vk_pipeline_resource_bindings_layout_get ( layout_handle );
        vk_layouts[i] = layout->vk_handle;
    }

    VkDescriptorSetAllocateInfo vk_set_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = workload->desc_allocator->vk_desc_pool,
        .descriptorSetCount = desc_layouts_count,
        .pSetLayouts = vk_layouts,
    };
    VkResult set_alloc_result = vkAllocateDescriptorSets ( device->vk_handle, &vk_set_alloc_info, workload->desc_sets_array );
    xg_vk_assert_m ( set_alloc_result ); // TODO test for available desc count, use multiple allocators if needed
}

VkDescriptorSet xg_vk_workload_resource_bindings_get_desc_set ( xg_device_h device_handle, xg_workload_h workload_handle, xg_resource_bindings_h bindings_handle ) {
    if ( xg_vk_resource_group_handle_is_workload_m ( bindings_handle ) ) {
        const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
        bindings_handle = xg_vk_resource_group_handle_remove_tag_m ( bindings_handle );
        return workload->desc_sets_array[bindings_handle];
    } else {
        const xg_vk_resource_bindings_t* bindings = xg_vk_pipeline_resource_group_get ( device_handle, bindings_handle );
        return bindings->vk_handle;
    }
}

void xg_vk_workload_update_resource_groups ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );

    VkWriteDescriptorSet writes_array[xg_vk_workload_resource_bindings_update_batch_size_m * xg_pipeline_resource_max_bindings_m];
    uint32_t writes_count = 0;
    VkDescriptorBufferInfo buffer_info_array[xg_vk_workload_resource_bindings_update_batch_size_m * xg_pipeline_resource_max_bindings_m];
    uint32_t buffer_info_count = 0;
    VkDescriptorImageInfo image_info_array[xg_vk_workload_resource_bindings_update_batch_size_m * xg_pipeline_resource_max_bindings_m];
    uint32_t image_info_count = 0;

    for ( size_t i = 0; i < workload->resource_cmd_buffers_count; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( workload->resource_cmd_buffers[i] );
        xg_resource_cmd_header_t* cmd_header = ( xg_resource_cmd_header_t* ) cmd_buffer->cmd_headers_allocator.base;
        std_assert_m ( std_align_test_ptr ( cmd_header, xg_resource_cmd_buffer_cmd_alignment_m ) );
        size_t cmd_headers_size = std_queue_local_used_size ( &cmd_buffer->cmd_headers_allocator );
        const xg_resource_cmd_header_t* cmd_headers_end = ( xg_resource_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.base + cmd_headers_size );

        for ( const xg_resource_cmd_header_t* header = cmd_header; header < cmd_headers_end; ++header ) {
            switch ( header->type ) {
                case xg_resource_cmd_resource_bindings_update_m: {
                    std_auto_m args = ( xg_resource_cmd_resource_bindings_update_t* ) header->args;

                    xg_resource_bindings_h group_handle = args->group;

                    if ( !xg_vk_resource_group_handle_is_workload_m ( group_handle ) ) {
                        continue;
                    }

                    group_handle = xg_vk_resource_group_handle_remove_tag_m ( group_handle );
                    xg_resource_bindings_layout_h resource_bindings_layout_handle = workload->desc_layouts_array[group_handle];
                    const xg_vk_resource_bindings_layout_t* resource_bindings_layout = xg_vk_pipeline_resource_bindings_layout_get ( resource_bindings_layout_handle );
                    VkDescriptorSet vk_set = workload->desc_sets_array[group_handle];

                    uint32_t buffers_count = args->buffer_count;
                    uint32_t textures_count = args->texture_count;
                    uint32_t samplers_count = args->sampler_count;
                    uint32_t raytrace_world_count = args->raytrace_world_count;

                    std_assert_m ( buffers_count <= xg_pipeline_resource_max_buffers_per_set_m );
                    std_assert_m ( textures_count <= xg_pipeline_resource_max_textures_per_set_m );
                    std_assert_m ( samplers_count <= xg_pipeline_resource_max_samplers_per_set_m );
                    std_assert_m ( raytrace_world_count <= xg_pipeline_resource_max_raytrace_worlds_per_set_m );

                    xg_buffer_resource_binding_t* buffers_array = std_align_ptr_m ( args + 1, xg_buffer_resource_binding_t );
                    xg_texture_resource_binding_t* textures_array = std_align_ptr_m ( buffers_array + buffers_count, xg_texture_resource_binding_t );
                    xg_sampler_resource_binding_t* samplers_array = std_align_ptr_m ( textures_array + textures_count, xg_sampler_resource_binding_t );
                    xg_raytrace_world_resource_binding_t* raytrace_worlds_array = std_align_ptr_m ( samplers_array + samplers_count, xg_raytrace_world_resource_binding_t );

                    for ( uint32_t i = 0; i < buffers_count; ++i ) {
                        const xg_buffer_resource_binding_t* binding = &buffers_array[i];
                        std_assert_m ( binding->shader_register != -1, "Resource binding shader register not set" );
                        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
                        const xg_resource_binding_layout_t* layout = &resource_bindings_layout->params.resources[binding_idx];
                        xg_resource_binding_e binding_type = layout->type;
                        const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( binding->range.handle );

                        VkDescriptorBufferInfo* info = &buffer_info_array[buffer_info_count++];
                        info->offset = binding->range.offset;
                        // TODO check for size <= maxUniformBufferRange
                        info->range = binding->range.size == xg_buffer_whole_size_m ? VK_WHOLE_SIZE : binding->range.size;
                        info->buffer = buffer->vk_handle;

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = NULL;
                        write->dstSet = vk_set;
                        write->dstBinding = binding_idx;
                        write->dstArrayElement = 0; // TODO
                        write->descriptorCount = 1;
                        write->pBufferInfo = info;

                        switch ( binding_type ) {
                            case xg_resource_binding_buffer_uniform_m:
                                write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                                break;

                            case xg_resource_binding_buffer_storage_m:
                                write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                                break;

                            default:
                                std_not_implemented_m();
                        }
                    }

                    for ( uint32_t i = 0; i < textures_count; ++i ) {
                        const xg_texture_resource_binding_t* binding = &textures_array[i];
                        std_assert_m ( binding->shader_register != -1, "Resource binding shader register not set" );
                        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
                        const xg_resource_binding_layout_t* layout = &resource_bindings_layout->params.resources[binding_idx];
                        xg_resource_binding_e binding_type = layout->type;
                        const xg_vk_texture_view_t* view = xg_vk_texture_get_view ( binding->texture, binding->view );

                        VkDescriptorImageInfo* info = &image_info_array[image_info_count++];
                        info->sampler = VK_NULL_HANDLE;
                        info->imageView = view->vk_handle;
                        info->imageLayout = xg_image_layout_to_vk ( binding->layout );

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = NULL;
                        write->dstSet = vk_set;
                        write->dstBinding = binding_idx;
                        write->dstArrayElement = 0; // TODO
                        write->descriptorCount = 1;
                        write->pImageInfo = info;

                        switch ( binding_type ) {
                            case xg_resource_binding_texture_to_sample_m:
                                write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                                break;

                            case xg_resource_binding_texture_storage_m:
                                write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                                break;

                            default:
                                std_not_implemented_m();
                        }
                    }

                    for ( uint32_t i = 0; i < samplers_count; ++i ) {
                        const xg_sampler_resource_binding_t* binding = &samplers_array[i];
                        std_assert_m ( binding->shader_register != -1, "Resource binding shader register not set" );
                        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
                        const xg_vk_sampler_t* sampler = xg_vk_sampler_get ( binding->sampler );

                        VkDescriptorImageInfo* info = &image_info_array[image_info_count++];
                        info->sampler = sampler->vk_handle;
                        info->imageView = VK_NULL_HANDLE;

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = NULL;
                        write->dstSet = vk_set;
                        write->dstBinding = binding_idx;
                        write->dstArrayElement = 0; // TODO
                        write->descriptorCount = 1;
                        write->pImageInfo = info;
                        write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    }

                    for ( uint32_t i = 0; i < raytrace_world_count; ++i ) {
                        xg_raytrace_world_resource_binding_t* binding = &raytrace_worlds_array[i];
                        std_assert_m ( binding->shader_register != -1, "Resource binding shader register not set" );
                        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
                        const xg_vk_raytrace_world_t* world = xg_vk_raytrace_world_get ( binding->world );

#if 0//xg_vk_enable_nv_raytracing_ext_m
                        VkWriteDescriptorSetAccelerationStructureNV as_info = {
                            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV,
                            .pNext = NULL,
                            .accelerationStructureCount = 1,
                            .pAccelerationStructures = ( const VkAccelerationStructureNV* ) &world->vk_handle,
                        };

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = &as_info;
                        write->dstSet = vk_set;
                        write->dstBinding = binding_idx;
                        write->descriptorCount = 1;
                        write->descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
#else
                        VkWriteDescriptorSetAccelerationStructureKHR as_info = {
                            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                            .pNext = NULL,
                            .accelerationStructureCount = 1,
                            .pAccelerationStructures = &world->vk_handle,
                        };

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = &as_info;
                        write->dstSet = vk_set;
                        write->dstBinding = binding_idx;
                        write->descriptorCount = 1;
                        write->descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
#endif
                    }
                    break;
                }

            }
        }

        if ( writes_count >= xg_vk_workload_resource_bindings_update_batch_size_m - xg_pipeline_resource_max_bindings_m ) {
            vkUpdateDescriptorSets ( device->vk_handle, writes_count, writes_array, 0, NULL );
            writes_count = 0;
            buffer_info_count = 0;
            image_info_count = 0;
        }
    }

    if ( writes_count > 0 ) {
        vkUpdateDescriptorSets ( device->vk_handle, writes_count, writes_array, 0, NULL );
    }
}

void xg_workload_destroy ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    // gen
    workload->gen = ( workload->gen + 1 ) & std_bit_mask_ls_64_m ( xg_workload_handle_gen_bits_m );

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
    //uint64_t workload_idx = ( uint64_t ) ( workload - xg_vk_workload_state->workload_array );
    //xg_vk_workload_buffer_t* uniform_buffer = &xg_vk_workload_state->device_contexts[device_idx].uniform_buffers[workload_idx];
    //uniform_buffer->used_size = 0;

    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
    xg_vk_workload_device_context_t* context = xg_vk_workload_device_context_get ( workload->device );

    for ( uint32_t i = 0; i < workload->desc_allocators_count; ++i ) {
        xg_vk_desc_allocator_t* allocator = workload->desc_allocators_array[i];
        vkResetDescriptorPool ( device->vk_handle, allocator->vk_desc_pool, 0 );
        xg_vk_desc_allocator_reset_counters ( allocator );
        std_list_push ( &context->desc_allocators_freelist, allocator );
    }

    for ( uint32_t i = 0; i < workload->uniform_buffers_count; ++i  ) {
        std_list_push ( &context->uniform_buffers_freelist, workload->uniform_buffers_array[i] );
    }
    for ( uint32_t i = 0; i < workload->staging_buffers_count; ++i ) {
        std_list_push ( &context->staging_buffers_freelist, workload->staging_buffers_array[i] );
    }

#if 0
    for ( uint32_t q = 0; q < xg_cmd_queue_count_m; ++q ) {
        xg_vk_cmd_allocator_t* cmd_allocators_array = workload->cmd_allocators_array[q];
        for ( uint32_t i = 0; i < workload->cmd_allocators_count; ++i ) {
            xg_vk_cmd_allocator_t* cmd_allocator = &cmd_allocators_array[i];
            vkResetCommandPool ( device->vk_handle, cmd_allocator->vk_cmd_pool, 0 ); // VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT
            cmd_allocator->cmd_buffers_count = 0;
        }
    }
#endif

#if 0
    for ( uint32_t i = 0; i < workload->timestamp_query_pools_count; ++i ) {
        xg_vk_workload_query_pool_t* pool = &workload->timestamp_query_pools[i];
        xg_vk_timestamp_query_pool_readback ( pool->handle, pool->buffer );
    }
#endif

    std_list_push ( &xg_vk_workload_state->workload_freelist, workload );
}

/*void xg_workload_set_swapchain_texture_acquired_event ( xg_workload_h workload_handle, xg_queue_event_h event_handle ) {
#if std_log_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = ( xg_vk_workload_t* ) std_pool_at ( &xg_vk_workload_state.workloads_pool, handle_idx );
    std_assert_m ( workload->gen == handle_gen );

    std_unused_m ( event_handle );
    //workload->swapchain_texture_acquired_event = event_handle;
}*/

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

// ======================================================================================= //
//                                          C M D
// ======================================================================================= //

/*
    merge sort -> chunk -> translate -> submit
        merge sort
            simple merge sort on the cmd headers, drag along the args? or not? could be parallelized easily
        chunk
            scan the merged buffer for 1ry and 2rt segments. maybe 1ry: renderpass changes, 2ry: pso changes. hard to parallelize, might require
            the merge sort step to look for cmds that mark the beginning of a 1ry/2ry buffer and pass that info along
        translate
            translate each chunk individually into native cmd buffers. easy to parallelize. 1ry buffers dispatch tasks for the translation
            of their 2ry buffers and at their termination add them as their cmd.
        submit
            simple step where all native buffers are submitted to the cmd queue. can't be parallelized, need to enforce order in the queue

*/

typedef struct {
    xg_cmd_header_t* cmd_headers;
    size_t count;
} xg_vk_workload_cmd_sort_result_t;

static xg_vk_workload_cmd_sort_result_t xg_vk_workload_sort_cmd_buffers ( xg_vk_workload_sort_context_t* context, xg_workload_h workload_handle ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    const xg_cmd_buffer_t* cmd_buffers[xg_cmd_buffer_max_cmd_buffers_per_workload_m];
    std_assert_m ( workload->cmd_buffers_count < xg_cmd_buffer_max_cmd_buffers_per_workload_m );
    uint64_t total_header_size = 0;

    for ( size_t i = 0; i < workload->cmd_buffers_count; ++i ) {
        const xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( workload->cmd_buffers[i] );
        cmd_buffers[i] = cmd_buffer;
        total_header_size += std_queue_local_used_size ( &cmd_buffer->cmd_headers_allocator );
    }

    size_t total_header_count = total_header_size / sizeof ( xg_cmd_header_t );

    xg_cmd_header_t* sort_result = context->result;
    xg_cmd_header_t* sort_temp = context->temp;
    if ( context->capacity < total_header_count ) {
        std_virtual_heap_free ( sort_result );
        std_virtual_heap_free ( sort_temp );
        sort_result = std_virtual_heap_alloc_array_m ( xg_cmd_header_t, total_header_count );
        sort_temp = std_virtual_heap_alloc_array_m ( xg_cmd_header_t, total_header_count );
        context->result = sort_result;
        context->temp = sort_temp;
        context->capacity = total_header_count;
    }

    xg_cmd_buffer_sort ( sort_result, sort_temp, total_header_count, cmd_buffers, workload->cmd_buffers_count );

    xg_vk_workload_cmd_sort_result_t result;
    result.cmd_headers = sort_result;
    result.count = total_header_count;
    return result;
}

typedef struct {
    xg_vk_workload_cmd_chunk_t* cmd_chunks_array;
    xg_vk_workload_queue_chunk_t* queue_chunks_array;
    uint32_t cmd_chunks_count;
    uint32_t queue_chunks_count;
} xg_vk_workload_cmd_chunk_result_t;

static xg_vk_workload_cmd_chunk_result_t xg_vk_workload_chunk_cmd_headers ( xg_vk_workload_chunk_context_t* context, const xg_cmd_header_t* cmd_headers, uint32_t cmd_count ) {
    xg_vk_workload_cmd_chunk_t* cmd_chunks_array = context->cmd_chunks_array;
    xg_vk_workload_queue_chunk_t* queue_chunks_array = context->queue_chunks_array;
    uint32_t cmd_chunks_count = 0;
    uint32_t queue_chunks_count = 0;

    bool in_renderpass = false;
    xg_vk_workload_cmd_chunk_t cmd_chunk = xg_vk_workload_cmd_chunk_m();
    xg_vk_workload_queue_chunk_t queue_chunk = xg_vk_workload_queue_chunk_m( .begin = 0 );

    bool separate_renderpasses = false;

    uint32_t cmd_it;
    for ( cmd_it = 0; cmd_it < cmd_count; ++cmd_it ) {
        const xg_cmd_header_t* header = &cmd_headers[cmd_it];
        xg_cmd_type_e cmd_type = header->type;

        switch ( cmd_type ) {
        case xg_cmd_graphics_renderpass_begin_m:
            std_assert_m ( !in_renderpass );
            std_assert_m ( queue_chunk.queue == xg_cmd_queue_graphics_m );
            if ( separate_renderpasses ) {
                if ( cmd_chunk.begin != -1 ) {
                    // close current chunk
                    cmd_chunk.end = cmd_it;
                    cmd_chunks_array[cmd_chunks_count++] = cmd_chunk;
                }
                cmd_chunk = xg_vk_workload_cmd_chunk_m( .begin = cmd_it, .queue = queue_chunk.queue );
            } else {
                if ( cmd_chunk.begin == -1 ) cmd_chunk.begin = cmd_it;
            }
            // begin new cmd chunk
            in_renderpass = true;
            break;
        case xg_cmd_graphics_renderpass_end_m:
            std_assert_m ( in_renderpass );
            std_assert_m ( queue_chunk.queue == xg_cmd_queue_graphics_m );
            std_assert_m ( cmd_chunk.begin != -1 );
            // close current chunk
            if ( separate_renderpasses ) {
                cmd_chunk.end = cmd_it + 1;
                cmd_chunks_array[cmd_chunks_count++] = cmd_chunk;
                cmd_chunk = xg_vk_workload_cmd_chunk_m( .queue = queue_chunk.queue );
            }
            in_renderpass = false;
            break;
        case xg_cmd_bind_queue_m:
            std_auto_m args = ( xg_cmd_bind_queue_params_t* ) header->args;
            if ( cmd_chunk.begin != -1 ) {
                // close cmd chunk
                cmd_chunk.end = cmd_it;
                cmd_chunks_array[cmd_chunks_count++] = cmd_chunk;
                // close queue chunk
                queue_chunk.end = cmd_chunks_count;
                queue_chunks_array[queue_chunks_count++] = queue_chunk;
            }
            // begin new queue chunk
            queue_chunk = xg_vk_workload_queue_chunk_m ( 
                .begin = cmd_chunks_count,
                .queue = args->queue,
                .signal_event = args->signal_event,
                .wait_count = args->wait_count,
            );
            std_mem_copy_static_array_m ( queue_chunk.wait_events, args->wait_events );
            std_mem_copy_static_array_m ( queue_chunk.wait_stages, args->wait_stages );
            cmd_chunk = xg_vk_workload_cmd_chunk_m ( .queue = queue_chunk.queue );
            break;
        case xg_cmd_draw_m:
        case xg_cmd_dynamic_viewport_m:
        case xg_cmd_dynamic_scissor_m:
            std_assert_m ( in_renderpass );
            std_assert_m ( queue_chunk.queue == xg_cmd_queue_graphics_m );
            if ( cmd_chunk.begin == -1 ) cmd_chunk.begin = cmd_it;
            break;
        case xg_cmd_compute_m:
        case xg_cmd_raytrace_m:
        case xg_cmd_texture_clear_m:
        case xg_cmd_texture_depth_stencil_clear_m:
            std_assert_m ( !in_renderpass );
            std_assert_m ( queue_chunk.queue == xg_cmd_queue_graphics_m || queue_chunk.queue == xg_cmd_queue_compute_m );
            if ( cmd_chunk.begin == -1 ) cmd_chunk.begin = cmd_it;
            break;
        case xg_cmd_copy_buffer_m:
        case xg_cmd_copy_texture_m:
        case xg_cmd_copy_buffer_to_texture_m:
        case xg_cmd_copy_texture_to_buffer_m:
        case xg_cmd_begin_debug_region_m:
        case xg_cmd_end_debug_region_m:
        case xg_cmd_barrier_set_m:
        case xg_cmd_reset_query_pool_m:
            std_assert_m ( !in_renderpass );
            if ( cmd_chunk.begin == -1 ) cmd_chunk.begin = cmd_it;
            break;
        case xg_cmd_write_timestamp_m:
        case xg_cmd_start_debug_capture_m:
        case xg_cmd_stop_debug_capture_m:
            if ( cmd_chunk.begin == -1 ) cmd_chunk.begin = cmd_it;
            break;
        }        
    }

    std_assert_m ( !in_renderpass );
    if ( cmd_chunk.begin != -1 ) {
        cmd_chunk.end = cmd_it;
        cmd_chunks_array[cmd_chunks_count++] = cmd_chunk;
        queue_chunk.end = cmd_chunks_count;
        queue_chunks_array[queue_chunks_count++] = queue_chunk;
    }

    xg_vk_workload_cmd_chunk_result_t result = {
        .cmd_chunks_array = cmd_chunks_array,
        .queue_chunks_array = queue_chunks_array,
        .cmd_chunks_count = cmd_chunks_count,
        .queue_chunks_count = queue_chunks_count,
    };
    return result;
}

// TODO
std_unused_static_m()
static void xg_vk_workload_debug_print_cmd_headers ( const xg_cmd_header_t* cmd_headers, uint32_t cmd_count ) {
    for ( uint32_t cmd_it = 0; cmd_it < cmd_count; ++cmd_it ) {
        const xg_cmd_header_t* header = &cmd_headers[cmd_it];
        xg_cmd_type_e cmd_type = header->type;

        switch ( cmd_type ) {
        case xg_cmd_graphics_renderpass_begin_m: {
            std_auto_m args = ( xg_cmd_renderpass_params_t* ) header->args;
            const xg_vk_renderpass_t* renderpass = xg_vk_renderpass_get(args->renderpass);
            std_log_info_m ( "renderpass_begin " std_fmt_str_m, renderpass->params.debug_name );
            for ( uint32_t i = 0; i < args->render_targets_count; ++i ) {
                xg_render_target_binding_t* binding = &args->render_targets[i];
                const xg_vk_texture_t* texture = xg_vk_texture_get ( binding->texture );
                std_log_info_m ( "RT" std_fmt_u32_m ": " std_fmt_str_m " " std_fmt_u64_m " " std_fmt_u64_m, i, texture->params.debug_name, texture->vk_handle, binding->view );
            }
            if ( args->depth_stencil.texture != xg_null_handle_m ) {
                const xg_vk_texture_t* texture = xg_vk_texture_get ( args->depth_stencil.texture );
                std_log_info_m ( "DS: " std_fmt_str_m " " std_fmt_u64_m, texture->params.debug_name, texture->vk_handle, texture->params.debug_name, texture->vk_handle );
            }
            break;
        }
        case xg_cmd_graphics_renderpass_end_m:  {
            std_log_info_m ( "renderpass_end" );
            break;
        }
        case xg_cmd_bind_queue_m:{
            std_auto_m args = ( xg_cmd_bind_queue_params_t* ) header->args;
            std_log_info_m ( "bind_queue " std_fmt_str_m, xg_cmd_queue_str ( args->queue ) );

            if ( args->signal_event != xg_null_handle_m ) {
                const xg_vk_gpu_queue_event_t* signal = xg_vk_gpu_queue_event_get ( args->signal_event );
                std_log_info_m ( "SIGNAL:" std_fmt_str_m " " std_fmt_u64_m, signal->params.debug_name, signal->vk_semaphore );
            } else {
                std_log_info_m ( "SIGNAL:-" );
            }

            if ( !args->wait_count ) {
                std_log_info_m ( "WAIT:-" );
            } else {
                for ( uint32_t i = 0; i < args->wait_count; ++i ) {
                const xg_vk_gpu_queue_event_t* wait = xg_vk_gpu_queue_event_get ( args->wait_events[i] );
                    std_log_info_m ( "WAIT:" std_fmt_str_m " " std_fmt_u64_m " " std_fmt_str_m, wait->params.debug_name, wait->vk_semaphore, xg_pipeline_stage_str ( args->wait_stages[i] ) );
                }
            }
        }
        break;
        case xg_cmd_draw_m: {
            std_log_info_m ( "draw" );
            break;
        }
        case xg_cmd_compute_m: {
            std_log_info_m ( "compute" );
        }
        break;
        case xg_cmd_raytrace_m: {
            std_log_info_m ( "raytrace" );
        }
        break;
        case xg_cmd_texture_clear_m: {
            std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;
            const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );
            std_log_info_m ( "texture_clear " std_fmt_str_m " " std_fmt_u64_m, texture->params.debug_name, texture->vk_handle );
        }
        break;
        case xg_cmd_texture_depth_stencil_clear_m: {
            std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;
            const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );
            std_log_info_m ( "depth_stencil_clear " std_fmt_str_m " " std_fmt_u64_m, texture->params.debug_name, texture->vk_handle );
        }
        break;
        case xg_cmd_copy_buffer_m: {
            std_log_info_m ( "copy_buffer" );
        }
        break;
        case xg_cmd_copy_texture_m: {
            std_log_info_m ( "copy_texture" );
        }
        break;
        case xg_cmd_copy_buffer_to_texture_m: {
            std_log_info_m ( "copy_buffer_to_texture" );
        }
        break;
        case xg_cmd_copy_texture_to_buffer_m: {
            std_log_info_m ( "copy_texture_to_buffer" );
        }
        break;
        case xg_cmd_begin_debug_region_m: {
            std_auto_m args = ( xg_cmd_begin_debug_region_t* ) header->args;
            std_log_info_m ( "debug_region_begin " std_fmt_str_m, args->name );
        }
        break;
        case xg_cmd_end_debug_region_m: {
            std_log_info_m ( "debug_region_end" );
        }
        break;
        case xg_cmd_barrier_set_m: {
            std_log_info_m ( "barrier_set:" );
            std_auto_m args = ( xg_cmd_barrier_set_t* ) header->args;
            char* base = ( char* ) ( args + 1 );
            if ( args->texture_memory_barriers > 0 ) {
                base = ( char* ) std_align_ptr ( base, std_alignof_m ( xg_texture_memory_barrier_t ) );

                for ( uint32_t i = 0; i < args->texture_memory_barriers; ++i ) {
                    xg_texture_memory_barrier_t* barrier = ( xg_texture_memory_barrier_t* ) base;
                    const xg_vk_texture_t* texture = xg_vk_texture_get ( barrier->texture );

                    VkImageAspectFlags aspect = xg_texture_flags_to_vk_aspect ( texture->flags );
                    if ( aspect == VK_IMAGE_ASPECT_NONE ) {
                        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                    }

                    std_log_info_m ( "texture barrier: " std_fmt_str_m " " std_fmt_u64_m " MIP:" std_fmt_u32_m "->" std_fmt_u32_m " ARRAY:" std_fmt_u32_m "->" std_fmt_u32_m " " std_fmt_str_m
                        std_fmt_newline_m std_fmt_tab_m "FROM:" std_fmt_str_m " " std_fmt_str_m " WAIT:" std_fmt_str_m " FLUSH:" std_fmt_str_m
                        std_fmt_newline_m std_fmt_tab_m "TO:" std_fmt_str_m " " std_fmt_str_m " STALL:" std_fmt_str_m " INVALIDATE:" std_fmt_str_m, 
                        texture->params.debug_name, texture->vk_handle, barrier->mip_base, barrier->mip_count, barrier->array_base, barrier->array_count, xg_vk_image_aspect_str ( aspect ),
                        xg_cmd_queue_str ( barrier->queue.old ), xg_texture_layout_str ( barrier->layout.old ), xg_pipeline_stage_str ( barrier->execution.blocker ), xg_memory_access_str ( barrier->memory.flushes ),
                        xg_cmd_queue_str ( barrier->queue.new ), xg_texture_layout_str ( barrier->layout.new ), xg_pipeline_stage_str ( barrier->execution.blocked ), xg_memory_access_str ( barrier->memory.invalidations ) );

                    base += sizeof ( xg_texture_memory_barrier_t );
                }
            }
        }
        break;
        case xg_cmd_start_debug_capture_m: {
            std_log_info_m ( "debug_capture_start" );
        }
        break;
        case xg_cmd_stop_debug_capture_m: {
            std_log_info_m ( "debug_capture_end" );
        }
        break;
        default:
            std_log_warn_m ( "Missing cmd print" );
        }        
    }
}

typedef struct {
    VkCommandBuffer* translated_chunks_array;
    uint32_t translated_chunks_count;
} xg_vk_workload_translate_cmd_chunks_result_t;

typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    xg_graphics_pipeline_dynamic_state_bit_e dynamic_flags;
} xg_vk_workload_translate_cache_t;

xg_vk_workload_translate_cmd_chunks_result_t xg_vk_workload_translate_cmd_chunks ( xg_vk_workload_translate_context_t* context, xg_device_h device_handle, xg_workload_h workload_handle, const xg_cmd_header_t* cmd_headers_array, const xg_vk_workload_cmd_chunk_t* cmd_chunks_array, uint32_t cmd_chunks_count ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    bool in_renderpass = false;

    xg_vk_workload_translate_cache_t cache = {
        .resolution_x = 0,
        .resolution_y = 0,
        .dynamic_flags = 0,
    };

    xg_vk_cmd_allocator_t* cmd_allocators = context->cmd_allocators;
    VkCommandBuffer* cmd_buffers_array = context->translated_chunks_array;
    uint32_t cmd_buffers_count = 0;

    VkDescriptorSet global_set = VK_NULL_HANDLE;
    if ( workload->global_bindings != xg_null_handle_m ) {
        global_set = xg_vk_workload_resource_bindings_get_desc_set ( device_handle, workload_handle, workload->global_bindings );
    }

    for ( uint32_t chunk_it = 0; chunk_it < cmd_chunks_count; ++chunk_it ) {
        xg_vk_workload_cmd_chunk_t chunk = cmd_chunks_array[chunk_it];
        xg_cmd_queue_e queue = chunk.queue;
        xg_vk_cmd_allocator_t* cmd_allocator = &cmd_allocators[queue];
        VkCommandBuffer vk_cmd_buffer = cmd_allocator->vk_cmd_buffers[cmd_allocator->cmd_buffers_count++];
        cmd_buffers_array[cmd_buffers_count++] = vk_cmd_buffer;
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL,
        };
        vkBeginCommandBuffer ( vk_cmd_buffer, &begin_info );
        //device->ext_api.cmd_set_checkpoint ( vk_cmd_buffer, (void*) -1 );

        for ( uint32_t cmd_it = chunk.begin; cmd_it < chunk.end; ++cmd_it ) {
            const xg_cmd_header_t* header = &cmd_headers_array[cmd_it];
            //device->ext_api.cmd_set_checkpoint ( vk_cmd_buffer, (void*) header->key );
            xg_cmd_type_e cmd_type = header->type;
            
            switch ( cmd_type ) {
            case xg_cmd_graphics_renderpass_begin_m: {
                std_auto_m args = ( xg_cmd_renderpass_params_t* ) header->args;
                std_assert_m ( !in_renderpass );
                const xg_vk_renderpass_t* renderpass = xg_vk_renderpass_get(args->renderpass);

                // Imageless framebuffer: pass the attachment textures to BeginRenderPass call
                VkImageView attachments_array[xg_pipeline_output_max_color_targets_m + 1];
                uint32_t attachments_count = args->render_targets_count;
                for ( size_t i = 0; i < attachments_count; ++i ) {
                    const xg_vk_texture_view_t* view = xg_vk_texture_get_view ( args->render_targets[i].texture, args->render_targets[i].view );
                    attachments_array[i] = view->vk_handle;
                }

                xg_texture_h depth_stencil_handle = args->depth_stencil.texture;
                if ( depth_stencil_handle != xg_null_handle_m ) {
                    const xg_vk_texture_view_t* view = xg_vk_texture_get_view ( depth_stencil_handle, xg_texture_view_m() );
                    attachments_array[attachments_count++] = view->vk_handle;
                }

                VkRenderPassAttachmentBeginInfo attachment_begin_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
                    .pNext = NULL,
                    .attachmentCount = attachments_count,
                    .pAttachments = attachments_array,
                };

                uint32_t resolution_x = renderpass->params.resolution_x;
                uint32_t resolution_y = renderpass->params.resolution_y;

                VkRenderPassBeginInfo pass_begin_info = {
                    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                    .pNext = &attachment_begin_info,
                    .renderPass = renderpass->vk_handle,
                    .framebuffer = renderpass->vk_framebuffer_handle,
                    .renderArea.offset.x = 0,
                    .renderArea.offset.y = 0,
                    .renderArea.extent.width = resolution_x,
                    .renderArea.extent.height = resolution_y,
                    .clearValueCount = 0,
                    .pClearValues = NULL,
                };
                vkCmdBeginRenderPass ( vk_cmd_buffer, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE );
                in_renderpass = true;

                cache.dynamic_flags = 0;
                cache.resolution_x = resolution_x;
                cache.resolution_y = resolution_y;
            }
            break;
            case xg_cmd_graphics_renderpass_end_m: {
                std_assert_m ( in_renderpass );
                vkCmdEndRenderPass ( vk_cmd_buffer );
                in_renderpass = false;
            }
            break;
            case xg_cmd_dynamic_viewport_m: {
                std_auto_m args = ( xg_viewport_state_t* ) header->args;
                std_assert_m ( in_renderpass );
                VkViewport vk_viewport = {
                    .x = ( float ) args->x,
                    .y = ( float ) args->y + ( float ) args->height,
                    .width = ( float ) args->width,
                    .height = - ( float ) args->height,
                    .minDepth = args->min_depth,
                    .maxDepth = args->max_depth,
                };
                vkCmdSetViewport ( vk_cmd_buffer, 0, 1, &vk_viewport );
                cache.dynamic_flags |= xg_graphics_pipeline_dynamic_state_bit_viewport_m;
            }
            break;
            case xg_cmd_dynamic_scissor_m: {
                std_auto_m args = ( xg_scissor_state_t* ) header->args;
                std_assert_m ( in_renderpass );
                uint32_t width = args->width;
                uint32_t height = args->height;
                if ( width == xi_scissor_width_full_m ) {
                    width = cache.resolution_x;
                }
                if ( height == xi_scissor_height_full_m ) {
                    height = cache.resolution_y;
                }
                VkRect2D vk_scissor = {
                    .offset.x = args->x,
                    .offset.y = args->y,
                    .extent.width = width,
                    .extent.height = height,
                };
                vkCmdSetScissor ( vk_cmd_buffer, 0, 1, &vk_scissor );
                cache.dynamic_flags |= xg_graphics_pipeline_dynamic_state_bit_scissor_m;
            }
            break;
            case xg_cmd_draw_m: {
                std_auto_m args = ( xg_cmd_draw_params_t* ) header->args;
                std_assert_m ( in_renderpass );

                const xg_vk_graphics_pipeline_t* pipeline = xg_vk_graphics_pipeline_get ( args->pipeline );

                vkCmdBindPipeline ( vk_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->common.vk_handle );

                if ( pipeline->state.dynamic_state & xg_graphics_pipeline_dynamic_state_bit_viewport_m && !( cache.dynamic_flags & xg_graphics_pipeline_dynamic_state_bit_viewport_m ) ) {                        
                    VkViewport vk_viewport = {
                        .x = 0,
                        .y = cache.resolution_y,
                        .width = cache.resolution_x,
                        .height = - ( float ) cache.resolution_y,
                        .minDepth = 0,
                        .maxDepth = 1,
                    };
                    vkCmdSetViewport ( vk_cmd_buffer, 0, 1, &vk_viewport );
                    cache.dynamic_flags |= xg_graphics_pipeline_dynamic_state_bit_viewport_m;
                }

                if ( pipeline->state.dynamic_state & xg_graphics_pipeline_dynamic_state_bit_scissor_m && !( cache.dynamic_flags & xg_graphics_pipeline_dynamic_state_bit_scissor_m ) ) {                        
                    VkRect2D vk_scissor = {
                        .offset.x = 0,
                        .offset.y = 0,
                        .extent.width = cache.resolution_x,
                        .extent.height = cache.resolution_y,
                    };
                    vkCmdSetScissor ( vk_cmd_buffer, 0, 1, &vk_scissor );
                    cache.dynamic_flags |= xg_graphics_pipeline_dynamic_state_bit_scissor_m;
                }

                VkDescriptorSet vk_sets[xg_shader_binding_set_count_m] = { [0 ... xg_shader_binding_set_count_m - 1] = VK_NULL_HANDLE };
                if ( global_set ) {
                    const xg_vk_resource_bindings_t* global_bindings = xg_vk_pipeline_resource_group_get ( device_handle, workload->global_bindings );
                    if ( pipeline->common.resource_layouts[0] == global_bindings->layout ) {
                        vk_sets[0] = global_set;
                    }
                }
                
                for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
                    xg_resource_bindings_h group_handle = args->bindings[i];

                    if ( group_handle == xg_null_handle_m ) {
                        continue;
                    }
                    
                    if ( xg_vk_resource_group_handle_is_workload_m ( group_handle ) ) {
                        group_handle = xg_vk_resource_group_handle_remove_tag_m ( group_handle );
                        vk_sets[i] = workload->desc_sets_array[group_handle];
                    } else {
                        const xg_vk_resource_bindings_t* group = xg_vk_pipeline_resource_group_get ( device_handle, group_handle );
                        vk_sets[i] = group->vk_handle;
                    }
                }

                for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ) {
                    if ( vk_sets[i] == VK_NULL_HANDLE ) {
                        ++i;
                        continue;
                    }

                    uint32_t j = i + 1;
                    while ( j < xg_shader_binding_set_count_m && vk_sets[j] != VK_NULL_HANDLE ) {
                        ++j;
                    }

                    vkCmdBindDescriptorSets ( vk_cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->common.vk_layout_handle, i, j - i, &vk_sets[i], 0, NULL );
                    i = j;
                }

                VkBuffer vk_buffers[xg_vertex_stream_max_bindings_m];
                VkDeviceSize vk_offsets[xg_vertex_stream_max_bindings_m];

                uint32_t vertex_buffers_count = args->vertex_buffers_count;
                uint32_t vertex_offset = args->vertex_offset;

                for ( size_t i = 0; i < vertex_buffers_count; ++i ) {
                    const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( args->vertex_buffers[i] );
                    vk_buffers[i] = buffer->vk_handle;
                    vk_offsets[i] = 0;
                }

                if ( vertex_buffers_count > 0 ) {
                    vkCmdBindVertexBuffers ( vk_cmd_buffer, 0, vertex_buffers_count, vk_buffers, vk_offsets );
                }

                // TODO instancing
                uint32_t instance_count = args->instance_count;
                uint32_t instance_offset = args->instance_offset;
                uint32_t primitive_count = args->primitive_count;
                xg_buffer_h index_buffer = args->index_buffer;
                if ( index_buffer != xg_null_handle_m ) {
                    uint32_t index_offset = args->index_offset;
                    const xg_vk_buffer_t* ibuffer = xg_vk_buffer_get ( index_buffer);
                    vkCmdBindIndexBuffer ( vk_cmd_buffer, ibuffer->vk_handle, 0, VK_INDEX_TYPE_UINT32 ); // TODO read index type from buffer
                    vkCmdDrawIndexed ( vk_cmd_buffer, primitive_count * 3, instance_count, index_offset, vertex_offset, instance_offset );
                } else {
                    vkCmdDraw ( vk_cmd_buffer, primitive_count * 3, instance_count, vertex_offset, instance_offset );
                }

                std_noop_m;
            }
            break;
            case xg_cmd_compute_m: {
                std_auto_m args = ( xg_cmd_compute_params_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_compute_pipeline_t* pipeline = xg_vk_compute_pipeline_get ( args->pipeline );

                vkCmdBindPipeline ( vk_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->common.vk_handle );

                VkDescriptorSet vk_sets[xg_shader_binding_set_count_m] = { [0 ... xg_shader_binding_set_count_m - 1] = VK_NULL_HANDLE };
                vk_sets[0] = global_set;
                
                for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
                    xg_resource_bindings_h group_handle = args->bindings[i];
                    
                    if ( group_handle == xg_null_handle_m ) {
                        continue;
                    }

                    if ( xg_vk_resource_group_handle_is_workload_m ( group_handle ) ) {
                        group_handle = xg_vk_resource_group_handle_remove_tag_m ( group_handle );
                        vk_sets[i] = workload->desc_sets_array[group_handle];
                    } else {
                        const xg_vk_resource_bindings_t* group = xg_vk_pipeline_resource_group_get ( device_handle, group_handle );
                        vk_sets[i] = group->vk_handle;
                    }
                }

                for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ) {
                    if ( vk_sets[i] == VK_NULL_HANDLE ) {
                        ++i;
                        continue;
                    }

                    uint32_t j = i + 1;
                    while ( j < xg_shader_binding_set_count_m && vk_sets[j] != VK_NULL_HANDLE ) {
                        ++j;
                    }

                    vkCmdBindDescriptorSets ( vk_cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->common.vk_layout_handle, i, j - i, &vk_sets[i], 0, NULL );
                    i = j;
                }

                vkCmdDispatch ( vk_cmd_buffer, args->workgroup_count_x, args->workgroup_count_y, args->workgroup_count_z );
            }
            break;
            case xg_cmd_raytrace_m: {
                std_auto_m args = ( xg_cmd_raytrace_params_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_raytrace_pipeline_t* pipeline = xg_vk_raytrace_pipeline_get ( args->pipeline );

                vkCmdBindPipeline ( vk_cmd_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->common.vk_handle );

                VkDescriptorSet vk_sets[xg_shader_binding_set_count_m] = { [0 ... xg_shader_binding_set_count_m - 1] = VK_NULL_HANDLE };
                vk_sets[0] = global_set;
                
                for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
                    xg_resource_bindings_h group_handle = args->bindings[i];
                    
                    if ( group_handle == xg_null_handle_m ) {
                        continue;
                    }

                    if ( xg_vk_resource_group_handle_is_workload_m ( group_handle ) ) {
                        group_handle = xg_vk_resource_group_handle_remove_tag_m ( group_handle );
                        vk_sets[i] = workload->desc_sets_array[group_handle];
                    } else {
                        const xg_vk_resource_bindings_t* group = xg_vk_pipeline_resource_group_get ( device_handle, group_handle );
                        vk_sets[i] = group->vk_handle;
                    }
                }

                for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ) {
                    if ( vk_sets[i] == VK_NULL_HANDLE ) {
                        ++i;
                        continue;
                    }

                    uint32_t j = i + 1;
                    while ( j < xg_shader_binding_set_count_m && vk_sets[j] != VK_NULL_HANDLE ) {
                        ++j;
                    }

                    vkCmdBindDescriptorSets ( vk_cmd_buffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->common.vk_layout_handle, i, j - i, &vk_sets[i], 0, NULL );
                    i = j;
                }

                VkStridedDeviceAddressRegionKHR callable_region = { 0 };
                xg_vk_device_ext_api ( device_handle )->trace_rays ( vk_cmd_buffer, &pipeline->sbt_gen_region, &pipeline->sbt_miss_region, &pipeline->sbt_hit_region, &callable_region,
                        args->ray_count_x, args->ray_count_y, args->ray_count_z );
            }
            break;
            case xg_cmd_copy_buffer_m: {
                std_auto_m args = ( xg_buffer_copy_params_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_buffer_t* source = xg_vk_buffer_get ( args->source );
                const xg_vk_buffer_t* dest = xg_vk_buffer_get ( args->destination );

                std_assert_m ( source->params.size == dest->params.size );

                VkBufferCopy copy;
                copy.srcOffset = 0;
                copy.dstOffset = 0;
                copy.size = source->params.size;

                vkCmdCopyBuffer ( vk_cmd_buffer, source->vk_handle, dest->vk_handle, 1, &copy );
            }
            break;
            case xg_cmd_copy_texture_m: {
                std_auto_m args = ( xg_texture_copy_params_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_texture_t* source = xg_vk_texture_get ( args->source.texture );
                const xg_vk_texture_t* dest = xg_vk_texture_get ( args->destination.texture );

                VkImageLayout source_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                VkImageLayout dest_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                // TODO support all texture types, sizes, depth/stencil case, ...
                //std_assert_m ( source->params.width == dest->params.width );
                //std_assert_m ( source->params.height == dest->params.height );
                //std_assert_m ( source->params.depth == dest->params.depth );

                // TODO use copyImage when possible?

                VkImageAspectFlags src_aspect = xg_texture_flags_to_vk_aspect ( source->flags );
                VkImageAspectFlags dst_aspect = xg_texture_flags_to_vk_aspect ( dest->flags );

                if ( src_aspect == 0 ) {
                    src_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                if ( dst_aspect == 0 ) {
                    dst_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                VkFilter filter = xg_sampler_filter_to_vk ( args->filter );

                uint32_t mip_count;
                {
                    uint32_t source_mip_count = source->params.mip_levels - args->source.mip_base;
                    uint32_t dest_mip_count = dest->params.mip_levels - args->destination.mip_base;
                    uint32_t copy_mip_count = args->mip_count;

                    if ( copy_mip_count == xg_texture_all_mips_m ) {
                        if ( source_mip_count != dest_mip_count ) {
                            std_log_warn_m ( "Copy texture command has source and destination textures with non matching mip levels count" );
                        }

                        mip_count = std_min_u32 ( source_mip_count, dest_mip_count );
                    } else {
                        std_assert_m ( source_mip_count >= copy_mip_count );
                        std_assert_m ( dest_mip_count >= copy_mip_count );
                        mip_count = copy_mip_count;
                    }
                }

                uint32_t array_count;
                {
                    uint32_t source_array_count = source->params.array_layers - args->source.array_base;
                    uint32_t dest_array_count = dest->params.array_layers - args->destination.array_base;
                    uint32_t copy_array_count = args->array_count;

                    if ( copy_array_count == xg_texture_whole_array_m ) {
                        if ( source_array_count != dest_array_count ) {
                            std_log_warn_m ( "Copy texture command has source and destination textures with non matching array layers count" );
                        }

                        array_count = std_min_u32 ( source_array_count, dest_array_count );
                    } else {
                        std_assert_m ( source_array_count >= copy_array_count );
                        std_assert_m ( dest_array_count >= copy_array_count );
                        array_count = copy_array_count;
                    }
                }

                for ( uint32_t i = 0; i < mip_count; ++i ) {
                    VkImageBlit blit = {
                        .srcOffsets[0].x = 0,
                        .srcOffsets[0].y = 0,
                        .srcOffsets[0].z = 0,
                        .srcOffsets[1].x = ( int32_t ) source->params.width >> ( args->source.mip_base + i ),
                        .srcOffsets[1].y = ( int32_t ) source->params.height >> ( args->source.mip_base + i ),
                        .srcOffsets[1].z = ( int32_t ) source->params.depth,
                        .dstOffsets[0].x = 0,
                        .dstOffsets[0].y = 0,
                        .dstOffsets[0].z = 0,
                        .dstOffsets[1].x = ( int32_t ) dest->params.width >> ( args->destination.mip_base + i ),
                        .dstOffsets[1].y = ( int32_t ) dest->params.height >> ( args->destination.mip_base + i ),
                        .dstOffsets[1].z = ( int32_t ) dest->params.depth,
                        .srcSubresource.aspectMask = src_aspect,
                        .srcSubresource.mipLevel = args->source.mip_base + i,
                        .srcSubresource.baseArrayLayer = args->source.array_base,
                        .srcSubresource.layerCount = array_count,
                        .dstSubresource.aspectMask = dst_aspect,
                        .dstSubresource.mipLevel = args->destination.mip_base + i,
                        .dstSubresource.baseArrayLayer = args->destination.array_base,
                        .dstSubresource.layerCount = array_count,
                    };

                    vkCmdBlitImage ( vk_cmd_buffer, source->vk_handle, source_layout, dest->vk_handle, dest_layout, 1, &blit, filter );
                }
            }
            break;
            case xg_cmd_copy_buffer_to_texture_m: {
                std_auto_m args = ( xg_buffer_to_texture_copy_params_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_buffer_t* source = xg_vk_buffer_get ( args->source );
                const xg_vk_texture_t* dest = xg_vk_texture_get ( args->destination );

                VkImageLayout dest_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                VkImageAspectFlags dst_aspect = xg_texture_flags_to_vk_aspect ( dest->flags );

                if ( dst_aspect == 0 ) {
                    dst_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                VkBufferImageCopy copy = {
                    .bufferOffset = args->source_offset,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource.aspectMask = dst_aspect,
                    .imageSubresource.mipLevel = args->mip_base,
                    .imageSubresource.baseArrayLayer = args->array_base,
                    .imageSubresource.layerCount = args->array_count,
                    .imageOffset.x = 0,
                    .imageOffset.y = 0,
                    .imageOffset.z = 0,
                    .imageExtent.width = dest->params.width,
                    .imageExtent.height = dest->params.height,
                    .imageExtent.depth = dest->params.depth,
                };

                vkCmdCopyBufferToImage ( vk_cmd_buffer, source->vk_handle, dest->vk_handle, dest_layout, 1, &copy );
            }
            break;
            case xg_cmd_copy_texture_to_buffer_m: {
                std_auto_m args = ( xg_texture_to_buffer_copy_params_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_texture_t* source = xg_vk_texture_get ( args->source );
                const xg_vk_buffer_t* dest = xg_vk_buffer_get ( args->destination );

                VkImageLayout source_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

                VkImageAspectFlags source_aspect = xg_texture_flags_to_vk_aspect ( source->flags );

                if ( source_aspect == 0 ) {
                    source_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                VkBufferImageCopy copy = {
                    .bufferOffset = args->destination_offset,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource.aspectMask = source_aspect,
                    .imageSubresource.mipLevel = args->mip_base,
                    .imageSubresource.baseArrayLayer = args->array_base,
                    .imageSubresource.layerCount = args->array_count,
                    .imageOffset.x = 0,
                    .imageOffset.y = 0,
                    .imageOffset.z = 0,
                    .imageExtent.width = source->params.width,
                    .imageExtent.height = source->params.height,
                    .imageExtent.depth = source->params.depth,
                };

                vkCmdCopyImageToBuffer ( vk_cmd_buffer, source->vk_handle, source_layout, dest->vk_handle, 1, &copy );
            }
            break;
            case xg_cmd_texture_clear_m: {
                std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );

                VkClearColorValue clear;
                std_mem_copy ( &clear, &args->clear, sizeof ( VkClearColorValue ) );

                VkImageSubresourceRange range = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };

                vkCmdClearColorImage ( vk_cmd_buffer, texture->vk_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range );
            }
            break;
            case xg_cmd_texture_depth_stencil_clear_m: {
                std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;
                std_assert_m ( !in_renderpass );

                const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );

                VkClearDepthStencilValue clear;
                std_mem_copy ( &clear, &args->clear, sizeof ( VkClearDepthStencilValue ) );

                VkImageAspectFlags aspect = 0;

                if ( texture->flags & xg_texture_flag_bit_depth_texture_m ) {
                    aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
                }

                if ( texture->flags & xg_texture_flag_bit_stencil_texture_m ) {
                    aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
                }

                std_assert_m ( aspect != 0 );

                VkImageSubresourceRange range = {
                    .aspectMask = aspect,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                };

                vkCmdClearDepthStencilImage ( vk_cmd_buffer, texture->vk_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range );
            }
            break;
            case xg_cmd_barrier_set_m: {
                std_auto_m args = ( xg_cmd_barrier_set_t* ) header->args;
                std_assert_m ( !in_renderpass );

                char* base = ( char* ) ( args + 1 );
                
                // TODO make it obligatory?
                std_static_assert_m ( xg_vk_enable_sync2_m );

                VkImageMemoryBarrier2KHR vk_texture_barriers[xg_vk_workload_max_texture_barriers_per_cmd_m];
                VkBufferMemoryBarrier2KHR vk_buffer_barriers[xg_vk_workload_max_texture_barriers_per_cmd_m];

                if ( args->memory_barriers > 0 ) {
                    std_not_implemented_m();
                }

                if ( args->buffer_memory_barriers > 0 ) {
                    base = ( char* ) std_align_ptr ( base, std_alignof_m ( xg_buffer_memory_barrier_t ) );

                    for ( uint32_t i = 0; i < args->buffer_memory_barriers; ++i ) {
                        VkBufferMemoryBarrier2KHR* vk_barrier = &vk_buffer_barriers[i];
                        xg_buffer_memory_barrier_t* barrier = ( xg_buffer_memory_barrier_t* ) base;
                        const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( barrier->buffer );

                        uint32_t src_queue_idx = VK_QUEUE_FAMILY_IGNORED;
                        uint32_t dst_queue_idx = VK_QUEUE_FAMILY_IGNORED;

                        if ( barrier->queue.old != barrier->queue.new ) {
                            src_queue_idx = device->queues[barrier->queue.old].vk_family_idx;
                            dst_queue_idx = device->queues[barrier->queue.new].vk_family_idx;
                        }

                        vk_barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        vk_barrier->pNext = NULL;
                        vk_barrier->srcAccessMask = xg_memory_access_to_vk ( barrier->memory.flushes );
                        vk_barrier->dstAccessMask = xg_memory_access_to_vk ( barrier->memory.invalidations );
                        vk_barrier->srcStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocker );
                        vk_barrier->dstStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocked );
                        vk_barrier->srcQueueFamilyIndex = src_queue_idx;
                        vk_barrier->dstQueueFamilyIndex = dst_queue_idx;
                        std_assert_m ( buffer->vk_handle );
                        vk_barrier->buffer = buffer->vk_handle;
                        vk_barrier->size = barrier->size;
                        vk_barrier->offset = barrier->offset;

                        base += sizeof ( xg_buffer_memory_barrier_t );
                    }
                }

                if ( args->texture_memory_barriers > 0 ) {
                    base = ( char* ) std_align_ptr ( base, std_alignof_m ( xg_texture_memory_barrier_t ) );

                    for ( uint32_t i = 0; i < args->texture_memory_barriers; ++i ) {
                        VkImageMemoryBarrier2KHR* vk_barrier = &vk_texture_barriers[i];
                        xg_texture_memory_barrier_t* barrier = ( xg_texture_memory_barrier_t* ) base;
                        const xg_vk_texture_t* texture = xg_vk_texture_get ( barrier->texture );

                        VkImageAspectFlags aspect = xg_texture_flags_to_vk_aspect ( texture->flags );

                        if ( aspect == VK_IMAGE_ASPECT_NONE ) {
                            aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                        }

                        uint32_t mip_count;
                        uint32_t array_count;

                        if ( barrier->mip_count == xg_texture_all_mips_m ) {
                            //mip_count = texture->params.mip_levels - barrier->mip_base;
                            mip_count = VK_REMAINING_MIP_LEVELS;
                        } else {
                            mip_count = barrier->mip_count;
                        }

                        if ( barrier->array_count == xg_texture_whole_array_m ) {
                            //array_count = texture->params.array_layers - barrier->array_base;
                            array_count = VK_REMAINING_ARRAY_LAYERS;
                        } else {
                            array_count = barrier->array_count;
                        }

                        uint32_t src_queue_idx = VK_QUEUE_FAMILY_IGNORED;
                        uint32_t dst_queue_idx = VK_QUEUE_FAMILY_IGNORED;

                        if ( barrier->queue.old != barrier->queue.new ) {
                            xg_queue_ownership_transfer_t queue = barrier->queue;
                            if ( queue.new == xg_cmd_queue_invalid_m ) queue.new = queue.old;
                            if ( queue.old == xg_cmd_queue_invalid_m ) queue.old = queue.new;
                            src_queue_idx = device->queues[queue.old].vk_family_idx;
                            dst_queue_idx = device->queues[queue.new].vk_family_idx;
                        } else if ( barrier->queue.old < xg_cmd_queue_count_m && barrier->queue.new < xg_cmd_queue_count_m ) {
                            src_queue_idx = device->queues[barrier->queue.old].vk_family_idx;
                            dst_queue_idx = device->queues[barrier->queue.new].vk_family_idx;
                        }

                        VkImageSubresourceRange range;
                        range.aspectMask = aspect;
                        range.baseMipLevel = barrier->mip_base;
                        range.levelCount = mip_count;
                        range.baseArrayLayer = barrier->array_base;
                        range.layerCount = array_count;

                        vk_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
                        vk_barrier->pNext = NULL;
                        vk_barrier->srcAccessMask = xg_memory_access_to_vk ( barrier->memory.flushes );
                        vk_barrier->srcStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocker );
                        vk_barrier->dstAccessMask = xg_memory_access_to_vk ( barrier->memory.invalidations );
                        vk_barrier->dstStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocked );
                        vk_barrier->oldLayout = xg_image_layout_to_vk ( barrier->layout.old );
                        vk_barrier->newLayout = xg_image_layout_to_vk ( barrier->layout.new );
                        vk_barrier->srcQueueFamilyIndex = src_queue_idx;
                        vk_barrier->dstQueueFamilyIndex = dst_queue_idx;
                        std_assert_m ( texture->vk_handle );
                        vk_barrier->image = texture->vk_handle;
                        vk_barrier->subresourceRange = range;

                        //vk_barrier->srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                        //vk_barrier->dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                        //vk_barrier->srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                        //vk_barrier->dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

                        base += sizeof ( xg_texture_memory_barrier_t );
                    }
                }

                VkDependencyInfoKHR vk_dependency_info = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
                    .pNext = NULL,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                    .memoryBarrierCount = 0,
                    .bufferMemoryBarrierCount = args->buffer_memory_barriers,
                    .pBufferMemoryBarriers = vk_buffer_barriers,
                    .imageMemoryBarrierCount = args->texture_memory_barriers,
                    .pImageMemoryBarriers = vk_texture_barriers,
                };

                // vkCmdPipelineBarrier2KHR
                xg_vk_instance_ext_api()->cmd_sync2_pipeline_barrier ( vk_cmd_buffer, &vk_dependency_info );

                std_noop_m;
            }
            break;
            case xg_cmd_start_debug_capture_m:
                break;
            case xg_cmd_stop_debug_capture_m:
                break;
            case xg_cmd_begin_debug_region_m: {
                std_auto_m args = ( xg_cmd_begin_debug_region_t* ) header->args;

                VkDebugUtilsLabelEXT label = {
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                    .pNext = NULL,
                    .pLabelName = args->name,
                    .color[3] = ( ( args->color_rgba >>  0 ) & 0xff ) / 255.f,
                    .color[2] = ( ( args->color_rgba >>  8 ) & 0xff ) / 255.f,
                    .color[1] = ( ( args->color_rgba >> 16 ) & 0xff ) / 255.f,
                    .color[0] = ( ( args->color_rgba >> 24 ) & 0xff ) / 255.f,
                };
                xg_vk_instance_ext_api()->cmd_begin_debug_region ( vk_cmd_buffer, &label );
            }
            break;
            case xg_cmd_end_debug_region_m:
                xg_vk_instance_ext_api()->cmd_end_debug_region ( vk_cmd_buffer );
                break;
            case xg_cmd_write_timestamp_m: {
                std_auto_m args = ( xg_cmd_write_timestamp_t* ) header->args;
                xg_vk_query_pool_t* pool = xg_vk_query_pool_get ( args->pool );
                vkCmdWriteTimestamp ( vk_cmd_buffer, xg_pipeline_stage_to_vk ( args->stage ), pool->vk_handle, args->idx );
            }
            break;
            case xg_cmd_reset_query_pool_m: {
                std_auto_m args = ( xg_query_pool_h* ) header->args;
                xg_vk_query_pool_t* pool = xg_vk_query_pool_get ( *args );
                vkCmdResetQueryPool ( vk_cmd_buffer, pool->vk_handle, 0, pool->params.capacity );
            }
            break;
            default:
                break;
            }
        }

        vkEndCommandBuffer ( vk_cmd_buffer );
    }

    xg_vk_workload_translate_cmd_chunks_result_t result;
    result.translated_chunks_array = cmd_buffers_array;
    result.translated_chunks_count = cmd_buffers_count;
    return result;
}

static void xg_vk_workload_log_device_lost ( xg_device_h device_handle ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    uint32_t data_count[xg_cmd_queue_count_m] = {0};
    device->ext_api.get_checkpoints ( device->queues[xg_cmd_queue_graphics_m].vk_handle, &data_count[xg_cmd_queue_graphics_m], NULL );
    device->ext_api.get_checkpoints ( device->queues[xg_cmd_queue_compute_m].vk_handle, &data_count[xg_cmd_queue_compute_m], NULL );
    device->ext_api.get_checkpoints ( device->queues[xg_cmd_queue_copy_m].vk_handle, &data_count[xg_cmd_queue_copy_m], NULL );
    char stack_buffer[1024];
    std_stack_t stack = std_static_stack_m ( stack_buffer );
    std_stack_string_append_format ( &stack, "Device " std_fmt_str_m " lost.\n", device->generic_properties.deviceName );

    for ( xg_cmd_queue_e queue_it = 0; queue_it < xg_cmd_queue_count_m; ++queue_it ) {
        uint32_t count = data_count[queue_it];
        std_stack_string_append_format ( &stack, std_fmt_str_m " checkpoints: " std_fmt_u32_m "\n", xg_cmd_queue_str ( queue_it ), data_count[queue_it] );
        if ( count ) {
            VkCheckpointDataNV* data = std_virtual_heap_alloc_array_m ( VkCheckpointDataNV, count );
            for ( uint32_t i = 0; i < count; ++i ) {
                data[i] = ( VkCheckpointDataNV ) {
                    .sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV,
                };
            }
            device->ext_api.get_checkpoints ( device->queues[queue_it].vk_handle, &data_count[queue_it], data );
            for ( uint32_t i = 0; i < count; ++i ) {
                uint64_t key = ( uint64_t ) data[i].pCheckpointMarker;
                const char* stage = xg_pipeline_stage_str ( xg_pipeline_stage_from_vk ( data[i].stage ) );
                char buffer[64] = "-";
                if ( key != -1 ) std_u64_to_str ( buffer, 64, key );
                std_stack_string_append_format ( &stack, std_fmt_tab_m std_fmt_str_m ": " std_fmt_str_m "\n", stage, buffer );
            }
            std_virtual_heap_free ( data );
        }
    }

    std_log_error_m ( stack_buffer );

    std_process_this_exit ( std_process_exit_code_error_m );
}

void xg_vk_workload_submit_cmd_chunks ( xg_vk_workload_submit_context_t* context, xg_workload_h workload_handle, VkCommandBuffer* cmd_buffers_array, uint32_t cmd_buffers_count, xg_vk_workload_cmd_chunk_t* cmd_chunks_array, uint32_t cmd_chunks_count, xg_vk_workload_queue_chunk_t* queue_chunks_array, uint32_t queue_chunks_count ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );

    std_assert_m ( xg_vk_workload_max_queue_chunks_m >= queue_chunks_count );
    VkSemaphore chunk_semaphores_array[xg_vk_workload_max_queue_chunks_m];
    xg_queue_event_h chunk_semaphores_handles_array[xg_vk_workload_max_queue_chunks_m];
    VkPipelineStageFlags chunk_semaphores_stages[xg_vk_workload_max_queue_chunks_m];

    bool first_graphics_chunk = true;

    for ( uint32_t queue_chunk_it = 0; queue_chunk_it < queue_chunks_count; ++queue_chunk_it ) {
        xg_vk_workload_queue_chunk_t queue_chunk = queue_chunks_array[queue_chunk_it];

        for ( uint32_t cmd_chunk_it = queue_chunk.begin; cmd_chunk_it < queue_chunk.end; ++cmd_chunk_it ) {
            xg_vk_workload_cmd_chunk_t cmd_chunk = cmd_chunks_array[cmd_chunk_it];
            std_assert_m ( cmd_chunk.queue == queue_chunk.queue );
        }

        //bool first_submit = queue_chunk_it == 0;
        //bool last_submit = queue_chunk_it == queue_chunks_count - 1;

        //const xg_vk_gpu_queue_event_t* chunk_complete_event = NULL;
        //if ( last_submit ) {
        //    if ( workload->execution_complete_gpu_event != xg_null_handle_m ) {
        //        workload_complete_event = xg_vk_gpu_queue_event_get ( workload->execution_complete_gpu_event );
        //        xg_gpu_queue_event_log_signal ( workload->execution_complete_gpu_event );
        //    }
        //} else {
        xg_queue_event_h chunk_complete_event_handle = xg_gpu_queue_event_create ( &xg_queue_event_params_m ( .device = workload->device, .debug_name = "workload_cmd_chunk_event" ) );
        context->events_array[context->events_count++] = chunk_complete_event_handle;
        const xg_vk_gpu_queue_event_t* chunk_complete_event = xg_vk_gpu_queue_event_get ( chunk_complete_event_handle );
        chunk_semaphores_array[queue_chunk_it] = chunk_complete_event->vk_semaphore;
        chunk_semaphores_handles_array[queue_chunk_it] = chunk_complete_event_handle;
        chunk_semaphores_stages[queue_chunk_it] = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        //}

        VkSemaphore wait_semaphores[xg_cmd_bind_queue_max_wait_events_m + 1];
        VkPipelineStageFlags wait_stages[xg_cmd_bind_queue_max_wait_events_m + 1];
        uint32_t wait_count = queue_chunk.wait_count;

        for ( uint32_t i = 0; i < wait_count; ++i ) {
            const xg_vk_gpu_queue_event_t* event = xg_vk_gpu_queue_event_get ( queue_chunk.wait_events[i] );
            wait_semaphores[i] = event->vk_semaphore;
            wait_stages[i] = xg_pipeline_stage_to_vk ( queue_chunk.wait_stages[i] );
            xg_gpu_queue_event_log_wait ( queue_chunk.wait_events[i] );
        }

#if !xg_debug_enable_disable_semaphore_frame_sync_m
        if ( first_graphics_chunk && queue_chunk.queue == xg_cmd_queue_graphics_m && workload->swapchain_texture_acquired_event != xg_null_handle_m ) {
            xg_gpu_queue_event_log_wait ( workload->swapchain_texture_acquired_event );
            const xg_vk_gpu_queue_event_t* event = xg_vk_gpu_queue_event_get ( workload->swapchain_texture_acquired_event );
            wait_semaphores[wait_count] = event->vk_semaphore;
            wait_stages[wait_count] = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            ++wait_count;
            first_graphics_chunk = false;
        }
#endif

        uint32_t submit_buffers_count = queue_chunk.end - queue_chunk.begin;
        
        VkSemaphore signal_semaphores[2];
        uint32_t signal_count = 0;
        signal_semaphores[signal_count++] = chunk_complete_event->vk_semaphore;
        xg_gpu_queue_event_log_signal ( chunk_complete_event_handle );
        if ( queue_chunk.signal_event != xg_null_handle_m ) {
            const xg_vk_gpu_queue_event_t* event = xg_vk_gpu_queue_event_get ( queue_chunk.signal_event );
            signal_semaphores[signal_count++] = event->vk_semaphore;
            xg_gpu_queue_event_log_signal ( queue_chunk.signal_event );
        }

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = NULL,
            .waitSemaphoreCount = wait_count,
            .pWaitSemaphores = wait_semaphores,
            .pWaitDstStageMask = wait_stages,
            .commandBufferCount = submit_buffers_count,
            .pCommandBuffers = cmd_buffers_array + queue_chunk.begin,
            .signalSemaphoreCount = signal_count,
            .pSignalSemaphores = signal_semaphores,
        };

        VkFence submit_fence = VK_NULL_HANDLE;

        const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
        VkResult result = vkQueueSubmit ( device->queues[queue_chunk.queue].vk_handle, 1, &submit_info, submit_fence );

        if ( result == VK_ERROR_DEVICE_LOST ) {
            xg_vk_workload_log_device_lost ( workload->device );
        } else {
            std_verify_m ( result == VK_SUCCESS );
        }

#if xg_debug_enable_flush_gpu_submissions_m || xg_debug_enable_disable_semaphore_frame_sync_m
        result = vkQueueWaitIdle ( device->queues[queue_chunk.queue].vk_handle );
        if ( result == VK_ERROR_DEVICE_LOST ) {
            xg_vk_workload_log_device_lost ( workload->device );
        } else {
            std_verify_m ( result == VK_SUCCESS );
        }
#endif
    }

    for ( uint32_t i = 0; i < queue_chunks_count; ++i ) {
        xg_gpu_queue_event_log_wait ( chunk_semaphores_handles_array[i] );
    }

    {
        uint32_t signal_count = 0;
        VkSemaphore signal_semaphore = VK_NULL_HANDLE;
        if ( workload->execution_complete_gpu_event != xg_null_handle_m ) {
            const xg_vk_gpu_queue_event_t* event = xg_vk_gpu_queue_event_get ( workload->execution_complete_gpu_event );
            xg_gpu_queue_event_log_signal ( workload->execution_complete_gpu_event );
            signal_semaphore = event->vk_semaphore;
            signal_count = 1;
        }

        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = NULL,
            .waitSemaphoreCount = queue_chunks_count,
            .pWaitSemaphores = chunk_semaphores_array,
            .pWaitDstStageMask = chunk_semaphores_stages,
            .commandBufferCount = 0,
            .pCommandBuffers = NULL,
            .signalSemaphoreCount = signal_count,
            .pSignalSemaphores = &signal_semaphore,
        };

        const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );
        VkFence submit_fence = fence->vk_fence;

        const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
        VkResult result = vkQueueSubmit ( device->queues[xg_cmd_queue_graphics_m].vk_handle, 1, &submit_info, submit_fence );
        std_verify_m ( result == VK_SUCCESS );
    }
}

static void xg_vk_workload_create_resources ( xg_workload_h workload_handle ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    std_assert_m ( workload );

    // TODO is this ok?
    xg_cmd_buffer_h cmd_buffer = xg_workload_add_cmd_buffer ( workload_handle );
    
    // TODO transition staging buffer to copy source?
    //

    xg_cmd_buffer_begin_debug_region ( cmd_buffer, 0, "xg_resource_upload", xg_debug_region_color_teal_m );

    for ( size_t i = 0; i < workload->resource_cmd_buffers_count; ++i ) {
        xg_resource_cmd_buffer_t* resource_cmd_buffer = xg_resource_cmd_buffer_get ( workload->resource_cmd_buffers[i] );
        std_assert_m ( resource_cmd_buffer );

        xg_resource_cmd_header_t* cmd_header = resource_cmd_buffer->cmd_headers_allocator.base;
        std_assert_m ( std_align_test_ptr ( cmd_header, xg_resource_cmd_buffer_cmd_alignment_m ) );
        size_t cmd_headers_size = std_queue_local_used_size ( &resource_cmd_buffer->cmd_headers_allocator );
        const xg_resource_cmd_header_t* cmd_headers_end = resource_cmd_buffer->cmd_headers_allocator.base + cmd_headers_size;

        for ( const xg_resource_cmd_header_t* header = cmd_header; header < cmd_headers_end; ++header ) {
            xg_resource_cmd_type_e cmd_type = header->type;
            switch ( cmd_type ) {
                case xg_resource_cmd_texture_create_m: {
                    std_auto_m args = ( xg_resource_cmd_texture_create_t* ) header->args;

                    xg_texture_h texture_handle = args->texture;
                    std_verify_m ( xg_texture_alloc ( texture_handle ) );
                    if ( args->init ) {
                        switch ( args->init_mode ) {
                        case xg_texture_init_mode_clear_m:
                            xg_cmd_buffer_texture_clear ( cmd_buffer, 0, texture_handle, args->clear );
                            break;
                        case xg_texture_init_mode_clear_depth_stencil_m:
                            xg_cmd_buffer_texture_depth_stencil_clear ( cmd_buffer, 0, texture_handle, args->depth_stencil_clear );
                            break;
                        case xg_texture_init_mode_upload_m:
                            // TODO batch
                            xg_cmd_buffer_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
                                .texture_memory_barriers_count = 1,
                                .texture_memory_barriers = &xg_texture_memory_barrier_m (
                                    .texture = texture_handle,
                                    .layout.old = xg_texture_layout_undefined_m,
                                    .layout.new = xg_texture_layout_copy_dest_m,
                                    .memory.flushes = xg_memory_access_bit_none_m,
                                    .memory.invalidations = xg_memory_access_bit_transfer_write_m,
                                    .execution.blocker = xg_pipeline_stage_bit_transfer_m,
                                    .execution.blocked = xg_pipeline_stage_bit_transfer_m,
                                ),
                            ) );
                            // TODO handle mip/array
                            xg_cmd_buffer_copy_buffer_to_texture ( cmd_buffer, 0, &xg_buffer_to_texture_copy_params_m (
                                .source = args->staging.handle,
                                .source_offset = args->staging.offset,
                                .destination = texture_handle,
                            ) );
                            break;
                        default:
                            break;
                        }

                        if ( args->init_layout != xg_texture_layout_undefined_m ) {
                            xg_cmd_buffer_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
                                .texture_memory_barriers_count = 1,
                                .texture_memory_barriers = &xg_texture_memory_barrier_m (
                                    .texture = texture_handle,
                                    .layout.old = xg_texture_layout_copy_dest_m,
                                    .layout.new = args->init_layout,
                                    .memory.flushes = xg_memory_access_bit_transfer_write_m,
                                    .memory.invalidations = xg_memory_access_bit_shader_read_m | xg_memory_access_bit_shader_write_m,
                                    .execution.blocker = xg_pipeline_stage_bit_transfer_m,
                                    .execution.blocked = xg_pipeline_stage_bit_fragment_shader_m,
                                ),
                            ) );
                        }
                    }
                }
                break;

                case xg_resource_cmd_buffer_create_m: {
                    std_auto_m args = ( xg_resource_cmd_buffer_create_t* ) header->args;
                    std_verify_m ( xg_buffer_alloc ( args->buffer ) );
                }
                break;
                
                default:
                    break;
            }
        }
    }

    xg_cmd_buffer_end_debug_region ( cmd_buffer, 0 );
}

static void xg_vk_workload_destroy_resources ( xg_workload_h workload_handle, xg_resource_cmd_buffer_time_e destroy_time ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    std_assert_m ( workload );

    for ( size_t i = 0; i < workload->resource_cmd_buffers_count; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( workload->resource_cmd_buffers[i] );
        std_assert_m ( cmd_buffer );

        xg_resource_cmd_header_t* cmd_header = ( xg_resource_cmd_header_t* ) cmd_buffer->cmd_headers_allocator.base;
        std_assert_m ( std_align_test_ptr ( cmd_header, xg_resource_cmd_buffer_cmd_alignment_m ) );
        size_t cmd_headers_size = std_queue_local_used_size ( &cmd_buffer->cmd_headers_allocator );
        const xg_resource_cmd_header_t* cmd_headers_end = ( xg_resource_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.base + cmd_headers_size );

        for ( const xg_resource_cmd_header_t* header = cmd_header; header < cmd_headers_end; ++header ) {
            switch ( header->type ) {
                case xg_resource_cmd_texture_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_texture_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    std_verify_m ( xg_texture_destroy ( args->texture ) );
                }
                break;

                case xg_resource_cmd_buffer_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_buffer_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    std_verify_m ( xg_buffer_destroy ( args->buffer ) );
                }
                break;

                case xg_resource_cmd_resource_bindings_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_resource_bindings_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    xg_vk_pipeline_destroy_resource_bindings ( workload->device, args->group );
                }
                break;

                case xg_resource_cmd_renderpass_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_renderpass_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    xg_vk_renderpass_destroy ( args->renderpass );
                }
                break;

                case xg_resource_cmd_queue_event_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_queue_event_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    xg_gpu_queue_event_destroy ( args->event );
                }
                break;
            }
        }
    }
}

static void xg_vk_workload_recycle_submission_contexts ( xg_vk_workload_device_context_t* device_context, uint64_t timeout ) {
    xg_device_h device_handle = device_context->device_handle;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    
    size_t count = std_ring_count ( &device_context->workload_contexts_ring );

#if xg_debug_enable_measure_workload_wait_time_m
    size_t initial_count = count;
#endif

    while ( count > 0 ) {
        xg_vk_workload_context_t* workload_context = &device_context->workload_contexts_array[std_ring_bot_idx ( &device_context->workload_contexts_ring )];

        // If context is in use and isn't submitted yet just return, can't do anything
        if ( !workload_context->is_submitted ) {
            break;
        }

        //uint64_t timeout = 0;
        const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_context->workload );
#if xg_debug_enable_measure_workload_wait_time_m
        std_tick_t wait_start_tick = 0;
#endif

        // If all contexts are in use, stall and wait for the oldest to finish
        if ( count == xg_workload_max_queued_workloads_m ) {
#if xg_debug_enable_measure_workload_wait_time_m
            std_log_warn_m ( std_pp_eval_string_m ( xg_workload_max_queued_workloads_m ) "/" std_pp_eval_string_m ( xg_workload_max_queued_workloads_m )
                " workloads queued up already, busy waiting on gpu workload " std_fmt_u64_m " to complete...", workload->id );
            wait_start_tick = std_tick_now();
#endif
            timeout = UINT64_MAX;
        }

        const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );
        VkResult fence_status = vkWaitForFences ( device->vk_handle, 1, &fence->vk_fence, VK_TRUE, timeout );

#if xg_debug_enable_measure_workload_wait_time_m

        if ( count == xg_workload_max_queued_workloads_m ) {
            float wait_time_ms = std_tick_to_milli_f32 ( std_tick_now() - wait_start_tick );
            std_log_warn_m ( "Busy waited for " std_fmt_f32_dec_m ( 2 ) " on gpu workload " std_fmt_u64_m, wait_time_ms, workload->id );
        }

#endif

        if ( fence_status == VK_TIMEOUT ) {
            break;
        } else if ( fence_status == VK_ERROR_DEVICE_LOST ) {
            xg_vk_workload_log_device_lost ( device_handle );
        } else if ( fence_status == VK_ERROR_OUT_OF_HOST_MEMORY || fence_status == VK_ERROR_OUT_OF_DEVICE_MEMORY  ) {
            std_log_error_m ( "Workload fence returned an error" );
        }

        xg_vk_cmd_allocator_reset ( &workload_context->translate.cmd_allocators[xg_cmd_queue_graphics_m], device_handle );
        xg_vk_cmd_allocator_reset ( &workload_context->translate.cmd_allocators[xg_cmd_queue_compute_m], device_handle );
        xg_vk_cmd_allocator_reset ( &workload_context->translate.cmd_allocators[xg_cmd_queue_copy_m], device_handle );

        VkResult r = vkResetFences ( device->vk_handle, 1, &fence->vk_fence );
        xg_vk_assert_m ( r );
        workload_context->is_submitted = false;

        for ( uint32_t i = 0; i < workload_context->submit.events_count; ++i ) {
            xg_gpu_queue_event_destroy ( workload_context->submit.events_array[i] );
        }

        xg_vk_workload_destroy_resources ( workload_context->workload, xg_resource_cmd_buffer_time_workload_complete_m );
        xg_workload_destroy ( workload_context->workload );
        workload_context->workload = xg_null_handle_m;

        std_ring_pop ( &device_context->workload_contexts_ring, 1 );

        count = std_ring_count ( &device_context->workload_contexts_ring );
    }

#if xg_debug_enable_measure_workload_wait_time_m

    if ( initial_count > 0 && count == 0 ) {
        std_log_warn_m ( "All GPU worloads were found completed when trying to recycle some. Is the GPU being left idle waiting?" );
    }

#endif
}

#if 1
xg_vk_workload_context_inline_t xg_vk_workload_context_create_inline ( xg_device_h device_handle ) {
    xg_workload_h workload_handle = xg_workload_create ( device_handle );
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );

    uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( device_context->device_handle != xg_null_handle_m );

    xg_vk_workload_recycle_submission_contexts ( device_context, 0 );

    xg_vk_workload_context_t* workload_context = &device_context->workload_contexts_array[std_ring_top_idx ( &device_context->workload_contexts_ring )];
    std_ring_push ( &device_context->workload_contexts_ring, 1 );
    xg_vk_workload_context_init ( workload_context );
    workload_context->workload = workload_handle;

    xg_vk_cmd_allocator_t* cmd_allocator = &workload_context->translate.cmd_allocators[xg_cmd_queue_graphics_m];
    VkCommandBuffer cmd_buffer = cmd_allocator->vk_cmd_buffers[cmd_allocator->cmd_buffers_count++];

    xg_vk_workload_context_inline_t inline_context = {
        .workload = workload_handle,
        .cmd_buffer = cmd_buffer,
        .impl = workload_context
    };
    return inline_context;
}

void xg_vk_workload_context_submit_inline ( xg_vk_workload_context_inline_t* inline_context ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( inline_context->workload );
    std_auto_m context = ( xg_vk_workload_context_t* ) inline_context->impl;

    // submit
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = NULL,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = NULL,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &inline_context->cmd_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = NULL,
    };

    const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );
    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
    VkResult result = vkQueueSubmit ( device->queues[xg_cmd_queue_graphics_m].vk_handle, 1, &submit_info, fence->vk_fence );
    std_verify_m ( result == VK_SUCCESS );
    context->is_submitted = true;
}
#endif

void xg_workload_submit ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );

    uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( device_context->device_handle != xg_null_handle_m );

    // allocate submission context
    // TODO offer some way to call this (and thus trigger end-of-workload resource destruction) without having to submit a new workload after this one is complete.
    xg_vk_workload_recycle_submission_contexts ( device_context, 0 );

    xg_vk_workload_create_resources ( workload_handle );
    xg_vk_workload_allocate_resource_groups ( workload_handle );
    xg_vk_workload_update_resource_groups ( workload_handle );
    xg_vk_workload_destroy_resources ( workload_handle, xg_resource_cmd_buffer_time_workload_start_m );

    // test cmd buffers, if empty process resource cmd buffers and exit here
    {
        size_t total_header_size = 0;

        for ( size_t i = 0; i < workload->cmd_buffers_count; ++i ) {
            xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( workload->cmd_buffers[i] );
            total_header_size += std_queue_local_used_size ( &cmd_buffer->cmd_headers_allocator );
        }


        if ( total_header_size == 0 ) {
            xg_vk_workload_destroy_resources ( workload_handle, xg_resource_cmd_buffer_time_workload_complete_m );
            xg_workload_destroy ( workload_handle );

            return;
        }
    }

    xg_vk_workload_context_t* workload_context = &device_context->workload_contexts_array[std_ring_top_idx ( &device_context->workload_contexts_ring )];
    std_ring_push ( &device_context->workload_contexts_ring, 1 );

    xg_vk_workload_context_init ( workload_context );
    workload_context->workload = workload_handle;
    //submit_context->desc_allocator = xg_vk_desc_allocator_pop ( device_context->device_handle, workload_handle );
    //std_assert_m ( workload_context->desc_allocator );

    // merge & sort
    xg_vk_workload_cmd_sort_result_t sort_result = xg_vk_workload_sort_cmd_buffers ( &workload_context->sort, workload_handle );

    // debug print
    //xg_vk_workload_debug_print_cmd_headers ( sort_result.cmd_headers, sort_result.count );

    // chunk
    xg_vk_workload_cmd_chunk_result_t chunk_result = xg_vk_workload_chunk_cmd_headers ( &workload_context->chunk, sort_result.cmd_headers, sort_result.count );

    // translate
    xg_vk_workload_translate_cmd_chunks_result_t translate_result = xg_vk_workload_translate_cmd_chunks ( &workload_context->translate, device_context->device_handle, workload_handle, sort_result.cmd_headers, chunk_result.cmd_chunks_array, chunk_result.cmd_chunks_count );

    // submit
    xg_vk_workload_submit_cmd_chunks ( &workload_context->submit, workload_handle, translate_result.translated_chunks_array, translate_result.translated_chunks_count, chunk_result.cmd_chunks_array, chunk_result.cmd_chunks_count, chunk_result.queue_chunks_array, chunk_result.queue_chunks_count );

    workload_context->is_submitted = true;

    // Close pending debug cpature
#if 0
    if ( translate_result.debug_capture_stop_on_workload_submit ) {
        xg_debug_capture_stop();
    }

    // Queue capture stop on present if requested
    if ( translate_result.debug_capture_stop_on_workload_present ) {
        workload->stop_debug_capture_on_present = true;
    }
#endif
}

bool xg_workload_is_complete ( xg_workload_h workload_handle ) {
    uint64_t handle_gen = std_bit_read_ms_64_m ( workload_handle, xg_workload_handle_gen_bits_m );
    uint64_t handle_idx = std_bit_read_ls_64_m ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_vk_workload_state->workload_array[handle_idx];
    return workload->gen > handle_gen;
}

void xg_workload_wait_all_workload_complete ( void ) {
    for ( size_t i = 0; i  < xg_max_active_devices_m; ++i ) {
        xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[i];
        if ( !device_context->is_active ) {
            continue;
        }

        // Wait for all workloads to complete and process on complete events like resource destruction
        xg_vk_workload_recycle_submission_contexts ( device_context, UINT64_MAX );
    }
}

#if 0
static xg_buffer_range_t xg_vk_workload_buffer_write ( xg_vk_workload_buffer_t* buffer, uint32_t alignment, void* data, size_t size ) {
#if 1
    char* base = buffer->alloc.mapped_address;
    std_assert_m ( base != NULL );
    uint64_t offset = 0; // this is relative to the start of the VkBuffer object, not to the underlying Vk memory allocation

    uint64_t used_size;
    uint64_t top;
    uint64_t new_used_size;

    do {
        used_size = buffer->used_size;
        top = std_align_u64 ( offset + buffer->used_size, alignment );
        new_used_size = top + data_size - offset;
    } while ( !std_compare_and_swap_u64 ( &buffer->used_size, &used_size, new_used_size ) );

    std_assert_m ( new_used_size < buffer->total_size );
    std_mem_copy ( base + top, data, data_size );

    xg_buffer_range_t range;
    range.handle = buffer->handle;
    range.offset = top;
    range.size = data_size;
    return range;
#else
    for ( ;; ) {
        uint64_t cap = buffer->total_size;
        uint64_t top = buffer->top;
        uint64_t bot = buffer->bot;
        uint64_t mask = cap - 1;

        uint64_t offset = top & mask;

        top = std_align_u64 ( top, alignment );
        
        // force wrap around if remaining free segment is too small
        if ( cap < offset + size ) {
            top = std_align_u64 ( top, cap );
        }

        // check for enough free size
        uint64_t new_top = top + size;
        if ( new_top - bot > cap ) {
            return xg_buffer_range_m();
        }
    
        if ( std_compare_and_swap_u64 ( &buffer->top, &top, new_top ) ) {
            xg_buffer_range_t range = {
                .handle = buffer->vk_handle;
                .offset = top & mask,
                .size = size,
            };
            return range;
        }
    }
#endif
}
#endif

xg_buffer_range_t xg_workload_write_uniform ( xg_workload_h workload_handle, void* data, size_t data_size ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_workload_buffer_t* uniform_buffer = workload->uniform_buffer;

    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
    uint64_t alignment = device->generic_properties.limits.minUniformBufferOffsetAlignment;

    uint64_t offset = uniform_buffer->used_size;
    offset = std_align_u64 ( offset, alignment );
    if ( offset + data_size > uniform_buffer->total_size ) {
        uniform_buffer = xg_vk_uniform_buffer_pop ( workload->device, workload_handle );
        workload->uniform_buffer = uniform_buffer;
        offset = 0;
        std_assert_m ( uniform_buffer->total_size > data_size );
    }

    char* base = uniform_buffer->alloc.mapped_address;
    std_mem_copy ( base + offset, data, data_size );
    uniform_buffer->used_size = offset + data_size;

    xg_buffer_range_t range = {
        .handle = uniform_buffer->handle,
        .offset = offset,
        .size = data_size,
    };
    return range;
}

xg_buffer_range_t xg_workload_write_staging ( xg_workload_h workload_handle, void* data, size_t data_size ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_workload_buffer_t* staging_buffer = workload->staging_buffer;

    uint64_t offset = staging_buffer->used_size;
    if ( offset + data_size > staging_buffer->total_size ) {
        staging_buffer = xg_vk_staging_buffer_pop ( workload->device, workload_handle );
        workload->staging_buffer = staging_buffer;
        offset = 0;
        std_assert_m ( staging_buffer->total_size > data_size );
    }

    char* base = staging_buffer->alloc.mapped_address;
    std_mem_copy ( base + offset, data, data_size );
    staging_buffer->used_size = offset + data_size;

    xg_buffer_range_t range = {
        .handle = staging_buffer->handle,
        .offset = offset,
        .size = data_size,
    };
    return range;
}

xg_cmd_buffer_h xg_workload_add_cmd_buffer ( xg_workload_h workload_handle ) {
#if std_log_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64_m ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64_m ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_vk_workload_state->workload_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    xg_cmd_buffer_h cmd_buffer = xg_cmd_buffer_create ( workload_handle );

    workload->cmd_buffers[workload->cmd_buffers_count++] = cmd_buffer;

    return cmd_buffer;
}

xg_resource_cmd_buffer_h xg_workload_add_resource_cmd_buffer ( xg_workload_h workload_handle ) {
#if std_log_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64_m ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64_m ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_vk_workload_state->workload_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    xg_resource_cmd_buffer_h cmd_buffer = xg_resource_cmd_buffer_open ( workload_handle );

    workload->resource_cmd_buffers[workload->resource_cmd_buffers_count++] = cmd_buffer;

    return cmd_buffer;
}

void xg_workload_set_execution_complete_gpu_event ( xg_workload_h workload_handle, xg_queue_event_h event ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    workload->execution_complete_gpu_event = event;
}

void xg_workload_init_swapchain_texture_acquired_gpu_event ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    if ( workload->swapchain_texture_acquired_event == xg_null_handle_m ) {
        workload->swapchain_texture_acquired_event = xg_gpu_queue_event_create ( &xg_queue_event_params_m ( .device = workload->device, .debug_name = "workload_swapchain_acquire" ) );
    }
}

void xg_workload_set_global_resource_group ( xg_workload_h workload_handle, xg_resource_bindings_h group ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    workload->global_bindings = group;
}
