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

#include <std_list.h>

static xg_vk_workload_state_t* xg_vk_workload_state;

#if defined ( std_compiler_gcc_m )
std_warnings_ignore_m ( "-Wint-to-pointer-cast" )
#endif

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

void xg_vk_workload_activate_device ( xg_device_h device_handle ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    std_assert_m ( device_idx < xg_max_active_devices_m );

    xg_vk_workload_device_context_t* context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( !context->is_active );
    context->is_active = true;

    context->device_handle = device_handle;

    context->submit_contexts_array = std_virtual_heap_alloc_array_m ( xg_vk_workload_submit_context_t, xg_workload_max_queued_workloads_m );

    context->submit_contexts_ring = std_ring ( xg_workload_max_queued_workloads_m );

    for ( size_t i = 0; i <  xg_workload_max_queued_workloads_m; ++i ) {
        xg_vk_workload_submit_context_t* submit_context = &context->submit_contexts_array[i];
        submit_context->is_submitted = false;
        submit_context->workload = xg_null_handle_m;
        
        submit_context->sort.result = NULL;
        submit_context->sort.temp = NULL;
        submit_context->sort.capacity = 0;

        // cmd pool
        {
            VkCommandPoolCreateInfo info;
            info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            info.pNext = NULL;
            info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // No support for cmd buffer reuse
            info.queueFamilyIndex = device->graphics_queue.vk_family_idx;
            VkResult result = vkCreateCommandPool ( device->vk_handle, &info, NULL, &submit_context->cmd_allocator.vk_cmd_pool );
            xg_vk_assert_m ( result );
        }

        // cmd buffers
        {
            VkCommandBufferAllocateInfo info;
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            info.pNext = NULL;
            info.commandPool = submit_context->cmd_allocator.vk_cmd_pool;
            info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;                               // TODO allocate some primary and some secondary
            info.commandBufferCount = xg_vk_workload_cmd_buffers_per_allocator_m;       // TODO preallocate only some and allocate more when we run out?
            VkResult result = vkAllocateCommandBuffers ( device->vk_handle, &info, submit_context->cmd_allocator.vk_cmd_buffers );
            xg_vk_assert_m ( result );
        }

        // desc pool
        {
            //xg_vk_desc_allocator_t* allocator = &submit_context->desc_allocator;
            //xg_vk_desc_allocator_init ( allocator, device_handle );
        }
    }

    context->desc_allocators_array = std_virtual_heap_alloc_array_m ( xg_vk_desc_allocator_t, xg_vk_workload_max_desc_allocators_m );
    context->desc_allocators_freelist = std_freelist_m ( context->desc_allocators_array, xg_vk_workload_max_desc_allocators_m );

    for ( uint32_t i = 0; i < xg_vk_workload_max_desc_allocators_m; ++i ) {
        xg_vk_desc_allocator_init ( &context->desc_allocators_array[i], device_handle );
    }
}

static void xg_vk_workload_deactivate_device_context ( xg_vk_workload_device_context_t* context ) {
    const xg_vk_device_t* device = xg_vk_device_get ( context->device_handle );

    for ( size_t i = 0; i <  xg_workload_max_queued_workloads_m; ++i ) {
        xg_vk_workload_submit_context_t* submit_context = &context->submit_contexts_array[i];

        //if ( submit_context->is_submitted ) {
        //    const xg_vk_workload_t* workload = xg_vk_workload_get ( submit_context->workload );
        //    const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );
        //    vkWaitForFences ( device->vk_handle, 1, &fence->vk_fence, VK_TRUE, UINT64_MAX );
        //}

        // Call this to process on workload complete events like resource destruction
        //xg_vk_workload_recycle_submission_contexts ( context, UINT64_MAX );

        vkDestroyCommandPool ( device->vk_handle, submit_context->cmd_allocator.vk_cmd_pool, NULL );
        //vkDestroyDescriptorPool ( device->vk_handle, submit_context->desc_allocator.vk_desc_pool, NULL );
    }

    for ( uint32_t i = 0; i < xg_vk_workload_max_desc_allocators_m; ++i ) {
        vkDestroyDescriptorPool ( device->vk_handle, context->desc_allocators_array[i].vk_desc_pool, NULL );
    }
}

void xg_vk_workload_deactivate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_workload_device_context_t* context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( context->is_active );
    xg_vk_workload_deactivate_device_context ( context );
}

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

    std_assert_m ( xg_workload_max_allocated_workloads_m <= ( 1 << xg_workload_handle_idx_bits_m ) );
}

void xg_vk_workload_reload ( xg_vk_workload_state_t* state ) {
    xg_vk_workload_state = state;
}

void xg_vk_workload_destroy_ptr ( xg_vk_workload_t* workload ) {
    // gen
    uint64_t gen = workload->gen + 1;
    gen = std_bit_read_ms_64_m ( gen, xg_workload_handle_gen_bits_m );

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
    //xg_vk_workload_uniform_buffer_t* uniform_buffer = &xg_vk_workload_state->device_contexts[device_idx].uniform_buffers[workload_idx];
    //uniform_buffer->used_size = 0;

    xg_buffer_destroy ( workload->uniform_buffer.handle );
    workload->uniform_buffer.handle = xg_null_handle_m;

    // framebuffer
    if ( workload->framebuffer != xg_null_handle_m ) {
        xg_vk_framebuffer_release ( workload->framebuffer );
    }

    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
    xg_vk_workload_device_context_t* context = xg_vk_workload_device_context_get ( workload->device );

    for ( uint32_t i = 0; i < workload->desc_allocators_count; ++i ) {
        xg_vk_desc_allocator_t* allocator = workload->desc_allocators_array[i];
        vkResetDescriptorPool ( device->vk_handle, allocator->vk_desc_pool, 0 );
        xg_vk_desc_allocator_reset_counters ( allocator );
        std_list_push ( &context->desc_allocators_freelist, allocator );
    }

#if 0
    for ( uint32_t i = 0; i < workload->timestamp_query_pools_count; ++i ) {
        xg_vk_workload_query_pool_t* pool = &workload->timestamp_query_pools[i];
        xg_vk_timestamp_query_pool_readback ( pool->handle, pool->buffer );
    }
#endif

    std_list_push ( &xg_vk_workload_state->workload_freelist, workload );
}

void xg_vk_workload_unload ( void ) {
    xg_workload_wait_all_workload_complete();

    std_virtual_heap_free ( xg_vk_workload_state->workload_array );
    // TODO
}

static xg_vk_desc_allocator_t* xg_vk_desc_allocator_pop ( xg_device_h device_handle, xg_workload_h workload_handle ) {
    xg_vk_workload_device_context_t* device_context = xg_vk_workload_device_context_get ( device_handle );
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_desc_allocator_t* desc_allocator = std_list_pop_m ( &device_context->desc_allocators_freelist );
    uint32_t desc_allocator_idx = std_atomic_increment_u32 ( &workload->desc_allocators_count ) - 1;
    workload->desc_allocators_array[desc_allocator_idx] = desc_allocator;
    return desc_allocator;
}

xg_workload_h xg_workload_create ( xg_device_h device_handle ) {
    xg_vk_workload_t* workload = std_list_pop_m ( &xg_vk_workload_state->workload_freelist );
    uint64_t workload_idx = ( uint64_t ) ( workload - xg_vk_workload_state->workload_array );
    xg_workload_h workload_handle = workload->gen << xg_workload_handle_idx_bits_m | workload_idx;

    workload->cmd_buffers_count = 0;
    workload->resource_cmd_buffers_count = 0;
    workload->desc_allocators_count = 0;
    workload->desc_layouts_count = 0;
    workload->id = xg_vk_workload_state->workloads_uid++;
    workload->device = device_handle;
    workload->desc_allocator = xg_vk_desc_allocator_pop ( device_handle, workload_handle );

    // https://github.com/krOoze/Hello_Triangle/blob/master/doc/Schema.pdf
    //workload->execution_complete_gpu_event = xg_gpu_queue_event_create ( device_handle ); // TODO pre-create these?
    workload->execution_complete_gpu_event = xg_null_handle_m;
    workload->execution_complete_cpu_event = xg_cpu_queue_event_create ( device_handle );
    workload->swapchain_texture_acquired_event = xg_null_handle_m;

    workload->stop_debug_capture_on_present = false;

    // TODO have all workload uniform buffers use a single permanently allocated buffer as backend,
    //      and just offset into that, in order to support dynamic uniform buffers ( just need to 
    //      rebind same descriptor with new offset instead of re-allocating a new descriptor 
    //      and/or writing to it )
    xg_buffer_h buffer_handle = xg_buffer_create ( & xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_mapped_m,
        .device = device_handle,
        .size = xg_workload_uniform_buffer_size_m,
        .allowed_usage = xg_buffer_usage_bit_uniform_m,
        .debug_name = "workload uniform buffer"
    ) );

    xg_buffer_info_t uniform_buffer_info;
    xg_buffer_get_info ( &uniform_buffer_info, buffer_handle );

    xg_vk_workload_uniform_buffer_t* uniform_buffer = &workload->uniform_buffer;
    uniform_buffer->handle = buffer_handle;
    uniform_buffer->alloc = uniform_buffer_info.allocation;
    uniform_buffer->used_size = 0;
    uniform_buffer->total_size = xg_workload_uniform_buffer_size_m;

    workload->framebuffer = xg_null_handle_m;

#if 0
    workload->timestamp_query_pools_count = 0;
#endif

    return workload_handle;
}

xg_buffer_range_t xg_workload_write_uniform ( xg_workload_h workload_handle, void* data, size_t data_size ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    //uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    //uint64_t workload_idx = ( uint64_t ) ( workload - xg_vk_workload_state->workload_array );
    //xg_vk_workload_uniform_buffer_t* uniform_buffer = &xg_vk_workload_state->device_contexts[device_idx].uniform_buffers[workload_idx];
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

#define xg_vk_resource_group_handle_is_permanent_m( h ) ( std_bit_read_ms_64_m( h, 1 ) == 0 )
#define xg_vk_resource_group_handle_is_workload_m( h ) ( std_bit_read_ms_64_m( h, 1 ) == 1 )
#define xg_vk_resource_group_handle_tag_as_permanent_m( h ) ( std_bit_write_ms_64_m( h, 1, 0 ) )
#define xg_vk_resource_group_handle_tag_as_workload_m( h ) ( std_bit_write_ms_64_m( h, 1, 1 ) )
#define xg_vk_resource_group_handle_remove_tag_m( h ) ( std_bit_clear_ms_64_m( h, 1 ) )

xg_pipeline_resource_group_h xg_workload_create_resource_group ( xg_workload_h workload_handle, xg_pipeline_state_h pipeline_handle, xg_shader_binding_set_e set ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );

    xg_vk_pipeline_common_t* pipeline = xg_vk_common_pipeline_get ( pipeline_handle );
    xg_vk_descriptor_set_layout_h layout_handle = pipeline->descriptor_set_layouts[set];

    uint32_t idx = std_atomic_increment_u32 ( &workload->desc_layouts_count ) - 1;
    workload->desc_layouts_array[idx] = layout_handle;

    xg_pipeline_resource_group_h group_handle = xg_vk_resource_group_handle_tag_as_workload_m ( idx );
    return group_handle;
}

void xg_vk_workload_allocate_resource_groups ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );

    uint32_t desc_layouts_count = workload->desc_layouts_count;
    if ( desc_layouts_count == 0 ) {
        return;
    }

    VkDescriptorSetLayout vk_layouts[xg_vk_workload_max_resource_groups_m];
    for ( uint32_t i = 0; i < desc_layouts_count; ++i ) {
        xg_vk_descriptor_set_layout_h layout_handle = workload->desc_layouts_array[i];
        const xg_vk_descriptor_set_layout_t* layout = xg_vk_descriptor_set_layout_get ( layout_handle );
        vk_layouts[i] = layout->vk_handle;
    }

    VkDescriptorSetAllocateInfo vk_set_alloc_info;
    vk_set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    vk_set_alloc_info.pNext = NULL;
    vk_set_alloc_info.descriptorPool = workload->desc_allocator->vk_desc_pool;
    vk_set_alloc_info.descriptorSetCount = desc_layouts_count;
    vk_set_alloc_info.pSetLayouts = vk_layouts;
    VkResult set_alloc_result = vkAllocateDescriptorSets ( device->vk_handle, &vk_set_alloc_info, workload->desc_sets_array );
    xg_vk_assert_m ( set_alloc_result ); // TODO test for available desc count, use multiple allocators if needed
}

void xg_vk_workload_update_resource_groups ( xg_workload_h workload_handle ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );

    // TODO better batching
    VkWriteDescriptorSet writes_array[xg_vk_workload_max_resource_groups_m * xg_pipeline_resource_max_bindings_m];
    uint32_t writes_count = 0;
    VkDescriptorBufferInfo buffer_info_array[xg_vk_workload_max_resource_groups_m * xg_pipeline_resource_max_bindings_m];
    uint32_t buffer_info_count = 0;
    VkDescriptorImageInfo image_info_array[xg_vk_workload_max_resource_groups_m * xg_pipeline_resource_max_bindings_m];
    uint32_t image_info_count = 0;

    for ( size_t i = 0; i < workload->resource_cmd_buffers_count; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( workload->resource_cmd_buffers[i] );
        xg_resource_cmd_header_t* cmd_header = ( xg_resource_cmd_header_t* ) cmd_buffer->cmd_headers_allocator.base;
        std_assert_m ( std_align_test_ptr ( cmd_header, xg_resource_cmd_buffer_cmd_alignment_m ) );
        size_t cmd_headers_size = std_queue_local_used_size ( &cmd_buffer->cmd_headers_allocator );
        const xg_resource_cmd_header_t* cmd_headers_end = ( xg_resource_cmd_header_t* ) ( cmd_buffer->cmd_headers_allocator.base + cmd_headers_size );

        for ( const xg_resource_cmd_header_t* header = cmd_header; header < cmd_headers_end; ++header ) {
            switch ( header->type ) {
                case xg_resource_cmd_resource_group_update_m: {
                    std_auto_m args = ( xg_resource_cmd_resource_group_update_t* ) header->args;

                    xg_pipeline_resource_group_h group_handle = args->group;

                    if ( !xg_vk_resource_group_handle_is_workload_m ( group_handle ) ) {
                        continue;
                    }

                    group_handle = xg_vk_resource_group_handle_remove_tag_m ( group_handle );
                    xg_vk_descriptor_set_layout_h desc_layout_handle = workload->desc_layouts_array[group_handle];
                    const xg_vk_descriptor_set_layout_t* set_layout = xg_vk_descriptor_set_layout_get ( desc_layout_handle );
                    VkDescriptorSet vk_set = workload->desc_sets_array[group_handle];

                    uint32_t buffers_count = args->buffer_count;
                    uint32_t textures_count = args->texture_count;
                    uint32_t samplers_count = args->sampler_count;

                    std_assert_m ( buffers_count <= xg_pipeline_resource_max_buffers_per_set_m );
                    std_assert_m ( textures_count <= xg_pipeline_resource_max_textures_per_set_m );
                    std_assert_m ( samplers_count <= xg_pipeline_resource_max_samplers_per_set_m );

                    xg_buffer_resource_binding_t* buffers_array = std_align_ptr_m ( args + 1, xg_buffer_resource_binding_t );
                    xg_texture_resource_binding_t* textures_array = std_align_ptr_m ( buffers_array + buffers_count, xg_texture_resource_binding_t );
                    xg_sampler_resource_binding_t* samplers_array = std_align_ptr_m ( textures_array + textures_count, xg_sampler_resource_binding_t );

                    for ( uint32_t i = 0; i < buffers_count; ++i ) {
                        const xg_buffer_resource_binding_t* binding = &buffers_array[i];
                        xg_resource_binding_e binding_type = set_layout->descriptors[binding->shader_register].type;
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
                        write->dstBinding = binding->shader_register;
                        write->dstArrayElement = 0; // TODO
                        write->descriptorCount = 1;
                        write->pBufferInfo = info;

                        switch ( binding_type ) {
                            //case xg_buffer_binding_type_uniform_m:
                            case xg_resource_binding_buffer_uniform_m:
                                write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                                break;

                            default:
                                std_not_implemented_m();
                        }
                    }

                    for ( uint32_t i = 0; i < textures_count; ++i ) {
                        const xg_texture_resource_binding_t* binding = &textures_array[i];
                        xg_resource_binding_e binding_type = set_layout->descriptors[binding->shader_register].type;
                        const xg_vk_texture_view_t* view = xg_vk_texture_get_view ( binding->texture, binding->view );

                        VkDescriptorImageInfo* info = &image_info_array[image_info_count++];
                        info->sampler = VK_NULL_HANDLE;
                        // TODO
                        info->imageView = view->vk_handle;
                        info->imageLayout = xg_image_layout_to_vk ( binding->layout );

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = NULL;
                        write->dstSet = vk_set;
                        write->dstBinding = binding->shader_register;
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
                        const xg_vk_sampler_t* sampler = xg_vk_sampler_get ( binding->sampler );

                        VkDescriptorImageInfo* info = &image_info_array[image_info_count++];
                        info->sampler = sampler->vk_handle;
                        info->imageView = VK_NULL_HANDLE;

                        VkWriteDescriptorSet* write = &writes_array[writes_count++];
                        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write->pNext = NULL;
                        write->dstSet = vk_set;
                        write->dstBinding = binding->shader_register;
                        write->dstArrayElement = 0; // TODO
                        write->descriptorCount = 1;
                        write->pImageInfo = info;
                        write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                    }

                    break;
                }

            }
        }
    }

    const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
    vkUpdateDescriptorSets ( device->vk_handle, writes_count, writes_array, 0, NULL );
}

xg_cmd_buffer_h xg_workload_add_cmd_buffer ( xg_workload_h workload_handle ) {
#if std_log_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64_m ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64_m ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_vk_workload_state->workload_array[handle_idx];
    std_assert_m ( workload->gen == handle_gen );

    xg_cmd_buffer_h cmd_buffer = xg_cmd_buffer_open ( workload_handle );

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

bool xg_workload_is_complete ( xg_workload_h workload_handle ) {
    uint64_t handle_gen = std_bit_read_ms_64_m ( workload_handle, xg_workload_handle_gen_bits_m );
    uint64_t handle_idx = std_bit_read_ls_64_m ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = &xg_vk_workload_state->workload_array[handle_idx];
    return workload->gen > handle_gen;
}

void xg_workload_destroy ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    xg_vk_workload_destroy_ptr ( workload );
}

/*void xg_workload_set_swapchain_texture_acquired_event ( xg_workload_h workload_handle, xg_gpu_queue_event_h event_handle ) {
#if std_log_assert_enabled_m
    uint64_t handle_gen = std_bit_read_ms_64 ( workload_handle, xg_workload_handle_gen_bits_m );
#endif
    uint64_t handle_idx = std_bit_read_ls_64 ( workload_handle, xg_workload_handle_idx_bits_m );
    xg_vk_workload_t* workload = ( xg_vk_workload_t* ) std_pool_at ( &xg_vk_workload_state.workloads_pool, handle_idx );
    std_assert_m ( workload->gen == handle_gen );

    std_unused_m ( event_handle );
    //workload->swapchain_texture_acquired_event = event_handle;
}*/

void xg_workload_set_execution_complete_gpu_event ( xg_workload_h workload_handle, xg_gpu_queue_event_h event ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    workload->execution_complete_gpu_event = event;
}

void xg_workload_init_swapchain_texture_acquired_gpu_event ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );
    if ( workload->swapchain_texture_acquired_event == xg_null_handle_m ) {
        workload->swapchain_texture_acquired_event = xg_gpu_queue_event_create ( workload->device );
    }
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

static xg_vk_workload_cmd_sort_result_t xg_vk_workload_sort_cmd_buffers ( xg_vk_workload_submit_context_t* context, xg_workload_h workload_handle ) {
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

    xg_cmd_header_t* sort_result = context->sort.result;
    xg_cmd_header_t* sort_temp = context->sort.temp;
    if ( context->sort.capacity < total_header_count ) {
        std_virtual_heap_free ( sort_result );
        std_virtual_heap_free ( sort_temp );
        sort_result = std_virtual_heap_alloc_array_m ( xg_cmd_header_t, total_header_count );
        sort_temp = std_virtual_heap_alloc_array_m ( xg_cmd_header_t, total_header_count );
        context->sort.result = sort_result;
        context->sort.temp = sort_temp;
        context->sort.capacity = total_header_count;
    }

    xg_cmd_buffer_sort_n ( sort_result, sort_temp, total_header_count, cmd_buffers, workload->cmd_buffers_count );

    xg_vk_workload_cmd_sort_result_t result;
    result.cmd_headers = sort_result;
    result.count = total_header_count;
    return result;
}

typedef struct {
    xg_cmd_header_t* begin;
    xg_cmd_header_t* end;
    xg_cmd_queue_e queue;
} xg_vk_workload_cmd_chunk_t;

typedef struct {
    xg_vk_workload_cmd_chunk_t* chunks;
    uint32_t count;
} xg_vk_workload_cmd_chunk_result_t;

std_unused_static_m()
static xg_vk_workload_cmd_chunk_result_t xg_vk_workload_chunk_cmd_headers ( xg_workload_h workload_handle, const xg_cmd_header_t* cmd_headers, uint32_t cmd_count ) {
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    std_unused_m ( workload );

    xg_vk_workload_cmd_chunk_result_t result = {};
#if 0
    for ( uint32_t i = 0; i < cmd_count; ++i ) {
        xg_cmd_header_t* header = &cmd_headers[i];

        switch ( header->type ) {
        case xg_cmd_graphics_streams_bind_m:
        case xg_cmd_graphics_pipeline_state_bind_m:
        case xg_cmd_graphics_render_textures_bind_m:
        }        
    }
#endif
    // TODO
    return result;
}

typedef struct {
    bool debug_capture_stop_on_workload_submit;
    bool debug_capture_stop_on_workload_present;
} xg_vk_cmd_translate_result_t;

typedef struct {
    uint32_t buffer_count;
    xg_buffer_resource_binding_t buffers[xg_pipeline_resource_max_buffers_per_set_m];
    uint32_t texture_count;
    xg_texture_resource_binding_t textures[xg_pipeline_resource_max_textures_per_set_m];
    uint32_t sampler_count;
    xg_sampler_resource_binding_t samplers[xg_pipeline_resource_max_samplers_per_set_m];
    uint32_t raytrace_world_count;
    xg_raytrace_world_resource_binding_t raytrace_worlds[xg_pipeline_resource_max_raytrace_worlds_per_set_m];
    VkDescriptorSet vk_set;

    xg_vk_descriptor_set_layout_h layout;
    xg_pipeline_resource_group_h group;

    // Flagged when a command binds to this set
    // Cleared when the binding gets propagated to Vulkan
    bool is_dirty;
    // Flagged when the bind command gets propagated to Vulkan
    // Cleared when a later bind command binds to this set
    //      or when a new pipeline is bound and the set is no longer compatible 
    bool is_active;
} xg_vk_translation_resource_binding_set_cache_t;

typedef struct {
    bool pending_states[xg_graphics_pipeline_dynamic_state_count_m];
    bool enabled_viewport;
    bool enabled_scissor;
    xg_viewport_state_t viewport;
    xg_scissor_state_t scissor;
} xg_vk_translation_dynamic_pipeline_state_cache_t;

typedef struct {
    bool active_renderpass;

    xg_pipeline_e pipeline_type;
    const xg_vk_graphics_pipeline_t* graphics_pipeline;
    const xg_vk_compute_pipeline_t* compute_pipeline;
    const xg_vk_raytrace_pipeline_t* raytrace_pipeline;
    bool dirty_pipeline;
    bool pipeline_type_change; // TODO make sure to properly handle this, avoid triggering on bound and unused pipelines, ...

    const xg_vk_graphics_renderpass_t* graphics_renderpass;
    bool dirty_renderpass;

    // For now render textures and pipeline are basically treated as part of one same thing, looks like whenever the client binds one it is also supposed to (re-?)bind the other. TODO understand what's the proper relationship here.
    xg_render_textures_binding_t render_textures;
    bool dirty_render_textures;

    //xg_resource_binding_t bindings[xg_shader_binding_set_count_m][xg_pipeline_resource_max_bindings_per_set_m];
    //uint32_t binding_counts[xg_shader_binding_set_count_m];
    //bool dirty_binding_sets;//[xg_shader_binding_set_count_m];
    xg_vk_translation_resource_binding_set_cache_t sets[xg_shader_binding_set_count_m];

    //xg_pipeline_resource_group_h groups[xg_shader_binding_set_count_m];

    uint64_t push_constants_hash;

    xg_vk_translation_dynamic_pipeline_state_cache_t dynamic_state;
} xg_vk_tranlsation_state_t;

static void xg_vk_translation_state_cache_flush ( xg_vk_tranlsation_state_t* state, xg_vk_workload_submit_context_t* context, xg_device_h device_handle, xg_workload_h workload_handle, VkCommandBuffer vk_cmd_buffer ) {
    // Clear pending dynamic state
    if ( state->dirty_pipeline && state->pipeline_type == xg_pipeline_graphics_m ) {
        for ( uint32_t i = 0; i < xg_graphics_pipeline_dynamic_state_count_m; ++i ) {
            if ( !state->graphics_pipeline->state.dynamic_state.enabled_states[i] ) {
                state->dynamic_state.pending_states[i] = false;
            }
        }
    }

    // Render textures
    if ( state->dirty_render_textures && state->pipeline_type == xg_pipeline_graphics_m ) {
        if ( state->active_renderpass && state->graphics_renderpass == NULL ) {
            vkCmdEndRenderPass ( vk_cmd_buffer );
            state->active_renderpass = false;
        }

        // List framebuffer attachments
        VkImageView framebuffer_attachments[xg_pipeline_output_max_color_targets_m + 1];
        //VkClearValue clear_values[xg_pipeline_output_max_color_targets_m + 1];
        size_t attachment_count = state->render_textures.render_targets_count;

        for ( size_t i = 0; i < state->render_textures.render_targets_count; ++i ) {
            framebuffer_attachments[i] = xg_vk_texture_get_view ( state->render_textures.render_targets[i].texture, state->render_textures.render_targets[i].view )->vk_handle;
            //std_mem_copy ( &clear_values[i], &current_render_textures.render_targets[i].clear, sizeof ( VkClearColorValue ) );
        }

        if ( state->render_textures.depth_stencil.texture != xg_null_handle_m ) {
            //const xg_vk_texture_t* texture = xg_vk_texture_get ( state->render_textures.depth_stencil.texture );
            xg_texture_h texture = state->render_textures.depth_stencil.texture;
            framebuffer_attachments[state->render_textures.render_targets_count] = xg_vk_texture_get_view ( texture, xg_texture_view_m() )->vk_handle;
            //clear_values[current_render_textures.render_targets_count].depthStencil.depth = current_render_textures.depth_stencil.clear.depth;
            //clear_values[current_render_textures.render_targets_count].depthStencil.stencil = current_render_textures.depth_stencil.clear.stencil;
            ++attachment_count;
        }

        /*
        // Create framebuffer
        VkFramebuffer* framebuffer = &context->framebuffers[context->framebuffers_count++];
        VkFramebufferCreateInfo framebuffer_create_info;
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.pNext = NULL;
        framebuffer_create_info.flags = 0;
        framebuffer_create_info.renderPass = pipeline->vk_renderpass_handle;
        framebuffer_create_info.attachmentCount = ( uint32_t ) current_render_textures.render_targets_count + 1;
        framebuffer_create_info.pAttachments = framebuffer_attachments;
        framebuffer_create_info.width = pipeline->state.viewport_state.width;
        framebuffer_create_info.height = pipeline->state.viewport_state.height;
        framebuffer_create_info.layers = 1;
        vkCreateFramebuffer ( device->vk_handle, &framebuffer_create_info, NULL, framebuffer );
        */

        // Imageless framebuffer: pass the attachment textures to BeginRenderPass call
        VkRenderPassAttachmentBeginInfo attachment_begin_info;
        attachment_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
        attachment_begin_info.pNext = NULL;
        attachment_begin_info.attachmentCount = ( uint32_t ) attachment_count;
        attachment_begin_info.pAttachments = framebuffer_attachments;

        // Dynamic viewport check
        uint32_t width = state->graphics_pipeline->state.viewport_state.width;
        uint32_t height = state->graphics_pipeline->state.viewport_state.height;

        if ( state->graphics_pipeline->state.dynamic_state.enabled_states[xg_graphics_pipeline_dynamic_state_viewport_m] ) {
            if ( state->dynamic_state.pending_states[xg_graphics_pipeline_dynamic_state_viewport_m] || state->dynamic_state.enabled_viewport ) {
                width = state->dynamic_state.viewport.width;
                height = state->dynamic_state.viewport.height;
            }
        }

        const xg_vk_renderpass_t* renderpass;
        const xg_vk_framebuffer_t* framebuffer;
        if ( state->graphics_renderpass == NULL ) {
            xg_vk_renderpass_h renderpass_handle = state->graphics_pipeline->renderpass;
            renderpass = xg_vk_renderpass_get ( renderpass_handle );
            xg_vk_framebuffer_h framebuffer_handle = xg_vk_framebuffer_acquire ( device_handle, renderpass_handle, width, height );
            framebuffer = xg_vk_framebuffer_get ( framebuffer_handle );
            xg_vk_workload_t* workload = xg_vk_workload_edit ( context->workload );
            workload->framebuffer = framebuffer_handle;
        } else {
            xg_vk_renderpass_h renderpass_handle = state->graphics_renderpass->renderpass_handle;
            renderpass = xg_vk_renderpass_get ( renderpass_handle );
            xg_vk_framebuffer_h framebuffer_handle = state->graphics_renderpass->framebuffer_handle;
            framebuffer = xg_vk_framebuffer_get ( framebuffer_handle );
        }

        // Begin render pass
        VkRenderPassBeginInfo pass_begin_info;
        pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass_begin_info.pNext = &attachment_begin_info;
        pass_begin_info.renderPass = renderpass->vk_renderpass;
        pass_begin_info.framebuffer = framebuffer->vk_handle;
        pass_begin_info.renderArea.offset.x = 0;
        pass_begin_info.renderArea.offset.y = 0;
        pass_begin_info.renderArea.extent.width = width;
        pass_begin_info.renderArea.extent.height = height;
        pass_begin_info.clearValueCount = 0;//( uint32_t ) attachment_count;
        pass_begin_info.pClearValues = NULL;//clear_values;

        // TODO: for now render passes are begun here and terminated when the cmd buffer is closed -- need to make the render pass object part of the API
        vkCmdBeginRenderPass ( vk_cmd_buffer, &pass_begin_info, VK_SUBPASS_CONTENTS_INLINE );
        state->active_renderpass = true;

        state->dirty_render_textures = false;
    }

    // Pipeline
    const xg_vk_pipeline_common_t* common_pipeline;

    switch ( state->pipeline_type ) {
        case xg_pipeline_graphics_m:
            common_pipeline = &state->graphics_pipeline->common;
            break;

        case xg_pipeline_compute_m:
            common_pipeline = &state->compute_pipeline->common;
            break;

        case xg_pipeline_raytrace_m:
            common_pipeline = &state->raytrace_pipeline->common;
    }

    uint32_t first_deactivated_set = xg_shader_binding_set_invalid_m;
    if ( state->dirty_pipeline ) {
        {
            VkPipelineBindPoint vk_pipeline_type;

            switch ( state->pipeline_type ) {
                case xg_pipeline_graphics_m:
                    vk_pipeline_type = VK_PIPELINE_BIND_POINT_GRAPHICS;
                    break;

                case xg_pipeline_compute_m:
                    if ( state->active_renderpass && state->graphics_renderpass == NULL ) {
                        vkCmdEndRenderPass ( vk_cmd_buffer );
                        state->active_renderpass = false;
                    }

                    vk_pipeline_type = VK_PIPELINE_BIND_POINT_COMPUTE;
                    break;

                case xg_pipeline_raytrace_m:
                    // TODO terminate current render pass?
                    vk_pipeline_type = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
                    break;
            }

            vkCmdBindPipeline ( vk_cmd_buffer, vk_pipeline_type, common_pipeline->vk_handle );

            if ( vk_pipeline_type == VK_PIPELINE_BIND_POINT_GRAPHICS ) {
                if ( state->dynamic_state.pending_states[xg_graphics_pipeline_dynamic_state_viewport_m] ) {
                    VkViewport vk_viewport;
                    vk_viewport.x = state->dynamic_state.viewport.x;
                    vk_viewport.y = state->dynamic_state.viewport.y + state->dynamic_state.viewport.height;
                    vk_viewport.width = state->dynamic_state.viewport.width;
                    vk_viewport.height = - ( float ) state->dynamic_state.viewport.height;
                    vk_viewport.minDepth = state->dynamic_state.viewport.min_depth;
                    vk_viewport.maxDepth = state->dynamic_state.viewport.max_depth;
                    vkCmdSetViewport ( vk_cmd_buffer, 0, 1, &vk_viewport );
                    VkRect2D vk_scissor;
                    vk_scissor.offset.x = 0;
                    vk_scissor.offset.y = 0;
                    vk_scissor.extent.width = state->dynamic_state.viewport.width;
                    vk_scissor.extent.height = state->dynamic_state.viewport.height;
                    vkCmdSetScissor ( vk_cmd_buffer, 0, 1, &vk_scissor );

                    state->dynamic_state.pending_states[xg_graphics_pipeline_dynamic_state_viewport_m] = false;
                    state->dynamic_state.enabled_viewport = true;
                }

                // If the pipeline declaration didn't enable a dynamic state, that means that state is static, which causes the dynamic
                // state previously bound to be cleared on pipeline bind. So we need to take that into account and clear the enabled flag
                // for the state.
                if ( !state->graphics_pipeline->state.dynamic_state.enabled_states[xg_graphics_pipeline_dynamic_state_viewport_m] ) {
                    state->dynamic_state.enabled_viewport = false;
                }
            }
        }

        state->dirty_pipeline = false;


        if ( state->push_constants_hash != common_pipeline->push_constants_hash ) {
            first_deactivated_set = 0;
        } else {
            //first_deactivated_set = xg_shader_binding_set_invalid_m;

            for ( uint32_t set = 0; set < xg_shader_binding_set_count_m; ++set ) {
                xg_vk_descriptor_set_layout_h set_layout_handle = common_pipeline->descriptor_set_layouts[set];

                bool is_active = state->sets[set].is_active;
                bool is_compatible = state->sets[set].layout == set_layout_handle;

                if ( is_active && !is_compatible ) {
                    first_deactivated_set = set;
                    break;
                }
            }
        }

        for ( uint32_t set = first_deactivated_set; set < xg_shader_binding_set_count_m; ++set ) {
            state->sets[set].is_active = false;
        }

        state->push_constants_hash = common_pipeline->push_constants_hash;
    }

    VkDescriptorSet vk_sets[xg_shader_binding_set_count_m] = { [0 ... xg_shader_binding_set_count_m - 1] = VK_NULL_HANDLE };

    // Pipeline resources
    for ( xg_shader_binding_set_e set_it = 0; set_it < xg_shader_binding_set_count_m; ++set_it ) {
        xg_vk_descriptor_set_layout_h set_layout_handle = common_pipeline->descriptor_set_layouts[set_it];
        xg_vk_translation_resource_binding_set_cache_t* set = &state->sets[set_it];

        bool is_dirty = set->is_dirty;
        bool is_active = set->is_active && !state->pipeline_type_change;
        bool is_compatible = set->layout == set_layout_handle;

        // One of these 2 conditions needs to be true for the resource bindings to need an update
        // 1. set is dirty
        //      in this case the new bind cmd needs to be processed and flushed
        // 2. set is not active, but it is compatible with the current pipeline
        //      in this case an old binding has become once again active because it is now compatible with the current pipeline
        // so if both conditions are false, skip
        if ( !is_dirty && ! ( is_compatible && !is_active ) ) {
            continue;
        }

#if 0
        VkPipelineBindPoint pipeline_type;
        switch ( state->pipeline_type ) {
            case xg_pipeline_graphics_m:
                pipeline_type = VK_PIPELINE_BIND_POINT_GRAPHICS;
                break;

            case xg_pipeline_compute_m:
                pipeline_type = VK_PIPELINE_BIND_POINT_COMPUTE;
                break;

            case xg_pipeline_raytrace_m:
                pipeline_type = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
                break;
        }

        // TODO do this after the for loop, only once
        vkCmdBindDescriptorSets ( vk_cmd_buffer, pipeline_type, common_pipeline->vk_layout_handle, set_it, 1, &vk_set, 0, NULL );
#else
        xg_pipeline_resource_group_h group_handle = set->group;
        if ( false && group_handle != xg_null_handle_m ) {
            VkDescriptorSet vk_set;
            if ( xg_vk_resource_group_handle_is_workload_m ( group_handle ) ) {
                const xg_vk_workload_t* workload = xg_vk_workload_get ( context->workload );
                group_handle = xg_vk_resource_group_handle_remove_tag_m ( group_handle );
                vk_set = workload->desc_sets_array[group_handle];
            } else {
                const xg_vk_pipeline_resource_group_t* group = xg_vk_pipeline_resource_group_get ( device_handle, group_handle );
                vk_set = group->vk_set;
            }

            set->is_dirty = false;
            set->is_active = true;
            set->layout = set_layout_handle;
            vk_sets[set_it] = vk_set;

        } else {
#endif
            const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

            uint32_t buffer_count = set->buffer_count;
            uint32_t texture_count = set->texture_count;
            uint32_t sampler_count = set->sampler_count;
            uint32_t raytrace_world_count = set->raytrace_world_count;

            // TODO is this even possible at this point? should it be possible?
            if ( buffer_count == 0 && texture_count == 0 && sampler_count == 0 && raytrace_world_count == 0 ) {
                continue;
            }

            set->is_dirty = false;
            set->is_active = true;
            set->layout = set_layout_handle;

            VkDescriptorSet vk_set = set->vk_set;

            if ( vk_set == VK_NULL_HANDLE ) {
                const xg_vk_descriptor_set_layout_t* vk_set_layout = xg_vk_descriptor_set_layout_get ( set_layout_handle );

                // TODO batch these
                uint32_t descriptor_counts[xg_resource_binding_count_m] = { 0 };
                for ( uint32_t i = 0; i < vk_set_layout->descriptor_count; ++i ) {
                    descriptor_counts[vk_set_layout->descriptors[i].type] += 1;
                }

                bool replace_desc_allocator = false;
                for ( uint32_t i = 0; i < vk_set_layout->descriptor_count; ++i ) {
                    xg_resource_binding_e type = vk_set_layout->descriptors[i].type;
                    if ( descriptor_counts[type] > context->desc_allocator->descriptor_counts[type] ) {
                        replace_desc_allocator = true;
                        break;
                    }
                }

                if ( replace_desc_allocator ) {
                    std_log_warn_m ( "Descriptor allocator ran out of descriptors, will be replaced" );
                    std_log_error_m ( "Unused resources left in the allocator:"
                        "\n" std_fmt_u32_m " sets" 
                        "\n" std_fmt_u32_m " samplers" 
                        "\n" std_fmt_u32_m " sampled textures" 
                        "\n" std_fmt_u32_m " storage textures" 
                        "\n" std_fmt_u32_m " uniform buffers" 
                        "\n" std_fmt_u32_m " storage buffers" 
                        "\n" std_fmt_u32_m " uniform texel buffers" 
                        "\n" std_fmt_u32_m " storage texel buffers",
                        context->desc_allocator->set_count,
                        context->desc_allocator->descriptor_counts[xg_resource_binding_sampler_m],
                        context->desc_allocator->descriptor_counts[xg_resource_binding_texture_to_sample_m],
                        context->desc_allocator->descriptor_counts[xg_resource_binding_texture_storage_m],
                        context->desc_allocator->descriptor_counts[xg_resource_binding_buffer_uniform_m],
                        context->desc_allocator->descriptor_counts[xg_resource_binding_buffer_storage_m],
                        context->desc_allocator->descriptor_counts[xg_resource_binding_buffer_texel_uniform_m],
                        context->desc_allocator->descriptor_counts[xg_resource_binding_buffer_texel_storage_m]
                    );
                    context->desc_allocator = xg_vk_desc_allocator_pop ( device_handle, workload_handle );
                }

                VkDescriptorSetAllocateInfo vk_set_alloc_info;
                vk_set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                vk_set_alloc_info.pNext = NULL;
                vk_set_alloc_info.descriptorPool = context->desc_allocator->vk_desc_pool;
                vk_set_alloc_info.descriptorSetCount = 1;
                vk_set_alloc_info.pSetLayouts = &vk_set_layout->vk_handle;
                VkResult set_alloc_result = vkAllocateDescriptorSets ( device->vk_handle, &vk_set_alloc_info, &vk_set );
                xg_vk_assert_m ( set_alloc_result );

                for ( uint32_t i = 0; i < xg_resource_binding_count_m; ++i ) {
                    context->desc_allocator->descriptor_counts[i] -= descriptor_counts[i];
                }

                std_assert_m ( context->desc_allocator->set_count > 0 );
                context->desc_allocator->set_count -= 1;

                set->vk_set = vk_set;

                VkWriteDescriptorSet writes[xg_pipeline_resource_max_bindings_m] = { 0 };
                uint32_t write_count = 0;
                VkDescriptorBufferInfo vk_buffer_info[xg_pipeline_resource_max_bindings_m] = { 0 };
                uint32_t buffer_info_count = 0;
                VkDescriptorImageInfo vk_image_info[xg_pipeline_resource_max_bindings_m] = { 0 };
                uint32_t image_info_count = 0;

                for ( uint32_t i = 0; i < buffer_count; ++i ) {
                    xg_buffer_resource_binding_t* binding = &set->buffers[i];
                    uint32_t binding_idx = vk_set_layout->shader_register_to_descriptor_idx[binding->shader_register];
                    xg_resource_binding_e binding_type = vk_set_layout->descriptors[binding_idx].type;
                    const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( binding->range.handle );

                    VkDescriptorBufferInfo* info = &vk_buffer_info[buffer_info_count++];
                    info->offset = binding->range.offset;
                    // TODO check for size <= maxUniformBufferRange
                    //if ( binding->range.size == xg_buffer_whole_size_m ) {
                    //    info->range = buffer->params.size;
                    //} else {
                    //    info->range = binding->range.size;
                    //}
                    info->range = binding->range.size == xg_buffer_whole_size_m ? buffer->params.size : binding->range.size; // VK_WHOLE_SIZE
                    info->buffer = buffer->vk_handle;

                    VkWriteDescriptorSet* write = &writes[write_count++];
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

                for ( uint32_t i = 0; i < texture_count; ++i ) {
                    xg_texture_resource_binding_t* binding = &set->textures[i];
                    uint32_t binding_idx = vk_set_layout->shader_register_to_descriptor_idx[binding->shader_register];
                    xg_resource_binding_e binding_type = vk_set_layout->descriptors[binding_idx].type;
                    const xg_vk_texture_view_t* view = xg_vk_texture_get_view ( binding->texture, binding->view );

                    VkDescriptorImageInfo* info = &vk_image_info[image_info_count++];
                    info->sampler = VK_NULL_HANDLE;
                    // TODO
                    info->imageView = view->vk_handle;
                    info->imageLayout = xg_image_layout_to_vk ( binding->layout );

                    VkWriteDescriptorSet* write = &writes[write_count++];
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

                for ( uint32_t i = 0; i < sampler_count; ++i ) {
                    xg_sampler_resource_binding_t* binding = &set->samplers[i];
                    uint32_t binding_idx = vk_set_layout->shader_register_to_descriptor_idx[binding->shader_register];
                    const xg_vk_sampler_t* sampler = xg_vk_sampler_get ( binding->sampler );

                    VkDescriptorImageInfo* info = &vk_image_info[image_info_count++];
                    info->sampler = sampler->vk_handle;
                    info->imageView = VK_NULL_HANDLE;

                    VkWriteDescriptorSet* write = &writes[write_count++];
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
                    xg_raytrace_world_resource_binding_t* binding = &set->raytrace_worlds[i];
                    uint32_t binding_idx = vk_set_layout->shader_register_to_descriptor_idx[binding->shader_register];
                    const xg_vk_raytrace_world_t* world = xg_vk_raytrace_world_get ( binding->world );

                    VkWriteDescriptorSetAccelerationStructureKHR as_info = {
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
                        .pNext = NULL,
                        .accelerationStructureCount = 1,
                        .pAccelerationStructures = &world->vk_handle,
                    };

                    VkWriteDescriptorSet* write = &writes[write_count++];
                    write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write->pNext = &as_info;
                    write->dstSet = vk_set;
                    write->dstBinding = binding_idx;
                    write->descriptorCount = 1;
                    write->descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                }

                vkUpdateDescriptorSets ( device->vk_handle, write_count, writes, 0, NULL );
            }
            vk_sets[set_it] = vk_set;

#if 0
            {
                VkPipelineBindPoint pipeline_type;

                switch ( state->pipeline_type ) {
                    case xg_pipeline_graphics_m:
                        pipeline_type = VK_PIPELINE_BIND_POINT_GRAPHICS;
                        break;

                    case xg_pipeline_compute_m:
                        pipeline_type = VK_PIPELINE_BIND_POINT_COMPUTE;
                        break;

                    case xg_pipeline_raytrace_m:
                        pipeline_type = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
                        break;
                }

                vkCmdBindDescriptorSets ( vk_cmd_buffer, pipeline_type, common_pipeline->vk_layout_handle, set_it, 1, &vk_set, 0, NULL );
            }
#endif
        }
    }

#if 1
    //if ( first_deactivated_set != xg_shader_binding_set_invalid_m ) 
    {
        VkPipelineBindPoint pipeline_type;
        switch ( state->pipeline_type ) {
            case xg_pipeline_graphics_m:
                pipeline_type = VK_PIPELINE_BIND_POINT_GRAPHICS;
                break;

            case xg_pipeline_compute_m:
                pipeline_type = VK_PIPELINE_BIND_POINT_COMPUTE;
                break;

            case xg_pipeline_raytrace_m:
                pipeline_type = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
                break;
        }

        for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i) {
            if ( vk_sets[i] == VK_NULL_HANDLE ) {
                continue;
            }

            uint32_t j = i;
            while ( j < xg_shader_binding_set_count_m && vk_sets[j] != VK_NULL_HANDLE ) {
                ++j;
            }

            if ( j > i ) {
                vkCmdBindDescriptorSets ( vk_cmd_buffer, pipeline_type, common_pipeline->vk_layout_handle, i, j - i, &vk_sets[i], 0, NULL );
            }
            i = j;
        }
    }
#endif

    state->pipeline_type_change = false;
}

static void xg_vk_submit_context_translate ( xg_vk_cmd_translate_result_t* result, xg_vk_workload_submit_context_t* context, xg_device_h device_handle, xg_workload_h workload_handle ) {
    VkCommandBuffer vk_cmd_buffer = context->cmd_allocator.vk_cmd_buffers[0]; // TODO
    context->cmd_allocator.cmd_buffers_count++;

    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    std_assert_m ( std_align_test_ptr ( context->cmd_headers, xg_cmd_buffer_cmd_alignment_m ) );

    xg_vk_tranlsation_state_t state;
    {
        std_mem_zero_m ( &state );

        state.render_textures.depth_stencil.texture = xg_null_handle_m;

        for ( size_t i = 0; i < xg_pipeline_output_max_color_targets_m; ++i ) {
            state.render_textures.render_targets[i].texture = xg_null_handle_m;
        }

        for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
            state.sets[i].group = xg_null_handle_m;
        }
    }

    bool debug_capture_started = false;
    bool debug_capture_stop_on_submit = false;
    bool debug_capture_stop_on_present = false;

    {
        VkCommandBufferBeginInfo begin_info;
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.pNext = NULL;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        begin_info.pInheritanceInfo = NULL;
        vkBeginCommandBuffer ( vk_cmd_buffer, &begin_info );
    }

    //xg_vk_workload_query_pool_t* timestamp_pool = xg_workload_get_timestamp_query_pool ( context->workload );
    //std_assert_m ( timestamp_pool );

    for ( size_t cmd_header_it = 0; cmd_header_it < context->cmd_headers_count; ++cmd_header_it ) {
        const xg_cmd_header_t* header = &context->cmd_headers[cmd_header_it];
        switch ( header->type ) {

            // -----------------------------------------------------------------------
            // Simple vertex input streams bind call.
            // The streams get sorted by id so that contiguous streams get bound within one single Vulkan call as per API design.
            case xg_cmd_graphics_streams_bind_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_bind_t* ) header->args;

                std_assert_m ( args->bindings_count < xg_vertex_stream_max_bindings_m );

                // Sort
                // This is done so that later segments of streams that are contiguous in their ids can be found easily.
                // This is done to reflect the Vulkan api design.
                {
                    xg_vertex_stream_binding_t tmp;
                    std_sort_insertion ( args->bindings, sizeof ( xg_vertex_stream_binding_t ), args->bindings_count, xg_vertex_stream_binding_cmp_f, &tmp );
                }

                // Get handles
                // xg_cmd_graphics_streams_bind_t stores the user handle for each buffer.
                // Dereference these handles to the actual buffer objects so that we can access the vulkan handles.
                // Vulkan handles and offsets are laid down in two buffers to later feed them to Vulkan.
                VkBuffer vk_buffers[xg_vertex_stream_max_bindings_m];
                VkDeviceSize vk_offsets[xg_vertex_stream_max_bindings_m];

                uint32_t buffers_count = args->bindings_count;

                for ( size_t i = 0; i < buffers_count; ++i ) {
                    const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( args->bindings[i].buffer );
                    vk_buffers[i] = buffer->vk_handle;
                    vk_offsets[i] = args->bindings[i].offset;
                }

                // Compute segments
                // We go from the sorted streams to segment of streams that are contiguous in their ids.
                // Each segments array value stores the number of contiguous streams.
                // The sum of these values will be the total streams count.
                size_t segments[xg_vertex_stream_max_bindings_m];
                size_t segments_count = 0;

                for ( size_t i = 0; i < buffers_count; ) {
                    size_t base = i;
                    uint32_t stream = args->bindings[i].stream_id;

                    while ( i + 1 < buffers_count ) {

                        if ( args->bindings[i + 1].stream_id != stream + 1 ) {
                            break;
                        }

                        ++stream;
                        ++i;
                    }

                    ++i;
                    segments[segments_count] = i - base;
                    ++segments_count;
                }

                // Record commands
                // We can now bind the segments through Vulkan.
                // Each call takes the base stream id, a streams count, and two arrays, one for the handles and the other for the offsets.
                size_t base = 0;

                for ( size_t i = 0; i < segments_count; ++i ) {
                    vkCmdBindVertexBuffers ( vk_cmd_buffer, args->bindings[base].stream_id, ( uint32_t ) segments[i], vk_buffers + base, vk_offsets + base );
                    base += segments[i];
                }
            }
            break;

            // -----------------------------------------------------------------------
            // For now pipeline states and render passes are stored together and treated as basically one single thing. This means that for
            // each new pipeline a new render pass needs to be created, and they are bound together.
            // Further more for now(?) framebuffers are implicit, meaning that the framebuffer object gets created at the last moment before
            // binding it. This means we need to store them, have a hash map to reuse them when the same render textures are provided multiple
            // times, and delete them once the gpu is done with the submission.
            // Going forward it is probably worth to treat render passes as separate objects that can be created by users through the API,
            // since it is much more realistic to have a number of pipeline states that all share the same render pass. So first the render pass
            // would begin and then a number of pipelines would be switched inside of it.
            // On the other hand framebuffers are fine the way they are now, making them an explicit object is a possibility but not a necessity.
            // The realistic case for them is a 1:1 match with render passes, meaning that a new framebuffer is bound every time a new
            // render pass begins. There might be exceptions though and multiple render passes could use the same framebuffer
            // (solid/translucent/.. color passes?) just like a single render pass could use multiple frame buffers (shadow pass?).
            // 2021-05-09
            // Using VK_KHR_imageless_framebuffer means framebuffers can be created together with render passes and do not need to reference
            // the actual textures, just the format and size.
            // https://github.com/KhronosGroup/Vulkan-Guide/blob/master/chapters/extensions/VK_KHR_imageless_framebuffer.md
            case xg_cmd_graphics_pipeline_state_bind_m: {
                std_auto_m args = ( xg_cmd_graphics_pipeline_state_bind_t* ) header->args;

                const xg_vk_graphics_pipeline_t* pipeline = xg_vk_graphics_pipeline_get ( args->pipeline );

                state.pipeline_type_change = state.pipeline_type != xg_pipeline_graphics_m;
                state.dirty_pipeline = state.graphics_pipeline != pipeline;
                state.graphics_pipeline = pipeline;
                state.pipeline_type = xg_pipeline_graphics_m;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_pipeline_state_set_viewport_m: {
                std_auto_m args = ( xg_cmd_graphics_pipeline_state_set_viewport_t* ) header->args;

                state.dynamic_state.pending_states[xg_graphics_pipeline_dynamic_state_viewport_m] = true;
                state.dynamic_state.viewport = args->viewport;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_pipeline_state_set_scissor_m: {
                std_auto_m args = ( xg_cmd_graphics_pipeline_state_set_scissor_t* ) header->args;

                state.dynamic_state.pending_states[xg_graphics_pipeline_dynamic_state_scissor_m] = true;
                state.dynamic_state.scissor = args->scissor;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_renderpass_begin_m: {
                std_auto_m args = ( xg_cmd_graphics_renderpass_begin_t* ) header->args;
            
                state.graphics_renderpass = xg_vk_graphics_renderpass_get ( args->renderpass );
                state.active_renderpass = true;
                //state.dirty_renderpass = true;
                state.dirty_render_textures = true;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_renderpass_end_m: {
                std_auto_m args = ( xg_cmd_graphics_renderpass_end_t* ) header->args;

                std_assert_m ( state.active_renderpass );
                std_assert_m ( state.graphics_renderpass == xg_vk_graphics_renderpass_get ( args->renderpass ) );
                vkCmdEndRenderPass ( vk_cmd_buffer );
                state.graphics_renderpass = NULL;
                state.active_renderpass = false;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_compute_pipeline_state_bind_m: {
                std_auto_m args = ( xg_cmd_compute_pipeline_state_bind_t* ) header->args;

                const xg_vk_compute_pipeline_t* pipeline = xg_vk_compute_pipeline_get ( args->pipeline );

                state.pipeline_type_change = state.pipeline_type != xg_pipeline_compute_m;
                state.compute_pipeline = pipeline;
                state.dirty_pipeline = true;
                state.pipeline_type = xg_pipeline_compute_m;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_pipeline_constant_write_m: {
                std_auto_m args = ( xg_cmd_pipeline_constant_data_write_t* ) header->args;

                // TODO
                std_unused_m ( args );
                std_not_implemented_m();
            }
            break;

            case xg_cmd_pipeline_resource_group_bind_m: {
                std_auto_m args = ( xg_cmd_pipeline_resource_group_bind_t* ) header->args;

                xg_pipeline_resource_group_h group_handle = args->group;
                xg_vk_translation_resource_binding_set_cache_t* set = &state.sets[args->set];
                //const xg_vk_pipeline_resource_group_t* group = xg_vk_pipeline_resource_group_get ( device_handle, group_handle );

                set->group = group_handle;
                //set->vk_set = group->vk_set;
                //set->layout = group->layout;
                set->is_dirty = true;
                set->is_active = false;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_pipeline_resource_bind_m: {
                std_auto_m args = ( xg_cmd_pipeline_resource_bind_t* ) header->args;

                /*
                    TODO:
                        ~have a DescriptorSetLayout cache in pipeline creation
                        reuse layouts so that this https://developer.nvidia.com/vulkan-shader-resource-binding can be done (set layout stays the same => bindings are still valid)
                            ~store the current layouts and the current set bindings in the submission context (or just use the one in the current pipeline? and/or add a local bindings in the function scope?)
                            acutally bind on the vk cmd buffer at draw time
                            only bind what is necessary, checking current layout vs state binding layout.
                            since current layouts are in the submission context they can be valid across pipeline changes/...(is this necessary?)


                        here (at resource bind cmd parse) just fill the table with the new resource bindings, marking sets as dirty when written to
                            add bitsets to type/stage to speedup search of used registers
                        create, fill and bind descriptor sets at draw time
                            find which sets need to be allocated and written by looking at sets dirty flags, using the current pipeline layout to allocate
                        at pipeline bind time need to check if need to clear current bindings
                            match old and new pipe layouts, fint first i-th non compatible set, keep 0-i bindings and clear i-n.

                        alternative: create, fill and bind descriptor sets at bind time, no check necessary at pipeline build time
                            vulkan docs state that sets can be bound before binding a pipe. if can assume that all bindings for a set come in the same cmd,
                            can avoid having the big table of current bindings completely and just allocate fill and bind immediately, the assumption is significant
                            however, it means that no state in the same set can leak and that all state has to come together in one cmd. good or bad?

                        decision: do the alternative. possibly have a higher level component that has the bindings table to track bindings and can be flushed to a cmd buffer.
                            is this the right choice? test out the other way?
                */

#if 0

                if ( args->bindings_count == 0 ) {
                    break;
                }

                state.sets[args->set].bindings_count = args->bindings_count;

                for ( uint64_t i = 0; i < args->bindings_count; ++i ) {
                    state.sets[args->set].bindings[i] = args->bindings[i];
                }

#endif

                uint32_t buffer_count = args->buffer_count;
                uint32_t texture_count = args->texture_count;
                uint32_t sampler_count = args->sampler_count;
                uint32_t raytrace_world_count = args->raytrace_world_count;

                std_assert_m ( buffer_count <= xg_pipeline_resource_max_buffers_per_set_m );
                std_assert_m ( texture_count <= xg_pipeline_resource_max_textures_per_set_m );
                std_assert_m ( sampler_count <= xg_pipeline_resource_max_samplers_per_set_m );
                std_assert_m ( raytrace_world_count <= xg_pipeline_resource_max_raytrace_worlds_per_set_m );

                xg_buffer_resource_binding_t* buffers = std_align_ptr_m ( args + 1, xg_buffer_resource_binding_t );
                xg_texture_resource_binding_t* textures = std_align_ptr_m ( buffers + buffer_count, xg_texture_resource_binding_t );
                xg_sampler_resource_binding_t* samplers = std_align_ptr_m ( textures + texture_count, xg_sampler_resource_binding_t );
                xg_raytrace_world_resource_binding_t* raytrace_worlds = std_align_ptr_m ( samplers + sampler_count, xg_raytrace_world_resource_binding_t );

                std_assert_m ( args->set < xg_shader_binding_set_count_m );

                xg_vk_translation_resource_binding_set_cache_t* set = &state.sets[args->set];

                for ( uint32_t i = 0; i < buffer_count; ++i ) {
                    set->buffers[i] = buffers[i];
                }

                for ( uint32_t i = 0; i < texture_count; ++i ) {
                    set->textures[i] = textures[i];
                }

                for ( uint32_t i = 0; i < sampler_count; ++i ) {
                    set->samplers[i] = samplers[i];
                }

                for ( uint32_t i = 0; i < raytrace_world_count; ++i ) {
                    set->raytrace_worlds[i] = raytrace_worlds[i];
                }

                set->buffer_count = buffer_count;
                set->texture_count = texture_count;
                set->sampler_count = sampler_count;
                set->raytrace_world_count = raytrace_world_count;

                set->is_dirty = true;
                set->is_active = false;
                set->layout = xg_null_handle_m;
                set->vk_set = VK_NULL_HANDLE;
                set->group = xg_null_handle_m;
            }
            break;

            // -----------------------------------------------------------------------
            // Upon receiving render textures we need to create a framebuffer. To do that we need the Vulkan render pass,
            // which is stored together with the PSO for now(?), so the render textures are cached and the creation/binding
            // of the framebuffer is deferred until we get a pipeline bind cmd.
            case xg_cmd_graphics_render_textures_bind_m: {
                std_auto_m args =  ( xg_cmd_graphics_render_texture_bind_t* ) header->args;

                state.render_textures.render_targets_count = args->render_targets_count;
                state.render_textures.depth_stencil = args->depth_stencil;

                for ( size_t i = 0; i < state.render_textures.render_targets_count; ++i ) {
                    state.render_textures.render_targets[i] = args->render_targets[i];
                }

                state.dirty_render_textures = true;

                // Defer bind until we get a pipeline/render pass
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_streams_submit_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_submit_t* ) header->args;

                xg_vk_translation_state_cache_flush ( &state, context, device_handle, workload_handle, vk_cmd_buffer );

                vkCmdDraw ( vk_cmd_buffer, args->count, 1, args->base, 0 );

                std_noop_m;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_streams_submit_indexed_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_submit_indexed_t* ) header->args;

                xg_vk_translation_state_cache_flush ( &state, context, device_handle, workload_handle, vk_cmd_buffer );

                const xg_vk_buffer_t* ibuffer = xg_vk_buffer_get ( args->ibuffer );

                // TODO size as param
                vkCmdBindIndexBuffer ( vk_cmd_buffer, ibuffer->vk_handle, 0, VK_INDEX_TYPE_UINT32 );
                vkCmdDrawIndexed ( vk_cmd_buffer, args->index_count, 1, args->ibuffer_base, 0, 0 );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_streams_submit_instanced_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_submit_instanced_t* ) header->args;

                xg_vk_translation_state_cache_flush ( &state, context, device_handle, workload_handle, vk_cmd_buffer );

                vkCmdDraw ( vk_cmd_buffer, args->vertex_count, args->instance_count, args->vertex_base, args->instance_base );

                std_noop_m;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_compute_dispatch_m: {
                std_auto_m args = ( xg_cmd_compute_dispatch_t* ) header->args;

                xg_vk_translation_state_cache_flush ( &state, context, device_handle, workload_handle, vk_cmd_buffer );

                vkCmdDispatch ( vk_cmd_buffer, args->workgroup_count_x, args->workgroup_count_y, args->workgroup_count_z );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_raytrace_pipeline_state_bind_m: {
                std_auto_m args = ( xg_cmd_raytrace_pipeline_state_bind_t* ) header->args;

                const xg_vk_raytrace_pipeline_t* pipeline = xg_vk_raytrace_pipeline_get ( args->pipeline );

                state.raytrace_pipeline = pipeline;
                state.dirty_pipeline = true;
                state.pipeline_type = xg_pipeline_raytrace_m;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_raytrace_trace_rays_m: {
                // TODO
                #if 1
                std_auto_m args = ( xg_cmd_raytrace_trace_rays_t* ) header->args;

                xg_vk_translation_state_cache_flush ( &state, context, device_handle, workload_handle, vk_cmd_buffer );

                const xg_vk_raytrace_pipeline_t* pipeline = state.raytrace_pipeline;
                std_assert_m ( pipeline );
                VkStridedDeviceAddressRegionKHR callable_region = { 0 };
                xg_vk_device_ext_api ( device_handle )->trace_rays ( vk_cmd_buffer,
                    &pipeline->sbt_gen_region, &pipeline->sbt_miss_region, &pipeline->sbt_hit_region, &callable_region,
                    args->ray_count_x, args->ray_count_y, args->ray_count_z );
                #endif
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_buffer_m : {
                std_auto_m args = ( xg_cmd_copy_buffer_t* ) header->args;

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

            // -----------------------------------------------------------------------
            case xg_cmd_copy_texture_m : {
                std_auto_m args = ( xg_cmd_copy_texture_t* ) header->args;

                const xg_vk_texture_t* source = xg_vk_texture_get ( args->source.texture );
                const xg_vk_texture_t* dest = xg_vk_texture_get ( args->destination.texture );

                // TODO assert that this is the case somewhere?
                VkImageLayout source_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; //xg_image_layout_to_vk ( source->layout );
                VkImageLayout dest_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; //xg_image_layout_to_vk ( dest->layout );

                // TODO support all texture types, sizes, depth/stencil case, ...
                //std_assert_m ( source->params.width == dest->params.width );
                //std_assert_m ( source->params.height == dest->params.height );
                //std_assert_m ( source->params.depth == dest->params.depth );

                // TODO use copyImage when possible?
#if 0
                VkImageCopy copy;
                copy.srcOffset.x = 0;
                copy.srcOffset.y = 0;
                copy.srcOffset.z = 0;
                copy.dstOffset.x = 0;
                copy.dstOffset.y = 0;
                copy.dstOffset.z = 0;
                copy.extent.width = ( uint32_t ) source->params.width;
                copy.extent.height = ( uint32_t ) source->params.height;
                copy.extent.depth = ( uint32_t ) source->params.depth;
                copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.srcSubresource.mipLevel = 0;
                copy.srcSubresource.baseArrayLayer = 0;
                copy.srcSubresource.layerCount = 1;
                copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.dstSubresource.mipLevel = 0;
                copy.dstSubresource.baseArrayLayer = 0;
                copy.dstSubresource.layerCount = 1;

                vkCmdCopyImage ( vk_cmd_buffer, source->vk_handle, source_layout, dest->vk_handle, dest_layout, 1, &copy );
#endif

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

                    if ( args->mip_count == xg_texture_all_mips_m ) {
                        if ( source_mip_count != dest_mip_count ) {
                            std_log_warn_m ( "Copy texture command has source and destination textures with non matching mip levels count" );
                        }

                        mip_count = std_min_u32 ( source_mip_count, dest_mip_count );
                    } else {
                        std_assert_m ( source_mip_count >= args->mip_count );
                        std_assert_m ( dest_mip_count >= args->mip_count );
                        mip_count = args->mip_count;
                    }
                }

                uint32_t array_count;
                {
                    uint32_t source_array_count = source->params.array_layers - args->source.array_base;
                    uint32_t dest_array_count = dest->params.array_layers - args->destination.array_base;

                    if ( args->array_count == xg_texture_whole_array_m ) {
                        if ( source_array_count != dest_array_count ) {
                            std_log_warn_m ( "Copy texture command has source and destination textures with non matching array layers count" );
                        }

                        array_count = std_min_u32 ( source_array_count, dest_array_count );
                    } else {
                        std_assert_m ( source_array_count >= args->array_count );
                        std_assert_m ( dest_array_count >= args->array_count );
                        array_count = args->array_count;
                    }
                }

                for ( uint32_t i = 0; i < mip_count; ++i ) {
                    VkImageBlit blit;
                    blit.srcOffsets[0].x = 0;
                    blit.srcOffsets[0].y = 0;
                    blit.srcOffsets[0].z = 0;
                    blit.srcOffsets[1].x = ( int32_t ) source->params.width >> ( args->source.mip_base + i );
                    blit.srcOffsets[1].y = ( int32_t ) source->params.height >> ( args->source.mip_base + i );
                    blit.srcOffsets[1].z = ( int32_t ) source->params.depth;

                    blit.dstOffsets[0].x = 0;
                    blit.dstOffsets[0].y = 0;
                    blit.dstOffsets[0].z = 0;
                    blit.dstOffsets[1].x = ( int32_t ) dest->params.width >> ( args->destination.mip_base + i );
                    blit.dstOffsets[1].y = ( int32_t ) dest->params.height >> ( args->destination.mip_base + i );
                    blit.dstOffsets[1].z = ( int32_t ) dest->params.depth;

                    blit.srcSubresource.aspectMask = src_aspect;
                    blit.srcSubresource.mipLevel = args->source.mip_base + i;
                    blit.srcSubresource.baseArrayLayer = args->source.array_base;
                    blit.srcSubresource.layerCount = array_count;

                    blit.dstSubresource.aspectMask = dst_aspect;
                    blit.dstSubresource.mipLevel = args->destination.mip_base + i;
                    blit.dstSubresource.baseArrayLayer = args->destination.array_base;
                    blit.dstSubresource.layerCount = array_count;

                    vkCmdBlitImage ( vk_cmd_buffer, source->vk_handle, source_layout, dest->vk_handle, dest_layout, 1, &blit, filter );

                    std_noop_m;
                }
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_texture_to_buffer_m: {
                std_auto_m args = ( xg_cmd_copy_texture_to_buffer_t* ) header->args;

                const xg_vk_texture_t* source = xg_vk_texture_get ( args->source );
                const xg_vk_buffer_t* dest = xg_vk_buffer_get ( args->destination );

                // TODO assert that this is the case somewhere?
                VkImageLayout source_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; //xg_image_layout_to_vk ( dest->layout );

                VkImageAspectFlags source_aspect = xg_texture_flags_to_vk_aspect ( source->flags );

                if ( source_aspect == 0 ) {
                    source_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                VkBufferImageCopy copy;
                copy.bufferOffset = args->destination_offset;
                copy.bufferRowLength = 0;
                copy.bufferImageHeight = 0;
                copy.imageSubresource.aspectMask = source_aspect;
                copy.imageSubresource.mipLevel = args->mip_base;
                copy.imageSubresource.baseArrayLayer = args->array_base;
                copy.imageSubresource.layerCount = args->array_count;
                copy.imageOffset.x = 0;
                copy.imageOffset.y = 0;
                copy.imageOffset.z = 0;
                copy.imageExtent.width = source->params.width;
                copy.imageExtent.height = source->params.height;
                copy.imageExtent.depth = source->params.depth;

                vkCmdCopyImageToBuffer ( vk_cmd_buffer, source->vk_handle, source_layout, dest->vk_handle, 1, &copy );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_buffer_to_texture_m: {
                std_auto_m args = ( xg_cmd_copy_buffer_to_texture_t* ) header->args;

                const xg_vk_buffer_t* source = xg_vk_buffer_get ( args->source );
                const xg_vk_texture_t* dest = xg_vk_texture_get ( args->destination );

                // TODO assert that this is the case somewhere?
                VkImageLayout dest_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; //xg_image_layout_to_vk ( dest->layout );

                VkImageAspectFlags dst_aspect = xg_texture_flags_to_vk_aspect ( dest->flags );

                if ( dst_aspect == 0 ) {
                    dst_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                }

                VkBufferImageCopy copy;
                copy.bufferOffset = args->source_offset;
                copy.bufferRowLength = 0;
                copy.bufferImageHeight = 0;
                copy.imageSubresource.aspectMask = dst_aspect;
                copy.imageSubresource.mipLevel = args->mip_base;
                copy.imageSubresource.baseArrayLayer = args->array_base;
                copy.imageSubresource.layerCount = args->array_count;
                copy.imageOffset.x = 0;
                copy.imageOffset.y = 0;
                copy.imageOffset.z = 0;
                copy.imageExtent.width = dest->params.width;
                copy.imageExtent.height = dest->params.height;
                copy.imageExtent.depth = dest->params.depth;

                vkCmdCopyBufferToImage ( vk_cmd_buffer, source->vk_handle, dest->vk_handle, dest_layout, 1, &copy );

                std_noop_m;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_barrier_set_m: {
                std_auto_m args = ( xg_cmd_barrier_set_t* ) header->args;

                char* base = ( char* ) ( args + 1 );

#if xg_vk_enable_sync2_m
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

                        vk_barrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        vk_barrier->pNext = NULL;
                        vk_barrier->srcAccessMask = xg_memory_access_to_vk ( barrier->memory.flushes );
                        vk_barrier->dstAccessMask = xg_memory_access_to_vk ( barrier->memory.invalidations );
                        vk_barrier->srcStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocker );
                        vk_barrier->dstStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocked );
                        vk_barrier->srcQueueFamilyIndex = device->graphics_queue.vk_family_idx;
                        vk_barrier->dstQueueFamilyIndex = device->graphics_queue.vk_family_idx;
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
                        vk_barrier->srcQueueFamilyIndex = device->graphics_queue.vk_family_idx;
                        vk_barrier->dstQueueFamilyIndex = device->graphics_queue.vk_family_idx;
                        std_assert_m ( texture->vk_handle );
                        vk_barrier->image = texture->vk_handle;
                        vk_barrier->subresourceRange = range;

                        base += sizeof ( xg_texture_memory_barrier_t );
                    }
                }

                if ( state.active_renderpass && state.graphics_renderpass == NULL ) {
                    vkCmdEndRenderPass ( vk_cmd_buffer );
                    state.active_renderpass = false;
                }

                VkDependencyInfoKHR vk_dependency_info;
                vk_dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
                vk_dependency_info.pNext = NULL;
                vk_dependency_info.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
                vk_dependency_info.memoryBarrierCount = 0;
                vk_dependency_info.bufferMemoryBarrierCount = args->buffer_memory_barriers;
                vk_dependency_info.pBufferMemoryBarriers = vk_buffer_barriers;
                vk_dependency_info.imageMemoryBarrierCount = args->texture_memory_barriers;
                vk_dependency_info.pImageMemoryBarriers = vk_texture_barriers;

                xg_vk_instance_ext_api()->cmd_sync2_pipeline_barrier ( vk_cmd_buffer, &vk_dependency_info );

                std_noop_m;
#else
                // TODO test this path, probably not working atm
                // --- !! WARNING !! ---
                // Support for non-sync2 (sync1?) devices is very poor
                // The code below assumes that all barriers in a set share the same srcStage and dstStage
                VkImageMemoryBarrier vk_texture_barriers[xg_vk_workload_max_texture_barriers_per_cmd_m];

                VkImageSubresourceRange range;
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                if ( args->memory_barriers > 0 ) {
                    std_not_implemented_m();
                }

                VkPipelineStageFlags srcStage = 0;
                VkPipelineStageFlags dstStage = 0;

                if ( args->texture_memory_barriers > 0 ) {
                    base = ( char* ) std_align_ptr ( base, std_alignof_m ( xg_texture_memory_barrier_t ) );

                    for ( uint32_t i = 0; i < args->texture_memory_barriers; ++i ) {
                        VkImageMemoryBarrier* vk_barrier = &vk_texture_barriers[i];
                        xg_texture_memory_barrier_t* barrier = ( xg_texture_memory_barrier_t* ) base;
                        const xg_vk_texture_t* texture = xg_vk_texture_get ( barrier->texture );

                        if ( srcStage == 0 && dstStage == 0 ) {
                            srcStage = xg_pipeline_stage_to_vk ( barrier->execution.blocker );
                            dstStage = xg_pipeline_stage_to_vk ( barrier->execution.blocked );
                        } else {
                            std_assert_m ( srcStage == xg_pipeline_stage_to_vk ( barrier->execution.blocker ) );
                            std_assert_m ( dstStage == xg_pipeline_stage_to_vk ( barrier->execution.blocked ) );
                        }

                        vk_barrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        vk_barrier->pNext = NULL;
                        vk_barrier->srcAccessMask = xg_memory_access_to_vk ( barrier->memory.flushes );
                        //vk_barrier->srcStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocker );
                        vk_barrier->dstAccessMask = xg_memory_access_to_vk ( barrier->memory.invalidations );
                        //vk_barrier->dstStageMask = xg_pipeline_stage_to_vk ( barrier->execution.blocked );
                        vk_barrier->oldLayout = xg_image_layout_to_vk ( barrier->layout.old );
                        vk_barrier->newLayout = xg_image_layout_to_vk ( barrier->layout.new );
                        vk_barrier->srcQueueFamilyIndex = device->graphics_queue.vk_family_idx;
                        vk_barrier->dstQueueFamilyIndex = device->graphics_queue.vk_family_idx;
                        vk_barrier->image = texture->vk_handle;
                        vk_barrier->subresourceRange = range;

                        //xg_vk_texture_set_layout ( barrier->texture, barrier->new_layout );

                        base += sizeof ( xg_texture_memory_barrier_t );
                    }
                }

                if ( state.active_renderpass && state.graphics_renderpass == NULL ) {
                    vkCmdEndRenderPass ( vk_cmd_buffer );
                    state.active_renderpass = false;
                }

                vkCmdPipelineBarrier ( vk_cmd_buffer, srcStage, dstStage, 0, 0, NULL, 0, NULL, args->texture_memory_barriers, vk_texture_barriers );
#endif
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_texture_clear_m: {
                std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;

                const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );

                VkClearColorValue clear;
                std_mem_copy ( &clear, &args->clear, sizeof ( VkClearColorValue ) );

                VkImageSubresourceRange range;
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                if ( state.active_renderpass && state.graphics_renderpass == NULL ) {
                    vkCmdEndRenderPass ( vk_cmd_buffer );
                    state.active_renderpass = false;
                }

                vkCmdClearColorImage ( vk_cmd_buffer, texture->vk_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_texture_depth_stencil_clear_m: {
                std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;

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

                VkImageSubresourceRange range;
                range.aspectMask = aspect;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                vkCmdClearDepthStencilImage ( vk_cmd_buffer, texture->vk_handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_start_debug_capture_m: {
                std_auto_m args = ( xg_cmd_start_debug_capture_t* ) header->args;

                if ( debug_capture_started ) {
                    std_log_warn_m ( "More than one debug capture has been queued for start on the same workload" );
                    continue;
                }

                debug_capture_started = true;

                if ( args->stop_time == xg_debug_capture_stop_time_workload_submit_m ) {
                    if ( debug_capture_stop_on_submit || debug_capture_stop_on_present ) {
                        std_log_warn_m ( "More than one debug capture has been queued for stop on the same workload" );
                        continue;
                    }

                    debug_capture_stop_on_submit = true;
                } else if ( args->stop_time == xg_debug_capture_stop_time_workload_present_m ) {
                    if ( debug_capture_stop_on_submit || debug_capture_stop_on_present ) {
                        std_log_warn_m ( "More than one debug capture has been queued for stop on the same workload" );
                        continue;
                    }

                    debug_capture_stop_on_present = true;
                }

                xg_debug_capture_start();
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_stop_debug_capture_m: {
                if ( debug_capture_stop_on_submit || debug_capture_stop_on_present ) {
                    std_log_warn_m ( "More than one debug capture has been queued for stop on the same workload" );
                    continue;
                }

                debug_capture_stop_on_submit = true;
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_begin_debug_region_m: {
                std_auto_m args = ( xg_cmd_begin_debug_region_t* ) header->args;

                VkDebugUtilsLabelEXT label;
                label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                label.pNext = NULL;
                label.pLabelName = args->name;
                label.color[3] = ( ( args->color_rgba >>  0 ) & 0xff ) / 255.f;
                label.color[2] = ( ( args->color_rgba >>  8 ) & 0xff ) / 255.f;
                label.color[1] = ( ( args->color_rgba >> 16 ) & 0xff ) / 255.f;
                label.color[0] = ( ( args->color_rgba >> 24 ) & 0xff ) / 255.f;
                xg_vk_instance_ext_api()->cmd_begin_debug_region ( vk_cmd_buffer, &label );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_end_debug_region_m: {
                xg_vk_instance_ext_api()->cmd_end_debug_region ( vk_cmd_buffer );
            }
            break;

                // -----------------------------------------------------------------------
#if 0

            case xg_cmd_write_timestamp_m: {
                std_auto_m args = ( xg_cmd_write_timestamp_t* ) header->args;
                std_unused_m ( args );

                VkPipelineStageFlags pipeline_stage = xg_pipeline_stage_to_vk ( args->dependency );
                uint32_t query_idx;
                VkQueryPool vk_pool = xg_vk_workload_get_timestamp_query ( &query_idx, context->workload );
                vkCmdWriteTimestamp ( vk_cmd_buffer, pipeline_stage, vk_pool, query_idx );

                std_noop_m;
            }
            break;
#endif
        }
    }

    if ( state.active_renderpass && state.graphics_renderpass == NULL ) {
        vkCmdEndRenderPass ( vk_cmd_buffer );
        state.active_renderpass = false;
    }

    result->debug_capture_stop_on_workload_submit = debug_capture_stop_on_submit;
    result->debug_capture_stop_on_workload_present = debug_capture_stop_on_present && !debug_capture_stop_on_submit;

    vkEndCommandBuffer ( vk_cmd_buffer );
}

std_unused_static_m()
static void xg_vk_submit_context_debug_print ( const xg_vk_workload_submit_context_t* context ) {
    std_assert_m ( std_align_test_ptr ( context->cmd_headers, xg_cmd_buffer_cmd_alignment_m ) );
    
    std_log_info_m ( "begin cmd buffer" );

    for ( size_t cmd_header_it = 0; cmd_header_it < context->cmd_headers_count; ++cmd_header_it ) {
        const xg_cmd_header_t* header = &context->cmd_headers[cmd_header_it];
        switch ( header->type ) {

            // -----------------------------------------------------------------------
            // Simple vertex input streams bind call.
            // The streams get sorted by id so that contiguous streams get bound within one single Vulkan call as per API design.
            case xg_cmd_graphics_streams_bind_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_bind_t* ) header->args;
                std_unused_m ( args );

                std_log_info_m ( "graphics streams bind" );
            }
            break;

            // -----------------------------------------------------------------------
            // For now pipeline states and render passes are stored together and treated as basically one single thing. This means that for
            // each new pipeline a new render pass needs to be created, and they are bound together.
            // Further more for now(?) framebuffers are implicit, meaning that the framebuffer object gets created at the last moment before
            // binding it. This means we need to store them, have a hash map to reuse them when the same render textures are provided multiple
            // times, and delete them once the gpu is done with the submission.
            // Going forward it is probably worth to treat render passes as separate objects that can be created by users through the API,
            // since it is much more realistic to have a number of pipeline states that all share the same render pass. So first the render pass
            // would begin and then a number of pipelines would be switched inside of it.
            // On the other hand framebuffers are fine the way they are now, making them an explicit object is a possibility but not a necessity.
            // The realistic case for them is a 1:1 match with render passes, meaning that a new framebuffer is bound every time a new
            // render pass begins. There might be exceptions though and multiple render passes could use the same framebuffer
            // (solid/translucent/.. color passes?) just like a single render pass could use multiple frame buffers (shadow pass?).
            // 2021-05-09
            // Using VK_KHR_imageless_framebuffer means framebuffers can be created together with render passes and do not need to reference
            // the actual textures, just the format and size.
            // https://github.com/KhronosGroup/Vulkan-Guide/blob/master/chapters/extensions/VK_KHR_imageless_framebuffer.md
            case xg_cmd_graphics_pipeline_state_bind_m: {
                std_auto_m args = ( xg_cmd_graphics_pipeline_state_bind_t* ) header->args;

                const xg_vk_graphics_pipeline_t* pipeline = xg_vk_graphics_pipeline_get ( args->pipeline );
                std_log_info_m ( "graphics pipeline bind: " std_fmt_str_m, pipeline->common.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_pipeline_state_set_viewport_m: {
                std_auto_m args = ( xg_cmd_graphics_pipeline_state_set_viewport_t* ) header->args;
                std_unused_m ( args );

                std_log_info_m ( "viewport bind" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_pipeline_state_set_scissor_m: {
                std_auto_m args = ( xg_cmd_graphics_pipeline_state_set_scissor_t* ) header->args;
                std_unused_m ( args );

                std_log_info_m ( "scissor bind" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_renderpass_begin_m: {
                std_auto_m args = ( xg_cmd_graphics_renderpass_begin_t* ) header->args;
            
                const xg_vk_graphics_renderpass_t* renderpass = xg_vk_graphics_renderpass_get ( args->renderpass );
                std_log_info_m ( "renderpass begin: " std_fmt_str_m, renderpass->params.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_renderpass_end_m: {
                std_auto_m args = ( xg_cmd_graphics_renderpass_end_t* ) header->args;

                const xg_vk_graphics_renderpass_t* renderpass = xg_vk_graphics_renderpass_get ( args->renderpass );
                std_log_info_m ( "renderpass end: " std_fmt_str_m, renderpass->params.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_compute_pipeline_state_bind_m: {
                std_auto_m args = ( xg_cmd_compute_pipeline_state_bind_t* ) header->args;

                const xg_vk_compute_pipeline_t* pipeline = xg_vk_compute_pipeline_get ( args->pipeline );
                std_log_info_m ( "compute pipeline bind: " std_fmt_str_m, pipeline->common.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_pipeline_constant_write_m: {
                std_auto_m args = ( xg_cmd_pipeline_constant_data_write_t* ) header->args;
                std_unused_m ( args );
                
                // TODO
                std_log_info_m ( "pipeline constant bind" );
            }
            break;

            case xg_cmd_pipeline_resource_group_bind_m: {
                std_auto_m args = ( xg_cmd_pipeline_resource_group_bind_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "pipeline resource group bind" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_pipeline_resource_bind_m: {
                std_auto_m args = ( xg_cmd_pipeline_resource_bind_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "pipeline resource bind" );
            }
            break;

            // -----------------------------------------------------------------------
            // Upon receiving render textures we need to create a framebuffer. To do that we need the Vulkan render pass,
            // which is stored together with the PSO for now(?), so the render textures are cached and the creation/binding
            // of the framebuffer is deferred until we get a pipeline bind cmd.
            case xg_cmd_graphics_render_textures_bind_m: {
                std_auto_m args =  ( xg_cmd_graphics_render_texture_bind_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "graphics render textures bind" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_streams_submit_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_submit_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "graphics draw" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_streams_submit_indexed_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_submit_indexed_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "graphics draw indexed" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_graphics_streams_submit_instanced_m: {
                std_auto_m args = ( xg_cmd_graphics_streams_submit_instanced_t* ) header->args;
                std_unused_m  ( args );

                // TODO
                std_log_info_m ( "graphics draw instanced" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_compute_dispatch_m: {
                std_auto_m args = ( xg_cmd_compute_dispatch_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "compute dispatch" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_raytrace_pipeline_state_bind_m: {
                std_auto_m args = ( xg_cmd_raytrace_pipeline_state_bind_t* ) header->args;
                const xg_vk_raytrace_pipeline_t* pipeline = xg_vk_raytrace_pipeline_get ( args->pipeline );

                // TODO
                std_log_info_m ( "raytrace pipeline bind: " std_fmt_str_m, pipeline->common.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_raytrace_trace_rays_m: {
                std_auto_m args = ( xg_cmd_raytrace_trace_rays_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "raytrace trace" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_buffer_m : {
                std_auto_m args = ( xg_cmd_copy_buffer_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "buffer copy" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_texture_m : {
                std_auto_m args = ( xg_cmd_copy_texture_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "texture copy" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_texture_to_buffer_m: {
                std_auto_m args = ( xg_cmd_copy_texture_to_buffer_t* ) header->args;
                std_unused_m ( args );

                // TODO
                std_log_info_m ( "texture to buffer copy" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_copy_buffer_to_texture_m: {
                std_auto_m args = ( xg_cmd_copy_buffer_to_texture_t* ) header->args;
                std_unused_m (args );

                // TODO
                std_log_info_m ( "buffer to texture copy" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_barrier_set_m: {
                std_auto_m args = ( xg_cmd_barrier_set_t* ) header->args;

                char* base = ( char* ) ( args + 1 );

#if xg_vk_enable_sync2_m
                if ( args->memory_barriers > 0 ) {
                    std_not_implemented_m();
                }

                if ( args->buffer_memory_barriers > 0 ) {
                    base = ( char* ) std_align_ptr ( base, std_alignof_m ( xg_buffer_memory_barrier_t ) );

                    for ( uint32_t i = 0; i < args->buffer_memory_barriers; ++i ) {
                        xg_buffer_memory_barrier_t* barrier = ( xg_buffer_memory_barrier_t* ) base;
                        const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( barrier->buffer );

                        // TODO
                        std_unused_m ( barrier );

                        std_log_info_m ( "buffer barrier: " std_fmt_str_m, buffer->params.debug_name );

                        base += sizeof ( xg_buffer_memory_barrier_t );
                    }
                }

                if ( args->texture_memory_barriers > 0 ) {
                    base = ( char* ) std_align_ptr ( base, std_alignof_m ( xg_texture_memory_barrier_t ) );

                    for ( uint32_t i = 0; i < args->texture_memory_barriers; ++i ) {
                        xg_texture_memory_barrier_t* barrier = ( xg_texture_memory_barrier_t* ) base;
                        const xg_vk_texture_t* texture = xg_vk_texture_get ( barrier->texture );

                        std_log_info_m ( "texture barrier: " std_fmt_str_m " " std_fmt_str_m " - > " std_fmt_str_m, 
                            texture->params.debug_name, xg_texture_layout_str ( barrier->layout.old ), xg_texture_layout_str ( barrier->layout.new ) );

                        base += sizeof ( xg_texture_memory_barrier_t );
                    }
                }
#else
              // TODO
#endif
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_texture_clear_m: {
                std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;
                const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );

                std_log_info_m ( "texture clear: " std_fmt_str_m, texture->params.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_texture_depth_stencil_clear_m: {
                std_auto_m args = ( xg_cmd_texture_clear_t* ) header->args;
                const xg_vk_texture_t* texture = xg_vk_texture_get ( args->texture );

                std_log_info_m ( "depth_stencil clear: " std_fmt_str_m, texture->params.debug_name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_start_debug_capture_m: {
                std_auto_m args = ( xg_cmd_start_debug_capture_t* ) header->args;

                std_unused_m ( args );
                std_log_info_m ( "debug capture start" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_stop_debug_capture_m: {
                std_log_info_m ( "debug capture stop" );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_begin_debug_region_m: {
                std_auto_m args = ( xg_cmd_begin_debug_region_t* ) header->args;

                std_log_info_m ( "debug region begin: " std_fmt_str_m, args->name );
            }
            break;

            // -----------------------------------------------------------------------
            case xg_cmd_end_debug_region_m: {
                std_log_info_m ( "debug region end" );
            }
            break;

            // -----------------------------------------------------------------------
        }
    }

    std_log_info_m ( "end cmd buffer" );
}

#if 0
static void xg_vk_workload_update_resource_groups ( xg_workload_h workload_handle ) {
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
                case xg_resource_cmd_resource_group_update_m: {
                    std_auto_m args = ( xg_resource_cmd_resource_group_update_t* ) header->args;

                    xg_pipeline_resource_group_h group = args->group;
                    xg_shader_binding_set_e set = args->set;

                    uint32_t buffer_count = args->buffer_count;
                    uint32_t texture_count = args->texture_count;
                    uint32_t sampler_count = args->sampler_count;

                    std_assert_m ( buffer_count <= xg_pipeline_resource_max_buffers_per_set_m );
                    std_assert_m ( texture_count <= xg_pipeline_resource_max_textures_per_set_m );
                    std_assert_m ( sampler_count <= xg_pipeline_resource_max_samplers_per_set_m );

                    xg_buffer_resource_binding_t* buffers = std_align_ptr_m ( args + 1, xg_buffer_resource_binding_t );
                    xg_texture_resource_binding_t* textures = std_align_ptr_m ( buffers + buffer_count, xg_texture_resource_binding_t );
                    xg_sampler_resource_binding_t* samplers = std_align_ptr_m ( textures + texture_count, xg_sampler_resource_binding_t );

                    xg_pipeline_resource_bindings_t bindings = xg_pipeline_resource_bindings_m (
                        .set = set,
                        .buffer_count = buffer_count,
                        .texture_count = texture_count,
                        .sampler_count = sampler_count,
                        //.buffers = buffers,
                        //.textures = textures,
                        //.samplers = samplers,
                    );
                    std_mem_copy ( bindings.buffers, buffers, sizeof ( xg_buffer_resource_binding_t ) * buffer_count );
                    std_mem_copy ( bindings.textures, textures, sizeof ( xg_texture_resource_binding_t ) * texture_count );
                    std_mem_copy ( bindings.samplers, samplers, sizeof ( xg_sampler_resource_binding_t ) * sampler_count );

                    xg_vk_pipeline_update_resource_group ( workload->device, group, &bindings );

                    break;
                }
            }
        }
    }
}
#endif

static void xg_vk_workload_create_resources ( xg_workload_h workload_handle ) {
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
                case xg_resource_cmd_texture_create_m: {
                    std_auto_m args = ( xg_resource_cmd_texture_create_t* ) header->args;

                    std_verify_m ( xg_texture_alloc ( args->texture ) );
                }
                break;

                case xg_resource_cmd_buffer_create_m: {
                    std_auto_m args = ( xg_resource_cmd_buffer_create_t* ) header->args;

                    std_verify_m ( xg_buffer_alloc ( args->buffer ) );
                }
                break;
            }
        }
    }
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

                case xg_resource_cmd_resource_group_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_resource_group_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    xg_vk_pipeline_destroy_resource_group ( workload->device, args->group );
                }
                break;

                case xg_resource_cmd_graphics_renderpass_destroy_m: {
                    std_auto_m args = ( xg_resource_cmd_graphics_renderpass_destroy_t* ) header->args;

                    if ( args->destroy_time != destroy_time ) {
                        continue;
                    }

                    xg_vk_graphics_renderpass_destroy ( args->renderpass );
                }
            }
        }
    }
}

static void xg_vk_workload_recycle_submission_contexts ( xg_vk_workload_device_context_t* device_context, uint64_t timeout ) {
    size_t count = std_ring_count ( &device_context->submit_contexts_ring );

#if xg_debug_enable_measure_workload_wait_time_m
    size_t initial_count = count;
#endif

    while ( count > 0 ) {
        xg_vk_workload_submit_context_t* submit_context = &device_context->submit_contexts_array[std_ring_bot_idx ( &device_context->submit_contexts_ring )];

        // If context is in use and isn't submitted yet just return, can't do anything
        if ( !submit_context->is_submitted ) {
            break;
        }

        //uint64_t timeout = 0;
        const xg_vk_workload_t* workload = xg_vk_workload_get ( submit_context->workload );
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

        const xg_vk_device_t* device = xg_vk_device_get ( device_context->device_handle );
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
        }

        if ( fence_status == VK_ERROR_OUT_OF_HOST_MEMORY || fence_status == VK_ERROR_OUT_OF_DEVICE_MEMORY || fence_status == VK_ERROR_DEVICE_LOST ) {
            std_log_error_m ( "Workload fence returned an error" );
        }

        // https://arm-software.github.io/vulkan_best_practice_for_mobile_developers/samples/performance/command_buffer_usage/command_buffer_usage_tutorial.html#:~:text=Resetting%20the%20command%20pool,-Resetting%20the%20pool&text=To%20reset%20the%20pool%20the,pool%20thus%20increasing%20memory%20overhead
        // "To reset the pool the flag RESET_COMMAND_BUFFER_BIT is not required, and it is actually better to avoid it since it prevents it from using a single large allocator for all buffers in the pool thus increasing memory overhead"
        vkResetCommandPool ( device->vk_handle, submit_context->cmd_allocator.vk_cmd_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT );
        --submit_context->cmd_allocator.cmd_buffers_count;
        //vkResetDescriptorPool ( device->vk_handle, submit_context->desc_allocator.vk_desc_pool, 0 );
        //xg_vk_desc_allocator_reset_counters ( &submit_context->desc_allocator );

        VkResult r = vkResetFences ( device->vk_handle, 1, &fence->vk_fence );
        xg_vk_assert_m ( r );
        submit_context->is_submitted = false;

        xg_vk_workload_destroy_resources ( submit_context->workload, xg_resource_cmd_buffer_time_workload_complete_m );
        xg_workload_destroy ( submit_context->workload );
        submit_context->workload = xg_null_handle_m;

        std_ring_pop ( &device_context->submit_contexts_ring, 1 );

        count = std_ring_count ( &device_context->submit_contexts_ring );
    }

#if xg_debug_enable_measure_workload_wait_time_m

    if ( initial_count > 0 && count == 0 ) {
        std_log_warn_m ( "All GPU worloads were found completed when trying to recycle some. Is the GPU being left idle waiting?" );
    }

#endif
}

xg_vk_workload_submit_context_t* xg_vk_workload_submit_context_create ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );

    uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( device_context->device_handle != xg_null_handle_m );

    xg_vk_workload_recycle_submission_contexts ( device_context, 0 );

    xg_vk_workload_submit_context_t* submit_context = &device_context->submit_contexts_array[std_ring_top_idx ( &device_context->submit_contexts_ring )];
    std_ring_push ( &device_context->submit_contexts_ring, 1 );

    submit_context->workload = workload_handle;
    return submit_context;
}

void xg_vk_workload_submit_context_submit ( xg_vk_workload_submit_context_t* submit_context ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( submit_context->workload );

    // submit
    {
        VkCommandBuffer vk_cmd_buffer = submit_context->cmd_allocator.vk_cmd_buffers[0];

        const xg_vk_gpu_queue_event_t* event = NULL;

        if ( workload->execution_complete_gpu_event != xg_null_handle_m ) {
            event = xg_vk_gpu_queue_event_get ( workload->execution_complete_gpu_event );
            xg_gpu_queue_event_log_signal ( workload->execution_complete_gpu_event );
        }

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pWaitSemaphores = NULL;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &vk_cmd_buffer;
        submit_info.signalSemaphoreCount = event ? 1 : 0;
        submit_info.pSignalSemaphores = event ? &event->vk_semaphore : NULL;

#if !xg_debug_enable_disable_semaphore_frame_sync_m

        if ( workload->swapchain_texture_acquired_event != xg_null_handle_m ) {
            xg_gpu_queue_event_log_wait ( workload->swapchain_texture_acquired_event );
            const xg_vk_gpu_queue_event_t* vk_event = xg_vk_gpu_queue_event_get ( workload->swapchain_texture_acquired_event );

            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &vk_event->vk_semaphore;
        }

#endif

        const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );

        const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
        VkResult result = vkQueueSubmit ( device->graphics_queue.vk_handle, 1, &submit_info, fence->vk_fence );
        std_verify_m ( result == VK_SUCCESS );

#if xg_debug_enable_flush_gpu_submissions_m || xg_debug_enable_disable_semaphore_frame_sync_m
        result = vkQueueWaitIdle ( device->graphics_queue.vk_handle );
#endif

        submit_context->is_submitted = true;
    }
}

void xg_workload_submit ( xg_workload_h workload_handle ) {
    xg_vk_workload_t* workload = xg_vk_workload_edit ( workload_handle );

    uint64_t device_idx = xg_vk_device_get_idx ( workload->device );
    xg_vk_workload_device_context_t* device_context = &xg_vk_workload_state->device_contexts[device_idx];
    std_assert_m ( device_context->device_handle != xg_null_handle_m );

    // allocate submission context
    // TODO offer some way to call this (and thus trigger end-of-workload resource destruction) without having to submit a new workload after this one is complete.
    xg_vk_workload_recycle_submission_contexts ( device_context, 0 );

    // test cmd buffers, if empty process resource cmd buffers and exit here
    {
        size_t total_header_size = 0;

        for ( size_t i = 0; i < workload->cmd_buffers_count; ++i ) {
            xg_cmd_buffer_t* cmd_buffer = xg_cmd_buffer_get ( workload->cmd_buffers[i] );
            total_header_size += std_queue_local_used_size ( &cmd_buffer->cmd_headers_allocator );
        }

        if ( total_header_size == 0 ) {
            xg_vk_workload_create_resources ( workload_handle );
            xg_vk_workload_allocate_resource_groups ( workload_handle );
            xg_vk_workload_update_resource_groups ( workload_handle );
            xg_vk_workload_destroy_resources ( workload_handle, xg_resource_cmd_buffer_time_workload_start_m );
            xg_vk_workload_destroy_resources ( workload_handle, xg_resource_cmd_buffer_time_workload_complete_m );
            xg_workload_destroy ( workload_handle );

            return;
        }
    }

    xg_vk_workload_submit_context_t* submit_context = &device_context->submit_contexts_array[std_ring_top_idx ( &device_context->submit_contexts_ring )];
    std_ring_push ( &device_context->submit_contexts_ring, 1 );

    submit_context->workload = workload_handle;
    submit_context->desc_allocator = xg_vk_desc_allocator_pop ( device_context->device_handle, workload_handle );
    std_assert_m ( submit_context->desc_allocator );

    // merge & sort
    // TODO split merge from sort
    xg_vk_workload_cmd_sort_result_t sort_result = xg_vk_workload_sort_cmd_buffers ( submit_context, workload_handle );

    // TODO chunk?
    //xg_vk_workload_cmd_chunk_result_t chunk_result = xg_vk_workload_chunk_cmd_headers ( sort_result.cmd_headers, sort_result.count );

    xg_vk_workload_create_resources ( workload_handle );
    xg_vk_workload_allocate_resource_groups ( workload_handle );
    xg_vk_workload_update_resource_groups ( workload_handle );
    xg_vk_workload_destroy_resources ( workload_handle, xg_resource_cmd_buffer_time_workload_start_m );

    // translate
    submit_context->cmd_headers = sort_result.cmd_headers;
    submit_context->cmd_headers_count = sort_result.count;
    //xg_vk_submit_context_debug_print(submit_context);
    xg_vk_cmd_translate_result_t translate_result;
    xg_vk_submit_context_translate ( &translate_result, submit_context, workload->device, workload_handle );

    // submit
    // TODO move this into xg_vk_device?
    {
        VkCommandBuffer vk_cmd_buffer = submit_context->cmd_allocator.vk_cmd_buffers[0];

        const xg_vk_gpu_queue_event_t* event = NULL;

        if ( workload->execution_complete_gpu_event != xg_null_handle_m ) {
            event = xg_vk_gpu_queue_event_get ( workload->execution_complete_gpu_event );
            xg_gpu_queue_event_log_signal ( workload->execution_complete_gpu_event );
        }

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info;
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = NULL;
        submit_info.waitSemaphoreCount = 0;
        submit_info.pWaitSemaphores = NULL;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &vk_cmd_buffer;
        submit_info.signalSemaphoreCount = event ? 1 : 0;
        submit_info.pSignalSemaphores = event ? &event->vk_semaphore : NULL;

#if !xg_debug_enable_disable_semaphore_frame_sync_m

        if ( workload->swapchain_texture_acquired_event != xg_null_handle_m ) {
            xg_gpu_queue_event_log_wait ( workload->swapchain_texture_acquired_event );
            const xg_vk_gpu_queue_event_t* vk_event = xg_vk_gpu_queue_event_get ( workload->swapchain_texture_acquired_event );

            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &vk_event->vk_semaphore;
        }

#endif

        const xg_vk_cpu_queue_event_t* fence = xg_vk_cpu_queue_event_get ( workload->execution_complete_cpu_event );

        const xg_vk_device_t* device = xg_vk_device_get ( workload->device );
        VkResult result = vkQueueSubmit ( device->graphics_queue.vk_handle, 1, &submit_info, fence->vk_fence );
        std_verify_m ( result == VK_SUCCESS );

#if xg_debug_enable_flush_gpu_submissions_m || xg_debug_enable_disable_semaphore_frame_sync_m
        result = vkQueueWaitIdle ( device->graphics_queue.vk_handle );
#endif

        submit_context->is_submitted = true;
    }

    // Close pending debug cpature
    if ( translate_result.debug_capture_stop_on_workload_submit ) {
        xg_debug_capture_stop();
    }

    // Queue capture stop on present if requested
    if ( translate_result.debug_capture_stop_on_workload_present ) {
        workload->stop_debug_capture_on_present = true;
    }
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
