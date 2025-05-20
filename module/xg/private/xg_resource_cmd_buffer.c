#include "xg_resource_cmd_buffer.h"

#if xg_enable_backend_vulkan_m
    #include "vulkan/xg_vk_texture.h"
    #include "vulkan/xg_vk_buffer.h"
    #include "vulkan/xg_vk_pipeline.h"
    #include "vulkan/xg_vk_workload.h"
#endif

#include <xg_enum.h>

#include <std_list.h>
#include <std_mutex.h>

static xg_resource_cmd_buffer_state_t* xg_resource_cmd_buffer_state;

static void xg_resource_cmd_buffer_alloc_memory ( xg_resource_cmd_buffer_t* cmd_buffer ) {
    cmd_buffer->cmd_headers_allocator = std_virtual_stack_create ( xg_cmd_buffer_resource_cmd_buffer_size_m );
    cmd_buffer->cmd_args_allocator = std_virtual_stack_create ( xg_cmd_buffer_resource_cmd_buffer_size_m );
}

static void xg_resource_cmd_buffer_free_memory ( xg_resource_cmd_buffer_t* cmd_buffer ) {
    std_virtual_stack_destroy ( &cmd_buffer->cmd_headers_allocator );
    std_virtual_stack_destroy ( &cmd_buffer->cmd_args_allocator );
    cmd_buffer->cmd_headers_allocator = ( std_virtual_stack_t ) {};
    cmd_buffer->cmd_args_allocator = ( std_virtual_stack_t ) {};
}

void xg_resource_cmd_buffer_load ( xg_resource_cmd_buffer_state_t* state ) {
    xg_resource_cmd_buffer_state = state;

    state->cmd_buffers_array = std_virtual_heap_alloc_array_m ( xg_resource_cmd_buffer_t, xg_cmd_buffer_max_resource_cmd_buffers_m );
    std_mem_zero_array_m ( state->cmd_buffers_array, xg_cmd_buffer_max_resource_cmd_buffers_m );
    state->cmd_buffers_freelist = std_freelist_m ( state->cmd_buffers_array, xg_cmd_buffer_max_resource_cmd_buffers_m );

    for ( size_t i = 0; i < xg_cmd_buffer_preallocated_resource_cmd_buffers_m; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = &state->cmd_buffers_array[i];
        xg_resource_cmd_buffer_alloc_memory ( cmd_buffer );
    }

    if ( xg_cmd_buffer_resource_cmd_buffer_size_m % std_virtual_page_size() != 0 ) {
        std_log_warn_m ( "Resource command buffer size is not a multiple of the system virtual page size, sub-optimal memory usage could result from this." );
    }

    std_mutex_init ( &state->cmd_buffers_mutex );
}

void xg_resource_cmd_buffer_reload ( xg_resource_cmd_buffer_state_t* state ) {
    xg_resource_cmd_buffer_state = state;
}

void xg_resource_cmd_buffer_unload ( void ) {
    for ( size_t i = 0; i < xg_cmd_buffer_max_resource_cmd_buffers_m; ++i ) {
        xg_resource_cmd_buffer_t* cmd_buffer = &xg_resource_cmd_buffer_state->cmd_buffers_array[i];
        xg_resource_cmd_buffer_free_memory ( cmd_buffer );
    }

    std_virtual_heap_free ( xg_resource_cmd_buffer_state->cmd_buffers_array );
    std_mutex_deinit ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );
}

xg_resource_cmd_buffer_h xg_resource_cmd_buffer_create ( xg_workload_h workload ) {
    std_mutex_lock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );

    xg_resource_cmd_buffer_t* cmd_buffer = std_list_pop_m ( &xg_resource_cmd_buffer_state->cmd_buffers_freelist );

    if ( cmd_buffer->cmd_headers_allocator.begin == NULL ) {
        std_log_info_m ( "Allocating additional resource command buffer, consider increasing xg_cmd_buffer_preallocated_resource_cmd_buffers_m" );
        // Buffers are linearly added to the pool. Therefore if freeing is allowed it must also be linear and back-to-front.
        xg_resource_cmd_buffer_alloc_memory ( cmd_buffer );
    }

    cmd_buffer->workload = workload;
    xg_resource_cmd_buffer_h handle = ( xg_resource_cmd_buffer_h ) ( cmd_buffer - xg_resource_cmd_buffer_state->cmd_buffers_array );

    std_mutex_unlock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );

    return handle;
}

void xg_resource_cmd_buffer_destroy ( xg_resource_cmd_buffer_h* cmd_buffer_handles, size_t count ) {
    std_mutex_lock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );

    for ( size_t i = 0; i < count; ++i ) {
        xg_resource_cmd_buffer_h handle = cmd_buffer_handles[i];
        xg_resource_cmd_buffer_t* cmd_buffer = &xg_resource_cmd_buffer_state->cmd_buffers_array[handle];

        std_virtual_stack_clear ( &cmd_buffer->cmd_headers_allocator );
        std_virtual_stack_clear ( &cmd_buffer->cmd_args_allocator );

        std_list_push ( &xg_resource_cmd_buffer_state->cmd_buffers_freelist, cmd_buffer );
    }

    std_mutex_unlock ( &xg_resource_cmd_buffer_state->cmd_buffers_mutex );
}

xg_resource_cmd_buffer_t* xg_resource_cmd_buffer_get ( xg_resource_cmd_buffer_h cmd_buffer_handle ) {
    return &xg_resource_cmd_buffer_state->cmd_buffers_array[cmd_buffer_handle];
}

//

static void* xg_resource_cmd_buffer_record_cmd ( xg_resource_cmd_buffer_t* cmd_buffer, xg_resource_cmd_type_e type, size_t args_size ) {
    std_virtual_stack_align ( &cmd_buffer->cmd_headers_allocator, xg_resource_cmd_buffer_cmd_alignment_m );
    std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, xg_resource_cmd_buffer_cmd_alignment_m );

    xg_resource_cmd_header_t* cmd_header = std_virtual_stack_alloc_m ( &cmd_buffer->cmd_headers_allocator, xg_resource_cmd_header_t );
    void* cmd_args = std_virtual_stack_alloc ( &cmd_buffer->cmd_args_allocator, args_size );

    cmd_header->type = ( uint16_t ) type;
    cmd_header->args = ( uint64_t ) cmd_args;

    return cmd_args;
}
#define xg_resource_cmd_buffer_record_cmd_m(cmd_buffer, cmd_type, args_type) (args_type*) xg_resource_cmd_buffer_record_cmd(cmd_buffer, cmd_type, sizeof(args_type))

xg_texture_h xg_resource_cmd_buffer_texture_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_texture_params_t* params, xg_texture_init_t* init ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_texture_create_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_texture_create_m, xg_resource_cmd_texture_create_t );

    xg_texture_h texture_handle = xg_texture_reserve ( params );
    std_assert_m ( texture_handle != xg_null_handle_m );

    cmd_args->texture = texture_handle;

    if ( init ) {
        cmd_args->init = true;
        cmd_args->init_mode = init->mode;
        // TODO just memcpy?
        if ( init->mode == xg_texture_init_mode_upload_m ) {
            // TODO proper sizing accounting for array size etc
            //      just store the offset in the cmd?
            size_t size = params->width * params->height * xg_format_size ( params->format );
            cmd_args->staging = xg_workload_write_staging ( cmd_buffer->workload, init->upload_data, size );
        } else if ( init->mode == xg_texture_init_mode_clear_m ) {
            cmd_args->clear = init->clear;
        } else if ( init->mode == xg_texture_init_mode_clear_depth_stencil_m ) {
            cmd_args->depth_stencil_clear = init->depth_stencil_clear;
        }
        cmd_args->init_layout = init->final_layout;
    } else {
        cmd_args->init = false;
    }

    return texture_handle;
}

void xg_resource_cmd_buffer_texture_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_texture_h texture, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_texture_destroy_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_texture_destroy_m, xg_resource_cmd_texture_destroy_t );

    cmd_args->texture = texture;
    cmd_args->destroy_time = destroy_time;
}

xg_buffer_h xg_resource_cmd_buffer_buffer_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_buffer_params_t* params, xg_buffer_init_t* init ) {
    xg_buffer_h buffer_handle;

    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_buffer_create_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_buffer_create_m, xg_resource_cmd_buffer_create_t );

    buffer_handle = xg_buffer_reserve ( params );
    std_assert_m ( buffer_handle != xg_null_handle_m );

    cmd_args->buffer = buffer_handle;

    if ( init ) {
        cmd_args->init = true;
        cmd_args->init_mode = init->mode;
        if ( init->mode == xg_buffer_init_mode_upload_m ) {
            size_t size = params->size;
            cmd_args->staging = xg_workload_write_staging ( cmd_buffer->workload, init->upload_data, size );
        } else if ( init->mode == xg_buffer_init_mode_clear_m ) {
            cmd_args->clear = init->clear;
        }
    } else {
        cmd_args->init = false;
    }

    return buffer_handle;
}

void xg_resource_cmd_buffer_buffer_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_buffer_h buffer, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_buffer_destroy_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_buffer_destroy_m, xg_resource_cmd_buffer_destroy_t );

    cmd_args->buffer = buffer;
    cmd_args->destroy_time = destroy_time;
}

void xg_resource_cmd_buffer_resource_bindings_update ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_resource_bindings_h group_handle, const xg_pipeline_resource_bindings_t* bindings ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_resource_bindings_update_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_resource_bindings_update_m, xg_resource_cmd_resource_bindings_update_t );

    uint32_t buffer_count = bindings->buffer_count;
    uint32_t texture_count = bindings->texture_count;
    uint32_t sampler_count = bindings->sampler_count;
    uint32_t raytrace_world_count = bindings->raytrace_world_count;

    cmd_args->group = group_handle;
    cmd_args->buffer_count = buffer_count;
    cmd_args->texture_count = texture_count;
    cmd_args->sampler_count = sampler_count;
    cmd_args->raytrace_world_count = raytrace_world_count;

    if ( buffer_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_buffer_resource_binding_t ) );
        xg_buffer_resource_binding_t* buffers = ( xg_buffer_resource_binding_t* ) std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_buffer_resource_binding_t, buffer_count );

        for ( uint32_t i = 0; i < buffer_count; ++i ) {
            buffers[i] = bindings->buffers[i];
        }
    }

    if ( texture_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_texture_resource_binding_t ) );
        xg_texture_resource_binding_t* textures = ( xg_texture_resource_binding_t* ) std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_texture_resource_binding_t, texture_count );

        for ( uint32_t i = 0; i < texture_count; ++i ) {
            textures[i] = bindings->textures[i];
        }
    }

    if ( sampler_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_sampler_resource_binding_t ) );
        xg_sampler_resource_binding_t* samplers = ( xg_sampler_resource_binding_t* ) std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_sampler_resource_binding_t, sampler_count );

        for ( uint32_t i = 0; i < sampler_count; ++i ) {
            samplers[i] = bindings->samplers[i];
        }
    }

    if ( raytrace_world_count > 0 ) {
        std_virtual_stack_align ( &cmd_buffer->cmd_args_allocator, std_alignof_m ( xg_raytrace_world_resource_binding_t ) );
        xg_raytrace_world_resource_binding_t* worlds = ( xg_raytrace_world_resource_binding_t* ) std_virtual_stack_alloc_array_m ( &cmd_buffer->cmd_args_allocator, xg_raytrace_world_resource_binding_t, raytrace_world_count );

        for ( uint32_t i = 0; i < raytrace_world_count; ++i ) {
            worlds[i] = bindings->raytrace_worlds[i];
        }
    }
}

xg_resource_bindings_h xg_resource_cmd_buffer_workload_resource_bindings_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_resource_bindings_params_t* params ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_bindings_h group_handle = xg_workload_create_resource_group ( cmd_buffer->workload, params->layout );
    std_assert_m ( group_handle != xg_null_handle_m );
    xg_resource_cmd_buffer_resource_bindings_update ( cmd_buffer_handle, group_handle, &params->bindings );
    return group_handle;
}

xg_resource_bindings_h xg_resource_cmd_buffer_resource_bindings_create ( xg_resource_cmd_buffer_h cmd_buffer_handle, const xg_resource_bindings_params_t* params ) {
    xg_resource_bindings_h group_handle = xg_vk_pipeline_create_resource_bindings ( params->layout );
    std_assert_m ( group_handle != xg_null_handle_m );
    xg_resource_cmd_buffer_resource_bindings_update ( cmd_buffer_handle, group_handle, &params->bindings );
    return group_handle;
}

void xg_resource_cmd_buffer_resource_bindings_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_resource_bindings_h group, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    xg_resource_cmd_resource_bindings_destroy_t* cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_resource_bindings_destroy_m, xg_resource_cmd_resource_bindings_destroy_t );

    cmd_args->group = group;
    cmd_args->destroy_time = destroy_time;
}

void xg_resource_cmd_buffer_graphics_renderpass_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_renderpass_h renderpass, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_renderpass_destroy_m, xg_resource_cmd_renderpass_destroy_t );

    cmd_args->renderpass = renderpass;
    cmd_args->destroy_time = destroy_time;
}

void xg_resource_cmd_buffer_queue_event_destroy ( xg_resource_cmd_buffer_h cmd_buffer_handle, xg_queue_event_h event, xg_resource_cmd_buffer_time_e destroy_time ) {
    xg_resource_cmd_buffer_t* cmd_buffer = xg_resource_cmd_buffer_get ( cmd_buffer_handle );
    std_auto_m cmd_args = xg_resource_cmd_buffer_record_cmd_m ( cmd_buffer, xg_resource_cmd_queue_event_destroy_m, xg_resource_cmd_queue_event_destroy_t );

    cmd_args->event = event;
    cmd_args->destroy_time = destroy_time;
}
