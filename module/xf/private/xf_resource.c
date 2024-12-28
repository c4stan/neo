#include "xf_resource.h"

#include <xg_enum.h>

#include <std_list.h>
#include <std_log.h>

#define xf_resource_handle_is_multi_m( h ) std_bit_test_64_m ( h, 63 )
#define xf_resource_handle_multi_idx_m( h ) ( h & 0xffff )
#define xf_resource_handle_sub_idx_m( h ) ( ( h >> 16 ) & 0xffff )

static xf_resource_state_t* xf_resource_state;

void xf_resource_load ( xf_resource_state_t* state ) {
    state->buffers_array = std_virtual_heap_alloc_array_m ( xf_buffer_t, xf_resource_max_buffers_m );
    state->buffers_freelist = std_freelist_m ( state->buffers_array, xf_resource_max_buffers_m );

    state->multi_buffers_array = std_virtual_heap_alloc_array_m ( xf_multi_buffer_t, xf_resource_max_multi_buffers_m );
    state->multi_buffers_freelist = std_freelist_m ( state->multi_buffers_array, xf_resource_max_multi_buffers_m );

    state->textures_array = std_virtual_heap_alloc_array_m ( xf_texture_t, xf_resource_max_textures_m );
    state->textures_freelist = std_freelist_m ( state->textures_array, xf_resource_max_textures_m );

    state->multi_textures_array = std_virtual_heap_alloc_array_m ( xf_multi_texture_t, xf_resource_max_multi_textures_m );
    state->multi_textures_freelist = std_freelist_m ( state->multi_textures_array, xf_resource_max_multi_textures_m );

    xf_resource_state = state;
}

void xf_resource_reload ( xf_resource_state_t* state ) {
    xf_resource_state = state;
}

void xf_resource_unload ( void ) {
    std_virtual_heap_free ( xf_resource_state->buffers_array );
    std_virtual_heap_free ( xf_resource_state->textures_array );
    std_virtual_heap_free ( xf_resource_state->multi_textures_array );
    std_virtual_heap_free ( xf_resource_state->multi_buffers_array );
}

xf_texture_h xf_resource_texture_declare ( const xf_texture_params_t* params ) {
    xf_texture_t* texture = std_list_pop_m ( &xf_resource_state->textures_freelist );
    *texture = xf_texture_m (
        .params = *params
    );
    xf_texture_h handle = ( xf_texture_h ) ( texture - xf_resource_state->textures_array );
    return handle;
}

void xf_resource_texture_destroy ( xf_texture_h texture_handle ) {
    if ( xf_resource_handle_is_multi_m ( texture_handle ) ) {
        uint32_t multi_idx = xf_resource_handle_multi_idx_m ( texture_handle );
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[multi_idx];
    
        for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
            xf_resource_texture_destroy ( multi_texture->textures[i] );
        }

        std_list_push ( &xf_resource_state->multi_buffers_freelist, multi_texture );
    } else {
        xf_texture_t* texture = &xf_resource_state->textures_array[texture_handle];
        std_list_push ( &xf_resource_state->textures_freelist, texture);
    }
}

void xf_resource_buffer_destroy ( xf_buffer_h buffer_handle ) {
    if ( xf_resource_handle_is_multi_m ( buffer_handle ) ) {
        uint32_t multi_idx = xf_resource_handle_multi_idx_m ( buffer_handle );
        xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[multi_idx];
    
        for ( uint32_t i = 0; i < multi_buffer->params.multi_buffer_count; ++i ) {
            xf_resource_buffer_destroy ( multi_buffer->buffers[i] );
        }

        std_list_push ( &xf_resource_state->multi_buffers_freelist, multi_buffer );
    } else {
        xf_buffer_t* buffer = &xf_resource_state->buffers_array[buffer_handle];
        std_list_push ( &xf_resource_state->buffers_freelist, buffer );
    }
}

xf_buffer_h xf_resource_buffer_declare ( const xf_buffer_params_t* params ) {
    xf_buffer_t* buffer = std_list_pop_m ( &xf_resource_state->buffers_freelist );
    std_mem_zero_m ( buffer );
    buffer->xg_handle = xg_null_handle_m;
    buffer->params = *params;
    buffer->alias = xf_null_handle_m;
    xf_buffer_h handle = ( xf_buffer_h ) ( buffer - xf_resource_state->buffers_array );
    return handle;
}

void xf_resource_texture_get_info ( xf_texture_info_t* info, xf_texture_h texture_handle ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    info->width = texture->params.width;
    info->height = texture->params.height;
    info->depth = texture->params.depth;
    info->mip_levels = texture->params.mip_levels;
    info->array_layers = texture->params.array_layers;
    info->dimension = texture->params.dimension;
    info->format = texture->params.format;
    info->sample_count = texture->params.samples_per_pixel;
    info->allow_aliasing = texture->params.allow_aliasing;
    info->debug_name = texture->params.debug_name;
    info->view_access = texture->params.view_access;
}

void xf_resource_buffet_get_info ( xf_buffer_info_t* info, xf_buffer_h buffer_handle ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

    info->size = buffer->params.size;
    info->allow_aliasing = buffer->params.allow_aliasing;
    info->debug_name = buffer->params.debug_name;
}

bool xf_resource_texture_is_multi ( xf_texture_h texture_handle ) {
    return xf_resource_handle_is_multi_m ( texture_handle );
}

bool xf_resource_buffer_is_multi ( xf_buffer_h buffer_handle ) {
    return xf_resource_handle_is_multi_m ( buffer_handle );
}

xf_texture_t* xf_resource_texture_get_no_alias ( xf_texture_h texture_handle ) {
    xf_texture_t* texture;
    bool is_multi_texture = xf_resource_handle_is_multi_m ( texture_handle );

    if ( is_multi_texture ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
        uint64_t subtexture_index = xf_resource_handle_sub_idx_m ( texture_handle );
        subtexture_index = ( subtexture_index + multi_texture->index ) % multi_texture->params.multi_texture_count;
        texture_handle = multi_texture->textures[subtexture_index];
    }

    texture = &xf_resource_state->textures_array[texture_handle];

    return texture;
}

xf_texture_t* xf_resource_texture_get ( xf_texture_h texture_handle ) {
    xf_texture_t* texture;
    bool is_multi_texture = xf_resource_handle_is_multi_m ( texture_handle );

    if ( is_multi_texture ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
        uint64_t subtexture_index = xf_resource_handle_sub_idx_m ( texture_handle );
        subtexture_index = ( subtexture_index + multi_texture->index ) % multi_texture->params.multi_texture_count;
        texture_handle = multi_texture->textures[subtexture_index];

        if ( multi_texture->alias != xf_null_handle_m ) {
            //texture_handle = multi_texture->alias;
            return xf_resource_texture_get ( multi_texture->alias );
        }
    }

    texture = &xf_resource_state->textures_array[texture_handle];

    if ( !is_multi_texture && texture->alias != xf_null_handle_m ) {
        //texture = &xf_resource_state.textures_array[texture->alias];
        return xf_resource_texture_get ( texture->alias );
    }

    return texture;
}

xf_multi_texture_t* xf_resource_multi_texture_get ( xf_texture_h texture_handle ) {
    bool is_multi_texture = xf_resource_handle_is_multi_m ( texture_handle );

    if ( is_multi_texture ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
        return multi_texture;
    } else {
        return NULL;
    }
}

xf_buffer_t* xf_resource_buffer_get ( xf_buffer_h buffer_handle ) {
    xf_buffer_t* buffer;
    bool is_multi_buffer = xf_resource_handle_is_multi_m ( buffer_handle );

    if ( is_multi_buffer ) {
        xf_multi_buffer_t* multi_buffer = & xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( buffer_handle )];
        uint64_t subbuffer_index = xf_resource_handle_sub_idx_m ( buffer_handle );
        subbuffer_index = ( subbuffer_index + multi_buffer->index ) % multi_buffer->params.multi_buffer_count;
        buffer_handle = multi_buffer->buffers[subbuffer_index];

        if ( multi_buffer->alias != xf_null_handle_m ) {
            return xf_resource_buffer_get ( multi_buffer->alias );
        }
    }

    buffer = &xf_resource_state->buffers_array[buffer_handle];

    if ( !is_multi_buffer && buffer->alias != xf_null_handle_m ) {
        return xf_resource_buffer_get ( buffer->alias );
    }

    return buffer;
}

void xf_resource_texture_update_info ( xf_texture_h texture_handle, const xg_texture_info_t* info ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    texture->params.width = info->width;
    texture->params.height = info->height;
    texture->params.depth = info->depth;
    texture->params.mip_levels = info->mip_levels;
    texture->params.array_layers = info->array_layers;
    texture->params.dimension = info->dimension;
    texture->params.format = info->format;
    texture->params.samples_per_pixel = info->samples_per_pixel;
    std_str_copy_static_m ( texture->params.debug_name, info->debug_name );
    texture->allowed_usage = info->allowed_usage;
}

void xf_resource_texture_map_to_new ( xf_texture_h texture_handle, xg_texture_h xg_handle, xg_texture_usage_bit_e allowed_usage ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    texture->xg_handle = xg_handle;
    texture->allowed_usage = allowed_usage;
    texture->state.shared = xf_texture_execution_state_m(); // TODO take as param, don't assume shared
}

void xf_resource_buffer_map_to_new ( xf_buffer_h buffer_handle, xg_buffer_h xg_handle, xg_buffer_usage_bit_e allowed_usage ) {
    xf_buffer_t* buffer = &xf_resource_state->buffers_array[buffer_handle];
    buffer->xg_handle = xg_handle;
    buffer->allowed_usage = allowed_usage;
    buffer->state = xf_buffer_execution_state_m(); // TODO
}

void xf_resource_texture_unmap ( xf_texture_h texture_handle ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    texture->xg_handle = xg_null_handle_m;
}

void xf_resource_buffer_unmap ( xf_buffer_h buffer_handle ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    buffer->xg_handle = xg_null_handle_m;
}

void xf_resource_texture_add_usage ( xf_texture_h texture_handle, xg_texture_usage_bit_e usage ) {
    // TODO should this take aliasing into account?
    if ( xf_resource_handle_is_multi_m ( texture_handle ) ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];

        for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
            xf_texture_t* texture = &xf_resource_state->textures_array[multi_texture->textures[i]];
            texture->required_usage |= usage;
        }
    } else {
        xf_texture_t* texture = &xf_resource_state->textures_array[texture_handle];
        texture->required_usage |= usage;
    }
}

void xf_resource_buffer_add_usage ( xf_buffer_h buffer_handle, xg_buffer_usage_bit_e usage ) {
    if ( xf_resource_handle_is_multi_m ( buffer_handle ) ) {
        xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( buffer_handle )];

        for ( uint32_t i = 0; i < multi_buffer->params.multi_buffer_count; ++i ) {
            xf_buffer_t* buffer = &xf_resource_state->buffers_array[multi_buffer->buffers[i]];
            buffer->required_usage |= usage;
        }
    } else {
        xf_buffer_t* buffer = &xf_resource_state->buffers_array[buffer_handle];
        buffer->required_usage |= usage;
    }
}

static void xf_resource_texture_set_execution_state ( xf_texture_h texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* state ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    if ( texture->params.view_access == xg_texture_view_access_default_only_m ) {
        texture->state.shared = *state;
    } else if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count;

        if ( view.u64 == xg_texture_view_m().u64 ) {
            mip_count = texture->params.mip_levels - view.mip_base;
        } else {
            mip_count = view.mip_count;
        }

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->state.mips[view.mip_base + i] = *state;
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_resource_texture_set_execution_layout ( xf_texture_h texture_handle, xg_texture_view_t view, xg_texture_layout_e layout ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    if ( texture->params.view_access == xg_texture_view_access_default_only_m ) {
        texture->state.shared.layout = layout;
    } else if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count;

        if ( view.u64 == xg_texture_view_m().u64 ) {
            mip_count = texture->params.mip_levels - view.mip_base;
        } else {
            mip_count = view.mip_count;
        }

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->state.mips[view.mip_base + i].layout = layout;
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_resource_texture_add_execution_stage ( xf_texture_h texture_handle, xg_texture_view_t view, xg_pipeline_stage_bit_e stage ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    if ( texture->params.view_access == xg_texture_view_access_default_only_m ) {
        texture->state.shared.stage |= stage;
    } else if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count;

        if ( view.u64 == xg_texture_view_m().u64 ) {
            mip_count = texture->params.mip_levels - view.mip_base;
        } else {
            mip_count = view.mip_count;
        }

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->state.mips[view.mip_base + i].stage |= stage;
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_resource_buffer_set_execution_state ( xf_buffer_h buffer_handle, const xf_buffer_execution_state_t* state ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    buffer->state = *state;
}

static void xf_resource_buffer_add_execution_stage ( xf_buffer_h buffer_handle, xg_pipeline_stage_bit_e stage ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    buffer->state.stage |= stage;
}

xf_texture_h xf_resource_multi_texture_declare ( const xf_multi_texture_params_t* params ) {
    xf_multi_texture_t* multi_texture = std_list_pop_m ( &xf_resource_state->multi_textures_freelist );
    multi_texture->params = *params;
    multi_texture->index = 0;
    multi_texture->alias = xf_null_handle_m;
    multi_texture->swapchain = xg_null_handle_m;

    for ( uint32_t i = 0; i < params->multi_texture_count; ++i ) {
        char id[8];
        std_u32_to_str ( id, 8, i, 0 );
        xf_texture_params_t texture_params = params->texture;
        std_stack_t stack = std_static_stack_m ( texture_params.debug_name );
        stack.top += std_str_len ( texture_params.debug_name );
        std_stack_string_append ( &stack, "(" );
        std_stack_string_append ( &stack, id );
        std_stack_string_append ( &stack, ")" );
        xf_texture_h handle = xf_resource_texture_declare ( &texture_params );
        // TODO tag handle here
        multi_texture->textures[i] = handle;
    }

    xf_texture_h handle = ( xf_texture_h ) ( multi_texture - xf_resource_state->multi_textures_array );
    handle = std_bit_set_ms_64_m ( handle, 1 );
    return handle;
}

xf_texture_h xf_resource_multi_texture_declare_from_swapchain ( xg_swapchain_h swapchain ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_swapchain_info_t info;
    std_verify_m ( xg->get_swapchain_info ( &info, swapchain ) );

    xf_multi_texture_params_t params = xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = info.width,
            .height = info.height,
            .format = info.format,
            .allow_aliasing = false,
        ),
        .multi_texture_count = ( uint32_t ) info.texture_count,
    );
    std_str_copy_static_m ( params.texture.debug_name, info.debug_name );

    xf_multi_texture_t* multi_texture = std_list_pop_m ( &xf_resource_state->multi_textures_freelist );
    multi_texture->params = params;
    multi_texture->index = 0;
    multi_texture->alias = xf_null_handle_m;
    multi_texture->swapchain = swapchain;

    for ( uint64_t i = 0; i < info.texture_count; ++i ) {
        xg_texture_info_t texture_info;
        xg->get_texture_info ( &texture_info, info.textures[i] );

        xf_texture_h texture_handle = xf_resource_texture_declare ( &params.texture );
        xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

        texture->xg_handle = info.textures[i];
        texture->is_external = true;
        xf_resource_texture_update_info ( texture_handle, &texture_info );
        // TODO check base texture mip mode
        texture->state.shared = xf_texture_execution_state_m (
            .layout = xg_texture_layout_undefined_m, // TODO take as param?
        );

        // TODO tag handle here
        multi_texture->textures[i] = texture_handle;
    }

    xf_texture_h handle = ( xf_texture_h ) ( multi_texture - xf_resource_state->multi_textures_array );
    handle = std_bit_set_ms_64_m ( handle, 1 );

    return handle;
}

void xf_resource_multi_texture_advance ( xf_texture_h multi_texture_handle ) {
    // TODO should this take aliasing into account?
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];
    multi_texture->index = ( multi_texture->index + 1 ) % multi_texture->params.multi_texture_count;
}

void xf_resource_multi_texture_set_index ( xf_texture_h multi_texture_handle, uint32_t index ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];
    multi_texture->index = index % multi_texture->params.multi_texture_count;
}

xg_swapchain_h xf_resource_multi_texture_get_swapchain ( xf_texture_h multi_texture_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];
    return multi_texture->swapchain;
}

xf_texture_h xf_resource_texture_declare_from_external ( xg_texture_h xg_texture ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_texture_info_t info;
    std_verify_m ( xg->get_texture_info ( &info, xg_texture ) );

    xf_texture_params_t params = xf_texture_params_m (
        .width = info.width,
        .height = info.height,
        .format = info.format,
        .allow_aliasing = false,
    );
    std_str_copy_static_m ( params.debug_name, info.debug_name );

    xf_texture_t* texture = std_list_pop_m ( &xf_resource_state->textures_freelist );
    xf_texture_h handle = ( xf_texture_h ) ( texture - xf_resource_state->textures_array );

    *texture = xf_texture_m (
        .xg_handle = xg_texture,
        .is_external = true,
    );
    xf_resource_texture_update_info ( handle, &info );

    return handle;
}

xf_texture_h xf_resource_multi_texture_get_texture ( xf_texture_h multi_texture_handle, int32_t offset ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];

    int32_t rem = ( ( int32_t ) multi_texture->index + offset ) % ( int32_t ) multi_texture->params.multi_texture_count;
    uint32_t index = rem < 0 ? ( uint32_t ) ( rem + ( int32_t ) multi_texture->params.multi_texture_count ) : ( uint32_t ) rem;

    uint64_t handle = multi_texture_handle & 0xffff;
    handle |= index << 16;
    handle = std_bit_set_ms_64_m ( handle, 1 );
    //return multi_texture->textures[index];
    return handle;
}

xf_texture_h xf_resource_multi_texture_get_default ( xf_texture_h multi_texture_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];
    xf_texture_h handle = ( xf_texture_h ) ( multi_texture - xf_resource_state->multi_textures_array );
    handle = std_bit_set_ms_64_m ( handle, 1 );
    return handle;
}

// TODO try using VkEvents instead of barriers
static void xf_resource_texture_barrier ( std_stack_t* stack, xf_texture_h texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* prev_state, const xf_texture_execution_state_t* new_state ) {
    const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    /*
        if same layout as previous and both are reads, no barrier needed, just accumulate the stage in the resource state
        if different layout as previous and both are reads, do layout update only barrier, and accumulate stage
        if prev or current access is write, do barrier with layout change and proper access and stage dependency, and replace resource state current values (no access and proper shader stage)
    */
    if ( !xg_memory_access_is_write ( prev_state->access ) && !xg_memory_access_is_write ( new_state->access ) ) {
        // read after a previous read
        // just accumulate the stage and do layout transition if necessary
        if ( prev_state->layout != new_state->layout ) {
            std_auto_m barrier = std_stack_alloc_m ( stack, xg_texture_memory_barrier_t );
            barrier->texture = texture->xg_handle;
            barrier->mip_base = view.mip_base;
            barrier->mip_count = view.mip_count;
            barrier->array_base = view.array_base;
            barrier->array_count = view.array_count;
            barrier->layout.old = prev_state->layout;
            barrier->layout.new = new_state->layout;
            barrier->memory.flushes = xg_memory_access_bit_none_m;
            barrier->memory.invalidations = new_state->access;
            barrier->execution.blocker = xg_pipeline_stage_bit_top_of_pipe_m;
            barrier->execution.blocked = new_state->stage;

            xf_resource_texture_set_execution_layout ( texture_handle, view, new_state->layout );
        }

        xf_resource_texture_add_execution_stage ( texture_handle, view, new_state->stage );
    } else {
        // wait on previous access and update the resource state
        std_auto_m barrier = std_stack_alloc_m ( stack, xg_texture_memory_barrier_t );
        barrier->texture = texture->xg_handle;
        barrier->mip_base = view.mip_base;
        barrier->mip_count = view.mip_count;
        barrier->array_base = view.array_base;
        barrier->array_count = view.array_count;
        barrier->layout.old = prev_state->layout;
        barrier->layout.new = new_state->layout;
        barrier->memory.flushes = prev_state->access;
        barrier->memory.invalidations = new_state->access;
        barrier->execution.blocker = prev_state->stage;
        barrier->execution.blocked = new_state->stage;

        xf_resource_texture_set_execution_state ( texture_handle, view, new_state );
    }
}

void xf_resource_texture_state_barrier ( std_stack_t* stack, xf_texture_h texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* new_state ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    if ( texture->params.view_access == xg_texture_view_access_default_only_m ) {
        xf_texture_execution_state_t* old_state = &texture->state.shared;
        xf_resource_texture_barrier ( stack, texture_handle, view, old_state, new_state );
    } else if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? texture->params.mip_levels : view.mip_count;

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            xf_texture_execution_state_t* old_state = &texture->state.mips[view.mip_base + i];
            xg_texture_view_t mip_view = view;
            mip_view.mip_base = view.mip_base + i;
            mip_view.mip_count = 1;
            xf_resource_texture_barrier ( stack, texture_handle, mip_view, old_state, new_state );
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_resource_buffer_barrier ( std_stack_t* stack, xf_buffer_h buffer_handle, const xf_buffer_execution_state_t* prev_state, const xf_buffer_execution_state_t* new_state ) {
    const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

    /*
        if same layout as previous and both are reads, no barrier needed, just accumulate the stage in the resource state
        if different layout as previous and both are reads, do layout update only barrier, and accumulate stage
        if prev or current access is write, do barrier with layout change and proper access and stage dependency, and replace resource state current values (no access and proper shader stage)
    */
    if ( !xg_memory_access_is_write ( prev_state->access ) && !xg_memory_access_is_write ( new_state->access ) ) {
        // read after a previous read
        // just accumulate the stage
        xf_resource_buffer_add_execution_stage ( buffer_handle, new_state->stage );
    } else {
        // wait on previous access and update the resource state
        std_auto_m barrier = std_stack_alloc_m ( stack, xg_buffer_memory_barrier_t );
        barrier->buffer = buffer->xg_handle;
        barrier->memory.flushes = prev_state->access;
        barrier->memory.invalidations = new_state->access;
        barrier->execution.blocker = prev_state->stage;
        barrier->execution.blocked = new_state->stage;

        xf_resource_buffer_set_execution_state ( buffer_handle, new_state );
    }
}

void xf_resource_buffer_state_barrier ( std_stack_t* stack, xf_buffer_h buffer_handle, const xf_buffer_execution_state_t* new_state ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    xf_buffer_execution_state_t* old_state = &buffer->state;
    xf_resource_buffer_barrier ( stack, buffer_handle, old_state, new_state );
}

void xf_resource_texture_alias ( xf_texture_h texture_handle, xf_texture_h alias ) {
    if ( xf_resource_handle_is_multi_m ( texture_handle ) ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
        multi_texture->alias = alias;
    } else {
        xf_texture_t* texture = &xf_resource_state->textures_array[texture_handle];
        texture->alias = alias;
    }
}

static xf_multi_texture_t* xf_resource_multi_texture_get_multi ( xf_texture_h texture_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
    return multi_texture;
}

void xf_resource_swapchain_resize ( xf_texture_h swapchain ) {
    xf_multi_texture_t* multi_texture = xf_resource_multi_texture_get_multi ( swapchain );

    for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
        xf_texture_t* texture = xf_resource_texture_get ( multi_texture->textures[i] );
        texture->state.shared.layout = xg_texture_layout_undefined_m;
    }
}
