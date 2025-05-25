#include <xf.h>

#include "xf_graph.h"
#include "xf_resource.h"

#include <xg_enum.h>

#include <std_list.h>
#include <std_hash.h>
#include <std_log.h>
#include <std_allocator.h>
#include <std_sort.h>

static xf_graph_state_t* xf_graph_state;

#define xf_graph_bitset_u64_count_m std_div_ceil_m ( xf_graph_max_graphs_m, 8 )

void xf_graph_load ( xf_graph_state_t* state ) {
    state->graphs_array = std_virtual_heap_alloc_array_m ( xf_graph_t, xf_graph_max_graphs_m );
    state->graphs_freelist = std_freelist_m ( state->graphs_array, xf_graph_max_graphs_m );
    state->graphs_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xf_graph_bitset_u64_count_m );
    std_mem_zero ( state->graphs_bitset, sizeof ( uint64_t ) * xf_graph_bitset_u64_count_m );

    xf_graph_state = state;
}

void xf_graph_reload ( xf_graph_state_t* state ) {
    xf_graph_state = state;
}

void xf_graph_unload ( void ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xf_graph_state->graphs_bitset, idx, xf_graph_bitset_u64_count_m ) ) {
        xf_graph_t* graph = &xf_graph_state->graphs_array[idx];
        xg_workload_h workload = xg->create_workload ( graph->params.device );
        xf_graph_destroy ( idx, workload );
        xg->submit_workload ( workload );
        ++idx;
    }

    std_virtual_heap_free ( xf_graph_state->graphs_array );
    std_virtual_heap_free ( xf_graph_state->graphs_bitset );
}

void xf_graph_load_shaders ( xs_i* xs, xs_database_h sdb ) {
    xf_graph_state->export_pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xf_export" ) );
}

xf_graph_h xf_graph_create ( const xf_graph_params_t* params ) {
    xf_graph_t* graph = std_list_pop_m ( &xf_graph_state->graphs_freelist );
    std_mem_zero_m ( graph );
    graph->params = *params;
    graph->heap.memory_handle = xg_null_memory_handle_m;
    graph->export_source_node = xf_null_handle_m;
    graph->export_node = xf_null_handle_m;
    graph->nodes_freelist = std_static_freelist_m ( graph->nodes_array );
    graph->query_contexts_ring = std_ring ( std_static_array_capacity_m ( graph->query_contexts_array ) );
    graph->resource_dependencies_allocator = std_virtual_stack_create ( sizeof ( xf_graph_subresource_dependencies_t ) * ( xf_graph_max_textures_m * xf_resource_max_mip_levels_m + xf_graph_max_buffers_m ) );
    graph->physical_resource_dependencies_allocator = std_virtual_stack_create ( sizeof ( xf_graph_subresource_dependencies_t ) * ( xf_graph_max_textures_m * 2 * xf_resource_max_mip_levels_m ) );
    graph->node_user_arg_allocator = std_virtual_stack_create ( 128 * xf_graph_max_nodes_m );

    xf_graph_h handle = ( xf_graph_h ) ( graph - xf_graph_state->graphs_array );
    std_bitset_set ( xf_graph_state->graphs_bitset, handle );
    return handle;
}

xf_node_h xf_graph_node_create ( xf_graph_h graph_handle, const xf_node_params_t* params ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    std_assert_m ( graph->nodes_count < xf_graph_max_nodes_m );
    std_assert_m ( !graph->is_finalized );
    
    //xf_node_t* node = &graph->nodes_array[graph->nodes_count++];
    xf_node_t* node = std_list_pop_m ( &graph->nodes_freelist );
    ++graph->nodes_count;
    std_mem_zero_m ( node );
    node->params = *params;
    node->enabled = true;
    node->renderpass = xg_null_handle_m;
    node->renderpass_params.render_textures = xg_render_textures_layout_m();

    bool copy_args = false;
    std_buffer_t user_args = std_buffer_m();

    if ( node->params.type == xf_node_type_custom_pass_m ) {
        xf_node_custom_pass_params_t* params = &node->params.pass.custom;
        copy_args = params->copy_args;
        user_args = params->user_args;
    } else if ( node->params.type == xf_node_type_compute_pass_m ) {
        xf_node_compute_pass_params_t* params = &node->params.pass.compute;
        copy_args = params->copy_uniform_data;
        user_args = params->uniform_data;
    } else if ( node->params.type == xf_node_type_raytrace_pass_m ) {
        xf_node_raytrace_pass_params_t* params = &node->params.pass.raytrace;
        copy_args = params->copy_uniform_data;
        user_args = params->uniform_data;
    }

    if ( copy_args && user_args.base ) {
        void* alloc = std_virtual_stack_alloc_align ( &graph->node_user_arg_allocator, user_args.size, 16 );
        std_mem_copy ( alloc, user_args.base, user_args.size );
        node->user_alloc = alloc;
    } else {
        node->user_alloc = NULL;
    }

    xf_node_h node_handle = ( xf_node_h ) ( node - graph->nodes_array );

    graph->nodes_declaration_order[node_handle] = node_handle;
    node->declaration_order = node_handle;

    return node_handle;
}

void xf_graph_node_destroy ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];

    // TODO free params

    std_list_push ( &graph->nodes_freelist, node );
    --graph->nodes_count;

    for ( uint32_t i = node->declaration_order; i < graph->nodes_count; ++i ) {
        graph->nodes_declaration_order[i] = graph->nodes_declaration_order[i + 1];
    }
}

static void xf_graph_add_texture_dependency ( xf_graph_h graph_handle, xf_node_h node_handle, xf_graph_texture_h texture_handle, xg_texture_view_t view, uint32_t resource_idx ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_texture_t* texture = &graph->textures_array[texture_handle];

    uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? texture->dependencies.subresource_count - view.mip_base : view.mip_count;
    for ( uint32_t i = 0; i < mip_count; ++i ) {
        std_assert_m ( i < texture->dependencies.subresource_count );
        xf_graph_subresource_dependencies_t* deps = &texture->dependencies.subresources[i + view.mip_base];
        std_assert_m ( deps->count + 1 < std_static_array_capacity_m ( deps->array ) );
        deps->array[deps->count++] = xf_resource_access_m ( .node = node_handle, .resource_idx = resource_idx );
    }
}

static void xf_graph_add_buffer_dependency ( xf_graph_h graph_handle, xf_node_h node_handle, xf_graph_buffer_h buffer_handle, uint32_t resource_idx ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_buffer_t* buffer = &graph->buffers_array[buffer_handle];
    xf_graph_subresource_dependencies_t* deps = &buffer->dependencies.subresources[0];
    deps->array[deps->count++] = xf_resource_access_m ( .node = node_handle, .resource_idx = resource_idx );
}

static void xf_graph_node_accumulate_dependencies ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];

    for ( uint32_t i = 0; i < node->textures_count; ++i ) {
        uint32_t resource_idx = node->textures_array[i];
        xf_node_resource_t* resource = &node->resources_array[resource_idx];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, resource->texture.graph_handle, resource->texture.view, resource_idx );
    }

    for ( uint32_t i = 0; i < node->buffers_count; ++i ) {
        uint32_t resource_idx = node->buffers_array[i];
        xf_node_resource_t* resource = &node->resources_array[resource_idx];
        xf_graph_add_buffer_dependency ( graph_handle, node_handle, resource->buffer.graph_handle, resource_idx );
    }
}

static void xf_graph_accumulate_physical_resources_dependencies ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        uint32_t node_handle = graph->nodes_execution_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_handle];
        for ( uint32_t tex_it = 0; tex_it < node->textures_count; ++tex_it ) {
            xf_node_resource_t* resource = &node->resources_array[node->textures_array[tex_it]];
            xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
            xf_graph_physical_texture_t* graph_physical_texture = &graph->physical_textures_array[graph_texture->physical_texture_handle];

            for ( uint32_t mip_it = 0; mip_it < graph_texture->dependencies.subresource_count; ++mip_it ) {
                xf_graph_subresource_dependencies_t* deps = &graph_texture->dependencies.subresources[mip_it];
                for ( uint32_t access_it = 0; access_it < deps->count; ++access_it ) {
                    xf_resource_access_t* access = &deps->array[access_it];
                    if ( access->node == node_handle ) {
                        xf_graph_subresource_dependencies_t* acc_deps = &graph_physical_texture->dependencies.subresources[mip_it];
                        std_assert_m ( acc_deps->count + 1 < std_static_array_capacity_m ( acc_deps->array ) );
                        acc_deps->array[acc_deps->count++] = *access;
                    }
                }
            }
        }
    }
}

static xf_resource_access_t* xf_graph_get_subresource_node_access ( xf_graph_subresource_dependencies_t* dependencies, xf_node_h node ) {
    for ( uint32_t i = 0; i < dependencies->count; ++i ) {
        xf_resource_access_t* access = &dependencies->array[i];
        if ( access->node == node ) {
            return &dependencies->array[i];
        }
    }
    return NULL;
}

static xg_memory_access_bit_e xf_graph_memory_access_from_resource_access ( xf_resource_access_e access ) {
    switch ( access ) {
    case xf_resource_access_sampled_m:
    case xf_resource_access_storage_read_m:
        return xg_memory_access_bit_shader_read_m;
    case xf_resource_access_storage_write_m:
        return xg_memory_access_bit_shader_write_m;
    case xf_resource_access_copy_read_m:
        return xg_memory_access_bit_transfer_read_m;
    case xf_resource_access_copy_write_m:
        return xg_memory_access_bit_transfer_write_m;
    case xf_resource_access_render_target_m:
        return xg_memory_access_bit_color_write_m;
    case xf_resource_access_depth_target_m:
        return xg_memory_access_bit_depth_stencil_write_m;
    default:
        return xg_memory_access_bit_none_m;
    }
}

static bool xf_graph_queue_supports_stage ( xg_cmd_queue_e queue, xg_pipeline_stage_bit_e stage ) {
    bool graphics = true;
    bool compute = true;
    bool copy = true;
    
    xg_pipeline_stage_bit_e graphics_stages = 
        xg_pipeline_stage_bit_draw_m |
        xg_pipeline_stage_bit_vertex_input_m |
        xg_pipeline_stage_bit_vertex_shader_m |
        xg_pipeline_stage_bit_fragment_shader_m |
        xg_pipeline_stage_bit_early_fragment_test_m |
        xg_pipeline_stage_bit_late_fragment_test_m |
        xg_pipeline_stage_bit_color_output_m |
        xg_pipeline_stage_bit_all_graphics_m;
    xg_pipeline_stage_bit_e compute_stages = 
        xg_pipeline_stage_bit_compute_shader_m |
        xg_pipeline_stage_bit_raytrace_shader_m |
        xg_pipeline_stage_bit_ray_acceleration_structure_build_m;
    //xg_pipeline_stage_bit_e copy_stages = xg_pipeline_stage_bit_transfer_m;

    if ( stage & graphics_stages ) {
        compute = false;
        copy = false;
    }

    if ( stage & compute_stages ) {
        copy = false;
    }

    if ( queue == xg_cmd_queue_graphics_m ) return graphics;
    if ( queue == xg_cmd_queue_compute_m ) return compute;
    if ( queue == xg_cmd_queue_copy_m ) return copy;
    return false;
}

static void xf_graph_add_queue_ownership_transitions ( xf_graph_h graph_handle, xf_node_h node_handle, uint32_t node_resource_idx, xf_texture_execution_state_t* state ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;
    xf_node_resource_t* resource = &node->resources_array[node_resource_idx];
    xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
    xf_graph_physical_texture_t* graph_physical_texture = &graph->physical_textures_array[graph_texture->physical_texture_handle];
    xf_texture_h texture_handle = graph_texture->handle;

    xg_texture_view_t view = resource->texture.view;
    uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? graph_texture->dependencies.subresource_count - view.mip_base : view.mip_count;
    for ( uint32_t i = 0; i < mip_count; ++i ) {
        uint32_t mip_idx = i + view.mip_base;
        xf_resource_access_t* access = xf_graph_get_subresource_node_access ( &graph_physical_texture->dependencies.subresources[mip_idx], node_handle );

        // Look at the previous access, if any
        if ( access > graph_physical_texture->dependencies.subresources[mip_idx].array ) {
            xf_resource_access_t* prev_access = access - 1;
            xf_node_t* prev_node = &graph->nodes_array[prev_access->node];
            xg_cmd_queue_e prev_node_queue = prev_node->enabled ? prev_node->params.queue : xg_cmd_queue_graphics_m;

            // Transition out of prev queue into this node's queue
            if ( prev_node_queue != node_queue ) {
                std_auto_m texture_releases_array = std_static_array_m ( xf_graph_texture_transition_t, prev_node->texture_releases.array );
                std_auto_m texture_acquires_array = std_static_array_m ( xf_graph_texture_transition_t, node->texture_acquires.array );
                texture_releases_array.count = prev_node->texture_releases.count;
                texture_acquires_array.count = node->texture_acquires.count;

                xf_node_resource_t* prev_resource = &prev_node->resources_array[prev_access->resource_idx];
                xg_pipeline_stage_bit_e src_stage = prev_resource->stage;
                xg_pipeline_stage_bit_e dst_stage = resource->stage;
                xg_cmd_queue_e src_queue = prev_node_queue;
                xg_cmd_queue_e dst_queue = node_queue;

                xf_texture_execution_state_t prev_state = xf_texture_execution_state_m (
                    .layout = prev_resource->texture.layout,
                    .stage = src_stage,
                    .access = xf_graph_memory_access_from_resource_access ( prev_resource->access ),
                );

                if ( xf_graph_queue_supports_stage ( src_queue, dst_stage ) ) {
                    // Layout transition on source queue
#if 1
                    // TODO removing this one extra barrier on the source queue causes the validation layer to complain
                    // about the texture not being in the destination format on the destination queue. Why?
                    std_array_push_m ( &texture_releases_array, xf_graph_texture_transition_m (
                        .state = *state,
                        .state.queue = src_queue,
                        .texture = texture_handle,
                        .view = resource->texture.view
                    ) );

                    std_array_push_m ( &texture_releases_array, xf_graph_texture_transition_m (
                        .state = *state,
                        .texture = texture_handle,
                        .view = resource->texture.view
                    ) );
#else
                    std_array_push_m ( &texture_releases_array, xf_graph_texture_transition_m (
                        .state = prev_state,
                        .state.queue = dst_queue,
                        .texture = texture_handle,
                        .view = resource->texture.view
                    ) );
#endif
                } else {
                    std_assert_m ( xf_graph_queue_supports_stage ( dst_queue, src_stage ) );
                    // Layout transition on dest queue
                    std_array_push_m ( &texture_releases_array, xf_graph_texture_transition_m (
                        .state = prev_state,
                        .state.queue = dst_queue,
                        .texture = texture_handle,
                        .view = resource->texture.view
                    ) );
                }

                prev_node->texture_releases.count = texture_releases_array.count;
                node->texture_acquires.count = texture_acquires_array.count;
            }
        }
    }
}

static void xf_graph_compute_resource_transitions ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        xf_node_h node_handle = node_it;
        xf_node_t* node = &graph->nodes_array[node_handle];
        
        std_auto_m texture_transitions_array = std_static_array_m ( xf_graph_texture_transition_t, node->texture_transitions.array );

        xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;

        for ( uint32_t tex_it = 0; tex_it < node->textures_count; ++tex_it ) {
            uint32_t resource_idx = node->textures_array[tex_it];
            xf_node_resource_t* resource = &node->resources_array[resource_idx];
            xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
            xf_texture_h texture_handle = graph_texture->handle;

            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = resource->texture.layout,
                .stage = resource->stage,
                .access = xf_graph_memory_access_from_resource_access ( resource->access ),
                .queue = node_queue,
            );

            std_array_push_m ( &texture_transitions_array, xf_graph_texture_transition_m (
                .state = state,
                .texture = texture_handle,
                .view = resource->texture.view
            ) );

            xf_graph_add_queue_ownership_transitions ( graph_handle, node_handle, resource_idx, &state );
        }

        node->texture_transitions.count = texture_transitions_array.count;
    }
}

static xg_texture_usage_bit_e xf_graph_texture_usage_from_access ( xf_resource_access_e access ) {
    switch ( access ) {
    case xf_resource_access_sampled_m:
        return xg_texture_usage_bit_sampled_m;
    case xf_resource_access_storage_read_m:
    case xf_resource_access_storage_write_m:
        return xg_texture_usage_bit_storage_m;
    case xf_resource_access_copy_read_m:
        return xg_texture_usage_bit_copy_source_m;
    case xf_resource_access_copy_write_m:
        return xg_texture_usage_bit_copy_dest_m;
    case xf_resource_access_render_target_m:
        return xg_texture_usage_bit_render_target_m;
    case xf_resource_access_depth_target_m:
        return xg_texture_usage_bit_depth_stencil_m;
    default:
        return xg_texture_usage_bit_none_m;
    }
}

static xg_buffer_usage_bit_e xf_graph_buffer_usage_from_access ( xf_resource_access_e access ) {
    switch ( access ) {
    case xf_resource_access_uniform_m:
        return xg_buffer_usage_bit_uniform_m;
    case xf_resource_access_storage_read_m:
    case xf_resource_access_storage_write_m:
        return xg_buffer_usage_bit_storage_m;
    case xf_resource_access_copy_read_m:
        return xg_buffer_usage_bit_copy_source_m;
    case xf_resource_access_copy_write_m:
        return xg_buffer_usage_bit_copy_dest_m;
    default:
        return xg_buffer_usage_bit_none_m;
    }
}

static void xf_graph_node_accumulate_resource_usage ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];

    for ( uint32_t i = 0; i < node->textures_count; ++i ) {
        xf_node_resource_t* resource = &node->resources_array[node->textures_array[i]];
        xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
        xf_resource_texture_add_usage ( graph_texture->handle, xf_graph_texture_usage_from_access ( resource->access ) );
    }

    for ( uint32_t i = 0; i < node->buffers_count; ++i ) {
        xf_node_resource_t* resource = &node->resources_array[node->buffers_array[i]];
        xf_graph_buffer_t* graph_buffer = &graph->buffers_array[resource->buffer.graph_handle];
        xf_resource_buffer_add_usage ( graph_buffer->handle, xf_graph_buffer_usage_from_access ( resource->access ) );
    }
}

static void xf_graph_add_node_dependency ( xf_graph_h graph_handle, xf_node_h from, xf_node_h to ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    
    {
        xf_node_t* node = &graph->nodes_array[from];

        bool existing = false;
        for ( uint32_t i = 0; i < node->next_nodes_count; ++i ) { 
            if ( node->next_nodes[i] == to ) {
                existing = true;
                break;
            }
        }

        if ( !existing ) {
            node->next_nodes[node->next_nodes_count++] = to;
        }
    }

    {
        xf_node_t* node = &graph->nodes_array[to];
        bool existing = false;
        for ( uint32_t i = 0; i < node->prev_nodes_count; ++i ) {
            if ( node->prev_nodes[i] == from ) {
                existing = true;
            }
        }

        if ( !existing ) {
            node->prev_nodes[node->prev_nodes_count++] = from;
        }
    }
}

static void xf_graph_add_node_dependencies ( xf_graph_h graph_handle, xf_graph_subresource_dependencies_t* deps ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_h prev_write = xf_null_handle_m;
    xf_node_h prev_reads[xf_graph_max_nodes_m];
    uint32_t prev_reads_count = 0;

    for ( uint32_t i = 0; i < deps->count; ++i ) {
        xf_node_h node_handle = deps->array[i].node;
        xf_node_t* node = &graph->nodes_array[node_handle];
        xf_node_resource_t* resource = &node->resources_array[deps->array[i].resource_idx];
        xf_resource_access_e access = resource->access;
        if ( xf_resource_access_is_write ( access ) ) {
            if ( prev_reads_count == 0 ) {
                if ( prev_write != xf_null_handle_m ) {
                    xf_graph_add_node_dependency ( graph_handle, prev_write, node_handle );
                }
            } else {
                for ( uint32_t j = 0; j < prev_reads_count; ++j ) {
                    xf_graph_add_node_dependency ( graph_handle, prev_reads[j], node_handle );
                }
            }
            prev_reads_count = 0;
            prev_write = node_handle;
        } else if ( xf_resource_access_is_read ( access ) ) {
            //std_assert_m ( !first ); // should assert this only for transient resources
            if ( prev_write != xf_null_handle_m ) {
                xf_graph_add_node_dependency ( graph_handle, prev_write, node_handle );
            }
            prev_reads[prev_reads_count++] = node_handle;
        }
    }
}

static void xf_graph_accumulate_node_dependencies ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_texture_t* texture = &graph->textures_array[i];
        xf_texture_info_t info;
        xf_resource_texture_get_info ( &info, texture->handle );

#if 1
        for ( uint32_t j = 0; j < texture->dependencies.subresource_count; ++j ) {
            xf_graph_add_node_dependencies ( graph_handle, &texture->dependencies.subresources[j] );
        }
#else
        if ( info.view_access == xg_texture_view_access_default_only_m ) {
            xf_graph_add_node_dependencies ( graph_handle, &texture->deps.shared );
        } else if ( info.view_access == xg_texture_view_access_separate_mips_m ) {
            for ( uint32_t j = 0; j < std_static_array_capacity_m ( texture->deps.mips ); ++j ) {
                xf_graph_add_node_dependencies ( graph_handle, &texture->deps.mips[j] );
            }
        } else {
            std_not_implemented_m();
        }
#endif
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_graph_buffer_t* buffer = &graph->buffers_array[i];
        //xf_graph_add_node_dependencies ( graph_handle, &buffer->deps );
        for ( uint32_t j = 0; j < buffer->dependencies.subresource_count; ++j ) {
            xf_graph_add_node_dependencies ( graph_handle, &buffer->dependencies.subresources[j] );
        }
    }

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];
        for ( uint32_t j = 0; j < node->params.node_dependencies_count; ++j ) {
            xf_node_t* source_node = &graph->nodes_array[node->params.node_dependencies[j]];
            source_node->next_nodes[source_node->next_nodes_count++] = i;
        }
    }
}

static void xf_graph_fill_prev_nodes_from_next ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];
        node->prev_nodes_count = 0;        
    }

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];

        for ( uint32_t j = 0; j < node->next_nodes_count; ++j ) {
            xf_node_h next_handle = node->next_nodes[j];
            xf_node_t* next_node = &graph->nodes_array[next_handle];
            next_node->prev_nodes[next_node->prev_nodes_count++] = node_handle;
        }
    }
}

typedef struct {
    xf_node_h nodes[xf_graph_max_nodes_m];
    uint32_t count;
} xf_graph_traverse_result_t;

static xf_graph_traverse_result_t xf_graph_topological_sort ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_traverse_result_t result = {};

    xf_node_h stack_buffer[xf_graph_max_nodes_m] = {};
    uint8_t count[xf_graph_max_nodes_m] = {};
    std_auto_m stack = std_static_array_m ( uint64_t, stack_buffer );
    std_auto_m nodes = std_static_array_m ( xf_node_h, result.nodes );

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        count[node_handle] = graph->nodes_array[node_handle].prev_nodes_count;
    }

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];
        if ( node->prev_nodes_count == 0 ) {
            std_array_push_m ( &stack, node_handle );
        }
    }

    while ( stack.count > 0 ) {
        xf_node_h node_handle = std_array_pop_m ( &stack );
        xf_node_t* node = &graph->nodes_array[node_handle];

        std_array_push_m ( &nodes, node_handle );

        for ( uint32_t i = 0; i < node->next_nodes_count; ++i ) {
            //count[node->next_nodes[i]] -= 1;
            xf_node_h next_handle = node->next_nodes[i];
            if ( --count[next_handle] == 0 ) {
                std_array_push_m ( &stack, next_handle );
            }
        }
    }

    result.count = nodes.count;
    return result;
}

static void xf_graph_build_cross_queue_deps ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    std_mem_zero_static_array_m ( graph->cross_queue_node_deps );
    for ( uint32_t i = 0; i < xf_graph_max_nodes_m; ++i ) {
        for ( uint32_t j = 0; j < xg_cmd_queue_count_m; ++j ) {
            graph->cross_queue_node_deps[i][j] = -1;
        }
    }

    xf_node_h last_nodes[xg_cmd_queue_count_m] = { -1, -1, -1 };

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_execution_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];
        xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;

        for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; q++ ) {
            if ( node_queue == q ) {
                if ( last_nodes[q] != -1 ) {
                    graph->cross_queue_node_deps[i][q] = last_nodes[q];
                }
                last_nodes[q] = i;
            } else {
                uint32_t latest_prev_order = 0;
                uint32_t latest_prev_idx = -1;

                for ( uint32_t j = 0; j < node->prev_nodes_count; ++j ) {
                    xf_node_h prev_handle = node->prev_nodes[j];
                    xf_node_t* prev_node = &graph->nodes_array[prev_handle];
                    xg_cmd_queue_e prev_node_queue = prev_node->enabled ? prev_node->params.queue : xg_cmd_queue_graphics_m;

                    if ( prev_node_queue == q ) {
                        if ( latest_prev_order <= prev_node->execution_order ) {
                            latest_prev_order = prev_node->execution_order;
                            latest_prev_idx = j;
                        }
                    }
                }

                if ( latest_prev_idx != -1 ) {
                    graph->cross_queue_node_deps[i][q] = latest_prev_order;
                }
            }
        }
    }
}

static void xf_graph_linearize ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    // TODO try to maximize distance between dependencies when determining execution order

    if ( graph->params.flags & xf_graph_flag_sort_m ) {
        xf_graph_fill_prev_nodes_from_next ( graph_handle );
        xf_graph_traverse_result_t traverse = xf_graph_topological_sort ( graph_handle );

        //for ( uint32_t i = 0; i < graph->nodes_count; ++i ) { traverse.nodes[i] = i; }

        //std_log_info_m ( std_fmt_str_m " node order:", graph->params.debug_name );
        for ( uint32_t i = 0; i < traverse.count; ++i ) {
            uint32_t node_idx = traverse.nodes[i];
            //xf_node_t* node = &graph->nodes_array[node_idx];
            char buffer[32] = " -";
            std_u32_to_str ( buffer, 32, i, 2 );
            //std_log_info_m ( std_fmt_tab_m std_fmt_str_m " - " std_fmt_str_m, buffer, node->params.debug_name );
            graph->nodes_execution_order[i] = node_idx;
            graph->nodes_array[node_idx].execution_order = i;
        }
    } else {
        for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
            xf_node_h node_handle = graph->nodes_declaration_order[i];
            graph->nodes_execution_order[i] = node_handle;
            graph->nodes_array[node_handle].execution_order = i;
        }

#if 0
        if ( graph->export_node != xf_null_handle_m ) {
            graph->nodes_execution_order[graph->export_source_node + 1] = graph->export_node;
            graph->nodes_array[graph->export_node].execution_order = graph->export_source_node + 1;
            for ( uint32_t i = graph->export_source_node + 2; i < graph->nodes_count; ++i ) {
                xf_node_h node_handle = graph->nodes_declaration_order[i];
                graph->nodes_execution_order[node_handle] = node_handle - 1;
                graph->nodes_array[node_handle - 1].execution_order = node_handle;
            }
        }
#endif
    }

    bool print_node_deps = graph->params.flags & xf_graph_flag_print_node_deps_m;
    if ( print_node_deps ) {
    std_log_info_m ( std_fmt_str_m " node dependencies:", graph->params.debug_name );
        for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
            xf_node_h node_handle = graph->nodes_declaration_order[i];
            xf_node_t* node = &graph->nodes_array[node_handle];
            char buffer[1024];
            std_stack_t stack = std_static_stack_m ( buffer );
            std_stack_string_append ( &stack, node->params.debug_name );
            std_stack_string_append ( &stack, " | next: [" );
            for ( uint32_t j = 0; j < node->next_nodes_count; ++j ) {
                if ( j > 0 ) {
                    std_stack_string_append ( &stack, " " );
                }
                xf_node_t* dep_node = &graph->nodes_array[node->next_nodes[j]];
                std_stack_string_append ( &stack, dep_node->params.debug_name );
                //std_assert_m ( node->next_nodes[j] > i );
            }
            std_stack_string_append ( &stack, "] | prev: [" );
            for ( uint32_t j = 0; j < node->prev_nodes_count; ++j ) {
                if ( j > 0 ) {
                    std_stack_string_append ( &stack, " " );
                }
                xf_node_t* dep_node = &graph->nodes_array[node->prev_nodes[j]];
                std_stack_string_append ( &stack, dep_node->params.debug_name );
                //std_assert_m ( node->prev_nodes[j] < i );
            }
            std_stack_string_append ( &stack, "]" );
            std_log_info_m ( std_fmt_tab_m std_fmt_str_m, buffer );
        }
    }

    xf_graph_build_cross_queue_deps ( graph_handle );
}

static xg_buffer_params_t xf_graph_buffer_params ( xg_device_h device, xf_buffer_h buffer_handle ) {
    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

    xg_buffer_params_t params = xg_buffer_params_m (
        .memory_type = buffer->params.upload ? xg_memory_type_upload_m : xg_memory_type_gpu_only_m, // TODO readback ?
        .device = device,
        .size = buffer->params.size,
        .allowed_usage = buffer->required_usage,
    );
    std_str_copy_static_m ( params.debug_name, buffer->params.debug_name );

    return params;
}

static xg_texture_params_t xf_graph_texture_params ( xg_device_h device, xf_texture_h texture_handle ) {
    const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    // TODO
    xg_texture_params_t params = xg_texture_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device,
        .width = texture->params.width,
        .height = texture->params.height,
        .depth = texture->params.depth,
        .mip_levels = texture->params.mip_levels,
        .array_layers = texture->params.array_layers,
        .dimension = texture->params.dimension,
        .format = texture->params.format,
        .samples_per_pixel = texture->params.samples_per_pixel,
        .allowed_usage = texture->required_usage | texture->params.usage,
        .view_access = texture->params.view_access,
    );
    std_str_copy_static_m ( params.debug_name, texture->params.debug_name ); // TODO not good for memory aliased textures

    // Tag all render tagets as copy src/dest. Matches xg_vk_pipeline framebuffer creation logic
    if ( texture->required_usage & xg_texture_usage_bit_render_target_m ) {
        params.allowed_usage |= xg_texture_usage_bit_copy_source_m | xg_texture_usage_bit_copy_dest_m;
    }

    if ( texture->required_usage & xg_texture_usage_bit_depth_stencil_m ) {
        params.allowed_usage |= xg_texture_usage_bit_copy_source_m | xg_texture_usage_bit_copy_dest_m;
    }

    // Tag storage textures as copy src/dest to allow aliasing passthrough. TODO more fine grained tagging
    if ( texture->required_usage & xg_texture_usage_bit_storage_m ) {
        params.allowed_usage |= xg_texture_usage_bit_copy_source_m | xg_texture_usage_bit_copy_dest_m;
    }

    params.initial_layout = xg_texture_layout_undefined_m;

    return params;
}

static void xf_graph_resource_lifespan_extend ( xf_graph_resource_lifespan_t* lifespan, uint32_t node, xg_cmd_queue_e queue ) {
    lifespan->first = std_min_i32 ( lifespan->first, node );
    lifespan->last[queue] = std_max_i32 ( lifespan->last[queue], node );
}

static int xf_graph_lifespan_overlap_test ( xf_graph_h graph_handle, const xf_graph_resource_lifespan_t* a, const xf_graph_resource_lifespan_t* b ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    bool before = true;
    bool after = true;
    for ( uint32_t i = 0; i < xg_cmd_queue_count_m; ++i ) {
        before &= graph->cross_queue_node_deps[b->first][i] >= a->last[i];
        after  &= graph->cross_queue_node_deps[a->first][i] >= b->last[i];
    }

    return before ? -1 : after ? 1 : 0;
}

static xg_texture_layout_e xf_graph_texture_layout_from_resource_access ( xf_texture_h texture_handle, xf_resource_access_e access ) {
    bool is_depth = xf_resource_texture_is_depth ( texture_handle ); // TODO stencil
    switch ( access ) {
    case xf_resource_access_sampled_m:
        return is_depth ? xg_texture_layout_depth_stencil_read_m : xg_texture_layout_shader_read_m;
    case xf_resource_access_storage_read_m:
        return xg_texture_layout_shader_write_m;
        //return xg_texture_layout_shader_read_m;
    case xf_resource_access_storage_write_m:
        return xg_texture_layout_shader_write_m;
    case xf_resource_access_copy_read_m:
        return xg_texture_layout_copy_source_m;
    case xf_resource_access_copy_write_m:
        return xg_texture_layout_copy_dest_m;
    case xf_resource_access_render_target_m:
        return xg_texture_layout_render_target_m;
    case xf_resource_access_depth_target_m:
        // TODO check read-only depth
        return xg_texture_layout_depth_stencil_target_m;
    default:
        return xg_texture_layout_undefined_m;
    }
}

static xf_graph_resource_dependencies_t xf_graph_alloc_physical_texture_resource_dependencies ( xf_graph_h graph_handle, xf_physical_texture_h physical_texture_handle, xg_texture_view_t view ) {
    xf_graph_resource_dependencies_t deps = {};
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_physical_texture_t* physical_texture = xf_resource_physical_texture_get ( physical_texture_handle );
    uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? physical_texture->info.mip_levels : view.mip_count;
    deps.subresources = std_virtual_stack_alloc_array_m ( &graph->physical_resource_dependencies_allocator, xf_graph_subresource_dependencies_t, mip_count );
    std_mem_zero_array_m ( deps.subresources, mip_count );
    deps.subresource_count = mip_count;
    return deps;
}

static void xf_graph_scan_physical_textures ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t physical_texture_hashes[xf_graph_max_textures_m * 2];
    uint64_t physical_texture_values[xf_graph_max_textures_m * 2];
    std_hash_map_t physical_textures_map = std_static_hash_map_m ( physical_texture_hashes, physical_texture_values );
    std_auto_m physical_textures_array = std_static_array_m ( xf_graph_physical_texture_t, graph->physical_textures_array );

    for ( uint32_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        xf_node_h node_handle = graph->nodes_declaration_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_handle];

        for ( uint32_t tex_it = 0; tex_it < node->textures_count; ++tex_it ) {
            xf_node_resource_t* resource = &node->resources_array[node->textures_array[tex_it]];
            xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
            xf_texture_t* texture = xf_resource_texture_get ( graph_texture->handle );
            xf_physical_texture_h physical_texture_handle = texture->physical_texture_handle;
        
            uint64_t physical_texture_idx = physical_textures_array.count;
            if ( std_hash_map_try_insert ( &physical_texture_idx, &physical_textures_map, std_hash_64_m ( physical_texture_handle ), physical_texture_idx ) ) {
                std_array_push_m ( &physical_textures_array, xf_graph_physical_texture_m ( 
                    .handle = physical_texture_handle,
                    .dependencies = xf_graph_alloc_physical_texture_resource_dependencies ( graph_handle, physical_texture_handle, xg_texture_view_m() )
                ) );
            }
            graph_texture->physical_texture_handle = physical_texture_idx;
            std_noop_m;
        }
    }

    graph->physical_textures_count = physical_textures_array.count;
}

static void xf_graph_alloc_texture_resource_dependencies ( xf_graph_h graph_handle, xf_graph_texture_h graph_texture_handle ) {
    xf_graph_resource_dependencies_t deps;
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_texture_t* graph_texture = &graph->textures_array[graph_texture_handle];
    xf_texture_info_t info;
    xf_resource_texture_get_info ( &info, graph_texture->handle );
    //uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? info.mip_levels : view.mip_count;
    std_assert_m ( graph_texture->subresources_bitset != 0 );
    uint32_t mip_count = 64 - std_bit_scan_rev_64 ( graph_texture->subresources_bitset );
    deps.subresources = std_virtual_stack_alloc_array_m ( &graph->resource_dependencies_allocator, xf_graph_subresource_dependencies_t, mip_count );
    std_mem_zero_array_m ( deps.subresources, mip_count );
    deps.subresource_count = mip_count;
    //return deps;
    graph_texture->dependencies = deps;
}

static xf_graph_resource_dependencies_t xf_graph_alloc_buffer_resource_dependencies ( xf_graph_h graph_handle ) {
    xf_graph_resource_dependencies_t deps;
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint32_t mip_count = 1;
    deps.subresources = std_virtual_stack_alloc_array_m ( &graph->resource_dependencies_allocator, xf_graph_subresource_dependencies_t, mip_count );
    std_mem_zero_array_m ( deps.subresources, mip_count );
    deps.subresource_count = mip_count;
    return deps;
}

static void xf_graph_texture_accumulate_subresources ( xf_graph_h graph_handle, xf_graph_texture_h graph_texture_handle, xg_texture_view_t view ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_texture_t* graph_texture = &graph->textures_array[graph_texture_handle];
    xf_texture_info_t info;
    xf_resource_texture_get_info ( &info, graph_texture->handle );
    uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? info.mip_levels - view.mip_base : view.mip_count;
    for ( uint32_t i = 0; i < mip_count; ++i ) {
        std_bitset_set ( &graph_texture->subresources_bitset, view.mip_base + i );
    }
}

static void xf_graph_allocate_texture_dependencies ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_alloc_texture_resource_dependencies ( graph_handle, i );
    }
}

static void xf_graph_scan_textures ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t texture_hashes[xf_graph_max_textures_m * 2];
    uint64_t texture_values[xf_graph_max_textures_m * 2];
    std_hash_map_t graph_textures_map = std_static_hash_map_m ( texture_hashes, texture_values );
    std_auto_m graph_textures_array = std_static_array_m ( xf_graph_texture_t, graph->textures_array );

    for ( uint32_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        xf_node_h node_handle = graph->nodes_declaration_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_handle];
        xf_node_resource_params_t* params = &node->params.resources;
        std_auto_m node_resources_array = std_static_array_m ( xf_node_resource_t, node->resources_array );
        node_resources_array.count = node->resources_count;
        std_auto_m node_textures_array = std_static_array_m ( uint32_t, node->textures_array );

        if ( node->enabled ) {
            for ( size_t resource_it = 0; resource_it < params->sampled_textures_count; ++resource_it ) {
                xf_shader_texture_dependency_t* resource = &params->sampled_textures[resource_it];
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m ( 
                        .handle = resource->texture, 
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_sampled_m;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = resource->stage, 
                    .access = access,
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = resource->view,
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->storage_texture_reads_count; ++resource_it ) {
                xf_shader_texture_dependency_t* resource = &params->storage_texture_reads[resource_it];
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m ( 
                        .handle = resource->texture,
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_storage_read_m;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = resource->stage, 
                    .access = access, 
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = resource->view,
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->storage_texture_writes_count; ++resource_it ) {
                xf_shader_texture_dependency_t* resource = &params->storage_texture_writes[resource_it];
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m ( 
                        .handle = resource->texture,
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_storage_write_m;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = resource->stage, 
                    .access = access, 
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = resource->view, 
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->copy_texture_reads_count; ++resource_it ) {
                xf_copy_texture_dependency_t* resource = &params->copy_texture_reads[resource_it];
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m ( 
                        .handle = resource->texture,
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_copy_read_m;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = xg_pipeline_stage_bit_transfer_m, 
                    .access = access, 
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = resource->view,
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->copy_texture_writes_count; ++resource_it ) {
                xf_copy_texture_dependency_t* resource = &params->copy_texture_writes[resource_it];
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m ( 
                        .handle = resource->texture,
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_copy_write_m;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = xg_pipeline_stage_bit_transfer_m, 
                    .access = access, 
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = resource->view,
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->render_targets_count; ++resource_it ) {
                xf_render_target_dependency_t* resource = &params->render_targets[resource_it];
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m (
                        .handle = resource->texture,
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_render_target_m;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = xg_pipeline_stage_bit_color_output_m, 
                    .access = access, 
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = resource->view,
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }

            if ( params->depth_stencil_target != xf_null_handle_m ) {
                uint64_t graph_texture_idx = graph_textures_array.count;
                if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( params->depth_stencil_target ), graph_texture_idx ) ) {
                    std_array_push_m ( &graph_textures_array, xf_graph_texture_m (
                        .handle = params->depth_stencil_target,
                    ) );
                }
                xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, xg_texture_view_m() );
                uint32_t node_resource_idx = node_resources_array.count;
                xf_resource_access_e access = xf_resource_access_depth_target_m;
                // TODO early
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_texture_m, 
                    .stage = xg_pipeline_stage_bit_late_fragment_test_m, 
                    .access = access, 
                    .texture.graph_handle = graph_texture_idx, 
                    .texture.view = xg_texture_view_m(),
                    .texture.layout = xf_graph_texture_layout_from_resource_access ( params->depth_stencil_target, access )
                ) );
                std_array_push_m ( &node_textures_array, node_resource_idx );
            }
        } else {
            xf_node_passthrough_params_t* passthrough = &node->params.passthrough;
            for ( uint32_t i = 0; i < params->render_targets_count; ++i ) {
                xf_render_target_dependency_t* resource = &params->render_targets[i];
                if ( passthrough->render_targets[i].mode != xf_passthrough_mode_ignore_m ) {
                    uint64_t graph_texture_idx = graph_textures_array.count;
                    if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                        std_array_push_m ( &graph_textures_array, xf_graph_texture_m (
                            .handle = resource->texture,
                        ) );
                    }
                    xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                    
                    if ( passthrough->render_targets[i].mode == xf_passthrough_mode_clear_m || passthrough->render_targets[i].mode == xf_passthrough_mode_copy_m ) {
                        uint32_t node_resource_idx = node_resources_array.count;
                        xf_resource_access_e access = xf_resource_access_copy_write_m;
                        std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                            .type = xf_node_resource_texture_m, 
                            .stage = xg_pipeline_stage_bit_transfer_m, 
                            .access = access, 
                            .texture.graph_handle = graph_texture_idx, 
                            .texture.view = resource->view,
                            .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                        ) );
                        std_array_push_m ( &node_textures_array, node_resource_idx );
                    }

                    if ( passthrough->render_targets[i].mode == xf_passthrough_mode_copy_m ) {
                        xf_copy_texture_dependency_t* copy_source = &passthrough->render_targets[i].copy_source;
                        graph_texture_idx = graph_textures_array.count;
                        if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( copy_source->texture ), graph_texture_idx ) ) {
                            std_array_push_m ( &graph_textures_array, xf_graph_texture_m (
                                .handle = copy_source->texture,
                            ) );
                        }
                        xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );

                        uint32_t node_resource_idx = node_resources_array.count;
                        xf_resource_access_e access = xf_resource_access_copy_read_m;
                        std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                            .type = xf_node_resource_texture_m, 
                            .stage = xg_pipeline_stage_bit_transfer_m, 
                            .access = access, 
                            .texture.graph_handle = graph_texture_idx, 
                            .texture.view = copy_source->view,
                            .texture.layout = xf_graph_texture_layout_from_resource_access ( copy_source->texture, access )
                        ) );
                        std_array_push_m ( &node_textures_array, node_resource_idx );
                    }
                }
            }

            for ( uint32_t i = 0; i < params->storage_texture_writes_count; ++i ) {
                xf_shader_texture_dependency_t* resource = &params->storage_texture_writes[i];
                if ( passthrough->storage_texture_writes[i].mode != xf_passthrough_mode_ignore_m ) {
                    uint64_t graph_texture_idx = graph_textures_array.count;
                    if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( resource->texture ), graph_texture_idx ) ) {
                        std_array_push_m ( &graph_textures_array, xf_graph_texture_m (
                            .handle = resource->texture,
                        ) );
                    }
                    xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );
                    
                    if ( passthrough->storage_texture_writes[i].mode == xf_passthrough_mode_clear_m || passthrough->storage_texture_writes[i].mode == xf_passthrough_mode_copy_m ) {
                        uint32_t node_resource_idx = node_resources_array.count;
                        xf_resource_access_e access = xf_resource_access_copy_write_m;
                        std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                            .type = xf_node_resource_texture_m, 
                            .stage = xg_pipeline_stage_bit_transfer_m, 
                            .access = access, 
                            .texture.graph_handle = graph_texture_idx, 
                            .texture.view = resource->view,
                            .texture.layout = xf_graph_texture_layout_from_resource_access ( resource->texture, access )
                        ) );
                        std_array_push_m ( &node_textures_array, node_resource_idx );
                    }

                    if ( passthrough->storage_texture_writes[i].mode == xf_passthrough_mode_copy_m ) {
                        xf_copy_texture_dependency_t* copy_source = &passthrough->storage_texture_writes[i].copy_source;
                        graph_texture_idx = graph_textures_array.count;
                        if ( std_hash_map_try_insert ( &graph_texture_idx, &graph_textures_map, std_hash_64_m ( copy_source->texture ), graph_texture_idx ) ) {
                            std_array_push_m ( &graph_textures_array, xf_graph_texture_m (
                                .handle = copy_source->texture,
                            ) );
                        }
                        xf_graph_texture_accumulate_subresources ( graph_handle, graph_texture_idx, resource->view );

                        uint32_t node_resource_idx = node_resources_array.count;
                        xf_resource_access_e access = xf_resource_access_copy_read_m;
                        std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                            .type = xf_node_resource_texture_m, 
                            .stage = xg_pipeline_stage_bit_transfer_m, 
                            .access = access, 
                            .texture.graph_handle = graph_texture_idx, 
                            .texture.view = copy_source->view,
                            .texture.layout = xf_graph_texture_layout_from_resource_access ( copy_source->texture, access )
                        ) );
                        std_array_push_m ( &node_textures_array, node_resource_idx );
                    }
                }
            }
#if 0 // TODO
            for ( uint32_t i = 0; i < params->storage_texture_writes_count; ++i ) {
                if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_clear_m ) {
                } else if if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_copy_m ) {
                }
            }
#endif
        }

        node->textures_count = node_textures_array.count;
        node->resources_count = node_resources_array.count;
    }

    graph->textures_count = graph_textures_array.count;
}

static void xf_graph_scan_multi_textures ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t texture_hashes[xf_graph_max_multi_textures_m * 2];
    std_hash_set_t textures_set = std_static_hash_set_m ( texture_hashes );
    std_auto_m multi_textures_array = std_static_array_m ( uint32_t, graph->multi_textures_array );

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_texture_h texture = graph->textures_array[i].handle;
        if ( xf_resource_texture_is_multi ( texture ) ) {
            texture = xf_resource_multi_texture_get_default ( texture );
            if ( std_hash_set_insert ( &textures_set, std_hash_64_m ( texture ) ) ) {
                std_array_push_m ( &multi_textures_array, i );
            }
        }
    }

    graph->multi_textures_count = multi_textures_array.count;
}

static void xf_graph_scan_buffers ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t buffer_hashes[xf_graph_max_buffers_m * 2];
    uint64_t buffer_values[xf_graph_max_buffers_m * 2];
    std_hash_map_t graph_buffers_map = std_static_hash_map_m ( buffer_hashes, buffer_values );
    std_auto_m graph_buffers_array = std_static_array_m ( xf_graph_buffer_t, graph->buffers_array );

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];
        xf_node_resource_params_t* params = &node->params.resources;
        std_auto_m node_resources_array = std_static_array_m ( xf_node_resource_t, node->resources_array );
        node_resources_array.count = node->resources_count;
        std_auto_m node_buffers_array = std_static_array_m ( uint32_t, node->buffers_array );

        if ( node->enabled ) {
            for ( size_t resource_it = 0; resource_it < params->uniform_buffers_count; ++resource_it ) {
                xf_shader_buffer_dependency_t* resource = &params->uniform_buffers[resource_it];
                uint64_t graph_buffer_idx = graph_buffers_array.count;
                if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( resource->buffer ), graph_buffer_idx ) ) {
                    std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m ( 
                        .handle = resource->buffer,
                        .dependencies = xf_graph_alloc_buffer_resource_dependencies ( graph_handle )
                    ) );
                }
                uint32_t node_resource_idx = node_resources_array.count;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_buffer_m, 
                    .stage = resource->stage, 
                    .access = xf_resource_access_uniform_m, 
                    .buffer.graph_handle = graph_buffer_idx ) );
                std_array_push_m ( &node_buffers_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->storage_buffer_reads_count; ++resource_it ) {
                xf_shader_buffer_dependency_t* resource = &params->storage_buffer_reads[resource_it];
                uint64_t graph_buffer_idx = graph_buffers_array.count;
                if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( resource->buffer ), graph_buffer_idx ) ) {
                    std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m ( 
                        .handle = resource->buffer,
                        .dependencies = xf_graph_alloc_buffer_resource_dependencies ( graph_handle )
                    ) );
                }
                uint32_t node_resource_idx = node_resources_array.count;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_buffer_m, 
                    .stage = resource->stage, 
                    .access = xf_resource_access_storage_read_m, 
                    .buffer.graph_handle = graph_buffer_idx ) );
                std_array_push_m ( &node_buffers_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->storage_buffer_writes_count; ++resource_it ) {
                xf_shader_buffer_dependency_t* resource = &params->storage_buffer_writes[resource_it];
                uint64_t graph_buffer_idx = graph_buffers_array.count;
                if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( resource->buffer ), graph_buffer_idx ) ) {
                    std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m ( 
                        .handle = resource->buffer,
                        .dependencies = xf_graph_alloc_buffer_resource_dependencies ( graph_handle )
                    ) );
                }
                uint32_t node_resource_idx = node_resources_array.count;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_buffer_m, 
                    .stage = resource->stage, 
                    .access = xf_resource_access_storage_write_m, 
                    .buffer.graph_handle = graph_buffer_idx ) );
                std_array_push_m ( &node_buffers_array, node_resource_idx );
            }
            
            for ( size_t resource_it = 0; resource_it < params->copy_buffer_reads_count; ++resource_it ) {
                xf_buffer_h buffer_handle = params->copy_buffer_reads[resource_it];
                uint64_t graph_buffer_idx = graph_buffers_array.count;
                if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( buffer_handle ), graph_buffer_idx ) ) {
                    std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m ( 
                        .handle = buffer_handle,
                        .dependencies = xf_graph_alloc_buffer_resource_dependencies ( graph_handle )
                    ) );
                }
                uint32_t node_resource_idx = node_resources_array.count;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_buffer_m, 
                    .stage = xg_pipeline_stage_bit_transfer_m, 
                    .access = xf_resource_access_copy_read_m, 
                    .buffer.graph_handle = graph_buffer_idx ) );
                std_array_push_m ( &node_buffers_array, node_resource_idx );
            }

            for ( size_t resource_it = 0; resource_it < params->copy_buffer_writes_count; ++resource_it ) {
                xf_buffer_h buffer_handle = params->copy_buffer_writes[resource_it];
                uint64_t graph_buffer_idx = graph_buffers_array.count;
                if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( buffer_handle ), graph_buffer_idx ) ) {
                    std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m ( 
                        .handle = buffer_handle,
                        .dependencies = xf_graph_alloc_buffer_resource_dependencies ( graph_handle )
                    ) );
                }
                uint32_t node_resource_idx = node_resources_array.count;
                std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                    .type = xf_node_resource_buffer_m, 
                    .stage = xg_pipeline_stage_bit_transfer_m, 
                    .access = xf_resource_access_copy_write_m, 
                    .buffer.graph_handle = graph_buffer_idx ) );
                std_array_push_m ( &node_buffers_array, node_resource_idx );
            }
        } else {
            xf_node_passthrough_params_t* passthrough = &node->params.passthrough;
            for ( uint32_t i = 0; i < params->storage_buffer_writes_count; ++i ) {
                xf_shader_buffer_dependency_t* resource = &params->storage_buffer_writes[i];
                if ( passthrough->storage_buffer_writes[i].mode != xf_passthrough_mode_ignore_m ) {
                    uint64_t graph_buffer_idx = graph_buffers_array.count;
                    if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( resource->buffer ), graph_buffer_idx ) ) {
                        std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m (
                            .handle = resource->buffer,
                            .dependencies = xf_graph_alloc_buffer_resource_dependencies ( graph_handle )
                        ) );
                    }
                    
                    if ( passthrough->storage_buffer_writes[i].mode == xf_passthrough_mode_clear_m || passthrough->storage_buffer_writes[i].mode == xf_passthrough_mode_copy_m ) {
                        uint32_t node_resource_idx = node_resources_array.count;
                        xf_resource_access_e access = xf_resource_access_copy_write_m;
                        std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                            .type = xf_node_resource_buffer_m, 
                            .stage = xg_pipeline_stage_bit_transfer_m, 
                            .access = access, 
                            .buffer.graph_handle = graph_buffer_idx, 
                        ) );
                        std_array_push_m ( &node_buffers_array, node_resource_idx );
                    }

                    if ( passthrough->storage_buffer_writes[i].mode == xf_passthrough_mode_copy_m ) {
                        xf_buffer_h buffer_handle = passthrough->storage_buffer_writes[i].copy_source;
                        graph_buffer_idx = graph_buffers_array.count;
                        if ( std_hash_map_try_insert ( &graph_buffer_idx, &graph_buffers_map, std_hash_64_m ( buffer_handle ), graph_buffer_idx ) ) {
                            std_array_push_m ( &graph_buffers_array, xf_graph_buffer_m (
                                .handle = buffer_handle,
                            ) );
                        }

                        uint32_t node_resource_idx = node_resources_array.count;
                        xf_resource_access_e access = xf_resource_access_copy_read_m;
                        std_array_push_m ( &node_resources_array, xf_node_resource_m ( 
                            .type = xf_node_resource_buffer_m, 
                            .stage = xg_pipeline_stage_bit_transfer_m, 
                            .access = access, 
                            .buffer.graph_handle = graph_buffer_idx, 
                        ) );
                        std_array_push_m ( &node_buffers_array, node_resource_idx );
                    }
                }
            }
        }

        node->buffers_count = node_buffers_array.count;
        node->resources_count = node_resources_array.count;
    }

    graph->buffers_count = graph_buffers_array.count;
}

static void xf_graph_scan_multi_buffers ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t buffer_hashes[xf_graph_max_multi_buffers_m * 2];
    std_hash_set_t buffers_set = std_static_hash_set_m ( buffer_hashes );
    std_auto_m multi_buffers_array = std_static_array_m ( uint32_t, graph->multi_buffers_array );

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_buffer_h buffer = graph->buffers_array[i].handle;
        if ( xf_resource_buffer_is_multi ( buffer ) ) {
            buffer = xf_resource_multi_buffer_get_default ( buffer );
            if ( std_hash_set_insert ( &buffers_set, std_hash_64_m ( buffer ) ) ) {
                std_array_push_m ( &multi_buffers_array, i );
            }
        }
    }

    graph->multi_buffers_count = multi_buffers_array.count;
}

static void xf_graph_scan_resources ( xf_graph_h graph_handle ) {
    xf_graph_scan_textures ( graph_handle );
    xf_graph_scan_buffers ( graph_handle );
    xf_graph_scan_multi_textures ( graph_handle );
    xf_graph_scan_multi_buffers ( graph_handle );
}

#if 1
typedef struct {
    uint32_t remap[4];
} xf_graph_export_data_t;

static void xf_graph_update_export_node ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    if ( graph->export_node != xf_null_handle_m ) {
        xf_graph_node_destroy ( graph_handle, graph->export_node );
        graph->export_node = xf_null_handle_m;
    }

    xf_node_h export_source_node = graph->export_source_node;
    if ( export_source_node != xf_null_handle_m ) {
#if 1
        xg_i* xg = std_module_get_m ( xg_module_name_m );

        xf_texture_info_t info;
        xf_resource_texture_get_info ( &info, graph->export_dest );

        xf_graph_export_data_t uniform_data = {
            .remap[0] = graph->export_channels[0],
            .remap[1] = graph->export_channels[1],
            .remap[2] = graph->export_channels[2],
            .remap[3] = graph->export_channels[3],
        };

        xs_i* xs = std_module_get_m ( xs_module_name_m );
        xg_pipeline_state_h pipeline = xs->get_pipeline_state ( xf_graph_state->export_pipeline );
        xg_pipeline_info_t pipe_info;
        xg->get_pipeline_info ( &pipe_info, pipeline );

        xf_node_h export_node_handle = xf_graph_node_create ( graph_handle, &xf_node_params_m (
            .debug_name = "export",
            .type = xf_node_type_compute_pass_m,
            .pass.compute = xf_node_compute_pass_params_m (
                .pipeline = pipeline,
                .workgroup_count = { std_div_ceil_u32 ( info.width, 8 ), std_div_ceil_u32 ( info.height, 8 ), 1 },
                .samplers_count = 1,
                .samplers = { xg->get_default_sampler ( graph->params.device, xg_default_sampler_linear_clamp_m ) },
                .uniform_data = std_buffer_struct_m ( &uniform_data ),
            ),
            .resources = xf_node_resource_params_m (
                .sampled_textures_count = 1,
                .sampled_textures = { xf_compute_texture_dependency_m ( .texture = graph->export_source ) },
                .storage_texture_writes_count = 1,
                .storage_texture_writes = { xf_compute_texture_dependency_m ( .texture = graph->export_dest ) },
            ),
        ) );
#else
        xf_node_h export_node_handle = xf_graph_node_create ( graph_handle, &xf_node_params_m (
            .debug_name = "export",
            .type = xf_node_type_copy_pass_m,
            .pass.copy = xf_node_copy_pass_params_m (
            ),
            .resources = xf_node_resource_params_m (
                .copy_texture_reads_count = 1,
                .copy_texture_reads = {
                    xf_copy_texture_dependency_m ( .texture = graph->export_source )
                },
                .copy_texture_writes_count = 1,
                .copy_texture_writes = {
                    xf_copy_texture_dependency_m ( .texture = graph->export_dest )
                }
            )
        ) );
#endif

        for ( uint32_t i = graph->nodes_count - 1; i >= export_source_node + 2; --i ) {
            graph->nodes_declaration_order[i] = graph->nodes_declaration_order[i - 1];
            graph->nodes_array[i - 1].declaration_order = i;
        }
        graph->nodes_declaration_order[export_source_node + 1] = export_node_handle;
        graph->export_node = export_node_handle;
        graph->nodes_array[export_node_handle].declaration_order = export_source_node + 1;
    }
}
#endif

static void xf_graph_compute_buffer_lifespans ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];

        for ( uint32_t i = 0; i < node->buffers_count; ++i ) {
            uint32_t idx = node->buffers_array[i];
            xf_node_resource_t* resource = &node->resources_array[idx];
            xf_graph_buffer_t* buffer = &graph->buffers_array[resource->buffer.graph_handle];
            xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;
            xf_graph_resource_lifespan_extend ( &buffer->lifespan, node->execution_order, node_queue );
        }
    }
}

static void xf_graph_compute_texture_lifespans ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];

        for ( uint32_t i = 0; i < node->textures_count; ++i ) {
            uint32_t idx = node->textures_array[i];
            xf_node_resource_t* resource = &node->resources_array[idx];
            xf_graph_texture_t* texture = &graph->textures_array[resource->texture.graph_handle];
            xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;
            xf_graph_resource_lifespan_extend ( &texture->lifespan, node->execution_order, node_queue );
        }
    }
}

static void xf_graph_compute_resource_lifespans ( xf_graph_h graph_handle ) {
    xf_graph_compute_texture_lifespans ( graph_handle );
    xf_graph_compute_buffer_lifespans ( graph_handle );
}

static bool xf_graph_texture_params_test ( xg_texture_params_t* t1, xg_texture_params_t* t2 ) {
    bool reusable = 
        t1->memory_type == t2->memory_type &&
        t1->device == t2->device &&
        t1->width == t2->width &&
        t1->height == t2->height &&
        t1->depth == t2->depth &&
        t1->mip_levels == t2->mip_levels &&
        t1->array_layers == t2->array_layers &&
        t1->dimension == t2->dimension &&
        t1->format == t2->format &&
        t1->allowed_usage == t2->allowed_usage;
        t1->samples_per_pixel == t2->samples_per_pixel &&
        t1->tiling == t2->tiling &&
        t1->view_access == t2->view_access;
    return reusable;
}

typedef struct xf_graph_transient_texture_t xf_graph_transient_texture_t;
struct xf_graph_transient_texture_t {
    xf_graph_transient_texture_t* next;
    xf_graph_texture_h handle;
    xg_memory_requirement_t req;
    xg_texture_params_t params;
};

static int xf_graph_transient_texture_sort ( const void* a, const void* b, const void* arg ) {
    std_unused_m ( arg );
    std_auto_m t1 = ( xf_graph_transient_texture_t* ) a;
    std_auto_m t2 = ( xf_graph_transient_texture_t* ) b;
    size_t s1 = t1->req.size;
    size_t s2 = t2->req.size;
    return s1 > s2 ? -1 : s1 < s2 ? 1 : 0;
}

typedef struct {
    xf_physical_texture_h handle;
    xg_memory_requirement_t req;
    xg_texture_params_t params;
    xf_graph_transient_texture_t* transient_textures_list;
    xf_graph_resource_lifespan_t lifespans[xf_graph_max_textures_m];
    uint32_t lifespans_count;
} xf_graph_committed_texture_t;

typedef struct {
    uint64_t begin;
    uint64_t end;
} xf_graph_memory_range_t;

static int xf_graph_memory_range_sort ( const void* a, const void* b, const void* arg ) {
    std_unused_m ( arg );
    std_auto_m r1 = ( xf_graph_memory_range_t* ) a;
    std_auto_m r2 = ( xf_graph_memory_range_t* ) b;
    
    if ( r1->begin > r2->begin ) {
        return 1;
    } else if ( r1->begin < r2->begin ) {
        return -1;
    } else if ( r1->end > r2->end ) {
        return 1;
    } else if ( r1->end < r2->end ) {
        return -1;
    } else {
        return 0;
    }
}

typedef struct {
    xf_graph_committed_texture_t* committed_texture;
    xf_graph_memory_range_t range;
} xf_graph_heap_texture_t;

typedef struct {
    uint64_t size;
    uint32_t textures_count;
    xf_graph_heap_texture_t textures_array[xf_graph_max_textures_m];
} xf_graph_memory_heap_build_t;

static void xf_graph_build_textures ( xf_graph_h graph_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    //
    // 1. Scan textures, separate in transient and permanent
    //
    // Non-transient textures
    xf_graph_texture_t* permanent_textures_array[xf_graph_max_textures_m]; 
    uint32_t permanent_textures_count = 0;
    // Transient textures
    xf_graph_transient_texture_t transient_textures_array[xf_graph_max_textures_m + 1];
    uint32_t transient_textures_count = 0;

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_texture_t* graph_texture = &graph->textures_array[i];
        xf_texture_h texture_handle = graph_texture->handle;
        const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

        if ( xf_resource_texture_is_multi ( texture_handle ) || !texture->params.allow_aliasing ) {
            permanent_textures_array[permanent_textures_count++] = graph_texture;
            continue;
        }

        xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( texture_handle );
        if ( physical_texture ) {
            std_assert_m ( physical_texture->handle != xg_null_handle_m );
            continue;
        }

        xg_texture_params_t params = xf_graph_texture_params ( graph->params.device, graph->textures_array[i].handle );
        transient_textures_array[transient_textures_count] = ( xf_graph_transient_texture_t ) {
            .req = xg->get_texture_memory_requirement ( &params ),
            .params = params,
            .handle = i,
            .next = NULL,
        };

        ++transient_textures_count;
    }

    bool print_lifespans = graph->params.flags & xf_graph_flag_print_resource_lifespan_m;
    if ( print_lifespans ) {
        std_log_info_m ( std_fmt_str_m " texture lifespans:", graph->params.debug_name ); 
        
        for ( uint32_t i = 0; i < transient_textures_count; ++i ) {
            xf_graph_transient_texture_t* transient_texture = &transient_textures_array[i];
            xf_graph_texture_t* graph_texture = &graph->textures_array[transient_texture->handle];
            xf_texture_t* texture = xf_resource_texture_get ( graph_texture->handle );
            std_log_info_m ( std_fmt_tab_m std_fmt_str_m ": " std_fmt_i32_m " -> " std_fmt_i32_m, texture->params.debug_name, graph_texture->lifespan.first, graph_texture->lifespan.last[0] );
        }
    }

    //
    // 2. Sort the transient textures by descending size
    //
    std_sort_insertion ( transient_textures_array, sizeof ( xf_graph_transient_texture_t ), transient_textures_count, xf_graph_transient_texture_sort, NULL, &transient_textures_array[xf_graph_max_textures_m] );

    bool alias_resources = graph->params.flags & xf_graph_flag_alias_resources_m;
    bool alias_memory = graph->params.flags & xf_graph_flag_alias_memory_m;

    //
    // 3. Alias identical transient textures into the same committed texture
    //
    xf_graph_committed_texture_t committed_textures_array[xf_graph_max_textures_m];
    uint32_t committed_textures_count = 0;

    for ( uint32_t i = 0; i < transient_textures_count; ++i ) {
        xf_graph_transient_texture_t* transient_texture = &transient_textures_array[i];
        xf_graph_texture_t* graph_texture = &graph->textures_array[transient_texture->handle];

        xf_graph_committed_texture_t* backing_texture = NULL;
        if ( alias_resources ) {
            for ( uint32_t j = 0; j < committed_textures_count; ++j ) {
                xf_graph_committed_texture_t* committed_texture = &committed_textures_array[j];

                if ( !xf_graph_texture_params_test ( &transient_texture->params, &committed_texture->params ) ) {
                    continue;
                }

                bool overlap = false;
                for ( uint32_t k = 0; k < committed_texture->lifespans_count; ++k ) {
                    if ( xf_graph_lifespan_overlap_test ( graph_handle, &committed_texture->lifespans[k], &graph_texture->lifespan ) == 0 ) {
                        overlap = true;
                        break;
                    }
                }

                if ( !overlap ) {
                    //xf_resource_texture_alias ( graph_texture->handle, committed_texture->handle );
                    //xf_resource_texture_bind ( graph_texture->handle, committed_texture->handle );
                    committed_texture->lifespans[committed_texture->lifespans_count++] = graph_texture->lifespan;
                    transient_texture->next = committed_texture->transient_textures_list;
                    committed_texture->transient_textures_list = transient_texture;
                    backing_texture = committed_texture;
                    break;
                }
            }
        }

        if ( !backing_texture ) {
            xg_texture_params_t params = transient_texture->params;
            std_stack_t stack = std_static_stack_m ( params.debug_name );
            char buffer[16];
            std_u32_to_str ( buffer, 16, committed_textures_count, 0 );
            std_stack_string_append ( &stack, graph->params.debug_name );
            std_stack_string_append ( &stack, " CT" ); // Committed Texture
            std_stack_string_append ( &stack, buffer );

            //xf_texture_t* xf_transient_texture = xf_resource_texture_get ( graph_texture->handle );
            //xf_texture_params_t xf_params = xf_transient_texture->params;
            //std_str_copy_static_m ( xf_params.debug_name, params.debug_name );
            //xf_texture_h xf_handle = xf_resource_texture_create ( &xf_params );
            //xf_physical_texture_params_t physical_texture_params = xf_physical_texture_params_m ( .is_external = false );
            //std_str_copy_static_m ( physical_texture_params.debug_name, params.debug_name );
            //xf_physical_texture_h physical_texture_handle = xf_resource_physical_texture_create ( &physical_texture_params );
            //xf_resource_texture_bind ( graph_texture->handle, physical_texture_handle );

            committed_textures_array[committed_textures_count++] = ( xf_graph_committed_texture_t ) {
                .params = params,
                .req = transient_texture->req,
                //.handle = physical_texture_handle,
                .lifespans_count = 1,
                .lifespans = { graph_texture->lifespan },
                .transient_textures_list = transient_texture,
            };
        }
    }

    // Debug print transient to committed aliasing
    bool print_alias_list = graph->params.flags & xf_graph_flag_print_resource_alias_m;
    if ( print_alias_list ) {
        for ( uint32_t i = 0; i < committed_textures_count; ++i ) {
            xf_graph_committed_texture_t* committed_texture = &committed_textures_array[i];
            std_log_info_m ( std_fmt_str_m ":", committed_texture->params.debug_name );
            
            xf_graph_transient_texture_t* transient_texture = committed_texture->transient_textures_list;
            while ( transient_texture ) {
                xf_graph_texture_t* graph_texture = &graph->textures_array[transient_texture->handle];
                //xf_texture_h texture_handle = graph->textures_array[transient_texture->handle].handle;
                xf_texture_t* texture = xf_resource_texture_get ( graph_texture->handle );
                int32_t last_lifespan = std_max_i32 ( std_max_i32 ( graph_texture->lifespan.last[0], graph_texture->lifespan.last[1] ), graph_texture->lifespan.last[2] );
                std_log_info_m ( std_fmt_tab_m std_fmt_str_m " " std_fmt_i32_m "->" std_fmt_i32_m, texture->params.debug_name, graph_texture->lifespan.first, last_lifespan );
                transient_texture = transient_texture->next;
            }
        }
    }

    //
    // 4. Alias non time overlapping committed textures into the same memory heap.
    //
    //      Given a committed resource r to place into a heap and the list of resources already allocated into the heap:
    //       - gather all resources from the list that lifespan overlap with r
    //       - compute the list of memory ranges that those resources occupy inside the heap
    //       - find a free space to be inside those ranges, if it exists
    //       - if found, end. if not found, grow the end of the heap and place it there.
    //      Once done for all r, allocate the heap and create the resources at the computed offsets
    xf_graph_memory_heap_build_t heap = {
        .size = 0,
        .textures_count = 0,
    };
    for ( uint32_t i = 0; i < committed_textures_count; ++i ) {
        xf_graph_committed_texture_t* texture = &committed_textures_array[i];

        // Collect memory ranges of time overlapping heap textures
        xf_graph_memory_range_t ranges_array[xf_graph_max_textures_m + 1];
        uint32_t ranges_count = 0;
        for ( uint32_t j = 0; j < heap.textures_count; ++j ) {
            xf_graph_heap_texture_t* heap_texture = &heap.textures_array[j];
            xf_graph_committed_texture_t* committed_texture = heap_texture->committed_texture;
            bool overlap = false;
            for ( uint32_t k = 0; k < committed_texture->lifespans_count; ++k ) {
                for ( uint32_t l = 0; l < texture->lifespans_count; ++l ) {
                    if ( xf_graph_lifespan_overlap_test ( graph_handle, &committed_texture->lifespans[k], &texture->lifespans[l] ) == 0 ) {
                        overlap = true;
                        break;
                    }
                }
                if ( overlap ) break;
            }

            if ( overlap ) {
                ranges_array[ranges_count++] = heap_texture->range;
            }
        }

        // Sort the ranges from heap start to end
        std_sort_insertion ( ranges_array, sizeof ( xf_graph_memory_range_t ), ranges_count, xf_graph_memory_range_sort, NULL, &ranges_array[xf_graph_max_textures_m] );

        // Merge the overlapping memory ranges (can happen when two heap textures don't temporally overlap each other but both overlap the new heap texture candidate)
        xf_graph_memory_range_t merged_ranges_array[xf_graph_max_textures_m];
        uint32_t merged_ranges_count = 0;
        if ( ranges_count > 0 ) {
            merged_ranges_array[merged_ranges_count++] = ranges_array[0];
        }

        for ( uint32_t j = 1; j < ranges_count; ++j ) {
            xf_graph_memory_range_t* last = &merged_ranges_array[merged_ranges_count - 1];
            xf_graph_memory_range_t* it = &ranges_array[j];
            if ( last->end >= it->begin ) {
                last->end = std_max_u64 ( last->end, it->end );
            } else {
                merged_ranges_array[merged_ranges_count++] = *it;
            }
        }

        // Best fit allocate the new texture in the heap, using the merged ranges to find the free location
        uint64_t best_fit = -1;
        uint64_t offset = 0;
        xg_memory_requirement_t req = texture->req;
        if ( merged_ranges_count == 0 ) {                       // Special case: empty heap
            if ( heap.size >= req.size ) {
                best_fit = heap.size - req.size;
            }
        } else if ( merged_ranges_array[0].begin > req.size ) { // Special case: heap start
            best_fit = merged_ranges_array[0].begin - req.size;
        } else {                                                // Special case: heap end
            uint64_t end = merged_ranges_array[merged_ranges_count - 1].end;
            uint64_t aligned_end = std_align_u64 ( end, req.align );
            if ( aligned_end + req.size <= heap.size ) {
                best_fit = heap.size - end - req.size;
                offset = aligned_end;
            }
        }
        for ( uint32_t j = 1; j < merged_ranges_count; ++j ) {  // Common case: in between two ranges
            uint64_t begin = merged_ranges_array[j - 1].end;
            uint64_t end = merged_ranges_array[j].begin;
            uint64_t aligned_begin = std_align_u64 ( begin, req.align );
            if ( aligned_begin + req.size <= end ) {
                uint64_t fit = end - begin - req.size;
                if ( fit < best_fit ) {
                    offset = aligned_begin;
                    best_fit = fit;
                }
            }
        }

        // Add the heap texture in the found location or grow the heap to make up space for it
        xf_graph_heap_texture_t heap_texture = { .committed_texture = texture };
        if ( best_fit == -1 || !alias_memory ) {
            uint64_t heap_end = merged_ranges_count > 0 ? merged_ranges_array[merged_ranges_count - 1].end : 0;
            if ( !alias_memory ) {
                heap_end = heap.size;
            }
            uint64_t aligned_begin = std_align_u64 ( heap_end, req.align );
            uint64_t end = aligned_begin + req.size;
            std_assert_m ( end >= heap.size );
            heap.size = end;
            heap_texture.range = ( xf_graph_memory_range_t ) { .begin = aligned_begin, .end = end };
        } else {
            heap_texture.range = ( xf_graph_memory_range_t ) { .begin = offset, .end = offset + req.size };
        }
        heap.textures_array[heap.textures_count++] = heap_texture;
    }

    std_assert_m ( committed_textures_count == heap.textures_count );

    // Alloc the heap and create the actual textures
    uint64_t align = 1;
    for ( uint32_t i = 0; i < committed_textures_count; ++i ) {
        align = std_align_u64 ( align, committed_textures_array[i].req.align );
    }

    xg_alloc_t heap_alloc = xg->alloc_memory ( &xg_alloc_params_m (
        .device = graph->params.device,
        .size = heap.size,
        .align = align, 
        .type = xg_memory_type_gpu_only_m, // TODO
        .debug_name = "xf_heap"
    ) );

    for ( uint32_t i = 0; i < heap.textures_count; ++i ) {
        xf_graph_heap_texture_t* heap_texture = &heap.textures_array[i];
        xf_graph_committed_texture_t* committed_texture = heap_texture->committed_texture;
        committed_texture->params.creation_address.base = heap_alloc.base;
        committed_texture->params.creation_address.offset = heap_alloc.offset + heap_texture->range.begin;
        xg_texture_h xg_handle = xg->cmd_create_texture ( resource_cmd_buffer, &committed_texture->params, NULL );
        xg_texture_info_t texture_info;
        xg->get_texture_info ( &texture_info, xg_handle );
        xf_physical_texture_h physical_texture_handle = xf_resource_physical_texture_create ( &xf_physical_texture_params_m (
            .handle = xg_handle,
            .info = texture_info,
        ) );
        committed_texture->handle = physical_texture_handle;

        xf_graph_transient_texture_t* transient_texture = committed_texture->transient_textures_list;
        while ( transient_texture ) {
            xf_texture_h texture_handle = graph->textures_array[transient_texture->handle].handle;
            xf_resource_texture_bind ( texture_handle, physical_texture_handle );
            transient_texture = transient_texture->next;
        }
    }

    for ( uint32_t i = 0; i < permanent_textures_count; ++i ) {
        xf_graph_texture_t* graph_texture = permanent_textures_array[i];
        xf_texture_h texture_handle = graph_texture->handle;
        xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

        bool create_new = false;

        if ( texture->physical_texture_handle == xg_null_handle_m ) {
            create_new = true;
        } else {
        xf_physical_texture_t* physical_texture = xf_resource_physical_texture_get ( texture->physical_texture_handle );
            if ( texture->required_usage &~ physical_texture->info.allowed_usage ) {
                // TODO free current device texture
                std_log_error_m ( "Texture required and allowed usage mismatch. Fix your usage declaration or implement this code path" );
                std_not_implemented_m();
                create_new = true;
            }
        }

        if ( create_new ) {
            xg_texture_params_t params = xf_graph_texture_params ( graph->params.device, texture_handle );
            xg_texture_h xg_handle = xg->cmd_create_texture ( resource_cmd_buffer, &params, NULL );
            xg_texture_info_t texture_info;
            xg->get_texture_info ( &texture_info, xg_handle );
            xf_physical_texture_h physical_texture_handle = xf_resource_physical_texture_create ( &xf_physical_texture_params_m (
                .handle = xg_handle,
                .info = texture_info,
            ) );
            xf_resource_texture_bind ( texture_handle, physical_texture_handle );

            uint32_t graph_texture_idx = graph_texture - graph->textures_array;
            graph->owned_textures_array[graph->owned_textures_count++] = graph_texture_idx;
        }
    }

    // Make sure all multi textures are backed
    for ( uint32_t i = 0; i < graph->multi_textures_count; ++i ) {
        xf_graph_texture_t* graph_texture = &graph->textures_array[graph->multi_textures_array[i]];
        xf_multi_texture_t* multi_texture = xf_resource_multi_texture_get ( graph_texture->handle );

        for ( uint32_t j = 0; j < multi_texture->params.multi_texture_count; ++j ) {
            xf_texture_h texture_handle = multi_texture->textures[j];
            xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
            if ( texture->physical_texture_handle == xg_null_handle_m ) {
                xg_texture_params_t params = xf_graph_texture_params ( graph->params.device, texture_handle );
                xg_texture_h xg_handle = xg->cmd_create_texture ( resource_cmd_buffer, &params, NULL );
                xg_texture_info_t texture_info;
                xg->get_texture_info ( &texture_info, xg_handle );
                xf_physical_texture_h physical_texture_handle = xf_resource_physical_texture_create ( &xf_physical_texture_params_m (
                    .handle = xg_handle,
                    .info = texture_info,
                ) );
                xf_resource_texture_bind ( texture_handle, physical_texture_handle );

                uint32_t graph_texture_idx = graph_texture - graph->textures_array;
                graph->owned_textures_array[graph->owned_textures_count++] = graph_texture_idx;
            }
        }
    }

    // Store the heap info into the graph for later release
    graph->heap.memory_handle = heap_alloc.handle;
    for ( uint32_t i = 0; i < heap.textures_count; ++i ) {
        graph->heap.textures_array[graph->heap.textures_count++] = heap.textures_array[i].committed_texture->handle;
    }
}

// TODO remove this, use xg init params
static uint64_t xf_graph_clear_owned_textures ( xf_graph_h graph_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, uint64_t key ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    uint64_t transition_key = ++key;
    uint64_t clear_key = ++key;

    xg_texture_memory_barrier_t texture_barriers_array[128]; // TODO
    std_stack_t texture_barriers_stack = std_static_stack_m ( texture_barriers_array );

    for ( uint32_t i = 0; i < graph->owned_textures_count; ++i ) {
        xf_graph_texture_t* graph_texture = &graph->textures_array[graph->owned_textures_array[i]];
        xf_texture_h texture_handle = graph_texture->handle;
        xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
        if ( texture->params.clear_on_create ) {
            xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( texture_handle );
            xg_texture_h xg_handle = physical_texture->handle;

            if ( physical_texture->info.view_access == xg_texture_view_access_default_only_m ) {
                xf_graph_texture_transition_t transition = xf_graph_texture_transition_m (
                    .state = xf_texture_execution_state_m (
                        .layout = xg_texture_layout_copy_dest_m,
                        .stage = xg_pipeline_stage_bit_transfer_m,
                        .access = xg_memory_access_bit_transfer_write_m,
                    ),
                    .texture = texture_handle,
                    .view = xg_texture_view_m()
                );

                xf_resource_texture_state_barrier ( &texture_barriers_stack, transition.texture, transition.view, &transition.state );
            } else {
                std_not_implemented_m();
            }

            if ( physical_texture->info.flags & xg_texture_flag_bit_depth_texture_m ) {
                xg->cmd_clear_depth_stencil_texture ( cmd_buffer, clear_key, xg_handle, texture->params.clear.depth_stencil );
            } else {
                xg->cmd_clear_texture ( cmd_buffer, clear_key, xg_handle, texture->params.clear.color );
            }
        }
    }

    xg_barrier_set_t barrier_set = xg_barrier_set_m();
    barrier_set.texture_memory_barriers = texture_barriers_array;
    barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
    xg->cmd_barrier_set ( cmd_buffer, transition_key, &barrier_set );

    return key;
}

static void xf_graph_build_buffers ( xf_graph_h graph_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_buffer_h buffer_handle = graph->buffers_array[i].handle;
        const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

        bool create_new = false;

        if ( buffer->xg_handle == xg_null_handle_m ) {
            create_new = true;
        } else {
            if ( buffer->required_usage &~ buffer->allowed_usage ) {
                // TODO free current device texture
                std_log_error_m ( "Buffer required and allowed usage mismatch. Fix your usage declaration or implement this code path" );
                std_not_implemented_m();
                create_new = true;
            }
        }

        if ( create_new ) {
            xg_buffer_params_t params = xf_graph_buffer_params ( graph->params.device, buffer_handle );
            xg_buffer_h new_buffer;
            if ( buffer->params.upload ) {
                // If tagged as upload the buffer needs to be available immediately
                new_buffer = xg->create_buffer ( &params );
            } else {
                new_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &params, NULL );
            }
            xf_resource_buffer_map_to_new ( buffer_handle, new_buffer, params.allowed_usage );
        }
    }

    // Make sure all multi buffers are backed
    for ( uint32_t i = 0; i < graph->multi_buffers_count; ++i ) {
        xf_graph_buffer_t* graph_buffer = &graph->buffers_array[graph->multi_buffers_array[i]];
        xf_multi_buffer_t* multi_buffer = xf_resource_multi_buffer_get ( graph_buffer->handle );

        for ( uint32_t j = 0; j < multi_buffer->params.multi_buffer_count; ++j ) {
            xf_buffer_h buffer_handle = multi_buffer->buffers[j];
            xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
            if ( buffer->xg_handle == xg_null_handle_m ) {
                xg_buffer_params_t params = xf_graph_buffer_params ( graph->params.device, buffer_handle );
                xg_buffer_h xg_handle = xg->cmd_create_buffer ( resource_cmd_buffer, &params, NULL );
                xf_resource_buffer_map_to_new ( buffer_handle, xg_handle, params.allowed_usage );
            }
        }
    }
}

// TODO remove this, use xg init params
static uint64_t xf_graph_clear_buffers ( xf_graph_h graph_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, uint64_t key ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    uint64_t transition_key = ++key;
    uint64_t clear_key = ++key;

    xg_buffer_memory_barrier_t buffer_barriers_array[128]; // TODO
    std_stack_t buffer_barriers_stack = std_static_stack_m ( buffer_barriers_array );

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_graph_buffer_t* graph_buffer = &graph->buffers_array[i];
        xf_buffer_h buffer_handle = graph_buffer->handle;
        xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
        if ( buffer->params.clear_on_create ) {
            xg_texture_h xg_handle = buffer->xg_handle;

            xf_graph_buffer_transition_t transition = xf_graph_buffer_transition_m (
                .state = xf_buffer_execution_state_m (
                    .stage = xg_pipeline_stage_bit_transfer_m,
                    .access = xg_memory_access_bit_transfer_write_m,
                ),
                .buffer = buffer_handle,
            );
            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, transition.buffer, &transition.state );
            xg->cmd_clear_buffer ( cmd_buffer, clear_key, xg_handle, buffer->params.clear_value );
        }
    }

    xg_barrier_set_t barrier_set = xg_barrier_set_m();
    barrier_set.buffer_memory_barriers = buffer_barriers_array;
    barrier_set.buffer_memory_barriers_count = std_stack_array_count_m ( &buffer_barriers_stack, xg_buffer_memory_barrier_t );
    xg->cmd_barrier_set ( cmd_buffer, transition_key, &barrier_set );

    return key;
}

#if 0
static void xf_graph_build_buffer ( xf_buffer_h buffer_handle, xg_device_h device, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

    bool create_new = false;

    if ( buffer->xg_handle == xg_null_handle_m ) {
        create_new = true;
    }

    if ( buffer->required_usage &~ buffer->allowed_usage ) {
        create_new = true;
    }

#if 0
    if ( buffer->params.upload ) {
        create_new = true;
    }
#endif

    if ( create_new ) {
        if ( buffer->xg_handle != xg_null_handle_m && !buffer->params.upload ) {
            std_assert_m ( false );
            //xg->cmd_destroy_buffer ( resource_cmd_buffer, buffer->xg_handle, xg_resource_cmd_buffer_time_workload_start_m );
        }

#if 0
        xg_buffer_params_t params = xg_buffer_params_m (
            .memory_type = buffer->params.upload ? xg_memory_type_upload_m : xg_memory_type_gpu_only_m, // TODO readback ?
            .device = device,
            .size = buffer->params.size,
            .allowed_usage = buffer->required_usage,
        );
        std_str_copy_static_m ( params.debug_name, buffer->params.debug_name );
#else
        xg_buffer_params_t params = xf_graph_buffer_params ( device, buffer_handle );
#endif

#if 0
        xg_buffer_h new_buffer;
        // TODO use a preallocated ring buffer?
        if ( buffer->params.upload ) {
            // TODO always do non-cmd create?
            new_buffer = xg->create_buffer ( &params );
            xg->cmd_destroy_buffer ( resource_cmd_buffer, new_buffer, xg_resource_cmd_buffer_time_workload_complete_m );
        } else {
            new_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &params );
        }
#else
        xg_buffer_h new_buffer;
        if ( buffer->params.upload ) {
            // If tagged as upload the buffer needs to be available immediately
            new_buffer = xg->create_buffer ( &params );
        } else {
            new_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &params, NULL );
        }
#endif

        xf_resource_buffer_map_to_new ( buffer_handle, new_buffer, params.allowed_usage );
    }

    // TODO
    std_unused_m ( cmd_buffer );
}
#endif

static void xf_graph_build_resources ( xf_graph_h graph_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xf_graph_build_textures ( graph_handle, xg, cmd_buffer, resource_cmd_buffer );
    xf_graph_build_buffers ( graph_handle, xg, cmd_buffer, resource_cmd_buffer );

    // TODO
    //xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    //for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
    //    xf_graph_build_buffer ( graph->buffers_array[i].handle, graph->params.device, xg, cmd_buffer, resource_cmd_buffer );
    //}
}

static void xf_graph_build_renderpasses ( xf_graph_t* graph, xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_node_t* node = &graph->nodes_array[node_handle];

        xg_render_textures_layout_t render_textures = xg_render_textures_layout_m();
        uint32_t resolution_x = 0;
        uint32_t resolution_y = 0;

        // check node render targets
        for ( size_t resource_it = 0; resource_it < node->params.resources.render_targets_count; ++resource_it ) {
            xf_render_target_dependency_t* resource = &node->params.resources.render_targets[resource_it];
            const xf_texture_t* texture = xf_resource_texture_get ( resource->texture );

            uint32_t mip_level = resource->view.mip_base;
            uint32_t width = texture->params.width / ( 1 << mip_level );
            uint32_t height = texture->params.height / ( 1 << mip_level );

            if ( resolution_x != 0 ) {
                std_assert_m ( resolution_x == width );
            }

            if ( resolution_y != 0 ) {
                std_assert_m ( resolution_y == height );
            }

            resolution_x = width;
            resolution_y = height;

            uint32_t j = render_textures.render_targets_count++;
            render_textures.render_targets[j].slot = resource_it;
            render_textures.render_targets[j].format = texture->params.format;
            render_textures.render_targets[j].samples_per_pixel = texture->params.samples_per_pixel;
        }

        // check node depth stencil
        {
            xf_texture_h depth_stencil = node->params.resources.depth_stencil_target;
            render_textures.depth_stencil_enabled = depth_stencil != xf_null_handle_m;

            if ( depth_stencil != xf_null_handle_m ) {
                const xf_texture_t* texture = xf_resource_texture_get ( depth_stencil );

                uint32_t width = texture->params.width;
                uint32_t height = texture->params.height;

                if ( resolution_x != 0 ) {
                    std_assert_m ( resolution_x == width );
                }

                if ( resolution_y != 0 ) {
                    std_assert_m ( resolution_y == height );
                }

                resolution_x = width;
                resolution_y = height;

                render_textures.depth_stencil.format = texture->params.format;
                render_textures.depth_stencil.samples_per_pixel = texture->params.samples_per_pixel;
            }
        }

        // determine if renderpass update is needed
        bool need_update = false;
        if ( render_textures.render_targets_count > 0 || render_textures.depth_stencil_enabled ) {
            need_update = node->renderpass == xg_null_handle_m;
            need_update |= resolution_x != node->renderpass_params.resolution_x;
            need_update |= resolution_y != node->renderpass_params.resolution_y;
            need_update |= render_textures.render_targets_count != node->renderpass_params.render_textures.render_targets_count;
            need_update |= render_textures.depth_stencil_enabled != node->renderpass_params.render_textures.depth_stencil_enabled;

            if ( !need_update && render_textures.depth_stencil_enabled && ( 
                render_textures.depth_stencil.format != node->renderpass_params.render_textures.depth_stencil.format 
                || render_textures.depth_stencil.samples_per_pixel != node->renderpass_params.render_textures.depth_stencil.samples_per_pixel 
            ) ) {
                need_update |= render_textures.depth_stencil.format != node->renderpass_params.render_textures.depth_stencil.format;
                need_update |= render_textures.depth_stencil.samples_per_pixel != node->renderpass_params.render_textures.depth_stencil.samples_per_pixel;
            }

            if ( !need_update ) {
                for ( uint32_t j = 0; j < render_textures.render_targets_count; ++j ) {
                    if ( 
                        render_textures.render_targets[j].slot != node->renderpass_params.render_textures.render_targets[j].slot 
                        || render_textures.render_targets[j].format != node->renderpass_params.render_textures.render_targets[j].format 
                        || render_textures.render_targets[j].samples_per_pixel != node->renderpass_params.render_textures.render_targets[j].samples_per_pixel 
                    ) {
                        need_update = true;
                        break;
                    }
                }
            }
        }

        // update renderpass
        if ( need_update ) {
            if ( node->renderpass != xg_null_handle_m ) {
                xg->cmd_destroy_renderpass ( resource_cmd_buffer, node->renderpass, xg_resource_cmd_buffer_time_workload_start_m );
            }

            xg_renderpass_params_t params = xg_renderpass_params_m (
                .device = graph->params.device,
                .render_textures = render_textures,
                .resolution_x = resolution_x,
                .resolution_y = resolution_y,
            );
            std_str_copy_static_m ( params.debug_name, node->params.debug_name );
            node->renderpass = xg->create_renderpass ( &params );

            node->renderpass_params.render_textures = render_textures;
            node->renderpass_params.resolution_x = resolution_x;
            node->renderpass_params.resolution_y = resolution_y;
        }
    }
}

static void xf_graph_add_resource_refs ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_resource_texture_add_ref ( graph->textures_array[i].handle );
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_resource_buffer_add_ref ( graph->buffers_array[i].handle );
    }
}

static void xf_graph_remove_resource_refs ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_resource_texture_remove_ref ( graph->textures_array[i].handle );
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_resource_buffer_remove_ref ( graph->buffers_array[i].handle );
    }
}

static void xf_graph_compute_segments ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xf_graph_segment_t* segments_array = graph->segments_array;
    uint32_t segments_count = 0;
    xf_graph_segment_t segment = xf_graph_segment_m( .begin = 0 );

    for ( uint32_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        uint32_t node_idx = graph->nodes_execution_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_idx];
        xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;

        bool cross_queue_dep = false;
        for ( xg_cmd_queue_e queue_it = 0; queue_it < xg_cmd_queue_count_m; ++queue_it ) {
            if ( queue_it != node_queue && graph->cross_queue_node_deps[node_it][queue_it] != -1 ) {
                cross_queue_dep = true;
            }
        }

        if ( node_it == 0 ) {
            segment.queue = node_queue;
        } else if ( node_queue != segment.queue || cross_queue_dep ) {
            segment.end = node_it - 1;
            segments_array[segments_count++] = segment;
        
            segment = xf_graph_segment_m ( .begin = node_it, .queue = node_queue );
        }

        node->segment = segments_count;
    }

    segment.end = graph->nodes_count - 1;
    segments_array[segments_count++] = segment;

    graph->segments_count = segments_count;

    // dependencies
    for ( uint32_t i = 0; i < segments_count; ++i ) {
        xf_graph_segment_t* segment = &graph->segments_array[i];
        for ( uint32_t j = segment->begin; j <= segment->end; ++j ) {
            for ( xg_cmd_queue_e q = xg_cmd_queue_graphics_m; q < xg_cmd_queue_count_m; ++q ) {
                int32_t prev = graph->cross_queue_node_deps[j][q];
                if ( prev != -1 ) {
                    uint32_t prev_idx = graph->nodes_execution_order[prev];
                    xf_node_t* prev_node = &graph->nodes_array[prev_idx];
                    if ( prev_node->segment != i ) {
                        xf_graph_segment_t* prev_segment = &graph->segments_array[prev_node->segment];
                        if ( !prev_segment->is_depended[segment->queue] ) {
                            segment->deps[segment->deps_count++] = prev_node->segment;
                            prev_segment->is_depended[segment->queue] = true;
                        }
                    }
                }
            }
        }
    }
}

static void xf_graph_create_segment_events ( xf_graph_h graph_handle, xg_i* xg ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    
    for ( uint32_t i = 0; i < graph->segments_count; ++i ) {
        xf_graph_segment_t* segment = &graph->segments_array[i];
        for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; ++q ) {
            xg_queue_event_h event = xg_null_handle_m;
            if ( segment->is_depended[q] ) {
                event = xg->create_queue_event ( &xg_queue_event_params_m ( .device = graph->params.device, .debug_name = "graph_event" ) );
            }
            segment->events[q] = event;
        }
    }
}

void xf_graph_print ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    char stack_buffer[2048];
    std_stack_t stack = std_static_stack_m ( stack_buffer );
    std_stack_string_append ( &stack, graph->params.debug_name );
    std_stack_string_append ( &stack, ":\n" );
    std_stack_string_append ( &stack, "DEPENDENCIES" std_fmt_tab_m std_fmt_tab_m "| EXECUTION ORDER" std_fmt_tab_m std_fmt_tab_m "| SEGMENTS""\n" );
    std_stack_string_append ( &stack, "GRAPH" );
    std_stack_string_append ( &stack, std_fmt_tab_m );
    std_stack_string_append ( &stack, "COMP" );
    std_stack_string_append ( &stack, std_fmt_tab_m );
    std_stack_string_append ( &stack, "COPY" );
    std_stack_string_append ( &stack, "\n" );

    uint32_t prev_segment_idx = -1;
    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        // deps table
        for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; ++q ) {
            char buffer[32] = " -";
            if ( graph->cross_queue_node_deps[i][q] != -1 ) {
                std_u32_to_str ( buffer, 32, graph->cross_queue_node_deps[i][q], 2 );
            }
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, std_fmt_tab_m );
        }
        // exec order
        uint32_t node_idx = graph->nodes_execution_order[i];
        xf_node_t* node = &graph->nodes_array[node_idx];
        char buffer[32] = " -";
        std_u32_to_str ( buffer, 32, i, 2 );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, " - " );
        std_stack_string_append ( &stack, node->params.debug_name );
        std_stack_string_append ( &stack, " (" );
        char* queue_name = "";
        xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;
        xg_cmd_queue_e q = node_queue;
        if ( q == xg_cmd_queue_graphics_m ) queue_name = "GRAPH";
        else if ( q == xg_cmd_queue_compute_m ) queue_name = "COMP";
        else if ( q == xg_cmd_queue_copy_m ) queue_name = "COPY";
        else std_not_implemented_m();
        std_stack_string_append ( &stack, queue_name );
        std_stack_string_append ( &stack, ")" );
        // segment
        uint32_t exec_order_len = std_str_len ( node->params.debug_name ) + std_str_len ( queue_name ) + 5 + 3;
        uint32_t segment_idx = node->segment;
        for ( uint32_t j = exec_order_len; j < 32; j += 8 ) {
            std_stack_string_append ( &stack, std_fmt_tab_m );
        }
        if ( prev_segment_idx != segment_idx ) {
            prev_segment_idx = segment_idx;
            xf_graph_segment_t* segment = &graph->segments_array[segment_idx];
            char buffer[32];
            std_u32_to_str ( buffer, 32, segment_idx, 2 );
            std_stack_string_append ( &stack, "--" );
            std_stack_string_append ( &stack, buffer );
            for ( uint32_t j = 0; j < segment->deps_count; ++j ) {
                if ( j == 0 ) {
                    std_stack_string_append ( &stack, " (" );
                }
                char buffer[32];
                std_u32_to_str ( buffer, 32, segment->deps[j], 0 );
                std_stack_string_append ( &stack, buffer );
                if ( j == segment->deps_count - 1 ) {
                    std_stack_string_append ( &stack, ")" );
                } else {
                    std_stack_string_append ( &stack, ", " );
                }
            }
        } else {
            std_stack_string_append ( &stack, "| " );
        }
        // end
        if ( i < graph->nodes_count - 1 ) {
            std_stack_string_append ( &stack, "\n" );
        }
    }
    std_log_info_m ( std_fmt_str_m, stack_buffer );
}

void xf_graph_invalidate ( xf_graph_h graph_handle, xg_workload_h workload ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        if ( node->renderpass != xg_null_handle_m ) {
            xg->cmd_destroy_renderpass ( resource_cmd_buffer, node->renderpass, xg_resource_cmd_buffer_time_workload_complete_m );
            node->renderpass = xg_null_handle_m;
        }
    }

    for ( uint32_t i = 0; i < graph->segments_count; ++i ) {
        for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; ++q ) {
            xg_queue_event_h event = graph->segments_array[i].events[q];
            if ( event != xg_null_handle_m ) {
                xg->cmd_destroy_queue_event ( resource_cmd_buffer, event, xg_resource_cmd_buffer_time_workload_complete_m );
                graph->segments_array[i].events[q] = xg_null_handle_m;
            }
        }
    }

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_texture_h texture_handle = graph->textures_array[i].handle;
        xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( texture_handle );
        if ( physical_texture && !physical_texture->is_external ) {
            xf_resource_texture_unbind ( graph->textures_array[i].handle );
        }
    }

    xf_resource_destroy_unreferenced ( xg, resource_cmd_buffer, xg_resource_cmd_buffer_time_workload_complete_m );

    if ( !xg_memory_handle_is_null_m ( graph->heap.memory_handle ) ) {
        xg->free_memory ( graph->heap.memory_handle );
        graph->heap.memory_handle = xg_null_memory_handle_m;
    }

    graph->is_finalized = false;
    graph->is_built = false;
    graph->textures_count = 0;
    graph->buffers_count = 0;
    graph->physical_textures_count = 0;
    graph->multi_textures_count = 0;
    graph->multi_buffers_count = 0;
    graph->owned_textures_count = 0;
    graph->segments_count = 0;

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        node->next_nodes_count = 0;
        node->prev_nodes_count = 0;
        node->resources_count = 0;
        node->textures_count = 0;
        node->buffers_count = 0;
        node->texture_transitions.count = 0;
        node->texture_acquires.count = 0;
        node->texture_releases.count = 0;
        node->buffer_transitions.count = 0;
        node->buffer_releases.count = 0;
    }

    std_virtual_stack_clear ( &graph->resource_dependencies_allocator );
    std_virtual_stack_clear ( &graph->physical_resource_dependencies_allocator );

    graph->is_invalidated = true;
}

//
// Graph Finalize
//      - Scan all graph resources and store them in separate arrays
//      - Preallocate space for storing subresource dependencies
//      - For each resource, build a list of usages and node dependencies
//      - Using the resource to node dependencies, build node to node dependencies for each node
//      - Using the node to node dependencies, linearize the graph
//      - Compute resource lifespans (first to last usage on the linearized graph)
//      - Compute graph segments (chunks of same-queue nodes)
//      - Bump xf resource refs (skip if invalidated, because that doesn't remove previous refs)
//
void xf_graph_finalize ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xf_graph_scan_resources ( graph_handle );
    xf_graph_allocate_texture_dependencies ( graph_handle );
    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes_declaration_order[i];
        xf_graph_node_accumulate_resource_usage ( graph_handle, node_handle );
        xf_graph_node_accumulate_dependencies ( graph_handle, node_handle );
    }
    xf_graph_accumulate_node_dependencies ( graph_handle );
    xf_graph_linearize ( graph_handle );
    xf_graph_compute_resource_lifespans ( graph_handle );
    xf_graph_compute_segments ( graph_handle );    
    if ( !graph->is_invalidated ) {
        xf_graph_add_resource_refs ( graph_handle );
    }

    if ( graph->params.flags & xf_graph_flag_print_execution_order_m ) {
        xf_graph_print ( graph_handle );
    }
    graph->is_finalized = true;
}

//
// Graph Build
//      - Build physical resources (compute aliasing, allocate, ...)
//      - Record clear commands for non transient textures that were flagged for clear at creation time
//      - Create queue events for the graph segments
//
uint64_t xf_graph_build ( xf_graph_h graph_handle, xg_workload_h workload, uint64_t key ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xf_graph_build_resources ( graph_handle, xg, cmd_buffer, resource_cmd_buffer );
    key = xf_graph_clear_owned_textures ( graph_handle, xg, cmd_buffer, key );
    key = xf_graph_clear_buffers ( graph_handle, xg, cmd_buffer, key );
    xf_graph_create_segment_events ( graph_handle, xg );

    graph->is_built = true;
    graph->is_invalidated = false;
    return key;
}

static void xf_graph_remap_upload_buffers ( xf_graph_h graph_handle, xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_buffer_h buffer_handle = graph->buffers_array[i].handle;
        const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
        xg_buffer_params_t params = xf_graph_buffer_params ( graph->params.device, buffer_handle );
        
        if ( buffer->params.upload ) {
            xg_buffer_h new_buffer = xg->create_buffer ( &params );
            xg->cmd_destroy_buffer ( resource_cmd_buffer, new_buffer, xg_resource_cmd_buffer_time_workload_complete_m );
            xf_resource_buffer_map_to_new ( buffer_handle, new_buffer, params.allowed_usage );
        }
    }
}

static void xf_graph_clear_physical_resources ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        node->texture_transitions.count = 0;
        node->texture_releases.count = 0;
        node->texture_acquires.count = 0;
        node->buffer_transitions.count = 0;
        node->buffer_releases.count = 0;
    }

    graph->physical_textures_count = 0;

    std_virtual_stack_clear ( &graph->physical_resource_dependencies_allocator );
}

static uint64_t xf_graph_prepare_for_execute ( xf_graph_h graph_handle, xg_i* xg, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, uint64_t key ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    //
    // 1. Finalize the graph
    //      This scans the graph resources, computes dependencies and linearizes the graph into segments
    //
    if ( !graph->is_finalized ) {
        xf_graph_update_export_node ( graph_handle );
        xf_graph_finalize ( graph_handle );
    }

    //
    // 2. Build the graph
    //      This allocates the physical textures and queue events, and records clear commands
    //
    if ( !graph->is_built ) {
        key = xf_graph_build ( graph_handle, workload, key );
    }

    //
    // 3. Prepare for execute
    //      - Clear the physical resources arrays and their dependencies
    //      - Scan the resources that will be used on this next execute and store their physical resources in arrays
    //      - Build a list of resource to node dependencies for each stored physical resource
    //      - Using that list, compute the physical resource transitions that need to be dispatched between nodes
    //
    //      This is ran again before every execute. The main reason is multi textures, since their effective
    //      physical texture handle potentially changes on every execute.
    //
    xf_graph_clear_physical_resources ( graph_handle );
    xf_graph_scan_physical_textures ( graph_handle );
    xf_graph_accumulate_physical_resources_dependencies ( graph_handle );
    xf_graph_compute_resource_transitions ( graph_handle );

    xf_graph_remap_upload_buffers ( graph_handle, xg, resource_cmd_buffer );

    return key;
}

typedef struct {
    xg_i* xg;
    const xf_node_params_t* params;
} xf_graph_clear_pass_routine_args_t;

void xf_graph_clear_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( const xf_graph_clear_pass_routine_args_t* ) user_args;
    
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = pass_args->xg;
    const xf_node_resource_params_t* resources = &pass_args->params->resources;

    for ( uint32_t i = 0; i < resources->copy_texture_writes_count; ++i ) {
        xg_texture_h texture = node_args->io->copy_texture_writes[i].texture;
        const xf_texture_clear_t* clear = &pass_args->params->pass.clear.textures[i];
        
        if ( clear->type == xf_texture_clear_color_m ) {
            xg->cmd_clear_texture ( cmd_buffer, key, texture, clear->color );
        } else if ( clear->type == xf_texture_clear_depth_stencil_m ) {
            xg->cmd_clear_depth_stencil_texture ( cmd_buffer, key, texture, clear->depth_stencil );
        } else {
            std_assert_m ( false );
        }
    }
}

typedef struct {
    xg_i* xg;
    const xf_node_params_t* params;
} xf_graph_copy_pass_routine_args_t;

void xf_graph_copy_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( const xf_graph_copy_pass_routine_args_t* ) user_args;
    
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = pass_args->xg;
    const xf_node_resource_params_t* resources = &pass_args->params->resources;

    std_assert_m ( resources->copy_texture_reads_count == resources->copy_texture_writes_count );

    for ( uint32_t i = 0; i < resources->copy_texture_writes_count; ++i ) {
        xf_copy_texture_resource_t src = node_args->io->copy_texture_reads[i];
        xf_copy_texture_resource_t dst = node_args->io->copy_texture_writes[i];
        xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
            .source = xf_copy_texture_resource_m ( src ),
            .destination = xf_copy_texture_resource_m ( dst ),
            //.filter = xg_sampler_filter_linear_m, // TODO use linear for not same-size textures? expose a param?
            .filter = pass_args->params->pass.copy.filter,
            .mip_count = std_min_u32 ( src.view.mip_count, dst.view.mip_count ),
        );
        xg->cmd_copy_texture ( cmd_buffer, key, &copy_params );
    }
}

typedef struct {
    xg_i* xg;
    xg_compute_pipeline_state_h pipeline;
    uint32_t workgroup_count[3];
    std_buffer_t uniform_data;
    const xf_node_params_t* params;
} xf_graph_compute_pass_routine_args_t;

void xf_graph_compute_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( const xf_graph_compute_pass_routine_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = pass_args->xg;

    xg_resource_bindings_layout_h layout = xg->get_pipeline_resource_layout ( pass_args->pipeline, xg_shader_binding_set_dispatch_m );
    const xf_node_resource_params_t* resources = &pass_args->params->resources;

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .texture_count = resources->sampled_textures_count + resources->storage_texture_reads_count + resources->storage_texture_writes_count,
        .buffer_count = resources->uniform_buffers_count + resources->storage_buffer_reads_count + resources->storage_buffer_writes_count + ( pass_args->uniform_data.base ? 1 : 0 ),
        .sampler_count = pass_args->params->pass.compute.samplers_count,
    );

    uint32_t binding_id = 0;
    uint32_t buffer_idx = 0;
    uint32_t texture_idx = 0;

    if ( pass_args->uniform_data.base ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m (
            .shader_register = binding_id++,
            .range = xg->write_workload_uniform ( node_args->workload, pass_args->uniform_data.base, pass_args->uniform_data.size ),
        );
    }

    for ( uint32_t i = 0; i < resources->uniform_buffers_count; ++i ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m ( 
            .shader_register = binding_id++,
            .range = xg_buffer_range_whole_buffer_m ( node_args->io->uniform_buffers[i] )
        );
    }

    for ( uint32_t i = 0; i < resources->storage_buffer_reads_count; ++i ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m ( 
            .shader_register = binding_id++,
            .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[i] )
        );
    }

    for ( uint32_t i = 0; i < resources->storage_buffer_writes_count; ++i ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m ( 
            .shader_register = binding_id++,
            .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_writes[i] )
        );
    }

    for ( uint32_t i = 0; i < resources->sampled_textures_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->sampled_textures[i], binding_id++ );
    }

    for ( uint32_t i = 0; i < resources->storage_texture_reads_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->storage_texture_reads[i], binding_id++ );
    }

    for ( uint32_t i = 0; i < resources->storage_texture_writes_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->storage_texture_writes[i], binding_id++ );
    }

    for ( uint32_t i = 0; i < pass_args->params->pass.compute.samplers_count; ++i ) {
        draw_bindings.samplers[i] = xg_sampler_resource_binding_m ( 
            .shader_register = binding_id++,
            .sampler = pass_args->params->pass.compute.samplers[i]
        );
    }

    xg_resource_bindings_h group = xg->cmd_create_workload_bindings ( node_args->resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = layout,
        .bindings = draw_bindings
    ) );

    uint32_t workgroup_count_x = pass_args->params->pass.compute.workgroup_count[0];
    uint32_t workgroup_count_y = pass_args->params->pass.compute.workgroup_count[1];
    uint32_t workgroup_count_z = pass_args->params->pass.compute.workgroup_count[2];

    xg->cmd_compute ( cmd_buffer, key, &xg_cmd_compute_params_m ( 
        .pipeline = pass_args->pipeline,
        .bindings = { xg_null_handle_m, xg_null_handle_m, xg_null_handle_m, group },
        .workgroup_count_x = workgroup_count_x,
        .workgroup_count_y = workgroup_count_y,
        .workgroup_count_z = workgroup_count_z,
    ) );
}

typedef struct {
    xg_i* xg;
    xg_raytrace_pipeline_state_h pipeline;
    uint32_t thread_count[3];
    std_buffer_t uniform_data;
    const xf_node_params_t* params;
} xf_graph_raytrace_pass_routine_args_t;

void xf_graph_raytrace_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( const xf_graph_raytrace_pass_routine_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = pass_args->xg;

    xg_resource_bindings_layout_h layout = xg->get_pipeline_resource_layout ( pass_args->pipeline, xg_shader_binding_set_dispatch_m );
    const xf_node_resource_params_t* resources = &pass_args->params->resources;

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .texture_count = resources->sampled_textures_count + resources->storage_texture_reads_count + resources->storage_texture_writes_count,
        .buffer_count = resources->uniform_buffers_count + resources->storage_buffer_reads_count + resources->storage_buffer_writes_count + ( pass_args->uniform_data.base ? 1 : 0 ),
        .sampler_count = pass_args->params->pass.raytrace.samplers_count,
        .raytrace_world_count = pass_args->params->pass.raytrace.raytrace_worlds_count,
    );

    uint32_t binding_id = 0;
    uint32_t buffer_idx = 0;
    uint32_t texture_idx = 0;

    for ( uint32_t i = 0; i < pass_args->params->pass.raytrace.raytrace_worlds_count; ++i ) {
        draw_bindings.raytrace_worlds[i] = xg_raytrace_world_resource_binding_m (
            .shader_register = binding_id++,
            .world = pass_args->params->pass.raytrace.raytrace_worlds[i],
        );
    }

    if ( pass_args->uniform_data.base ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m (
            .shader_register = binding_id++,
            .range = xg->write_workload_uniform ( node_args->workload, pass_args->uniform_data.base, pass_args->uniform_data.size ),
        );
    }

    for ( uint32_t i = 0; i < resources->uniform_buffers_count; ++i ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m ( 
            .shader_register = binding_id++,
            .range = xg_buffer_range_whole_buffer_m ( node_args->io->uniform_buffers[i] )
        );
    }

    for ( uint32_t i = 0; i < resources->storage_buffer_reads_count; ++i ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m ( 
            .shader_register = binding_id++,
            .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[i] )
        );
    }

    for ( uint32_t i = 0; i < resources->storage_buffer_writes_count; ++i ) {
        draw_bindings.buffers[buffer_idx++] = xg_buffer_resource_binding_m ( 
            .shader_register = binding_id++,
            .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_writes[i] )
        );
    }

    for ( uint32_t i = 0; i < resources->sampled_textures_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->sampled_textures[i], binding_id++ );
    }

    for ( uint32_t i = 0; i < resources->storage_texture_reads_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->storage_texture_reads[i], binding_id++ );
    }

    for ( uint32_t i = 0; i < resources->storage_texture_writes_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->storage_texture_writes[i], binding_id++ );
    }

    for ( uint32_t i = 0; i < pass_args->params->pass.raytrace.samplers_count; ++i ) {
        draw_bindings.samplers[i] = xg_sampler_resource_binding_m ( 
            .shader_register = binding_id++,
            .sampler = pass_args->params->pass.raytrace.samplers[i]
        );
    }

    xg_resource_bindings_h group = xg->cmd_create_workload_bindings ( node_args->resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = layout,
        .bindings = draw_bindings
    ) );

    uint32_t thread_count_x = pass_args->params->pass.raytrace.thread_count[0];
    uint32_t thread_count_y = pass_args->params->pass.raytrace.thread_count[1];
    uint32_t thread_count_z = pass_args->params->pass.raytrace.thread_count[2];

    xg->cmd_raytrace ( cmd_buffer, key, &xg_cmd_raytrace_params_m ( 
        .pipeline = pass_args->pipeline,
        .bindings = { xg_null_handle_m, xg_null_handle_m, xg_null_handle_m, group },
        .ray_count_x = thread_count_x,
        .ray_count_y = thread_count_y,
        .ray_count_z = thread_count_z,
    ) );
}

void xf_graph_destroy ( xf_graph_h graph_handle, xg_workload_h xg_workload ) {
    if ( graph_handle == xg_null_handle_m ) {
        return;
    }

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg->wait_all_workload_complete();
    //xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( xg_workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( xg_workload );

    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];

        // Renderpass
        if ( node->renderpass != xg_null_handle_m ) {
            xg->cmd_destroy_renderpass ( resource_cmd_buffer, node->renderpass, xg_resource_cmd_buffer_time_workload_complete_m );
        }
    }

    //xg->wait_all_workload_complete();
    size_t query_contexts_count = std_ring_count ( &graph->query_contexts_ring );
    while ( query_contexts_count > 0 ) {
        xf_graph_query_context_t* context = &graph->query_contexts_array[std_ring_bot_idx ( &graph->query_contexts_ring )];
        xg->wait_for_workload ( context->workload );
        std_assert_m ( xg->is_workload_complete ( context->workload ) );
        xg->destroy_query_pool ( context->pool );
        std_ring_pop ( &graph->query_contexts_ring, 1 );
        query_contexts_count = std_ring_count ( &graph->query_contexts_ring );
    }


    if ( !xg_memory_handle_is_null_m ( graph->heap.memory_handle ) ) {
        for ( uint32_t i = 0; i < graph->heap.textures_count; ++i ) {
            //xf_physical_texture_h physical_texture_handle = graph->heap.textures_array[i];
            //xf_resource_texture_unbind ( texture_handle );
        }

        //xg->free_memory ( graph->heap.memory_handle );
    }

    //for ( uint32_t i = 0; i < graph->owned_textures_count; ++i ) {
    //    xf_graph_texture_t* graph_texture = &graph->textures_array[graph->owned_textures_array[i]];
    //    xf_resource_texture_unbind ( graph_texture->handle );
    //}

    for ( uint32_t i = 0; i < graph->segments_count; ++i ) {
        for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; ++q ) {
            xg_queue_event_h event = graph->segments_array[i].events[q];
            if ( event != xg_null_handle_m ) {
                xg->cmd_destroy_queue_event ( resource_cmd_buffer, event, xg_resource_cmd_buffer_time_workload_complete_m );
            }
        }
    }

    xf_graph_remove_resource_refs ( graph_handle );

    xf_resource_destroy_unreferenced ( xg, resource_cmd_buffer, xg_resource_cmd_buffer_time_workload_complete_m );

    if ( !xg_memory_handle_is_null_m ( graph->heap.memory_handle ) ) {
        xg->free_memory ( graph->heap.memory_handle );
    }

    std_virtual_stack_destroy ( &graph->resource_dependencies_allocator );
    std_virtual_stack_destroy ( &graph->physical_resource_dependencies_allocator );
    std_virtual_stack_destroy ( &graph->node_user_arg_allocator );

    std_list_push ( &xf_graph_state->graphs_freelist, graph );
    std_bitset_clear ( xf_graph_state->graphs_bitset, graph_handle );
}

const uint64_t* xf_graph_get_timings ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    return graph->latest_timings;
}

static void xf_graph_timestamp_query_pool_consume ( xf_graph_h graph_handle, xg_i* xg ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    
    size_t count = std_ring_count ( &graph->query_contexts_ring );
    while ( count > 0 ) {
        xf_graph_query_context_t* context = &graph->query_contexts_array[std_ring_bot_idx ( &graph->query_contexts_ring )];
        if ( !xg->is_workload_complete ( context->workload ) ) {
            break;
        }

        std_buffer_t buffer = std_buffer_m ( .base = graph->latest_timings, .size = context->size );
        xg->read_query_pool ( buffer, context->pool );
        xg->destroy_query_pool ( context->pool );
        std_ring_pop ( &graph->query_contexts_ring, 1 );
        count = std_ring_count ( &graph->query_contexts_ring );
    }
}

static xg_query_pool_h xf_graph_timestamp_query_pool_create ( xf_graph_h graph_handle, xg_workload_h workload, xg_i* xg ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xg_query_pool_h pool = xg->create_query_pool ( &xg_query_pool_params_m (
        .device = graph->params.device,
        .type = xg_query_pool_type_timestamp_m,
        .capacity = graph->nodes_count * 2,
        .debug_name = "xf_graph_timestamp_pool"
    ) );
    uint64_t pool_idx = std_ring_top_idx ( &graph->query_contexts_ring );
    graph->query_contexts_array[pool_idx] = ( xf_graph_query_context_t ) {
        .pool = pool,
        .workload = workload,
        .size = sizeof ( uint64_t ) * 2 * graph->nodes_count,
    };
    std_ring_push ( &graph->query_contexts_ring, 1 );
    return pool;
}

uint64_t xf_graph_execute ( xf_graph_h graph_handle, xg_workload_h xg_workload, uint64_t base_key ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    std_assert_m ( graph );

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( xg_workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( xg_workload );
    uint64_t sort_key = base_key;

    // Prepare
    sort_key = xf_graph_prepare_for_execute ( graph_handle, xg, xg_workload, resource_cmd_buffer, sort_key );

    // Update swapchain resources
    // TODO only do this for write-swapchains? Or forbit reads from swapchains completely?
    for ( uint64_t i = 0; i < graph->multi_textures_count; ++i ) {
        xf_texture_h multi_texture_handle = graph->textures_array[graph->multi_textures_array[i]].handle;
        xg_swapchain_h swapchain = xf_resource_multi_texture_get_swapchain ( multi_texture_handle );

        if ( swapchain != xg_null_handle_m ) {
            xg_swapchain_info_t info;
            xg->get_swapchain_info ( &info, swapchain );

            if ( !info.acquired ) {
                xg_swapchain_acquire_result_t acquire;
                xg->acquire_swapchain ( &acquire, swapchain, xg_workload );
                xf_resource_multi_texture_set_index ( multi_texture_handle, acquire.idx );

                if ( acquire.resize ) {
                    xf_resource_swapchain_resize ( multi_texture_handle );
                    xf_resource_texture_update_external ( multi_texture_handle );
                }
            }
        }
    }
    
    // Update renderpasses
    xf_graph_build_renderpasses ( graph, xg, resource_cmd_buffer );

// TODO avoid this limit?
#define xf_graph_max_barriers_per_pass_m 128
    // Initial queue barriers
    xg_queue_event_h wait_events_array[3];
    uint32_t wait_events_count = 0;
#if 1
    {
        xf_graph_texture_transition_t texture_transitions_arrays[xg_cmd_queue_count_m][xf_graph_max_barriers_per_pass_m];
        uint32_t texture_transitions_counts[xg_cmd_queue_count_m] = { 0 };
        xg_queue_event_h events[xg_cmd_queue_count_m] = {
            xg->create_queue_event ( &xg_queue_event_params_m ( .device = graph->params.device, .debug_name = "xf_pre_signal_0" ) ),
            xg->create_queue_event ( &xg_queue_event_params_m ( .device = graph->params.device, .debug_name = "xf_pre_signal_1" ) ),
            xg->create_queue_event ( &xg_queue_event_params_m ( .device = graph->params.device, .debug_name = "xf_pre_signal_2" ) ),
        };
        xg->cmd_destroy_queue_event ( resource_cmd_buffer, events[0], xg_resource_cmd_buffer_time_workload_complete_m );
        xg->cmd_destroy_queue_event ( resource_cmd_buffer, events[1], xg_resource_cmd_buffer_time_workload_complete_m );
        xg->cmd_destroy_queue_event ( resource_cmd_buffer, events[2], xg_resource_cmd_buffer_time_workload_complete_m );

        for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
            xf_graph_texture_t* graph_texture = &graph->textures_array[i];
            xf_graph_physical_texture_t* graph_physical_texture = &graph->physical_textures_array[graph_texture->physical_texture_handle];
            xf_texture_t* texture = xf_resource_texture_get ( graph_texture->handle );
            std_unused_m ( texture );
            //xf_physical_texture_t* physical_texture = xf_resource_physical_texture_get ( graph_physical_texture->handle );
            xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
            
            for ( uint32_t j = 0; j < graph_physical_texture->dependencies.subresource_count; ++j ) {
                xf_graph_subresource_dependencies_t* deps = &graph_physical_texture->dependencies.subresources[j];
                if ( deps->count > 0 ) {
                    xf_resource_access_t* access = &deps->array[0];
                    xf_node_t* node = &graph->nodes_array[access->node];
                    xg_cmd_queue_e node_queue = node->enabled ? node->params.queue : xg_cmd_queue_graphics_m;
                    if ( node_queue != physical_texture->state.shared.execution.queue && physical_texture->state.shared.execution.queue != xg_cmd_queue_invalid_m ) {
                        std_assert_m ( !physical_texture->state.shared.queue_transition );

                        xf_node_resource_t* resource = &node->resources_array[access->resource_idx];

                        xf_texture_execution_state_t prev_state = physical_texture->state.shared.execution;
                        xf_texture_execution_state_t new_state = xf_texture_execution_state_m (
                            .layout = resource->texture.layout,
                            .stage = resource->stage,
                            .access = xf_graph_memory_access_from_resource_access ( resource->access ),
                            .queue = node_queue
                        );

                        if ( xf_graph_queue_supports_stage ( new_state.queue, prev_state.stage ) ) {
                            // Layout transition on dest queue
                            texture_transitions_arrays[prev_state.queue][texture_transitions_counts[prev_state.queue]++] = xf_graph_texture_transition_m (
                                .state = prev_state,
                                .state.queue = new_state.queue,
                                .texture = graph_texture->handle,
                                .view = resource->texture.view
                            );
                        } else {
                            std_assert_m ( xf_graph_queue_supports_stage ( prev_state.queue, new_state.stage ) );
                        
                            // Layout transition on source queue
                            // TODO removing this one extra barrier on the source queue causes the validation layer to complain 
                            //      about the texture not being in the destination format on the destination queue. Why?
                            texture_transitions_arrays[prev_state.queue][texture_transitions_counts[prev_state.queue]++] = xf_graph_texture_transition_m (
                                .state = new_state,
                                .state.queue = prev_state.queue,
                                .texture = graph_texture->handle,
                                .view = resource->texture.view
                            );

                            texture_transitions_arrays[prev_state.queue][texture_transitions_counts[prev_state.queue]++] = xf_graph_texture_transition_m (
                                .state = new_state,
                                .texture = graph_texture->handle,
                                .view = resource->texture.view
                            );
                        }
                    }
                }
            }
        }

        for ( uint32_t queue_it = 0; queue_it < xg_cmd_queue_count_m; ++queue_it ) {
            if ( texture_transitions_counts[queue_it] > 0 ) {
                xg->cmd_bind_queue ( cmd_buffer, sort_key++, &xg_cmd_bind_queue_params_m (
                    .queue = queue_it,
                    .signal_events = { events[queue_it] },
                    .signal_count = 1,
                ) );

                xg_texture_memory_barrier_t texture_barriers_array[xf_graph_max_barriers_per_pass_m];
                std_stack_t texture_barriers_stack = std_static_stack_m ( texture_barriers_array );

                for ( uint32_t i = 0; i < texture_transitions_counts[queue_it]; ++i ) {
                    xf_graph_texture_transition_t* transition = &texture_transitions_arrays[queue_it][i];
                    xf_resource_texture_state_barrier ( &texture_barriers_stack, transition->texture, transition->view, &transition->state );
                }

                xg_barrier_set_t barrier_set = xg_barrier_set_m();
                barrier_set.texture_memory_barriers = texture_barriers_array;
                barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                xg->cmd_barrier_set ( cmd_buffer, sort_key++, &barrier_set );

            }
        }
        
        for ( uint32_t i = 0; i < xg_cmd_queue_count_m; ++i ) {
            if ( texture_transitions_counts[i] ) wait_events_array[wait_events_count++] = events[i];
        }
    }
#endif

    xf_graph_timestamp_query_pool_consume ( graph_handle, xg );
    xg_query_pool_h timestamp_query_pool = xf_graph_timestamp_query_pool_create ( graph_handle, xg_workload, xg );
    xg->cmd_reset_query_pool ( cmd_buffer, sort_key++, timestamp_query_pool );

    // Execute
    for ( size_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        uint32_t node_idx = graph->nodes_execution_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_idx];
        xf_node_params_t* params = &node->params;

        xg_texture_memory_barrier_t texture_barriers[xf_graph_max_barriers_per_pass_m];
        std_stack_t texture_barriers_stack = std_static_stack_m ( texture_barriers );
        xg_buffer_memory_barrier_t buffer_barriers[xf_graph_max_barriers_per_pass_m]; // TODO split defines
        std_stack_t buffer_barriers_stack = std_static_stack_m ( buffer_barriers );

        for ( uint32_t i = 0; i < node->texture_acquires.count; ++i ) {
            xf_graph_texture_transition_t* transition = &node->texture_acquires.array[i];
            xf_resource_texture_state_barrier ( &texture_barriers_stack, transition->texture, transition->view, &transition->state );
        }

        xf_node_io_t io;
        uint32_t resource_it = 0;

        for ( uint32_t i = 0; i < node->texture_transitions.count; ++i ) {
            xf_graph_texture_transition_t* transition = &node->texture_transitions.array[i];
            xf_resource_texture_state_barrier ( &texture_barriers_stack, transition->texture, transition->view, &transition->state );
        }

#if 0
        for ( uint32_t i = 0; i < node->buffer_transitions.count; ++i ) {
            xf_graph_buffer_transition_t* transition = &node->buffer_transitions.array[i];
            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, transition->buffer, &transition->state );
        }
#endif

        if ( !node->enabled ) {
            xg_barrier_set_t barrier_set = xg_barrier_set_m();
            barrier_set.texture_memory_barriers = texture_barriers;
            barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
            barrier_set.buffer_memory_barriers = buffer_barriers;
            barrier_set.buffer_memory_barriers_count = std_stack_array_count_m ( &buffer_barriers_stack, xg_buffer_memory_barrier_t );
            xg->cmd_barrier_set ( cmd_buffer, sort_key++, &barrier_set );

            // TODO
            //xg->cmd_begin_debug_region ( cmd_buffer, sort_key, node->params.debug_name, node->params.debug_color );
            for ( uint32_t i = 0; i < params->resources.render_targets_count; ++i ) {
                if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_clear_m ) {
                    xf_render_target_dependency_t* target = &node->params.resources.render_targets[i];
                    xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( target->texture );
                    xg->cmd_clear_texture ( cmd_buffer, sort_key, physical_texture->handle, params->passthrough.render_targets[i].clear );
                } else if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_copy_m ) {
                    xf_render_target_dependency_t* target = &node->params.resources.render_targets[i];
                    xf_copy_texture_dependency_t* source = &params->passthrough.render_targets[i].copy_source;
                    xf_physical_texture_t* target_physical_texture = xf_resource_texture_get_physical_texture ( target->texture );
                    xf_physical_texture_t* source_physical_texture = xf_resource_texture_get_physical_texture ( source->texture );
                    xg_texture_copy_params_t copy_params = {
                        .source.texture = source_physical_texture->handle,
                        .source.mip_base = source->view.mip_base,
                        .source.array_base = source->view.array_base,
                        .destination.texture = target_physical_texture->handle,
                        .destination.mip_base = target->view.mip_base,
                        .destination.array_base = target->view.array_base,
                        .mip_count = source->view.mip_count,
                        .array_count = source->view.array_count,
                        .aspect = source->view.aspect,
                        .filter = xg_sampler_filter_point_m,
                    };
                    xg->cmd_copy_texture ( cmd_buffer, sort_key, &copy_params );
                }
            }

            // TODO factor this into an outer function and call twice? need to share/wrap a bunch of local state...
            for ( uint32_t i = 0; i < params->resources.storage_texture_writes_count; ++i ) {
                if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_clear_m ) {
                    xf_shader_texture_dependency_t* texture_write = &node->params.resources.storage_texture_writes[i];
                    xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( texture_write->texture );
                    xg->cmd_clear_texture ( cmd_buffer, sort_key, physical_texture->handle, params->passthrough.storage_texture_writes[i].clear );
                } else if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_copy_m ) {
                    xf_shader_texture_dependency_t* target = &node->params.resources.storage_texture_writes[i];
                    xf_copy_texture_dependency_t* source = &params->passthrough.storage_texture_writes[i].copy_source;
                    xf_physical_texture_t* target_physical_texture = xf_resource_texture_get_physical_texture ( target->texture );
                    xf_physical_texture_t* source_physical_texture = xf_resource_texture_get_physical_texture ( source->texture );
                    xg_texture_copy_params_t copy_params = {
                        .source.texture = source_physical_texture->handle,
                        .source.mip_base = source->view.mip_base,
                        .source.array_base = source->view.array_base,
                        .destination.texture = target_physical_texture->handle,
                        .destination.mip_base = target->view.mip_base,
                        .destination.array_base = target->view.array_base,
                        .mip_count = source->view.mip_count,
                        .array_count = source->view.array_count,
                        .aspect = source->view.aspect,
                        .filter = xg_sampler_filter_point_m,
                    };
                    xg->cmd_copy_texture ( cmd_buffer, sort_key, &copy_params );
                }
            }

            for ( uint32_t i = 0; i < params->resources.storage_buffer_writes_count; ++i ) {
                if ( params->passthrough.storage_buffer_writes[i].mode == xf_passthrough_mode_clear_m ) {
                    xf_shader_buffer_dependency_t* buffer_write = &node->params.resources.storage_buffer_writes[i];
                    xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_write->buffer );
                    xg->cmd_clear_buffer ( cmd_buffer, sort_key, buffer->xg_handle, params->passthrough.storage_buffer_writes[i].clear );
                } else if ( params->passthrough.storage_buffer_writes[i].mode == xf_passthrough_mode_copy_m ) {
                    xf_shader_buffer_dependency_t* target = &node->params.resources.storage_buffer_writes[i];
                    xf_buffer_h source = params->passthrough.storage_buffer_writes[i].copy_source;
                    xf_buffer_t* target_buffer = xf_resource_buffer_get ( target->buffer );
                    xf_buffer_t* source_buffer = xf_resource_buffer_get ( source );
                    xg_buffer_copy_params_t copy_params = {
                        .source = source_buffer->xg_handle,
                        .destination = target_buffer->xg_handle,
                    };
                    xg->cmd_copy_buffer ( cmd_buffer, sort_key, &copy_params );
                }
            }
        } else {
            for ( size_t i = 0; i < node->params.resources.sampled_textures_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_sampled_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.sampled_textures[i].texture = physical_texture->handle;
                io.sampled_textures[i].view = resource->texture.view;
                io.sampled_textures[i].layout = resource->texture.layout;
            }

            for ( size_t i = 0; i < node->params.resources.storage_texture_reads_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_storage_read_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.storage_texture_reads[i].texture = physical_texture->handle;
                io.storage_texture_reads[i].view = resource->texture.view;
                io.storage_texture_reads[i].layout = resource->texture.layout;
            }

            for ( size_t i = 0; i < node->params.resources.storage_texture_writes_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_storage_write_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.storage_texture_writes[i].texture = physical_texture->handle;
                io.storage_texture_writes[i].view = resource->texture.view;
                io.storage_texture_writes[i].layout = resource->texture.layout;
            }

            for ( size_t i = 0; i < node->params.resources.copy_texture_reads_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_copy_read_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.copy_texture_reads[i].texture = physical_texture->handle;
                io.copy_texture_reads[i].view = resource->texture.view;
            }

            for ( size_t i = 0; i < node->params.resources.copy_texture_writes_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_copy_write_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.copy_texture_writes[i].texture = physical_texture->handle;
                io.copy_texture_writes[i].view = resource->texture.view;
            }

            for ( size_t i = 0; i < node->params.resources.render_targets_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_render_target_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.render_targets[i].texture = physical_texture->handle;
                io.render_targets[i].view = resource->texture.view;
            }

            if ( node->params.resources.depth_stencil_target != xf_null_handle_m ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_texture_m );
                std_assert_m ( resource->access == xf_resource_access_depth_target_m );
                xf_graph_texture_t* graph_texture = &graph->textures_array[resource->texture.graph_handle];
                xf_physical_texture_t* physical_texture = xf_resource_texture_get_physical_texture ( graph_texture->handle );
                io.depth_stencil_target = physical_texture->handle;
                ++resource_it;
            } else {
                io.depth_stencil_target = xg_null_handle_m;
            }

            // TODO precomp buffer barriers
            for ( size_t i = 0; i < node->params.resources.uniform_buffers_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_buffer_m );
                std_assert_m ( resource->access == xf_resource_access_uniform_m );
                xf_graph_buffer_t* graph_buffer = &graph->buffers_array[resource->buffer.graph_handle];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( graph_buffer->handle );
                std_assert_m ( buffer->xg_handle != xg_null_handle_m );
                io.uniform_buffers[i] = buffer->xg_handle;

                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    .stage = resource->stage,
                    .access = xf_graph_memory_access_from_resource_access ( resource->access )
                );
                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, graph_buffer->handle, &state );
            }

            for ( size_t i = 0; i < node->params.resources.storage_buffer_reads_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_buffer_m );
                std_assert_m ( resource->access == xf_resource_access_storage_read_m );
                xf_graph_buffer_t* graph_buffer = &graph->buffers_array[resource->buffer.graph_handle];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( graph_buffer->handle );
                std_assert_m ( buffer->xg_handle != xg_null_handle_m );
                io.storage_buffer_reads[i] = buffer->xg_handle;

                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    .stage = resource->stage,
                    .access = xf_graph_memory_access_from_resource_access ( resource->access )
                );
                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, graph_buffer->handle, &state );
            }

            for ( size_t i = 0; i < node->params.resources.storage_buffer_writes_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_buffer_m );
                std_assert_m ( resource->access == xf_resource_access_storage_write_m );
                xf_graph_buffer_t* graph_buffer = &graph->buffers_array[resource->buffer.graph_handle];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( graph_buffer->handle );
                std_assert_m ( buffer->xg_handle != xg_null_handle_m );
                io.storage_buffer_writes[i] = buffer->xg_handle;

                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    .stage = resource->stage,
                    .access = xf_graph_memory_access_from_resource_access ( resource->access )
                );
                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, graph_buffer->handle, &state );
            }

            for ( size_t i = 0; i < node->params.resources.copy_buffer_reads_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_buffer_m );
                std_assert_m ( resource->access == xf_resource_access_copy_read_m );
                xf_graph_buffer_t* graph_buffer = &graph->buffers_array[resource->buffer.graph_handle];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( graph_buffer->handle );
                std_assert_m ( buffer->xg_handle != xg_null_handle_m );
                io.copy_buffer_reads[i] = buffer->xg_handle;

                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    .stage = resource->stage,
                    .access = xf_graph_memory_access_from_resource_access ( resource->access )
                );
                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, graph_buffer->handle, &state );
            }

            for ( size_t i = 0; i < node->params.resources.copy_buffer_writes_count; ++i, ++resource_it ) {
                xf_node_resource_t* resource = &node->resources_array[resource_it];
                std_assert_m ( resource->type == xf_node_resource_buffer_m );
                std_assert_m ( resource->access == xf_resource_access_copy_write_m );
                xf_graph_buffer_t* graph_buffer = &graph->buffers_array[resource->buffer.graph_handle];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( graph_buffer->handle );
                std_assert_m ( buffer->xg_handle != xg_null_handle_m );
                io.copy_buffer_writes[i] = buffer->xg_handle;

                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    .stage = resource->stage,
                    .access = xf_graph_memory_access_from_resource_access ( resource->access )
                );
                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, graph_buffer->handle, &state );
            }
        }

        xf_graph_segment_t* segment = &graph->segments_array[node->segment];
        if ( node_it == segment->begin ) {
            xg_cmd_bind_queue_params_t bind_queue_params = xg_cmd_bind_queue_params_m (
                .queue = segment->queue,
                .wait_count = segment->deps_count + wait_events_count,
            );
            for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; ++q ) {
                if ( segment->is_depended[q] ) {
                    bind_queue_params.signal_events[bind_queue_params.signal_count++] = segment->events[q];
                }
            }
            for ( uint32_t i = 0; i < segment->deps_count; ++i ) {
                bind_queue_params.wait_stages[i] = xg_pipeline_stage_bit_top_of_pipe_m;
                bind_queue_params.wait_events[i] = graph->segments_array[segment->deps[i]].events[segment->queue];
            }
            for ( uint32_t i = 0; i < wait_events_count; ++i ) {
                bind_queue_params.wait_stages[segment->deps_count + i] = xg_pipeline_stage_bit_top_of_pipe_m;
                bind_queue_params.wait_events[segment->deps_count + i] = wait_events_array[i];
            }
            wait_events_count = 0;
            xg->cmd_bind_queue ( cmd_buffer, sort_key++, &bind_queue_params );
        }

        // if timing enabled
        xg->cmd_query_timestamp ( cmd_buffer, sort_key++, &xg_cmd_query_timestamp_params_m (
            .pool = timestamp_query_pool,
            .idx = node_it * 2 + 0,
            .stage = xg_pipeline_stage_bit_top_of_pipe_m,
        ) );

        if ( node->enabled ) {
            xg->cmd_begin_debug_region ( cmd_buffer, sort_key++, node->params.debug_name, node->params.debug_color );
            {
                xg_barrier_set_t barrier_set = xg_barrier_set_m();
                barrier_set.texture_memory_barriers = texture_barriers;
                barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                xg->cmd_barrier_set ( cmd_buffer, sort_key++, &barrier_set );
            }

            if ( node->renderpass != xg_null_handle_m && node->params.pass.custom.auto_renderpass ) {
                xg_cmd_renderpass_params_t renderpass_params = xg_cmd_renderpass_params_m (
                    .renderpass = node->renderpass,
                    .render_targets_count = node->params.resources.render_targets_count,
                );

                for ( uint32_t i = 0; i < node->params.resources.render_targets_count; ++i ) {
                    renderpass_params.render_targets[i] = xf_render_target_binding_m ( io.render_targets[i] );
                }

                if ( node->params.resources.depth_stencil_target != xf_null_handle_m ) {
                    renderpass_params.depth_stencil.texture = io.depth_stencil_target;
                }

                xg->cmd_begin_renderpass ( cmd_buffer, sort_key++, &renderpass_params );
            }

            xf_node_execute_args_t node_args = {
                .device = graph->params.device,
                .cmd_buffer = cmd_buffer,
                .resource_cmd_buffer = resource_cmd_buffer,
                .workload = xg_workload,
                .renderpass = node->renderpass,
                .base_key = sort_key,
                .io = &io,
                .debug_name = node->params.debug_name,
            };

            if ( node->params.type == xf_node_type_custom_pass_m ) {
                void* user_args = node->params.pass.custom.user_args.base;

                if ( node->user_alloc ) {
                    user_args = node->user_alloc;
                }

                node->params.pass.custom.routine ( &node_args, user_args );
                sort_key += node->params.pass.custom.key_space_size;
            } else if ( node->params.type == xf_node_type_compute_pass_m ) {
                std_buffer_t uniform_data = node->params.pass.compute.uniform_data;

                if ( node->user_alloc ) {
                    uniform_data.base = node->user_alloc;
                }

                std_assert_m ( node->params.pass.compute.pipeline != xg_null_handle_m );

                xf_graph_compute_pass_routine_args_t user_args = {
                    .xg = xg,
                    .pipeline = node->params.pass.compute.pipeline,
                    .workgroup_count[0] = node->params.pass.compute.workgroup_count[0],
                    .workgroup_count[1] = node->params.pass.compute.workgroup_count[1],
                    .workgroup_count[2] = node->params.pass.compute.workgroup_count[2],
                    .uniform_data = uniform_data,
                    .params = &node->params,
                };

                xf_graph_compute_pass_routine ( &node_args, &user_args );
            } else if ( node->params.type == xf_node_type_raytrace_pass_m ) {
                std_buffer_t uniform_data = node->params.pass.raytrace.uniform_data;

                if ( node->user_alloc ) {
                    uniform_data.base = node->user_alloc;
                }

                std_assert_m ( node->params.pass.raytrace.pipeline != xg_null_handle_m );

                xf_graph_raytrace_pass_routine_args_t user_args = {
                    .xg = xg,
                    .pipeline = node->params.pass.raytrace.pipeline,
                    .thread_count[0] = node->params.pass.raytrace.thread_count[0],
                    .thread_count[1] = node->params.pass.raytrace.thread_count[1],
                    .thread_count[2] = node->params.pass.raytrace.thread_count[2],
                    .uniform_data = uniform_data,
                    .params = &node->params,
                };

                xf_graph_raytrace_pass_routine ( &node_args, &user_args );
            } else if ( node->params.type == xf_node_type_copy_pass_m ) {
                xf_graph_copy_pass_routine_args_t user_args = {
                    .xg = xg,
                    .params = &node->params,
                };

                xf_graph_copy_pass_routine ( &node_args, &user_args );
            } else if ( node->params.type == xf_node_type_clear_pass_m ) {
                xf_graph_clear_pass_routine_args_t user_args = {
                    .xg = xg,
                    .params = &node->params,
                };

                xf_graph_clear_pass_routine ( &node_args, &user_args );
            } else {
                std_assert_m ( false );
            }

            if ( node->renderpass != xg_null_handle_m && node->params.pass.custom.auto_renderpass ) {
                xg->cmd_end_renderpass ( cmd_buffer, sort_key++ );
            }
            

            if ( node->params.resources.presentable_texture != xf_null_handle_m ) {
                xf_texture_h texture_handle = node->params.resources.presentable_texture;

                xg_texture_memory_barrier_t present_barrier[1];
                std_stack_t present_barrier_stack = std_static_stack_m ( present_barrier );

                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = xg_texture_layout_present_m,
                    .stage = xg_pipeline_stage_bit_bottom_of_pipe_m,
                    .access = xg_memory_access_bit_none_m,
                    .queue = xg_cmd_queue_graphics_m,
                );

                xf_resource_texture_state_barrier ( &present_barrier_stack, texture_handle, xg_texture_view_m(), &state );
                std_assert_m ( present_barrier_stack.top == present_barrier + 1 );

                xg_barrier_set_t barrier_set = xg_barrier_set_m();
                barrier_set.texture_memory_barriers = present_barrier;
                barrier_set.texture_memory_barriers_count = 1;
                xg->cmd_barrier_set ( cmd_buffer, sort_key++, &barrier_set );
            }
        }

        if ( node->texture_releases.count ) {
            std_stack_clear ( &texture_barriers_stack );
            std_stack_clear ( &buffer_barriers_stack );

            for ( uint32_t i = 0; i < node->texture_releases.count; ++i ) {
                xf_graph_texture_transition_t* transition = &node->texture_releases.array[i];
                xf_resource_texture_state_barrier ( &texture_barriers_stack, transition->texture, transition->view, &transition->state );
            }

    #if 0
            for ( uint32_t i = 0; i < node->buffer_releases.count; ++i ) {
                xf_graph_buffer_transition_t* transition = &node->buffer_releases.array[i];
                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, transition->buffer, &transition->state );
            }
    #endif
            
            xg_barrier_set_t barrier_set = xg_barrier_set_m();
            barrier_set.texture_memory_barriers = texture_barriers;
            barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
            barrier_set.buffer_memory_barriers = buffer_barriers;
            barrier_set.buffer_memory_barriers_count = std_stack_array_count_m ( &buffer_barriers_stack, xg_buffer_memory_barrier_t );
            xg->cmd_barrier_set ( cmd_buffer, sort_key++, &barrier_set );
        }

        // TODO
        if ( node->enabled ) {
            xg->cmd_end_debug_region ( cmd_buffer, sort_key++ );
        }

        // if timing enabled
        xg->cmd_query_timestamp ( cmd_buffer, sort_key++, &xg_cmd_query_timestamp_params_m (
            .pool = timestamp_query_pool,
            .idx = node_it * 2 + 1,
            .stage = xg_pipeline_stage_bit_bottom_of_pipe_m,
        ) );
    }

    // Advance multi resources, skip those created without auto_advance
    // TODO buffers
    for ( uint64_t i = 0; i < graph->multi_textures_count; ++i ) {
        xf_texture_h texture_handle = graph->textures_array[graph->multi_textures_array[i]].handle;
        xf_multi_texture_t* multi_texture = xf_resource_multi_texture_get ( texture_handle );
        xg_swapchain_h swapchain = xf_resource_multi_texture_get_swapchain ( texture_handle );
        if ( swapchain == xg_null_handle_m && multi_texture->params.auto_advance ) {
            xf_resource_multi_texture_advance ( texture_handle );
        }
    }

    for ( uint32_t i = 0; i < graph->multi_buffers_count; ++i ) {
        xf_buffer_h buffer_handle = graph->buffers_array[graph->multi_buffers_array[i]].handle;
        xf_multi_buffer_t* multi_buffer = xf_resource_multi_buffer_get ( buffer_handle );
        if ( multi_buffer->params.auto_advance ) {
            xf_resource_multi_buffer_advance ( buffer_handle );
        }
    }

    //xf_graph_cleanup ( graph_handle );

    return sort_key;
}

void xf_graph_advance_multi_textures ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    // Advance multi resources
    for ( uint64_t i = 0; i < graph->multi_textures_count; ++i ) {
        xf_texture_h texture_handle = graph->textures_array[graph->multi_textures_array[i]].handle;
        xg_swapchain_h swapchain = xf_resource_multi_texture_get_swapchain ( texture_handle );
        if ( swapchain == xg_null_handle_m ) {
            xf_resource_multi_texture_advance ( texture_handle );
        }
    }
}

void xf_graph_advance_multi_buffers ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    // Advance multi resources
    for ( uint64_t i = 0; i < graph->multi_buffers_count; ++i ) {
        xf_buffer_h buffer_handle = graph->buffers_array[graph->multi_buffers_array[i]].handle;
        xf_resource_multi_buffer_advance ( buffer_handle );
    }
}

void xf_graph_get_info ( xf_graph_info_t* info, xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    info->device = graph->params.device;
    info->node_count = graph->nodes_count;

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        info->nodes[i] = graph->nodes_execution_order[i];
    }
}

void xf_graph_get_node_info ( xf_node_info_t* info, xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    info->enabled = node->enabled;
    info->passthrough = node->params.passthrough.enable;
    std_str_copy_static_m ( info->debug_name, node->params.debug_name );
    info->resources = node->params.resources;
    for ( uint32_t i = 0; i < node->textures_count; ++i ) {
        xf_node_resource_t* resource = &node->resources_array[node->textures_array[i]];
        info->texture_info[i].handle = graph->textures_array[resource->texture.graph_handle].handle;
        info->texture_info[i].access = resource->access;
    }
    info->texture_count = node->textures_count;
}

void xf_graph_debug_print ( xf_graph_h graph_handle ) {
    xf_graph_print ( graph_handle );
}

void xf_graph_node_set_enabled ( xf_graph_h graph_handle, xf_node_h node_handle, bool enabled ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    if ( node->params.passthrough.enable ) {
        node->enabled = enabled;
    }
}

void xf_graph_node_enable ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    node->enabled = true;
}

void xf_graph_node_disable ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    if ( node->params.passthrough.enable ) {
        node->enabled = false;
    }
}

void xf_graph_set_texture_export ( xf_graph_h graph_handle, xf_node_h node, xf_texture_h texture, xf_texture_h dest, xf_export_channel_e channel_remap[4] ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    graph->export_source_node = node;
    graph->export_source = texture;
    graph->export_dest = dest;
    graph->export_channels[0] = channel_remap[0];
    graph->export_channels[1] = channel_remap[1];
    graph->export_channels[2] = channel_remap[2];
    graph->export_channels[3] = channel_remap[3];
}
