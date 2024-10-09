#include <xf.h>

#include "xf_graph.h"
#include "xf_resource.h"

#include <xg_enum.h>

#include <std_list.h>
#include <std_hash.h>
#include <std_log.h>
#include <std_allocator.h>

static xf_graph_state_t* xf_graph_state;

void xf_graph_load ( xf_graph_state_t* state ) {
    state->graphs_array = std_virtual_heap_alloc_array_m ( xf_graph_t, xf_graph_max_graphs_m );
    state->graphs_freelist = std_freelist_m ( state->graphs_array, xf_graph_max_graphs_m );

    state->nodes_array = std_virtual_heap_alloc_array_m ( xf_node_t, xf_graph_max_nodes_m );
    state->nodes_freelist = std_freelist_m ( state->nodes_array, xf_graph_max_nodes_m );

    xf_graph_state = state;
}

void xf_graph_reload ( xf_graph_state_t* state ) {
    xf_graph_state = state;
}

void xf_graph_unload ( void ) {
    // TODO free active graphs resources
    std_virtual_heap_free ( xf_graph_state->graphs_array );
    std_virtual_heap_free ( xf_graph_state->nodes_array );
}

xf_graph_h xf_graph_create ( xg_device_h device, xg_swapchain_h swapchain ) {
    xf_graph_t* graph = std_list_pop_m ( &xf_graph_state->graphs_freelist );
    graph->nodes_count = 0;
    graph->device = device;
    graph->swapchain = swapchain;

    xf_graph_h handle = ( xf_graph_h ) ( graph - xf_graph_state->graphs_array );
    return handle;
}

/*
    TODO
        go through the ordered nodes before each execute, take note for each resource usage in each pass
        the previous use for that resource (read/write, pipeline stage) so that when filling barriers
        all data is ready for each pass resource
        this means some of the stuff done when a node is added to a graph can be removed (we're basically
        moving it to execute time)
*/

static void xf_graph_node_accumulate_reads_writes ( xf_node_h node ) {
    const xf_node_params_t* params = &xf_graph_state->nodes_array[node].params;

    // Shader resources
    for ( size_t i = 0; i < params->shader_texture_reads_count; ++i ) {
        const xf_node_shader_texture_dependency_t* dep = &params->shader_texture_reads[i];
        xf_resource_texture_add_reader ( dep->texture, dep->view, node );
    }

    for ( size_t i = 0; i < params->shader_texture_writes_count; ++i ) {
        const xf_node_shader_texture_dependency_t* dep = &params->shader_texture_writes[i];
        xf_resource_texture_add_writer ( dep->texture, dep->view, node );
    }

    for ( size_t i = 0; i < params->shader_buffer_reads_count; ++i ) {
        xf_buffer_h buffer = params->shader_buffer_reads[i].buffer;
        xf_resource_buffer_add_reader ( buffer, node );
    }

    for ( size_t i = 0; i < params->shader_buffer_writes_count; ++i ) {
        xf_buffer_h buffer = params->shader_buffer_writes[i].buffer;
        xf_resource_buffer_add_writer ( buffer, node );
    }

    // Copy resources
    for ( size_t i = 0; i < params->copy_texture_reads_count; ++i ) {
        const xf_node_copy_texture_dependency_t* dep = &params->copy_texture_reads[i];
        xf_resource_texture_add_reader ( dep->texture, dep->view, node );
    }

    for ( size_t i = 0; i < params->copy_texture_writes_count; ++i ) {
        const xf_node_copy_texture_dependency_t* dep = &params->copy_texture_writes[i];
        xf_resource_texture_add_writer ( dep->texture, dep->view, node );
    }

    for ( size_t i = 0; i < params->copy_buffer_reads_count; ++i ) {
        xf_buffer_h buffer = params->copy_buffer_reads[i];
        xf_resource_buffer_add_reader ( buffer, node );
    }

    for ( size_t i = 0; i < params->copy_buffer_writes_count; ++i ) {
        xf_buffer_h buffer = params->copy_buffer_writes[i];
        xf_resource_buffer_add_writer ( buffer, node );
    }

    // Render targets and depth stencil
    for ( size_t i = 0; i < params->render_targets_count; ++i ) {
        const xf_node_render_target_dependency_t* dep = &params->render_targets[i];
        xf_resource_texture_add_writer ( dep->texture, dep->view, node );
    }

    if ( params->depth_stencil_target != xf_null_handle_m ) {
        std_assert_m ( params->depth_stencil_target != xf_null_handle_m );
        xf_texture_h texture = params->depth_stencil_target;
        xf_resource_texture_add_writer ( texture, xg_default_texture_view_m, node );
    }
}

static void xf_graph_node_accumulate_resource_usage ( xf_node_h node ) {
    const xf_node_params_t* params = &xf_graph_state->nodes_array[node].params;

    // Shader resources
    for ( size_t i = 0; i < params->shader_texture_reads_count; ++i ) {
        const xf_node_shader_texture_dependency_t* dep = &params->shader_texture_reads[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_resource_m );
    }

    for ( size_t i = 0; i < params->shader_texture_writes_count; ++i ) {
        const xf_node_shader_texture_dependency_t* dep = &params->shader_texture_writes[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_storage_m );
    }

    for ( size_t i = 0; i < params->shader_buffer_reads_count; ++i ) {
        xf_buffer_h buffer = params->shader_buffer_reads[i].buffer;
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_uniform_m );
    }

    for ( size_t i = 0; i < params->shader_buffer_writes_count; ++i ) {
        xf_buffer_h buffer = params->shader_buffer_writes[i].buffer;
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_storage_m );
    }

    // Copy resources
    for ( size_t i = 0; i < params->copy_texture_reads_count; ++i ) {
        const xf_node_copy_texture_dependency_t* dep = &params->copy_texture_reads[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_copy_source_m );
    }

    for ( size_t i = 0; i < params->copy_texture_writes_count; ++i ) {
        const xf_node_copy_texture_dependency_t* dep = &params->copy_texture_writes[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_copy_dest_m );
    }

    for ( size_t i = 0; i < params->copy_buffer_reads_count; ++i ) {
        xf_buffer_h buffer = params->copy_buffer_reads[i];
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_copy_source_m );
    }

    for ( size_t i = 0; i < params->copy_buffer_writes_count; ++i ) {
        xf_buffer_h buffer = params->copy_buffer_writes[i];
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_copy_dest_m );
    }

    // Render targets and depth stencil
    for ( size_t i = 0; i < params->render_targets_count; ++i ) {
        const xf_node_render_target_dependency_t* dep = &params->render_targets[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_render_target_m );
    }

    if ( params->depth_stencil_target != xf_null_handle_m ) {
        std_assert_m ( params->depth_stencil_target != xf_null_handle_m );
        xf_texture_h texture = params->depth_stencil_target;
        xf_resource_texture_add_usage ( texture, xg_texture_usage_bit_depth_stencil_m );
    }
}

#if 0
static void xf_graph_node_add_resource_dependency_edges ( xf_node_t* node, xf_resource_dependencies_t* deps ) {
    for ( size_t i = 0; i < deps->writers_count; ++i ) {
        node->edges[node->edge_count++] = deps->writers[i];
    }
}

static void xf_graph_node_resolve_edges ( xf_node_h node_handle ) {
    xf_node_t* node = &xf_graph_state.nodes_array[node_handle];
    const xf_node_params_t* params = &node->params;

    // Shader resources
    for ( size_t i = 0; i < params->shader_texture_reads_count; ++i ) {
        const xf_texture_t* resource = xf_resource_texture_get ( params->shader_texture_reads[i].texture );
        xf_graph_node_add_resource_dependency_edges ( node, &resource->deps );
    }

    for ( size_t i = 0; i < params->shader_buffer_reads_count; ++i ) {
        const xf_texture_t* resource = xf_resource_texture_get ( params->shader_buffer_reads[i].buffer );
        xf_graph_node_add_resource_dependency_edges ( node, &resource->deps );
    }

    // Copy resources
    for ( size_t i = 0; i < params->copy_texture_reads_count; ++i ) {
        const xf_texture_t* resource = xf_resource_texture_get ( params->copy_texture_reads[i].texture );
        xf_graph_node_add_resource_dependency_edges ( node, &resource->deps );
    }

    for ( size_t i = 0; i < params->copy_buffer_reads_count; ++i ) {
        const xf_texture_t* resource = xf_resource_texture_get ( params->copy_buffer_reads[i].buffer );
        xf_graph_node_add_resource_dependency_edges ( node, &resource->deps );
    }
}
#endif

xf_node_h xf_graph_add_node ( xf_graph_h graph_handle, const xf_node_params_t* params ) {
    xf_node_t* node = std_list_pop_m ( &xf_graph_state->nodes_freelist );
    std_assert_m ( node );
    node->params = *params;
    node->edge_count = 0;
    node->enabled = true;
    node->renderpass = xg_null_handle_m;
    node->renderpass_params.render_textures = xg_render_textures_layout_m();
    node->renderpass_params.resolution_x = 0;
    node->renderpass_params.resolution_y = 0;

    if ( node->params.copy_args && node->params.user_args.base ) {
        void* alloc = std_virtual_heap_alloc ( node->params.user_args.size, 16 ); // TODO some kind of linear allocator
        std_mem_copy ( alloc, node->params.user_args.base, node->params.user_args.size );
        //node->params.user_args = node->user_args; // TODO is this necessary?
        node->user_args = alloc;
    } else {
        node->user_args = node->params.user_args.base;
    }

    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    std_assert_m ( graph );

    xf_node_h node_handle = ( xf_node_h ) ( node - xf_graph_state->nodes_array );

    graph->nodes[graph->nodes_count++] = node_handle;

    // Accumulate resource usage on resources, will be used later when creating them
    xf_graph_node_accumulate_resource_usage ( node_handle );
    xf_graph_node_accumulate_reads_writes ( node_handle );

    return node_handle;
}

typedef struct {
    size_t node_indices[xf_graph_max_nodes_m];
    size_t node_indices_count;
    //size_t stack[xf_graph_max_nodes_m];
    //size_t stack_size;
    char node_flags[xf_graph_max_nodes_m];
    xf_node_h nodes_map_keys[xf_graph_max_nodes_m * 2];
    uint64_t nodes_map_payloads[xf_graph_max_nodes_m * 2];
    std_hash_map_t nodes_map;
} xf_graph_linearize_data_t;

static void xf_graph_linearize_node ( xf_graph_linearize_data_t* ld, xf_node_h node );

#if 0
static void xf_graph_linearize_node_dependencies ( xf_graph_linearize_data_t* ld, const xf_node_t* node ) {
    for ( size_t node_it = 0; node_it < node->params.node_dependencies_count; ++node_it ) {
        xf_node_h node_handle = node->params.node_dependencies[node_it];
        void* lookup = std_map_lookup ( &ld->nodes_map, &node_handle );
        std_assert_m ( lookup );
        uint64_t node_idx = * ( uint64_t* ) lookup;

        if ( !ld->node_flags[node_idx] ) {
            ld->node_indices[ld->node_indices_count++] = node_idx;
            ld->stack[ld->stack_size++] = node_idx;
            ld->node_flags[node_idx] = 1;

            xf_node_t* dependent_node = &xf_graph_state.nodes_array[node_idx];
            xf_graph_linearize_node ( ld, dependent_node );
        }
    }
}
#endif

static void xf_graph_linearize_resource_dependencies ( xf_graph_linearize_data_t* data, const xf_resource_dependencies_t* deps ) {
    // First push all other readers to this resource
#if 0
    for ( size_t i = deps->readers_count; i > 0; --i ) {
        xf_node_h reader_handle = deps->readers[i - 1];
        void* lookup = std_map_lookup ( &data->nodes_map, &reader_handle );
        std_assert_m ( lookup );
        uint64_t reader_idx = * ( uint64_t* ) lookup;

        if ( !data->node_flags[reader_idx] ) {
            data->node_indices[data->node_indices_count++] = reader_idx;
            data->stack[data->stack_size++] = reader_idx;
            data->node_flags[reader_idx] = 1;

            xf_node_t* reader_node = &xf_graph_state.nodes_array[reader_handle];
            xf_graph_linearize_node ( data, reader_node );
        }
    }

#endif

    // Then push the writer
    for ( size_t i = 0; i < deps->writers_count; ++i ) {
        xf_node_h writer_handle = deps->writers[i];
        void* lookup = std_hash_map_lookup ( &data->nodes_map, writer_handle );
        std_assert_m ( lookup );
        uint64_t writer_idx = * ( uint64_t* ) lookup;

        if ( !data->node_flags[writer_idx] ) {
            xf_graph_linearize_node ( data, writer_handle );

            //data->stack[data->stack_size++] = writer_idx;
            //data->node_flags[writer_idx] = 1;
        }
    }
}

static void xf_graph_linearize_node ( xf_graph_linearize_data_t* ld, xf_node_h node_handle ) {
    void* lookup = std_hash_map_lookup ( &ld->nodes_map, node_handle );
    std_assert_m ( lookup );
    uint64_t node_idx = * ( uint64_t* ) lookup;
    ld->node_flags[node_idx] = 1;

    // TODO inline this here, rename this fun to that
    //xf_graph_linearize_node_dependencies ( ld, node );

    xf_node_t* node = &xf_graph_state->nodes_array[node_handle];

    for ( size_t resource_it = 0; resource_it < node->params.shader_texture_reads_count; ++resource_it ) {
        //const xf_texture_t* resource = xf_resource_texture_get ( node->params.shader_texture_reads[resource_it].texture );
        xf_resource_dependencies_t t;
        std_mem_zero_m ( &t );
        const xf_resource_dependencies_t* deps = xf_resource_texture_get_deps ( node->params.shader_texture_reads[resource_it].texture, node->params.shader_texture_reads[resource_it].view, &t );
        xf_graph_linearize_resource_dependencies ( ld, deps );
    }

    for ( size_t resource_it = 0; resource_it < node->params.shader_buffer_reads_count; ++resource_it ) {
        const xf_buffer_t* resource = xf_resource_buffer_get ( node->params.shader_buffer_reads[resource_it].buffer );
        xf_graph_linearize_resource_dependencies ( ld, &resource->deps );
    }

    for ( size_t resource_it = 0; resource_it < node->params.copy_texture_reads_count; ++resource_it ) {
        //const xf_texture_t* resource = xf_resource_texture_get ( node->params.copy_texture_reads[resource_it].texture );
        xf_resource_dependencies_t t;
        std_mem_zero_m ( &t );
        const xf_resource_dependencies_t* deps = xf_resource_texture_get_deps ( node->params.copy_texture_reads[resource_it].texture, node->params.copy_texture_reads[resource_it].view, &t );
        xf_graph_linearize_resource_dependencies ( ld, deps );
    }

    for ( size_t resource_it = 0; resource_it < node->params.copy_buffer_reads_count; ++resource_it ) {
        const xf_buffer_t* resource = xf_resource_buffer_get ( node->params.copy_buffer_reads[resource_it] );
        xf_graph_linearize_resource_dependencies ( ld, &resource->deps );
    }

    ld->node_indices[ld->node_indices_count++] = node_idx;
}

// DFS based topological sort
static void xf_graph_linearize ( xf_graph_t* graph ) {
    // TODO skip if already linearized and not dirty?
    // TODO try to maximize distance between dependencies when determining execution order

    xf_graph_linearize_data_t ld;
    ld.node_indices_count = 0;
    //ld.stack_size = 0;
    std_mem_zero_m ( &ld.node_flags );

    // Create node handle -> graph node idx temp map, used to lookup node handles coming from resources and tell if they're part of the graph or not
    // Could by avoided if nodes were stored directly inside their graph owner instead of being stored separately and referenced by handle
    ld.nodes_map = std_hash_map ( ld.nodes_map_keys, ld.nodes_map_payloads, xf_graph_max_nodes_m * 2 );

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        uint64_t key = ( uint64_t ) ( graph->nodes[i] );
        uint64_t payload = i;
        std_hash_map_insert ( &ld.nodes_map, key, payload );
    }

    // Traverse up and linearize
    //std_ring_t ring = std_ring ( xf_graph_max_nodes_m );

    //std_ring_push ( &ring, 1 );
    //uint64_t idx = std_ring_top_idx ( &ring );

#if 0
    ld.stack[ld.stack_size++] = root_idx;
#endif

#if 0

    while ( ld.stack_size > 0 ) {
        size_t node_idx = ld.stack[--ld.stack_size];
        ld.node_indices[ld.node_indices_count++] = node_idx;
        ld.node_flags[node_idx] = 1;

        xf_node_t* node = &xf_graph_state.nodes_array[graph->nodes[node_idx]];
        xf_graph_linearize_node ( &ld, node );
    }

#else

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node = graph->nodes[i];

        if ( ld.node_flags[i] == 0 ) {
            xf_node_t* node_p = &xf_graph_state->nodes_array[node];
            std_unused_m ( node_p );
            xf_graph_linearize_node ( &ld, node );
        }
    }

#endif

    // Store execution order
#if 0

    for ( size_t i = 0; i < ld.node_indices_count; ++i ) {
        size_t j = ld.node_indices_count - i - 1;
        graph->nodes_execution_order[i] = ld.node_indices[j];
    }

#else

    for ( size_t i = 0; i < ld.node_indices_count; ++i ) {
        graph->nodes_execution_order[i] = ld.node_indices[i];
    }

#endif
}

/*
static size_t xf_graph_deps_distance_score ( size_t* nodes_list, size_t nodes_list_count, const xf_resource_dependencies_t* deps ) {
    size_t writer_idx = ( size_t ) deps.writer;

    for ( size_t i = nodes_list_count - 1; i < nodes_list_count; --i ) {
        if ( nodes_list[i] == writer_idx ) {
            size_t distance = nodes_list_count - i;
            return distance;
        }
    }
}
*/

static void xf_graph_build_texture ( xf_graph_t* graph, xf_texture_h texture_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    if ( texture->is_dirty ) {
        if ( texture->is_user_bind ) {
            xg_texture_info_t info;
            xg->get_texture_info ( &info, texture->xg_handle );
            xf_resource_texture_update_info ( texture_handle, &info );
            std_assert_m ( ( texture->required_usage & texture->allowed_usage ) == texture->required_usage );
        } else {
            bool create_new = false;

            if ( texture->xg_handle == xg_null_handle_m ) {
                create_new = true;
            }

            // TODO only create new if current usage is smaller than required?
            if ( texture->required_usage != texture->allowed_usage ) {
                create_new = true;

                if ( texture->xg_handle != xg_null_handle_m ) {
                    xg->cmd_destroy_texture ( resource_cmd_buffer, texture->xg_handle, xg_resource_cmd_buffer_time_workload_start_m );
                }
            }

            if ( create_new ) {
                xg_texture_params_t params;
                params.memory_type = xg_memory_type_gpu_only_m;
                params.device = graph->device;
                params.width = texture->params.width;
                params.height = texture->params.height;
                params.depth = texture->params.depth;
                params.mip_levels = texture->params.mip_levels;
                params.array_layers = texture->params.array_layers;
                params.dimension = texture->params.dimension;
                params.format = texture->params.format;
                params.samples_per_pixel = texture->params.samples_per_pixel;
                params.allowed_usage = texture->required_usage;
                params.view_access = texture->params.view_access;
                std_str_copy_static_m ( params.debug_name, texture->params.debug_name );

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
                xg_texture_h new_texture = xg->cmd_create_texture ( resource_cmd_buffer, &params );
                xf_resource_texture_map_to_new ( texture_handle, new_texture );

                xg_texture_info_t info;
                xg->get_texture_info ( &info, texture->xg_handle );
                xf_resource_texture_set_allowed_usage ( texture_handle, params.allowed_usage );

                if ( texture->params.clear_on_create ) {
                    // TODO support depth stencil clears
                    // TODO is hard coding 0 as key ok here?
                    xg->cmd_clear_texture ( cmd_buffer, new_texture, texture->params.clear.color, 0 );
                }
            }

            // TODO
            std_unused_m ( cmd_buffer );
            // doesn't need to accumulate on a shared sort key, just key everything to 0 and let the sorter move it all at the beginning
    #if 0

            if ( texture->deps.write.node != xf_null_handle_m ) {
                if ( texture->write_event == xg_null_handle_m ) {
                    texture->write_event = xg->create_event ( device );
                } else {
                    xg->cmd_reset_event ( cmd_buffer, texture->write_event, *sort_key );
                    *sort_key += 1;
                }
            }

    #endif

            xf_resource_texture_set_dirty ( texture_handle, false );
        }
    }

    // TODO better way of doing this?
    xf_resource_texture_alias ( texture_handle, xf_null_handle_m );

    if ( xf_resource_texture_is_multi ( texture_handle ) ) {
        xf_texture_h multi_texture_handle = xf_resource_multi_texture_get_default ( texture_handle );
        if ( std_hash_set_insert ( &graph->multi_textures_hash_set, std_hash_64_m ( multi_texture_handle ) ) ) {
            //xf_texture_info_t info;
            //xf_resource_texture_get_info ( &info, multi_texture_handle );
            //std_log_info_m ( info.debug_name );
            graph->multi_textures_array[graph->multi_textures_count++] = multi_texture_handle;
        }
    }
}

static void xf_graph_build_buffer ( xf_buffer_h buffer_handle, xg_i* xg, xg_device_h device, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );

    if ( buffer->is_dirty ) {
        bool create_new = false;

        if ( buffer->xg_handle == xg_null_handle_m ) {
            create_new = true;
        }

        if ( buffer->required_usage != buffer->allowed_usage ) {
            create_new = true;

            if ( buffer->xg_handle != xg_null_handle_m ) {
                xg->cmd_destroy_buffer ( resource_cmd_buffer, buffer->xg_handle, xg_resource_cmd_buffer_time_workload_start_m );
            }
        }

        if ( create_new ) {
            xg_buffer_params_t params;
            params.memory_type = xg_memory_type_gpu_only_m;
            params.device = device;
            params.size = buffer->params.size;
            params.allowed_usage = buffer->required_usage;
            xg_buffer_h new_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &params );
            xf_resource_buffer_map_to_new ( buffer_handle, new_buffer );
        }

        // TODO
        std_unused_m ( cmd_buffer );
#if 0

        if ( buffer->deps.write.node != xf_null_handle_m ) {
            if ( buffer->write_event == xg_null_handle_m ) {
                buffer->write_event = xg->create_event ( device );
            } else {
                xg->cmd_reset_event ( cmd_buffer, buffer->write_event, *sort_key );
                *sort_key += 1;
            }
        }

#endif

        xf_resource_buffer_set_dirty ( buffer_handle, false );
    }
}

static void xf_graph_build_resources ( xf_graph_t* graph, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    graph->multi_textures_hash_set = std_hash_set ( graph->multi_textures_hashes, xf_graph_max_multi_textures_per_graph_m );
    graph->multi_textures_count = 0;

    // Doesn't matter here if we follow nodes execution order or not
    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &xf_graph_state->nodes_array[graph->nodes[i]];

        for ( size_t resource_it = 0; resource_it < node->params.render_targets_count; ++resource_it ) {
            xf_node_render_target_dependency_t* resource = &node->params.render_targets[resource_it];
            xf_graph_build_texture ( graph, resource->texture, xg, cmd_buffer, resource_cmd_buffer );
        }

        if ( node->params.depth_stencil_target != xf_null_handle_m ) {
            xf_graph_build_texture ( graph, node->params.depth_stencil_target, xg, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.shader_texture_writes_count; ++resource_it ) {
            xf_node_shader_texture_dependency_t* resource = &node->params.shader_texture_writes[resource_it];
            xf_graph_build_texture ( graph, resource->texture, xg, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.shader_texture_reads_count; ++resource_it ) {
            xf_node_shader_texture_dependency_t* resource = &node->params.shader_texture_reads[resource_it];
            xf_graph_build_texture ( graph, resource->texture, xg, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.shader_buffer_writes_count; ++resource_it ) {
            xf_buffer_dependency_t* resource = &node->params.shader_buffer_writes[resource_it];
            xf_graph_build_buffer ( resource->buffer, xg, graph->device, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.shader_buffer_reads_count; ++resource_it ) {
            xf_buffer_dependency_t* resource = &node->params.shader_buffer_reads[resource_it];
            xf_graph_build_buffer ( resource->buffer, xg, graph->device, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.copy_texture_writes_count; ++resource_it ) {
            xf_texture_h texture_handle = node->params.copy_texture_writes[resource_it].texture;
            xf_graph_build_texture ( graph, texture_handle, xg, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.copy_texture_reads_count; ++resource_it ) {
            xf_texture_h texture_handle = node->params.copy_texture_reads[resource_it].texture;
            xf_graph_build_texture ( graph, texture_handle, xg, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.copy_buffer_writes_count; ++resource_it ) {
            xf_buffer_h buffer_handle = node->params.copy_buffer_writes[resource_it];
            xf_graph_build_buffer ( buffer_handle, xg, graph->device, cmd_buffer, resource_cmd_buffer );
        }

        for ( size_t resource_it = 0; resource_it < node->params.copy_buffer_reads_count; ++resource_it ) {
            xf_buffer_h buffer_handle = node->params.copy_buffer_reads[resource_it];
            xf_graph_build_buffer ( buffer_handle, xg, graph->device, cmd_buffer, resource_cmd_buffer );
        }
    }
}

static void xf_graph_build_nodes ( xf_graph_t* graph, xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = graph->nodes[i];
        xf_node_t* node = &xf_graph_state->nodes_array[node_handle];

        xg_render_textures_layout_t render_textures = xg_render_textures_layout_m();
        uint32_t resolution_x = 0;
        uint32_t resolution_y = 0;

        // check node render targets
        for ( size_t resource_it = 0; resource_it < node->params.render_targets_count; ++resource_it ) {
            xf_node_render_target_dependency_t* resource = &node->params.render_targets[resource_it];
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
            xf_texture_h depth_stencil = node->params.depth_stencil_target;
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

            xg_graphics_renderpass_params_t params = xg_graphics_renderpass_params_m (
                .device = graph->device,
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

static void xf_graph_build ( xf_graph_t* graph, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    /*
        build
            - find root
            - linearize graph
            - sort nodes
                is this necessary?
                    yes for cpu, not for gpu only passes, but might as well sort it all?
                    drawback is having to respect dependencies instead of being able to run all nodes in parallel
                    maybe only sort if there's at least one cpu dependency, run all gpu only nodes in parallel (just make sure the sort key passed is right)
                        for now just sort everything for ease, optimize for max parallelism later
            -
        execute
            before executing a pass need to
                - add a wait on all dependencies
                    wait on signal on gpu
                    wait on tasks that are running the prev passes on cpu
                        can run the execute routine as a tk task and store a handle to the task in the node
                        following nodes that depend on this one can just wait on that
                - transition all resources
            can do all of this by reserving some sort key space to use for this setup, to ensure it happens right before the pass
    */

    // Linearize graph
    xf_graph_linearize ( graph );

    // Build resources
    xf_graph_build_resources ( graph, xg, cmd_buffer, resource_cmd_buffer );

    xf_graph_build_nodes ( graph, xg, resource_cmd_buffer );
}

void xf_graph_execute ( xf_graph_h graph_handle, xg_workload_h xg_workload ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    std_assert_m ( graph );

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( xg_workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( xg_workload );

    // Build
    xf_graph_build ( graph, xg, cmd_buffer, resource_cmd_buffer );

    // Update swapchain resources
    // TODO only do this for write-swapchains? Or forbit reads from swapchains completely?
    for ( uint64_t i = 0; i < graph->multi_textures_count; ++i ) {
        xg_swapchain_h swapchain = xf_resource_multi_texture_get_swapchain ( graph->multi_textures_array[i] );

        if ( swapchain != xg_null_handle_m ) {
            xg_swapchain_info_t info;
            xg->get_swapchain_info ( &info, swapchain );

            if ( !info.acquired ) {
                bool resize = false;
                uint32_t idx = xg->acquire_next_swapchain_texture ( swapchain, xg_workload, &resize );
                xf_resource_multi_texture_set_index ( graph->multi_textures_array[i], idx );

                if ( resize ) {
                    xf_resource_swapchain_resize ( graph->multi_textures_array[i] );
                }
            }
        }
    }

    // Execute
    {
        // TODO take this as param
        uint64_t sort_key = 0;

        for ( size_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
            xf_node_h node_handle = graph->nodes[graph->nodes_execution_order[node_it]];
            xf_node_t* node = &xf_graph_state->nodes_array[node_handle];
            xf_node_params_t* params = &node->params;

            // TODO avoid this limit?
#define xf_graph_max_barriers_per_pass_m 128
            xg_texture_memory_barrier_t texture_barriers[xf_graph_max_barriers_per_pass_m];
            std_stack_t texture_barriers_stack = std_static_stack_m ( texture_barriers );
            xg_buffer_memory_barrier_t buffer_barriers[xf_graph_max_barriers_per_pass_m]; // TODO split defines
            std_stack_t buffer_barriers_stack = std_static_stack_m ( buffer_barriers );

            if ( !node->enabled ) {
                for ( uint32_t i = 0; i < params->render_targets_count; ++i ) {
                    if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_clear_m ) {
                        xf_node_render_target_dependency_t* target = &node->params.render_targets[i];

                        xf_texture_execution_state_t state = xf_texture_execution_state_m (
                            .layout = xg_texture_layout_copy_dest_m,
                            .stage = xg_pipeline_stage_bit_transfer_m,
                            .access = xg_memory_access_bit_transfer_write_m
                        );
                        xf_resource_texture_state_barrier ( &texture_barriers_stack, target->texture, target->view, &state );

                        xg->cmd_begin_debug_region ( cmd_buffer, node->params.debug_name, node->params.debug_color, sort_key );

                        {
                            xg_barrier_set_t barrier_set = xg_barrier_set_m();
                            barrier_set.texture_memory_barriers = texture_barriers;
                            barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                            xg->cmd_barrier_set ( cmd_buffer, &barrier_set, sort_key );
                        }

                        const xf_texture_t* texture = xf_resource_texture_get ( target->texture );
                        xg->cmd_clear_texture ( cmd_buffer, texture->xg_handle, params->passthrough.render_targets[i].clear, sort_key );

                        xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                    } else if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_alias_m ) {
                        xf_resource_texture_alias ( params->render_targets[i].texture, params->passthrough.render_targets[i].alias );
                    } else if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_copy_m ) {
                        xf_node_render_target_dependency_t* target = &node->params.render_targets[i];
                        xf_node_copy_texture_dependency_t* source = &params->passthrough.render_targets[i].copy_source;
                        const xf_texture_t* target_texture = xf_resource_texture_get ( target->texture );
                        const xf_texture_t* source_texture = xf_resource_texture_get ( source->texture );

                        xf_texture_execution_state_t target_state = xf_texture_execution_state_m (
                            .layout = xg_texture_layout_copy_dest_m,
                            .stage = xg_pipeline_stage_bit_transfer_m,
                            .access = xg_memory_access_bit_transfer_write_m
                        );
                        xf_resource_texture_state_barrier ( &texture_barriers_stack, target->texture, target->view, &target_state );
                        xf_texture_execution_state_t source_state = xf_texture_execution_state_m (
                            .layout = xg_texture_layout_copy_source_m,
                            .stage = xg_pipeline_stage_bit_transfer_m,
                            .access = xg_memory_access_bit_transfer_read_m
                        );
                        xf_resource_texture_state_barrier ( &texture_barriers_stack, source->texture, source->view, &source_state );

                        xg->cmd_begin_debug_region ( cmd_buffer, node->params.debug_name, node->params.debug_color, sort_key );

                        {
                            xg_barrier_set_t barrier_set = xg_barrier_set_m();
                            barrier_set.texture_memory_barriers = texture_barriers;
                            barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                            xg->cmd_barrier_set ( cmd_buffer, &barrier_set, sort_key );
                        }

                        xg_texture_copy_params_t copy_params;
                        copy_params.source.texture = source_texture->xg_handle;
                        copy_params.source.mip_base = source->view.mip_base;
                        copy_params.source.array_base = source->view.array_base;
                        copy_params.destination.texture = target_texture->xg_handle;
                        copy_params.destination.mip_base = target->view.mip_base;
                        copy_params.destination.array_base = target->view.array_base;
                        copy_params.mip_count = source->view.mip_count;
                        copy_params.array_count = source->view.array_count;
                        copy_params.aspect = source->view.aspect;
                        copy_params.filter = xg_sampler_filter_point_m;
                        xg->cmd_copy_texture ( cmd_buffer, &copy_params, sort_key );

                        xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                    }
                }

                // TODO factor this into an outer function and call twice? need to share/wrap a bunch of local state...
                for ( uint32_t i = 0; i < params->shader_texture_writes_count; ++i ) {
                    if ( params->passthrough.shader_texture_writes[i].mode == xf_passthrough_mode_clear_m ) {
                        xf_node_shader_texture_dependency_t* texture_write = &node->params.shader_texture_writes[i];

                        xf_texture_execution_state_t state = xf_texture_execution_state_m (
                            .layout = xg_texture_layout_copy_dest_m,
                            .stage = xg_pipeline_stage_bit_transfer_m,
                            .access = xg_memory_access_bit_transfer_write_m
                        );
                        xf_resource_texture_state_barrier ( &texture_barriers_stack, texture_write->texture, texture_write->view, &state );

                        xg->cmd_begin_debug_region ( cmd_buffer, node->params.debug_name, node->params.debug_color, sort_key );

                        {
                            xg_barrier_set_t barrier_set = xg_barrier_set_m();
                            barrier_set.texture_memory_barriers = texture_barriers;
                            barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                            xg->cmd_barrier_set ( cmd_buffer, &barrier_set, sort_key );
                        }

                        const xf_texture_t* texture = xf_resource_texture_get ( texture_write->texture );
                        xg->cmd_clear_texture ( cmd_buffer, texture->xg_handle, params->passthrough.shader_texture_writes[i].clear, sort_key );

                        xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                    } else if ( params->passthrough.shader_texture_writes[i].mode == xf_passthrough_mode_alias_m ) {
                        xf_resource_texture_alias ( params->shader_texture_writes[i].texture, params->passthrough.shader_texture_writes[i].alias );
                    } else if ( params->passthrough.shader_texture_writes[i].mode == xf_passthrough_mode_copy_m ) {
                        xf_node_shader_texture_dependency_t* target = &node->params.shader_texture_writes[i];
                        xf_node_copy_texture_dependency_t* source = &params->passthrough.shader_texture_writes[i].copy_source;
                        const xf_texture_t* target_texture = xf_resource_texture_get ( target->texture );
                        const xf_texture_t* source_texture = xf_resource_texture_get ( source->texture );

                        xf_texture_execution_state_t target_state = xf_texture_execution_state_m (
                            .layout = xg_texture_layout_copy_dest_m,
                            .stage = xg_pipeline_stage_bit_transfer_m,
                            .access = xg_memory_access_bit_transfer_write_m
                        );
                        xf_resource_texture_state_barrier ( &texture_barriers_stack, target->texture, target->view, &target_state );
                        xf_texture_execution_state_t source_state = xf_texture_execution_state_m (
                            .layout = xg_texture_layout_copy_source_m,
                            .stage = xg_pipeline_stage_bit_transfer_m,
                            .access = xg_memory_access_bit_transfer_read_m
                        );
                        xf_resource_texture_state_barrier ( &texture_barriers_stack, source->texture, source->view, &source_state );

                        xg->cmd_begin_debug_region ( cmd_buffer, node->params.debug_name, node->params.debug_color, sort_key );

                        {
                            xg_barrier_set_t barrier_set = xg_barrier_set_m();
                            barrier_set.texture_memory_barriers = texture_barriers;
                            barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                            xg->cmd_barrier_set ( cmd_buffer, &barrier_set, sort_key );
                        }

                        xg_texture_copy_params_t copy_params;
                        copy_params.source.texture = source_texture->xg_handle;
                        copy_params.source.mip_base = source->view.mip_base;
                        copy_params.source.array_base = source->view.array_base;
                        copy_params.destination.texture = target_texture->xg_handle;
                        copy_params.destination.mip_base = target->view.mip_base;
                        copy_params.destination.array_base = target->view.array_base;
                        copy_params.mip_count = source->view.mip_count;
                        copy_params.array_count = source->view.array_count;
                        copy_params.aspect = source->view.aspect;
                        copy_params.filter = xg_sampler_filter_point_m;
                        xg->cmd_copy_texture ( cmd_buffer, &copy_params, sort_key );

                        xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                    }
                }

                continue;
            }

            xf_node_io_t io;

            for ( size_t i = 0; i < node->params.shader_texture_reads_count; ++i ) {
                xf_node_shader_texture_dependency_t* resource = &node->params.shader_texture_reads[i];
                const xf_texture_t* texture = xf_resource_texture_get ( resource->texture );
                xg_texture_layout_e layout = xg_texture_layout_shader_read_m;

                if ( texture->allowed_usage & xg_texture_usage_bit_depth_stencil_m ) {
                    layout = xg_texture_layout_depth_stencil_read_m;
                }

                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = layout,
                    //.stage = xg_shading_stage_to_pipeline_stage ( resource->shading_stage ),
                    .stage = resource->stage,
                    .access = xg_memory_access_bit_shader_read_m
                );

                xf_resource_texture_state_barrier ( &texture_barriers_stack, resource->texture, resource->view, &state );

                io.shader_texture_reads[i].texture = texture->xg_handle;
                io.shader_texture_reads[i].view = resource->view;
                io.shader_texture_reads[i].layout = layout;
            }

            for ( size_t i = 0; i < node->params.shader_texture_writes_count; ++i ) {
                xf_node_shader_texture_dependency_t* resource = &node->params.shader_texture_writes[i];
                const xf_texture_t* texture = xf_resource_texture_get ( resource->texture );
                xg_texture_layout_e layout = xg_texture_layout_shader_write_m;
                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = layout,
                    //.stage = xg_shading_stage_to_pipeline_stage ( resource->shading_stage ),
                    .stage = resource->stage,
                    .access = xg_memory_access_bit_shader_write_m
                );

                xf_resource_texture_state_barrier ( &texture_barriers_stack, resource->texture, resource->view, &state );

                io.shader_texture_writes[i].texture = texture->xg_handle;
                io.shader_texture_writes[i].view = resource->view;
                io.shader_texture_writes[i].layout = layout;
            }

            for ( size_t i = 0; i < node->params.shader_buffer_reads_count; ++i ) {
                xf_buffer_dependency_t* resource = &node->params.shader_buffer_reads[i];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( resource->buffer );
                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    //.stage = xg_shading_stage_to_pipeline_stage ( resource->shading_stage ),
                    .stage = resource->stage,
                    .access = xg_memory_access_bit_shader_read_m
                );

                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, resource->buffer, &state );

                io.shader_buffer_reads[i] = buffer->xg_handle;
            }

            for ( size_t i = 0; i < node->params.shader_buffer_writes_count; ++i ) {
                xf_buffer_dependency_t* resource = &node->params.shader_buffer_writes[i];
                const xf_buffer_t* buffer = xf_resource_buffer_get ( resource->buffer );
                xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                    //.stage = xg_shading_stage_to_pipeline_stage ( resource->shading_stage ),
                    .stage = resource->stage,
                    .access = xg_memory_access_bit_shader_write_m
                );

                xf_resource_buffer_state_barrier ( &buffer_barriers_stack, resource->buffer, &state );

                io.shader_buffer_writes[i] = buffer->xg_handle;
            }

            for ( size_t i = 0; i < node->params.copy_texture_reads_count; ++i ) {
                xf_node_copy_texture_dependency_t* copy = &node->params.copy_texture_reads[i];
                const xf_texture_t* texture = xf_resource_texture_get ( copy->texture );
                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = xg_texture_layout_copy_source_m,
                    .stage = xg_pipeline_stage_bit_transfer_m,
                    .access = xg_memory_access_bit_transfer_read_m
                );

                xf_resource_texture_state_barrier ( &texture_barriers_stack, copy->texture, copy->view, &state );

                io.copy_texture_reads[i].texture = texture->xg_handle;
                io.copy_texture_reads[i].view = copy->view;
                //io.copy_texture_reads[i].layout = xg_texture_layout_copy_source_m;
            }

            for ( size_t i = 0; i < node->params.copy_texture_writes_count; ++i ) {
                xf_node_copy_texture_dependency_t* copy = &node->params.copy_texture_writes[i];
                const xf_texture_t* texture = xf_resource_texture_get ( copy->texture );
                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = xg_texture_layout_copy_dest_m,
                    .stage = xg_pipeline_stage_bit_transfer_m,
                    .access = xg_memory_access_bit_transfer_write_m
                );

                xf_resource_texture_state_barrier ( &texture_barriers_stack, copy->texture, copy->view, &state );

                io.copy_texture_writes[i].texture = texture->xg_handle;
                io.copy_texture_writes[i].view = copy->view;
                //io.copy_texture_writes[i].layout = xg_texture_layout_copy_dest_m;
            }

            for ( size_t i = 0; i < node->params.render_targets_count; ++i ) {
                xf_node_render_target_dependency_t* target = &node->params.render_targets[i];
                const xf_texture_t* texture = xf_resource_texture_get ( target->texture );
                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = xg_texture_layout_render_target_m,
                    .stage = xg_pipeline_stage_bit_color_output_m,
                    .access = xg_memory_access_bit_color_write_m
                );

                xf_resource_texture_state_barrier ( &texture_barriers_stack, target->texture, target->view, &state );

                io.render_targets[i].texture = texture->xg_handle;
                io.render_targets[i].view = target->view;
            }

            if ( node->params.depth_stencil_target != xf_null_handle_m ) {
                std_assert_m ( node->params.depth_stencil_target != xf_null_handle_m );
                xf_texture_h texture_handle = node->params.depth_stencil_target;
                const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = xg_texture_layout_depth_stencil_target_m,
                    .stage = xg_pipeline_stage_bit_late_fragment_test_m,
                    .access = xg_memory_access_bit_depth_stencil_write_m
                );

                // TODO support early fragment test
                xf_resource_texture_state_barrier ( &texture_barriers_stack, texture_handle, xg_default_texture_view_m, &state );

                io.depth_stencil_target = texture->xg_handle;
            }

            xg->cmd_begin_debug_region ( cmd_buffer, node->params.debug_name, node->params.debug_color, sort_key );

            if ( node->renderpass != xg_null_handle_m ) {
                xg->cmd_begin_renderpass ( cmd_buffer, node->renderpass, sort_key );
            }

            {
                xg_barrier_set_t barrier_set = xg_barrier_set_m();
                barrier_set.texture_memory_barriers = texture_barriers;
                barrier_set.texture_memory_barriers_count = ( texture_barriers_stack.top - texture_barriers_stack.begin ) / sizeof ( xg_texture_memory_barrier_t );
                barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                xg->cmd_barrier_set ( cmd_buffer, &barrier_set, sort_key++ );
            }

            {
                xf_node_execute_args_t node_args;
                node_args.device = graph->device;
                node_args.cmd_buffer = cmd_buffer;
                node_args.resource_cmd_buffer = resource_cmd_buffer;
                node_args.workload = xg_workload;
                node_args.base_key = sort_key;
                node_args.io = &io;
                node_args.debug_name = node->params.debug_name;
                //node_args.params = &node->params;
                node->params.execute_routine ( &node_args, node->user_args );
            }

            sort_key += node->params.key_space_size;

            if ( node->renderpass != xg_null_handle_m ) {
                xg->cmd_end_renderpass ( cmd_buffer, node->renderpass, sort_key );
            }

            if ( node->params.presentable_texture != xf_null_handle_m ) {
                xf_texture_h texture_handle = node->params.presentable_texture;

                xg_texture_memory_barrier_t present_barrier[1];
                std_stack_t present_barrier_stack = std_static_stack_m ( present_barrier );

                xf_texture_execution_state_t state = xf_texture_execution_state_m (
                    .layout = xg_texture_layout_present_m,
                    .stage = xg_pipeline_stage_bit_bottom_of_pipe_m,
                    .access = xg_memory_access_bit_none_m
                );

                xf_resource_texture_state_barrier ( &present_barrier_stack, texture_handle, xg_default_texture_view_m, &state );
                std_assert_m ( present_barrier_stack.top == present_barrier + 1 );

                xg_barrier_set_t barrier_set = xg_barrier_set_m();
                barrier_set.texture_memory_barriers = present_barrier;
                barrier_set.texture_memory_barriers_count = 1;
                xg->cmd_barrier_set ( cmd_buffer, &barrier_set, sort_key );
            }

            xg->cmd_end_debug_region ( cmd_buffer, sort_key );
        }
    }

    // Advance multi resources
    for ( uint64_t i = 0; i < graph->multi_textures_count; ++i ) {
        xg_swapchain_h swapchain = xf_resource_multi_texture_get_swapchain ( graph->multi_textures_array[i] );
        if ( swapchain == xg_null_handle_m ) {
            xf_resource_multi_texture_advance ( graph->multi_textures_array[i] );
        }
    }
}

void xf_graph_get_info ( xf_graph_info_t* info, xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    info->device = graph->device;
    info->swapchain = graph->swapchain;
    info->node_count = graph->nodes_count;

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        info->nodes[i] = graph->nodes[i];
    }
}

void xf_graph_get_node_info ( xf_node_info_t* info, xf_node_h node_handle ) {
    xf_node_t* node = &xf_graph_state->nodes_array[node_handle];
    info->enabled = node->enabled;
}

void xf_graph_debug_print ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xf_graph_linearize ( graph );

    std_log_info_m ( "Graph execution order:" );

    for ( uint64_t i = 0; i < graph->nodes_count; ++i ) {
        size_t node_idx = graph->nodes_execution_order[i];
        xf_node_t* node = &xf_graph_state->nodes_array[node_idx];
        std_log_info_m ( std_fmt_tab_m std_fmt_str_m, node->params.debug_name );
    }
}

void xf_graph_node_enable ( xf_node_h node_handle ) {
    xf_node_t* node = &xf_graph_state->nodes_array[node_handle];
    node->enabled = true;
}

void xf_graph_node_disable ( xf_node_h node_handle ) {
    xf_node_t* node = &xf_graph_state->nodes_array[node_handle];
    if ( node->params.passthrough.enable ) {
        node->enabled = false;
    }
}

void xf_graph_debug_ui ( xi_i* xi, xi_workload_h workload, xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &xf_graph_state->nodes_array[graph->nodes[graph->nodes_execution_order[i]]];

        if ( node->params.passthrough.enable ) {
            bool node_enabled = node->enabled;

            xi_label_state_t label_state = xi_label_state_m ();
            std_str_copy_static_m ( label_state.text, node->params.debug_name );
            xi->add_label ( workload, &label_state );

            xi_switch_state_t switch_state = xi_switch_state_m (
                .width = 14,
                .height = 14,
                .value = node_enabled,
                .style = xi_style_m (
                    .horizontal_alignment = xi_horizontal_alignment_right_to_left_m
                ),
            );
            xi->add_switch ( workload, &switch_state );

            xi->newline();

            if ( switch_state.value != node_enabled ) {
                node->enabled = switch_state.value;
            }
        }
    }
}
