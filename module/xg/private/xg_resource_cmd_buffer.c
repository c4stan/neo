#include "xg_resource_cmd_buffer.h"

#if xg_enable_backend_vulkan_m
    #include "vulkan/xg_vk_texture.h"
    #include "vulkan/xg_vk_buffer.h"
    #include "vulkan/xg_vk_pipeline.h"
#endif

#include <std_list.h>
#include <std_mutex.h>

static xg_resource_cmd_buffer_state_t* xg_resource_cmd_buffer_state;

static void xg_resource_cmd_buffer_alloc_memory ( xg_resource_cmd_buffer_t* cmd_buffer ) {
    cmd_buffer->cmd_headers_allocator = std_queue_local_create ( xg_cmd_buffer_resource_cmd_buffer_size_m );
    cmd_buffer->cmd_args_allocator = std_queue_local_create ( xg_cmd_buffer_resource_cmd_buffer_size_m );
}

static void xg_resource_cmd_buffer_free_memory ( xg_resource_cmd_buffer_t* cmd_buffer ) {
    std_queue_local_destroy ( &cmd_buffer->cmd_headers_allocator );
    std_queue_local_destroy ( &cmd_buffer->cmd_args_allocator );
}

void xg_resource_cmd_buffer_load ( xg_resource_cmd_buffer_state_t* state ) {
    // Copied from xg_cmd_buffer

    xg_resource_cmd_buffer_state = state;

    xg_resource_cmd_buffer_state->cmd_buffers_array = std_virtual_heap_alloc_array_m ( xg_resource_cmd_buffer_t, xg_cmd_buffer_max_resource_cmd_buffers_m );
    xg_resource_cmd_buffer_state->cmd_buffers_freelist = std_freelist_m ( xg_resource_cmd_buffer_state->cmd_buffers_array, xg_cmd_buffer_max_resource_cmd_buffers_m );
    std_mutex_init ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < xg_cmd_buffer_preallocated_resource_cmd_buffers_m; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = &xg_resource_cmd_buffer_state->cmd_buffers_array[i];
        xg_resource_cmd_buffer_alloc_memory ( cmd_buffer );
    }

    xg_resource_cmd_buffer_state->allocated_cmd_buffers_count = xg_cmd_buffer_preallocated_cmd_buffers_m;

    if ( xg_cmd_buffer_resource_cmd_buffer_size_m % std_virtual_page_size() != 0 ) {
        std_log_warn_m ( "Resource command buffer size is not a multiple of the system virtual page size, sub-optimal memory usage could result from this." );
    }
}

void xg_resource_cmd_buffer_reload ( xg_resource_cmd_buffer_state_t* state ) {
    xg_resource_cmd_buffer_state = state;
}

void xg_resource_cmd_buffer_unload ( void ) {
    for ( size_t i = 0; i < xg_resource_cmd_buffer_state->allocated_cmd_buffers_count; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = &xg_resource_cmd_buffer_state->cmd_buffers_array[i];
        xg_resource_cmd_buffer_free_memory ( cmd_buffer );
    }

    std_virtual_heap_free ( xg_resource_cmd_buffer_state->cmd_buffers_array );
    std_mutex_deinit ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );
}

void xg_resource_cmd_buffer_open_n ( xg_resource_cmd_buffer_h* cmd_buffer_handles, size_t count, xg_workload_h workload ) {
    // Copied from xg_cmd_buffer

    std_mutex_lock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = std_list_pop_m ( &xg_resource_cmd_buffer_state->cmd_buffers_freelist );

        if ( cmd_buffer == NULL ) {
            std_assert_m ( xg_resource_cmd_buffer_state->allocated_cmd_buffers_count + 1 <= xg_cmd_buffer_max_resource_cmd_buffers_m );
            std_log_info_m ( "Allocating additional resource command buffer " std_fmt_u64_m "/" std_fmt_int_m ", consider increasing xg_cmd_buffer_preallocated_resource_cmd_buffers_m.", xg_resource_cmd_buffer_state->allocated_cmd_buffers_count + 1, xg_cmd_buffer_max_resource_cmd_buffers_m );
            // Buffers are linearly added to the pool. Therefore if freeing is allowed it must also be linear and back-to-front.
            cmd_buffer = &xg_resource_cmd_buffer_state->cmd_buffers_array[xg_resource_cmd_buffer_state->allocated_cmd_buffers_count++];
            xg_resource_cmd_buffer_alloc_memory ( cmd_buffer );
        }

        cmd_buffer->workload = workload;

        xg_resource_cmd_buffer_h cmd_buffer_handle = ( xg_resource_cmd_buffer_h ) ( cmd_buffer - xg_resource_cmd_buffer_state->cmd_buffers_array );
        cmd_buffer_handles[i] = cmd_buffer_handle;
        //xg_workload_add_resource_cmd_buffer ( workload_handle, cmd_buffer_handle );
    }

    std_mutex_unlock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );
}

xg_resource_cmd_buffer_h xg_resource_cmd_buffer_open ( xg_workload_h workload ) {
    xg_resource_cmd_buffer_h handle;
    xg_resource_cmd_buffer_open_n ( &handle, 1, workload );
    return handle;
}

#if 0
void xg_resource_cmd_buffer_close ( xg_resource_cmd_buffer_h* cmd_buffer_handles, size_t count ) {
    // Copied from xg_cmd_buffer

    for ( size_t i = 0; i < count; ++i ) {
        xg_resource_cmd_buffer_h handle = cmd_buffer_handles[i];
        xg_resource_cmd_buffer_t* cmd_buffer = &xg_resource_cmd_buffer_state.cmd_buffers_array[handle];

        xg_workload_add_resource_cmd_buffer ( cmd_buffer->workload, handle );
    }
}
#endif

void xg_resource_cmd_buffer_discard ( xg_resource_cmd_buffer_h* cmd_buffer_handles, size_t count ) {
    // Copied from xg_cmd_buffer

    std_mutex_lock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        xg_resource_cmd_buffer_h handle = cmd_buffer_handles[i];
        xg_resource_cmd_buffer_t* cmd_buffer = &xg_resource_cmd_buffer_state->cmd_buffers_array[handle];

        std_queue_local_clear ( &cmd_buffer->cmd_headers_allocator );
        std_queue_local_clear ( &cmd_buffer->cmd_args_allocator );

        std_list_push ( &xg_resource_cmd_buffer_state->cmd_buffers_freelist, cmd_buffer );
    }

    std_mutex_unlock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );
}

xg_resource_cmd_buffer_t* xg_resource_cmd_buffer_get ( xg_resource_cmd_buffer_h cmd_buffer_handle ) {
    return &xg_resource_cmd_buffer_state->cmd_buffers_array[cmd_buffer_handle];
}

//

static void* xg_resource_cmd_buffer_record_cmd ( xg_resource_cmd_buffer_t* cmd_buffer, xg_resource_cmd_type_e type, size_t args_size ) {
    // Copied from xg_cmd_buffer

    std_queue_local_align_push ( &cmd_buffer->cmd_headers_allocator, xg_resource_cmd_buffer_cmd_alignment_m );
    std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, xg_resource_cmd_buffer_cmd_alignment_m );

    xg_resource_cmd_header_t* cmd_header = std_queue_local_emplace_array_m ( &cmd_buffer->cmd_headers_allocator, xg_resource_cmd_header_t, 1 );
    void* cmd_args = std_queue_local_emplace ( &cmd_buffer->cmd_args_allocator, args_size );

    cmd_header->type = ( uint16_t ) type;
    cmd_header->args = ( uint64_t ) cmd_args;

    return cmd_args;
}
#define xg_resource_cmd_buffer_record_cmd_m(cmd_buffer, cmd_type, args_type) (args_type*) xg_resource_cmd_buffer_record_cmd(cmd_buffer, cmd_type, sizeof(args_type))

xg_texture_h xg_resource_cmd_buffer_texture_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_texture_params_t* params ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_texture_create_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_texture_create_m, xg_resource_cmd_texture_create_t );

    xg_texture_h texture_handle = xg_texture_reserve ( params );
    std_assert_m ( texture_handle != xg_null_handle_m );

    cmd_args->texture = texture_handle;

    return texture_handle;
}

void xg_resource_cmd_buffer_texture_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_texture_h texture, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_texture_destroy_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_texture_destroy_m, xg_resource_cmd_texture_destroy_t );

    cmd_args->texture = texture;
    cmd_args->destroy_time = destroy_time;
}

xg_buffer_h xg_resource_cmd_buffer_buffer_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_buffer_params_t* params ) {
    xg_buffer_h buffer_handle;

    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_buffer_create_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_buffer_create_m, xg_resource_cmd_buffer_create_t );

    buffer_handle = xg_buffer_reserve ( params );
    std_assert_m ( buffer_handle != xg_null_handle_m );

    cmd_args->buffer = buffer_handle;

    return buffer_handle;
}

void xg_resource_cmd_buffer_buffer_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_buffer_h buffer, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_buffer_destroy_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_buffer_destroy_m, xg_resource_cmd_buffer_destroy_t );

    cmd_args->buffer = buffer;
    cmd_args->destroy_time = destroy_time;
}

xg_pipeline_resource_group_h xg_resource_cmd_buffer_resource_group_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_pipeline_resource_group_params_t* params ) {
    xg_pipeline_resource_group_h group_handle = xg_vk_pipeline_create_resource_group ( params->device, params->pipeline, params->bindings.set );
    std_assert_m ( group_handle != xg_null_handle_m );

    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );

    xg_resource_cmd_resource_group_update_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_resource_group_update_m, xg_resource_cmd_resource_group_update_t );

    const xg_pipeline_resource_bindings_t* bindings = &params->bindings;
    xg_resource_binding_set_e set = bindings->set;
    uint32_t buffer_count = bindings->buffer_count;
    uint32_t texture_count = bindings->texture_count;
    uint32_t sampler_count = bindings->sampler_count;

    cmd_args->group = group_handle;
    cmd_args->set = set;
    cmd_args->buffer_count = buffer_count;
    cmd_args->texture_count = texture_count;
    cmd_args->sampler_count = sampler_count;

    if ( buffer_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_buffer_resource_binding_t ) );
        xg_buffer_resource_binding_t* buffers = ( xg_buffer_resource_binding_t* ) std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_buffer_resource_binding_t, buffer_count );

        for ( uint32_t i = 0; i < buffer_count; ++i ) {
            buffers[i] = bindings->buffers[i];
        }
    }

    if ( texture_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_texture_resource_binding_t ) );
        xg_texture_resource_binding_t* textures = ( xg_texture_resource_binding_t* ) std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_texture_resource_binding_t, texture_count );

        for ( uint32_t i = 0; i < texture_count; ++i ) {
            textures[i] = bindings->textures[i];
        }
    }

    if ( sampler_count > 0 ) {
        std_queue_local_align_push ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_sampler_resource_binding_t ) );
        xg_sampler_resource_binding_t* samplers = ( xg_sampler_resource_binding_t* ) std_queue_local_emplace_array_m ( &cmd_buffer->cmd_args_allocator, xg_sampler_resource_binding_t, sampler_count );

        for ( uint32_t i = 0; i < sampler_count; ++i ) {
            samplers[i] = bindings->samplers[i];
        }
    }

    return group_handle;
}

void xg_resource_cmd_buffer_resource_group_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_pipeline_resource_group_h group, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_resource_group_destroy_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_resource_group_destroy_m, xg_resource_cmd_resource_group_destroy_t );

    cmd_args->group = group;
    cmd_args->destroy_time = destroy_time;
}

void xg_resource_cmd_buffer_graphics_renderpass_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_renderpass_h renderpass, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_graphics_renderpass_destroy_m, xg_resource_cmd_graphics_renderpass_destroy_t );

    cmd_args->renderpass = renderpass;
    cmd_args->destroy_time = destroy_time;
}

#if 0
void xg_resource_cmd_buffer_timestamp_query_buffer_clear ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_query_buffer_h query_buffer, xg_resource_cmd_buffer_time_e time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_timestamp_query_buffer_clear_m, xg_resource_cmd_timestamp_query_buffer_args_t );
    cmd_args->buffer = query_buffer;
    cmd_args->time = time;
}

xg_timestamp_query_buffer_results_t xg_resource_cmd_buffer_timestamp_query_buffer_readback ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_query_buffer_h query_buffer, xg_resource_cmd_buffer_time_e time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_timestamp_query_buffer_readback_m, xg_resource_cmd_timestamp_query_buffer_args_t );
    cmd_args->buffer = query_buffer;
    cmd_args->time = time;
    return xg_timestamp_query_buffer_get_results ( query_buffer );
}

void xg_resource_cmd_buffer_timestamp_query_buffer_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_query_buffer_h query_buffer, xg_resource_cmd_buffer_time_e time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_timestamp_query_buffer_destroy_m, xg_resource_cmd_timestamp_query_buffer_args_t );
    cmd_args->buffer = query_buffer;
    cmd_args->time = time;
}
#endif
