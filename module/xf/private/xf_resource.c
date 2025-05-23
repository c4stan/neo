#include "xf_resource.h"

#include <xg_enum.h>

#include <std_list.h>
#include <std_log.h>

#define xf_resource_handle_tag_as_multi_m( h ) std_bit_set_ms_64_m ( h, 1 )
#define xf_resource_handle_is_multi_m( h ) std_bit_test_64_m ( h, 63 )
#define xf_resource_handle_multi_idx_m( h ) ( h & 0xffff )
#define xf_resource_handle_sub_idx_m( h ) ( ( h >> 16 ) & 0xffff )

static xf_resource_state_t* xf_resource_state;

void xf_resource_load ( xf_resource_state_t* state ) {
    state->buffers_array = std_virtual_heap_alloc_array_m ( xf_buffer_t, xf_resource_max_buffers_m );
    state->buffers_freelist = std_freelist_m ( state->buffers_array, xf_resource_max_buffers_m );
    state->buffers_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xf_resource_max_buffers_m ) );
    std_mem_zero_array_m ( state->buffers_bitset, std_bitset_u64_count_m ( xf_resource_max_buffers_m ) );

    state->multi_buffers_array = std_virtual_heap_alloc_array_m ( xf_multi_buffer_t, xf_resource_max_multi_buffers_m );
    state->multi_buffers_freelist = std_freelist_m ( state->multi_buffers_array, xf_resource_max_multi_buffers_m );
    state->multi_buffers_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xf_resource_max_multi_buffers_m ) );
    std_mem_zero_array_m ( state->multi_buffers_bitset, std_bitset_u64_count_m ( xf_resource_max_multi_buffers_m ) );

    state->textures_array = std_virtual_heap_alloc_array_m ( xf_texture_t, xf_resource_max_textures_m );
    state->textures_freelist = std_freelist_m ( state->textures_array, xf_resource_max_textures_m );
    state->textures_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xf_resource_max_textures_m ) );
    std_mem_zero_array_m ( state->textures_bitset, std_bitset_u64_count_m ( xf_resource_max_textures_m ) );

    state->multi_textures_array = std_virtual_heap_alloc_array_m ( xf_multi_texture_t, xf_resource_max_multi_textures_m );
    state->multi_textures_freelist = std_freelist_m ( state->multi_textures_array, xf_resource_max_multi_textures_m );
    state->multi_textures_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xf_resource_max_multi_textures_m ) );
    std_mem_zero_array_m ( state->multi_textures_bitset, std_bitset_u64_count_m ( xf_resource_max_multi_textures_m ) );

    state->physical_textures_array = std_virtual_heap_alloc_array_m ( xf_physical_texture_t, xf_resource_max_physical_textures_m );
    state->physical_textures_freelist = std_freelist_m ( state->physical_textures_array, xf_resource_max_physical_textures_m );
    state->physical_textures_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xf_resource_max_physical_textures_m ) );
    std_mem_zero_array_m ( state->physical_textures_bitset, std_bitset_u64_count_m ( xf_resource_max_physical_textures_m ) );

    xf_resource_state = state;
}

void xf_resource_reload ( xf_resource_state_t* state ) {
    xf_resource_state = state;
}

void xf_resource_unload ( void ) {
    uint64_t idx;

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->physical_textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_physical_textures_m ) ) ) {
        xf_physical_texture_t* texture = &xf_resource_state->physical_textures_array[idx];
        std_log_info_m ( "Destroying physical texture " std_fmt_u64_m ": " std_fmt_str_m, idx, texture->info.debug_name );
        xf_resource_physical_texture_destroy ( idx );
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_textures_m ) ) ) {
        xf_texture_t* texture = &xf_resource_state->textures_array[idx];
        std_log_info_m ( "Destroying texture " std_fmt_u64_m ": " std_fmt_str_m, idx, texture->params.debug_name );
        xf_resource_texture_destroy ( idx );
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->buffers_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_buffers_m ) ) ) {
        xf_buffer_t* buffer = &xf_resource_state->buffers_array[idx];
        std_log_info_m ( "Destroying buffer " std_fmt_u64_m ": " std_fmt_str_m, idx, buffer->params.debug_name );
        xf_resource_buffer_destroy ( idx );
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->multi_textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_multi_textures_m ) ) ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[idx];
        std_log_info_m ( "Destroying multi texture " std_fmt_u64_m ": " std_fmt_str_m, idx, multi_texture->params.texture.debug_name );
        xf_resource_texture_destroy ( xf_resource_handle_tag_as_multi_m ( idx ) );
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->multi_buffers_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_multi_buffers_m ) ) ) {
        xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[idx];
        std_log_info_m ( "Destroying multi buffer " std_fmt_u64_m ": " std_fmt_str_m, idx, multi_buffer->params.buffer.debug_name );
        xf_resource_buffer_destroy ( xf_resource_handle_tag_as_multi_m ( idx ) );
        ++idx;
    }

    std_virtual_heap_free ( xf_resource_state->buffers_array );
    std_virtual_heap_free ( xf_resource_state->textures_array );
    std_virtual_heap_free ( xf_resource_state->multi_textures_array );
    std_virtual_heap_free ( xf_resource_state->multi_buffers_array );
    std_virtual_heap_free ( xf_resource_state->physical_textures_array );
    std_virtual_heap_free ( xf_resource_state->buffers_bitset );
    std_virtual_heap_free ( xf_resource_state->textures_bitset );
    std_virtual_heap_free ( xf_resource_state->multi_textures_bitset );
    std_virtual_heap_free ( xf_resource_state->multi_buffers_bitset );
    std_virtual_heap_free ( xf_resource_state->physical_textures_bitset );
}

// ======================================================================================= //
//                                      T E X T U R E
// ======================================================================================= //

xf_texture_h xf_resource_texture_create ( const xf_texture_params_t* params ) {
    xf_texture_t* texture = std_list_pop_m ( &xf_resource_state->textures_freelist );
    *texture = xf_texture_m (
        .params = *params
    );
    xf_texture_h handle = ( xf_texture_h ) ( texture - xf_resource_state->textures_array );
    std_bitset_set ( xf_resource_state->textures_bitset, handle );
    return handle;
}

void xf_resource_texture_destroy ( xf_texture_h texture_handle ) {
    if ( xf_resource_handle_is_multi_m ( texture_handle ) ) {
        uint32_t multi_idx = xf_resource_handle_multi_idx_m ( texture_handle );
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[multi_idx];
    
        for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
            xf_resource_texture_destroy ( multi_texture->textures[i] );
        }

        std_list_push ( &xf_resource_state->multi_textures_freelist, multi_texture );
        std_bitset_clear ( xf_resource_state->multi_textures_bitset, multi_idx );
    } else {
        xf_texture_t* texture = &xf_resource_state->textures_array[texture_handle];
        std_list_push ( &xf_resource_state->textures_freelist, texture );
        std_bitset_clear ( xf_resource_state->textures_bitset, texture_handle );
    }
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

    xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( texture_handle );
    if ( physical_texture ) {
        info->xg_handle = physical_texture->handle;
    } else {
        info->xg_handle = xg_null_handle_m;
    }
}

bool xf_resource_texture_is_multi ( xf_texture_h texture_handle ) {
    return xf_resource_handle_is_multi_m ( texture_handle );
}

xf_texture_t* xf_resource_texture_get ( xf_texture_h texture_handle ) {
    if ( xf_resource_handle_is_multi_m ( texture_handle ) ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
        uint64_t subtexture_index = xf_resource_handle_sub_idx_m ( texture_handle );
        subtexture_index = ( subtexture_index + multi_texture->index ) % multi_texture->params.multi_texture_count;
        texture_handle = multi_texture->textures[subtexture_index];
    }

    xf_texture_t* texture = &xf_resource_state->textures_array[texture_handle];
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

xf_physical_texture_t* xf_resource_texture_get_physical_texture ( xf_texture_h handle ) {
    xf_texture_t* texture = xf_resource_texture_get ( handle );
    if ( texture->physical_texture_handle != xf_null_handle_m ) {
        return xf_resource_physical_texture_get ( texture->physical_texture_handle );
    } else {
        return NULL;
    }
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
    //texture->allowed_usage = info->allowed_usage;
}

static void xf_resource_physical_texture_set_execution_state ( xf_physical_texture_h texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* new_state ) {
    xf_physical_texture_t* texture = xf_resource_physical_texture_get ( texture_handle );

    if ( texture->info.view_access == xg_texture_view_access_default_only_m ) {
        //if ( texture->state.shared.execution.queue == xg_cmd_queue_invalid_m ) {
        //    // First acquire
        //    std_assert_m ( !state->release );
        //} else if ( texture->state.shared.execution.queue != state->queue ) {
        //    // Release or Acquire
        //    std_assert_m ( texture->state.shared.execution.release != state->release );
        //    std_assert_m ( state->queue != xg_cmd_queue_invalid_m );
        //} else if ( texture->state.shared.execution.release ) {
        //    // Regular transition
        //    std_assert_m ( state->queue != xg_cmd_queue_invalid_m );
        //}
        xf_texture_state_t* state = &texture->state.shared;
        if ( state->execution.queue == xg_cmd_queue_invalid_m ) {
            state->prev_queue = new_state->queue;
            state->queue_transition = false;
        } else if ( state->queue_transition ) {
            std_assert_m ( new_state->queue == state->execution.queue );
            state->queue_transition = false;
            state->prev_queue = new_state->queue;
        } else if ( state->execution.queue != new_state->queue ) {
            state->queue_transition = true;
            state->prev_queue = state->execution.queue;
        }
        texture->state.shared.execution = *new_state;
    } else if ( texture->info.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count;

        if ( view.u64 == xg_texture_view_m().u64 ) {
            mip_count = texture->info.mip_levels - view.mip_base;
        } else {
            mip_count = view.mip_count;
        }

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->state.mips[view.mip_base + i].execution = *new_state;
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_resource_physical_texture_set_execution_layout ( xf_physical_texture_h texture_handle, xg_texture_view_t view, xg_texture_layout_e layout ) {
    xf_physical_texture_t* texture = xf_resource_physical_texture_get ( texture_handle );

    if ( texture->info.view_access == xg_texture_view_access_default_only_m ) {
        texture->state.shared.execution.layout = layout;
    } else if ( texture->info.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count;

        if ( view.u64 == xg_texture_view_m().u64 ) {
            mip_count = texture->info.mip_levels - view.mip_base;
        } else {
            mip_count = view.mip_count;
        }

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->state.mips[view.mip_base + i].execution.layout = layout;
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_resource_physical_texture_add_execution_stage ( xf_physical_texture_h texture_handle, xg_texture_view_t view, xg_pipeline_stage_bit_e stage ) {
    xf_physical_texture_t* texture = xf_resource_physical_texture_get ( texture_handle );

    if ( texture->info.view_access == xg_texture_view_access_default_only_m ) {
        texture->state.shared.execution.stage |= stage;
    } else if ( texture->info.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count;

        if ( view.u64 == xg_texture_view_m().u64 ) {
            mip_count = texture->info.mip_levels - view.mip_base;
        } else {
            mip_count = view.mip_count;
        }

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->state.mips[view.mip_base + i].execution.stage |= stage;
        }
    } else {
        std_not_implemented_m();
    }
}

void xf_resource_texture_update_external ( xf_texture_h texture_handle ) {
    if ( xf_resource_handle_is_multi_m ( texture_handle ) ) {
        uint32_t multi_idx = xf_resource_handle_multi_idx_m ( texture_handle );
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[multi_idx];
    
        for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
            xf_resource_texture_update_external ( multi_texture->textures[i] );
        }
    } else {
        //xf_texture_t* texture = &xf_resource_state->textures_array[texture_handle];
        //if ( texture->is_external ) {
            xg_i* xg = std_module_get_m ( xg_module_name_m );
            xg_texture_info_t texture_info;
            xg->get_texture_info ( &texture_info, texture_handle );
            xf_resource_texture_update_info ( texture_handle, &texture_info );
        //}
    }
}


xf_texture_h xf_resource_texture_create_from_external ( xg_texture_h xg_texture ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_texture_info_t info;
    std_verify_m ( xg->get_texture_info ( &info, xg_texture ) );

    xf_texture_params_t texture_params = xf_texture_params_m (
        .allow_aliasing = false,
    );
    std_str_copy_static_m ( texture_params.debug_name, info.debug_name );
    xf_texture_h texture_handle = xf_resource_texture_create ( &texture_params );

    xf_physical_texture_params_t physical_texture_params = xf_physical_texture_params_m (
        .is_external = true,
        .handle = xg_texture,
        .info = info,
    );
    xf_physical_texture_h physical_texture_handle = xf_resource_physical_texture_create ( &physical_texture_params );

    // TODO remove, keep info in physical texture only
    xf_resource_texture_update_info ( texture_handle, &info );
    xf_resource_texture_bind ( texture_handle, physical_texture_handle );

    return texture_handle;
}

// TODO try using VkEvents instead of barriers?
static void xf_resource_texture_barrier ( std_stack_t* stack, xf_physical_texture_h physical_texture_handle, xg_texture_view_t view, const xf_texture_state_t* prev_state, const xf_texture_execution_state_t* new_state ) {
    xf_physical_texture_t* physical_texture = xf_resource_physical_texture_get ( physical_texture_handle );

    /*
        if same layout as previous and both are reads, no barrier needed, just accumulate the stage in the resource state
        if different layout as previous and both are reads, do layout update only barrier, and accumulate stage
        if prev or current access is write, do barrier with layout change and proper access and stage dependency, and replace resource state current values (no access and proper shader stage)
    */
    if ( !xg_memory_access_is_write ( prev_state->execution.access ) && !xg_memory_access_is_write ( new_state->access ) && prev_state->execution.queue == new_state->queue && !prev_state->queue_transition ) {
        // read after a previous read
        // just accumulate the stage and do layout transition if necessary
        if ( prev_state->execution.layout != new_state->layout ) {
            std_auto_m barrier = std_stack_alloc_m ( stack, xg_texture_memory_barrier_t );
            *barrier = xg_texture_memory_barrier_m (
                .texture = physical_texture->handle,
                .mip_base = view.mip_base,
                .mip_count = view.mip_count,
                .array_base = view.array_base,
                .array_count = view.array_count,
                .layout.old = prev_state->execution.layout,
                .layout.new = new_state->layout,
                .memory.flushes = xg_memory_access_bit_none_m,
                .memory.invalidations = new_state->access,
                .execution.blocker = xg_pipeline_stage_bit_top_of_pipe_m,
                .execution.blocked = new_state->stage,
                .queue.old = prev_state->execution.queue,
                .queue.new = new_state->queue,
            );

            xf_resource_physical_texture_set_execution_layout ( physical_texture_handle, view, new_state->layout );
        }

        xf_resource_physical_texture_add_execution_stage ( physical_texture_handle, view, new_state->stage );
    } else {
        xg_cmd_queue_e prev_queue = prev_state->execution.queue;
        if ( prev_state->queue_transition ) {
            prev_queue = prev_state->prev_queue;
        }

        // wait on previous access and update the resource state
        std_auto_m barrier = std_stack_alloc_m ( stack, xg_texture_memory_barrier_t );
        *barrier = xg_texture_memory_barrier_m (
            .texture = physical_texture->handle,
            .mip_base = view.mip_base,
            .mip_count = view.mip_count,
            .array_base = view.array_base,
            .array_count = view.array_count,
            .layout.old = prev_state->execution.layout,
            .layout.new = new_state->layout,
            .memory.flushes = prev_state->execution.access,
            .memory.invalidations = new_state->access,
            .execution.blocker = prev_state->execution.stage,
            .execution.blocked = new_state->stage,
            .queue.old = prev_queue,
            .queue.new = new_state->queue,
        );

        xf_resource_physical_texture_set_execution_state ( physical_texture_handle, view, new_state );
    }
}

void xf_resource_texture_state_barrier ( std_stack_t* stack, xf_texture_h texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* new_state ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    xf_physical_texture_h physical_texture_handle = texture->physical_texture_handle;
    xf_resource_physical_texture_state_barrier ( stack, physical_texture_handle, view, new_state );
}

void xf_resource_physical_texture_state_barrier ( std_stack_t* stack, xf_physical_texture_h physical_texture_handle, xg_texture_view_t view, const xf_texture_execution_state_t* new_state ) {
    xf_physical_texture_t* physical_texture = xf_resource_physical_texture_get ( physical_texture_handle );

    if ( physical_texture->info.view_access == xg_texture_view_access_default_only_m ) {
        xf_texture_state_t* old_state = &physical_texture->state.shared;
        xf_resource_texture_barrier ( stack, physical_texture_handle, view, old_state, new_state );
    } else if ( physical_texture->info.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? physical_texture->info.mip_levels : view.mip_count;

        for ( uint32_t i = 0; i < mip_count; ++i ) {
            xf_texture_state_t* old_state = &physical_texture->state.mips[view.mip_base + i];
            xg_texture_view_t mip_view = view;
            mip_view.mip_base = view.mip_base + i;
            mip_view.mip_count = 1;
            xf_resource_texture_barrier ( stack, physical_texture_handle, mip_view, old_state, new_state );
        }
    } else {
        std_not_implemented_m();
    }
}

void xf_resource_texture_add_ref ( xf_texture_h handle ) {
    bool is_multi = xf_resource_handle_is_multi_m ( handle );
    
    if ( is_multi ) {
        xf_multi_texture_t* multi_texture = xf_resource_multi_texture_get ( handle );
        multi_texture->ref_count += 1;
    } else {
        xf_texture_t* texture = xf_resource_texture_get ( handle );
        texture->ref_count += 1;
        if ( texture->ref_count > 1 ) {
            //std_log_warn_m ( std_fmt_str_m ":" std_fmt_u32_m, texture->params.debug_name, texture->ref_count );
        }
    }
}

void xf_resource_texture_remove_ref ( xf_texture_h handle ) {
    bool is_multi = xf_resource_handle_is_multi_m ( handle );
    
    if ( is_multi ) {
        xf_multi_texture_t* multi_texture = xf_resource_multi_texture_get ( handle );
        uint32_t ref_count = multi_texture->ref_count;
        if ( ref_count > 1 ) {
            multi_texture->ref_count = ref_count - 1;
        } else {
            multi_texture->ref_count = 0;
            for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
                xf_resource_texture_unbind ( multi_texture->textures[i] );
            }
        }
    } else {
        xf_texture_t* texture = xf_resource_texture_get ( handle );
        uint32_t ref_count = texture->ref_count;
        if ( ref_count > 1 ) {
            std_assert_m ( !texture->is_multi );
            texture->ref_count = ref_count - 1;
        } else {
            texture->ref_count = 0;
            xf_resource_texture_unbind ( handle );
        }
    }
}

void xf_resource_texture_bind ( xf_texture_h texture_handle, xf_physical_texture_h physical_texture_handle ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    texture->physical_texture_handle = physical_texture_handle;
    xf_resource_physical_texture_add_ref ( physical_texture_handle );
}

void xf_resource_texture_unbind ( xf_texture_h texture_handle ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    xf_physical_texture_h physical_texture_handle = texture->physical_texture_handle;
    if ( physical_texture_handle != xf_null_handle_m ) {
        xf_resource_physical_texture_remove_ref ( physical_texture_handle );
        texture->physical_texture_handle = xf_null_handle_m;
    }
}

bool xf_resource_texture_is_depth ( xf_texture_h texture_handle ) {
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    return xg_format_has_depth ( texture->params.format );
}

// ======================================================================================= //
//                                       B U F F E R
// ======================================================================================= //

void xf_resource_buffer_destroy ( xf_buffer_h buffer_handle ) {
    if ( xf_resource_handle_is_multi_m ( buffer_handle ) ) {
        uint32_t multi_idx = xf_resource_handle_multi_idx_m ( buffer_handle );
        xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[multi_idx];
    
        for ( uint32_t i = 0; i < multi_buffer->params.multi_buffer_count; ++i ) {
            xf_resource_buffer_destroy ( multi_buffer->buffers[i] );
        }

        std_list_push ( &xf_resource_state->multi_buffers_freelist, multi_buffer );
        std_bitset_clear ( xf_resource_state->multi_buffers_bitset, multi_idx );
    } else {
        xf_buffer_t* buffer = &xf_resource_state->buffers_array[buffer_handle];
        std_list_push ( &xf_resource_state->buffers_freelist, buffer );
        std_bitset_clear ( xf_resource_state->buffers_bitset, buffer_handle );
    }
}

xf_buffer_h xf_resource_buffer_create ( const xf_buffer_params_t* params ) {
    xf_buffer_t* buffer = std_list_pop_m ( &xf_resource_state->buffers_freelist );
    *buffer = xf_buffer_m ( .params = *params );
    if ( params->clear_on_create ) {
        buffer->required_usage |= xg_texture_usage_bit_copy_dest_m;
    }
    xf_buffer_h handle = ( xf_buffer_h ) ( buffer - xf_resource_state->buffers_array );
    std_bitset_set ( xf_resource_state->buffers_bitset, handle );
    return handle;
}

void xf_resource_buffer_get_info ( xf_buffer_info_t* info, xf_buffer_h buffer_handle ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

    info->size = buffer->params.size;
    info->allow_aliasing = buffer->params.allow_aliasing;
    info->debug_name = buffer->params.debug_name;
}

bool xf_resource_buffer_is_multi ( xf_buffer_h buffer_handle ) {
    return xf_resource_handle_is_multi_m ( buffer_handle );
}

xf_buffer_t* xf_resource_buffer_get ( xf_buffer_h buffer_handle ) {
    xf_buffer_t* buffer;
    bool is_multi_buffer = xf_resource_handle_is_multi_m ( buffer_handle );

    if ( is_multi_buffer ) {
        xf_multi_buffer_t* multi_buffer = & xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( buffer_handle )];
        uint64_t subbuffer_index = xf_resource_handle_sub_idx_m ( buffer_handle );
        subbuffer_index = ( subbuffer_index + multi_buffer->index ) % multi_buffer->params.multi_buffer_count;
        buffer_handle = multi_buffer->buffers[subbuffer_index];
    }

    buffer = &xf_resource_state->buffers_array[buffer_handle];
    return buffer;
}

void xf_resource_buffer_map_to_new ( xf_buffer_h buffer_handle, xg_buffer_h xg_handle, xg_buffer_usage_bit_e allowed_usage ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    buffer->xg_handle = xg_handle;
    buffer->allowed_usage = allowed_usage;
    buffer->state = xf_buffer_execution_state_m(); // TODO
}

void xf_resource_texture_add_usage ( xf_texture_h texture_handle, xg_texture_usage_bit_e usage ) {
#if 1
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
#else
    xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
    texture->required_usage |= usage;
#endif
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

static void xf_resource_buffer_set_execution_state ( xf_buffer_h buffer_handle, const xf_buffer_execution_state_t* state ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    buffer->state = *state;
}

static void xf_resource_buffer_add_execution_stage ( xf_buffer_h buffer_handle, xg_pipeline_stage_bit_e stage ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    buffer->state.stage |= stage;
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
        *barrier = xg_buffer_memory_barrier_m (
            .buffer = buffer->xg_handle,
            .memory.flushes = prev_state->access,
            .memory.invalidations = new_state->access,
            .execution.blocker = prev_state->stage,
            .execution.blocked = new_state->stage,
            .size = buffer->params.size,
        );

        xf_resource_buffer_set_execution_state ( buffer_handle, new_state );
    }
}

void xf_resource_buffer_state_barrier ( std_stack_t* stack, xf_buffer_h buffer_handle, const xf_buffer_execution_state_t* new_state ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
    xf_buffer_execution_state_t* old_state = &buffer->state;
    xf_resource_buffer_barrier ( stack, buffer_handle, old_state, new_state );
}

xf_multi_buffer_t* xf_resource_multi_buffer_get ( xf_buffer_h buffer_handle ) {
    bool is_multi_buffer = xf_resource_handle_is_multi_m ( buffer_handle );

    if ( is_multi_buffer ) {
        xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( buffer_handle )];
        return multi_buffer;
    } else {
        return NULL;
    }
}

void xf_resource_buffer_add_ref ( xf_buffer_h handle ) {
    bool is_multi = xf_resource_handle_is_multi_m ( handle );
    
    if ( is_multi ) {
        xf_multi_buffer_t* multi_buffer = xf_resource_multi_buffer_get ( handle );
        multi_buffer->ref_count += 1;
    } else {
        xf_buffer_t* buffer = xf_resource_buffer_get ( handle );
        buffer->ref_count += 1;
    }
}

void xf_resource_buffer_remove_ref ( xf_buffer_h handle ) {
    bool is_multi = xf_resource_handle_is_multi_m ( handle );
    
    if ( is_multi ) {
        xf_multi_buffer_t* multi_buffer = xf_resource_multi_buffer_get ( handle );
        uint32_t ref_count = multi_buffer->ref_count;
        if ( ref_count >= 1 ) {
            multi_buffer->ref_count = ref_count - 1;
        //} else {
            //xf_resource_buffer_destroy ( handle );
        }
    } else {
        xf_buffer_t* buffer = xf_resource_buffer_get ( handle );
        uint32_t ref_count = buffer->ref_count;
        if ( ref_count >= 1 ) {
            std_assert_m ( !buffer->is_multi );
            buffer->ref_count = ref_count - 1;
        //} else {
        //    xf_resource_buffer_destroy ( handle );
        }
    }
}

// ======================================================================================= //
//                                M U L T I   T E X T U R E
// ======================================================================================= //

xf_texture_h xf_resource_multi_texture_create ( const xf_multi_texture_params_t* params ) {
    xf_multi_texture_t* multi_texture = std_list_pop_m ( &xf_resource_state->multi_textures_freelist );
    *multi_texture = xf_multi_texture_m ( .params = *params );

    for ( uint32_t i = 0; i < params->multi_texture_count; ++i ) {
        char id[8];
        std_u32_to_str ( id, 8, i, 0 );
        xf_texture_params_t texture_params = params->texture;
        std_stack_t stack = std_static_stack_m ( texture_params.debug_name );
        stack.top += std_str_len ( texture_params.debug_name );
        std_stack_string_append ( &stack, "(" );
        std_stack_string_append ( &stack, id );
        std_stack_string_append ( &stack, ")" );
        xf_texture_h handle = xf_resource_texture_create ( &texture_params );
        xf_texture_t* texture = xf_resource_texture_get ( handle );
        texture->is_multi = true;
        texture->ref_count = 1;
        multi_texture->textures[i] = handle;
    }

    xf_texture_h handle = ( xf_texture_h ) ( multi_texture - xf_resource_state->multi_textures_array );
    std_bitset_set ( xf_resource_state->multi_textures_bitset, handle );
    handle = xf_resource_handle_tag_as_multi_m ( handle );
    return handle;
}

xf_texture_h xf_resource_multi_texture_create_from_swapchain ( xg_swapchain_h swapchain ) {
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
    *multi_texture = xf_multi_texture_m (
        .params = params,
        .index = 0,
        .swapchain = swapchain
    );

    for ( uint64_t i = 0; i < info.texture_count; ++i ) {
        xg_texture_info_t texture_info;
        xg->get_texture_info ( &texture_info, info.textures[i] );

        xf_physical_texture_h physical_texture_handle = xf_resource_physical_texture_create ( &xf_physical_texture_params_m ( 
            .is_external = true, 
            .handle = info.textures[i],
            .info = texture_info
        ) );

        xf_texture_h texture_handle = xf_resource_texture_create ( &params.texture );
        xf_resource_texture_bind ( texture_handle, physical_texture_handle );
        xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
        texture->is_multi = true;
        texture->ref_count = 1;
        xf_resource_texture_update_info ( texture_handle, &texture_info );
        // TODO check base texture mip mode
        //texture->state.shared = xf_texture_execution_state_m (
        //    .layout = xg_texture_layout_undefined_m, // TODO take as param?
        //);

        multi_texture->textures[i] = texture_handle;
    }

    xf_texture_h handle = ( xf_texture_h ) ( multi_texture - xf_resource_state->multi_textures_array );
    std_bitset_set ( xf_resource_state->multi_textures_bitset, handle );
    handle = xf_resource_handle_tag_as_multi_m ( handle );

    return handle;
}

void xf_resource_multi_texture_advance ( xf_texture_h multi_texture_handle ) {
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

xf_texture_h xf_resource_multi_texture_get_texture ( xf_texture_h multi_texture_handle, int32_t offset ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];

    uint32_t texture_count = multi_texture->params.multi_texture_count;
    int32_t rem = ( ( int32_t ) multi_texture->index + offset ) % ( int32_t ) texture_count;
    uint32_t index = rem < 0 ? ( uint32_t ) ( rem + ( int32_t ) texture_count ) : ( uint32_t ) rem;
    std_assert_m ( index < texture_count );

    uint64_t handle = multi_texture_handle & 0xffff;
    handle |= index << 16;
    handle = xf_resource_handle_tag_as_multi_m ( handle );
    //return multi_texture->textures[index];
    return handle;
}

xf_texture_h xf_resource_multi_texture_get_default ( xf_texture_h multi_texture_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];
    xf_texture_h handle = ( xf_texture_h ) ( multi_texture - xf_resource_state->multi_textures_array );
    handle = xf_resource_handle_tag_as_multi_m ( handle );
    return handle;
}

static xf_multi_texture_t* xf_resource_multi_texture_get_multi ( xf_texture_h texture_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( texture_handle ) );
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( texture_handle )];
    return multi_texture;
}

void xf_resource_swapchain_resize ( xf_texture_h swapchain ) {
    xf_multi_texture_t* multi_texture = xf_resource_multi_texture_get_multi ( swapchain );

    for ( uint32_t i = 0; i < multi_texture->params.multi_texture_count; ++i ) {
        xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( multi_texture->textures[i] );
        physical_texture->state.shared.execution.layout = xg_texture_layout_undefined_m;
    }
}

// ======================================================================================= //
//                                 M U L T I   B U F F E R
// ======================================================================================= //

xf_buffer_h xf_resource_multi_buffer_create ( const xf_multi_buffer_params_t* params ) {
    xf_multi_buffer_t* multi_buffer = std_list_pop_m ( &xf_resource_state->multi_buffers_freelist );
    *multi_buffer = xf_multi_buffer_m ( .params = *params );

    for ( uint32_t i = 0; i < params->multi_buffer_count; ++i ) {
        char id[8];
        std_u32_to_str ( id, 8, i, 0 );
        xf_buffer_params_t buffer_params = params->buffer;
        std_stack_t stack = std_static_stack_m ( buffer_params.debug_name );
        stack.top += std_str_len ( buffer_params.debug_name );
        std_stack_string_append ( &stack, "(" );
        std_stack_string_append ( &stack, id );
        std_stack_string_append ( &stack, ")" );
        xf_buffer_h handle = xf_resource_buffer_create ( &buffer_params );
        xf_buffer_t* buffer = xf_resource_buffer_get ( handle );
        buffer->is_multi = true;
        buffer->ref_count = 1;
        multi_buffer->buffers[i] = handle;
    }

    xf_buffer_h handle = ( xf_buffer_h ) ( multi_buffer - xf_resource_state->multi_buffers_array );
    std_bitset_set ( xf_resource_state->multi_buffers_bitset, handle );
    handle = xf_resource_handle_tag_as_multi_m ( handle );
    return handle;
}

void xf_resource_multi_buffer_advance ( xf_buffer_h multi_buffer_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_buffer_handle ) );
    xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( multi_buffer_handle )];
    multi_buffer->index = ( multi_buffer->index + 1 ) % multi_buffer->params.multi_buffer_count;
}

xf_buffer_h xf_resource_multi_buffer_get_buffer ( xf_buffer_h multi_buffer_handle, int32_t offset ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_buffer_handle ) );
    xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( multi_buffer_handle )];

    uint32_t buffer_count = multi_buffer->params.multi_buffer_count;
    int32_t rem = ( ( int32_t ) multi_buffer->index + offset ) % ( int32_t ) buffer_count;
    uint32_t index = rem < 0 ? ( uint32_t ) ( rem + ( int32_t ) buffer_count ) : ( uint32_t ) rem;
    std_assert_m ( index < buffer_count );

    uint64_t handle = multi_buffer_handle & 0xffff;
    handle |= index << 16;
    handle = xf_resource_handle_tag_as_multi_m ( handle );
    return handle;
}

xf_buffer_h xf_resource_multi_buffer_get_default ( xf_buffer_h multi_buffer_handle ) {
    std_assert_m ( xf_resource_handle_is_multi_m ( multi_buffer_handle ) );
    xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[xf_resource_handle_multi_idx_m ( multi_buffer_handle )];
    xf_buffer_h handle = ( xf_buffer_h ) ( multi_buffer - xf_resource_state->multi_buffers_array );
    handle = xf_resource_handle_tag_as_multi_m ( handle );
    return handle;
}

// ======================================================================================= //
//                               D E V I C E   T E X T U R E
// ======================================================================================= //

xf_physical_texture_h xf_resource_physical_texture_create ( const xf_physical_texture_params_t* params ) {
    xf_physical_texture_t* texture = std_list_pop_m ( &xf_resource_state->physical_textures_freelist );
    *texture = xf_physical_texture_m (
        .is_external = params->is_external,
        .handle = params->handle,
        .info = params->info,
    );
    xf_physical_texture_h handle = texture - xf_resource_state->physical_textures_array;
    std_bitset_set ( xf_resource_state->physical_textures_bitset, handle );
    return handle;
}

void xf_resource_physical_texture_destroy ( xf_physical_texture_h handle ) {
    xf_physical_texture_t* texture = &xf_resource_state->physical_textures_array[handle];
    std_list_push ( &xf_resource_state->physical_textures_freelist, texture );
    std_bitset_clear ( xf_resource_state->physical_textures_bitset, handle );
}

void xf_resource_physical_texture_add_ref ( xf_physical_texture_h handle ) {
    xf_physical_texture_t* texture = &xf_resource_state->physical_textures_array[handle];
    texture->ref_count += 1;
}

void xf_resource_physical_texture_remove_ref ( xf_physical_texture_h handle ) {
    xf_physical_texture_t* texture = &xf_resource_state->physical_textures_array[handle];
    uint32_t ref_count = texture->ref_count;
    if ( ref_count > 1 ) {
        texture->ref_count = ref_count - 1;
    } else {
        texture->ref_count = 0;
        //xf_resource_physical_texture_destroy ( handle );
    }
}

xf_physical_texture_t* xf_resource_physical_texture_get ( xf_physical_texture_h handle ) {
    xf_physical_texture_t* texture = &xf_resource_state->physical_textures_array[handle];
    return texture;
}

void xf_resource_physical_texture_map_to_new ( xf_physical_texture_h physical_texture_handle, xg_texture_h xg_handle, const xg_texture_info_t* info ) {
    xf_physical_texture_t* texture = xf_resource_physical_texture_get ( physical_texture_handle );
    texture->handle = xg_handle;
    texture->info = *info;
    texture->state.shared = xf_texture_state_m(); // TODO take as param, don't assume shared
}

// ======================================================================================= //

void xf_resource_destroy_unreferenced ( xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer, xg_resource_cmd_buffer_time_e time ) {
    uint64_t idx;

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->physical_textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_physical_textures_m ) ) ) {
        xf_physical_texture_t* texture = &xf_resource_state->physical_textures_array[idx];
        if ( texture->ref_count == 0 ) {
            if ( texture->handle != xg_null_handle_m && !texture->is_external ) {
                xg->cmd_destroy_texture ( resource_cmd_buffer, texture->handle, time );
            }
            xf_resource_physical_texture_destroy ( idx );
        }
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_textures_m ) ) ) {
        xf_texture_t* texture = &xf_resource_state->textures_array[idx];
        if ( texture->ref_count == 0 ) {
            xf_resource_texture_destroy ( idx );
        }
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->buffers_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_buffers_m ) ) ) {
        xf_buffer_t* buffer = &xf_resource_state->buffers_array[idx];
        if ( buffer->ref_count == 0 ) {
            if ( buffer->xg_handle != xg_null_handle_m ) {
                xg->cmd_destroy_buffer ( resource_cmd_buffer, buffer->xg_handle, time );
            }
            xf_resource_buffer_destroy ( idx );
        }
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->multi_textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_multi_textures_m ) ) ) {
        xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[idx];
        if ( multi_texture->ref_count == 0 ) {
            xf_resource_texture_destroy ( xf_resource_handle_tag_as_multi_m ( idx ) );
        }
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->multi_buffers_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_multi_buffers_m ) ) ) {
        xf_multi_buffer_t* multi_buffer = &xf_resource_state->multi_buffers_array[idx];
        if ( multi_buffer->ref_count == 0 ) {
            for ( uint32_t i = 0; i < multi_buffer->params.multi_buffer_count; ++i ) {
                xf_buffer_t* buffer = &xf_resource_state->buffers_array[multi_buffer->buffers[i]];
                if ( buffer->xg_handle != xg_null_handle_m ) {
                    xg->cmd_destroy_buffer ( resource_cmd_buffer, buffer->xg_handle, time );
                }
            }
            xf_resource_buffer_destroy ( xf_resource_handle_tag_as_multi_m ( idx ) );
        }
        ++idx;
    }
}

xf_texture_h xf_resource_multi_texture_get_base ( xf_texture_h multi_texture_handle ) {
    xf_multi_texture_t* multi_texture = &xf_resource_state->multi_textures_array[xf_resource_handle_multi_idx_m ( multi_texture_handle )];
    uint64_t subtexture_index = xf_resource_handle_sub_idx_m ( multi_texture_handle );
    subtexture_index = ( subtexture_index + multi_texture->index ) % multi_texture->params.multi_texture_count;
    xf_texture_h texture_handle = multi_texture->textures[subtexture_index];
    return texture_handle;
}

uint32_t xf_resource_texture_list ( xf_texture_h* textures, uint32_t capacity ) {
    uint64_t idx = 0;
    uint32_t i = 0;
    while ( std_bitset_scan ( &idx, xf_resource_state->textures_bitset, idx, std_bitset_u64_count_m ( xf_resource_max_textures_m ) ) ) {
        textures[i++] = idx;
        ++idx;
    }

    return idx;
}

xf_texture_execution_state_t xf_resource_texture_get_state ( xf_texture_h texture_handle, xg_texture_view_t view ) {
    xf_physical_texture_t* texture = xf_resource_texture_get_physical_texture ( texture_handle );
    std_assert_m ( texture );
    if ( texture->info.view_access == xg_texture_view_access_default_only_m ) { 
        return texture->state.shared.execution;
    } else if ( texture->info.view_access == xg_texture_view_access_separate_mips_m ) {
        std_assert_m ( view.mip_count == 1 );
        return texture->state.mips[view.mip_base].execution;
    } else {
        std_not_implemented_m();
        return xf_texture_execution_state_m();
    }
}
