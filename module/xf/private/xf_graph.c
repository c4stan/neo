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

xf_graph_h xf_graph_create ( const xf_graph_params_t* params ) {
    xf_graph_t* graph = std_list_pop_m ( &xf_graph_state->graphs_freelist );
    std_mem_zero_m ( graph );
    graph->params = *params;
    graph->heap.memory_handle = xg_null_memory_handle_m;

    xf_graph_h handle = ( xf_graph_h ) ( graph - xf_graph_state->graphs_array );
    std_bitset_set ( xf_graph_state->graphs_bitset, handle );
    return handle;
}

xf_node_h xf_graph_node_create ( xf_graph_h graph_handle, const xf_node_params_t* params ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    std_assert_m ( graph->nodes_count < xf_graph_max_nodes_m );
    std_assert_m ( !graph->is_finalized );
    
    xf_node_t* node = &graph->nodes_array[graph->nodes_count++];
    std_mem_zero_m ( node );
    node->params = *params;
    node->enabled = true;
    node->renderpass = xg_null_handle_m;
    node->renderpass_params.render_textures = xg_render_textures_layout_m();

    if ( node->params.type == xf_node_type_custom_pass_m) {
        xf_node_custom_pass_params_t* pass = &node->params.pass.custom;
        if ( pass->copy_args && pass->user_args.base ) {
            void* alloc = std_virtual_heap_alloc ( pass->user_args.size, 16 ); // TODO some kind of linear allocator
            std_mem_copy ( alloc, pass->user_args.base, pass->user_args.size );
            node->user_alloc = alloc;
        } else {
            node->user_alloc = NULL;
        }
    } else if ( node->params.type == xf_node_type_compute_pass_m ) {
        xf_node_compute_pass_params_t* pass = &node->params.pass.compute;
        if ( pass->copy_uniform_data && pass->uniform_data.base ) {
            void* alloc = std_virtual_heap_alloc ( pass->uniform_data.size, 16 ); // TODO some kind of linear allocator
            std_mem_copy ( alloc, pass->uniform_data.base, pass->uniform_data.size );
            node->user_alloc = alloc;
        } else {
            node->user_alloc = NULL;
        }
    }

    xf_node_h node_handle = ( xf_node_h ) ( node - graph->nodes_array );

    // Accumulate resource usage on resources, will be used later when creating them
    // TODO don't do this here
    //xf_graph_node_accumulate_resource_usage ( node_handle );
    //xf_graph_node_accumulate_reads_writes ( node_handle );

    return node_handle;
}

/*
    TODO
        go through the ordered nodes before each execute, take note for each resource usage in each pass
        the previous use for that resource (read/write, pipeline stage) so that when filling barriers
        all data is ready for each pass resource
        this means some of the stuff done when a node is added to a graph can be removed (we're basically
        moving it to execute time)
*/

static void xf_graph_clear_resource_dependencies ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        std_mem_zero_static_array_m ( graph->textures_array[i].deps.mips );
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        std_mem_zero_m ( &graph->buffers_array[i].deps );
    }
}

static int xf_graph_resource_access_cmp ( const void* a, const void* b, const void* arg ) {
    std_auto_m graph = ( xf_graph_t* ) arg;
    std_auto_m a1 = ( xf_graph_resource_access_t* ) a;
    std_auto_m a2 = ( xf_graph_resource_access_t* ) b;

    xf_node_t* n1 = &graph->nodes_array[a1->node];
    xf_node_t* n2 = &graph->nodes_array[a2->node];
    uint32_t o1 = n1->execution_order;
    uint32_t o2 = n2->execution_order;
    return o1 < o2 ? -1 : o1 > o2 ? 1 : 0;
}

std_unused_static_m()
static void xf_graph_sort_resource_dependencies ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_texture_t* texture = &graph->textures_array[i];
        xf_texture_info_t info;
        xf_resource_texture_get_info ( &info, texture->handle );
        
        xf_graph_resource_access_t tmp;
        if ( info.view_access == xg_texture_view_access_default_only_m ) {
            if ( texture->deps.shared.count > 0 ) {
                std_sort_insertion ( texture->deps.shared.array, sizeof ( xf_graph_resource_access_t ), texture->deps.shared.count, xf_graph_resource_access_cmp, graph, &tmp );
            }
        } else if ( info.view_access == xg_texture_view_access_separate_mips_m ) {
            uint32_t mip_count = info.mip_levels;
            for ( uint32_t i = 0; i < mip_count; ++i ) {
                if ( texture->deps.mips[i].count > 0 ) {
                    std_sort_insertion ( texture->deps.mips[i].array, sizeof ( xf_graph_resource_access_t ), texture->deps.mips[i].count, xf_graph_resource_access_cmp, graph, &tmp );
                }
            }
        } else {
            std_not_implemented_m();
        }
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_graph_buffer_t* buffer = &graph->buffers_array[i];
        xf_graph_resource_access_t tmp;
        if ( buffer->deps.count > 0 ) {
            std_sort_insertion ( buffer->deps.array, sizeof ( xf_graph_resource_access_t ), buffer->deps.count, xf_graph_resource_access_cmp, graph, &tmp );
        }
    }
}

static void xf_graph_add_texture_dependency ( xf_graph_h graph_handle, xf_node_h node_handle, xf_graph_texture_h texture_handle, xg_texture_view_t view, xf_graph_resource_access_e access ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_texture_t* texture = &graph->textures_array[texture_handle];
    xf_texture_info_t info;
    xf_resource_texture_get_info ( &info, texture->handle );

    if ( info.view_access == xg_texture_view_access_default_only_m ) {
        texture->deps.shared.array[texture->deps.shared.count++] = xf_graph_resource_access_m ( .node = node_handle, .access = access );
    } else if ( info.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? info.mip_levels - view.mip_base : view.mip_count;
        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->deps.mips[view.mip_base + i].array[texture->deps.mips[view.mip_base + i].count++] = xf_graph_resource_access_m ( .node = node_handle, .access = access );
        }
        //texture->deps.mips[view.mip_base].access[texture->deps.mips[view.mip_base].access_count++] = xf_graph_resource_access_m ( .node = node_handle, .access = access );
    } else {
        std_not_implemented_m();
    }
}

static void xf_graph_add_buffer_dependency ( xf_graph_h graph_handle, xf_node_h node_handle, xf_graph_buffer_h buffer_handle, xf_graph_resource_access_e access ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_buffer_t* buffer = &graph->buffers_array[buffer_handle];
    buffer->deps.array[buffer->deps.count++] = xf_graph_resource_access_m ( .node = node_handle, .access = access );
}

static void xf_graph_node_accumulate_dependencies ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    const xf_node_resource_params_t* params = &node->params.resources;

    // Shader resources
    for ( size_t i = 0; i < params->sampled_textures_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->sampled_textures[i];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.sampled_textures[i], dep->view, xf_graph_resource_access_sampled_m );
    }

    for ( size_t i = 0; i < params->storage_texture_reads_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->storage_texture_reads[i];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.storage_texture_reads[i], dep->view, xf_graph_resource_access_storage_read_m );
    }

    for ( size_t i = 0; i < params->storage_texture_writes_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->storage_texture_writes[i];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.storage_texture_writes[i], dep->view, xf_graph_resource_access_storage_write_m );
    }

    for ( size_t i = 0; i < params->uniform_buffers_count; ++i ) {
        xf_graph_add_buffer_dependency ( graph_handle, node_handle, node->resource_refs.uniform_buffers[i], xf_graph_resource_access_uniform_m );
    }

    for ( size_t i = 0; i < params->storage_buffer_reads_count; ++i ) {
        xf_graph_add_buffer_dependency ( graph_handle, node_handle, node->resource_refs.storage_buffer_reads[i], xf_graph_resource_access_storage_read_m );
    }

    for ( size_t i = 0; i < params->storage_buffer_writes_count; ++i ) {
        xf_graph_add_buffer_dependency ( graph_handle, node_handle, node->resource_refs.storage_buffer_writes[i], xf_graph_resource_access_storage_write_m );
    }

    // Copy resources
    for ( size_t i = 0; i < params->copy_texture_reads_count; ++i ) {
        const xf_copy_texture_dependency_t* dep = &params->copy_texture_reads[i];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.copy_texture_reads[i], dep->view, xf_graph_resource_access_copy_read_m );
    }

    for ( size_t i = 0; i < params->copy_texture_writes_count; ++i ) {
        const xf_copy_texture_dependency_t* dep = &params->copy_texture_writes[i];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.copy_texture_writes[i], dep->view, xf_graph_resource_access_copy_write_m );
    }

    for ( size_t i = 0; i < params->copy_buffer_reads_count; ++i ) {
        xf_graph_add_buffer_dependency ( graph_handle, node_handle, node->resource_refs.copy_buffer_reads[i], xf_graph_resource_access_copy_read_m );
    }

    for ( size_t i = 0; i < params->copy_buffer_writes_count; ++i ) {
        xf_graph_add_buffer_dependency ( graph_handle, node_handle, node->resource_refs.copy_buffer_writes[i], xf_graph_resource_access_copy_write_m );
    }

    // Render targets and depth stencil
    for ( size_t i = 0; i < params->render_targets_count; ++i ) {
        const xf_render_target_dependency_t* dep = &params->render_targets[i];
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.render_targets[i], dep->view, xf_graph_resource_access_render_target_m );
    }

    if ( params->depth_stencil_target != xf_null_handle_m ) {
        std_assert_m ( params->depth_stencil_target != xf_null_handle_m );
        xf_graph_add_texture_dependency ( graph_handle, node_handle, node->resource_refs.depth_stencil_target, xg_texture_view_m(), xf_graph_resource_access_depth_target_m );
    }
}

static void xf_graph_node_accumulate_resource_usage ( xf_graph_h graph_handle, xf_node_h node ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    const xf_node_resource_params_t* params = &graph->nodes_array[node].params.resources;

    // Shader resources
    for ( size_t i = 0; i < params->sampled_textures_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->sampled_textures[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_sampled_m );
    }

    for ( size_t i = 0; i < params->storage_texture_reads_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->storage_texture_reads[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_storage_m );
    }

    for ( size_t i = 0; i < params->storage_texture_writes_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->storage_texture_writes[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_storage_m );
    }

    for ( size_t i = 0; i < params->uniform_buffers_count; ++i ) {
        xf_buffer_h buffer = params->uniform_buffers[i].buffer;
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_uniform_m );
    }

    for ( size_t i = 0; i < params->storage_buffer_reads_count; ++i ) {
        xf_buffer_h buffer = params->storage_buffer_reads[i].buffer;
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_storage_m );
    }

    for ( size_t i = 0; i < params->storage_buffer_writes_count; ++i ) {
        xf_buffer_h buffer = params->storage_buffer_writes[i].buffer;
        xf_resource_buffer_add_usage ( buffer, xg_buffer_usage_bit_storage_m );
    }

    // Copy resources
    for ( size_t i = 0; i < params->copy_texture_reads_count; ++i ) {
        const xf_copy_texture_dependency_t* dep = &params->copy_texture_reads[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_copy_source_m );
    }

    for ( size_t i = 0; i < params->copy_texture_writes_count; ++i ) {
        const xf_copy_texture_dependency_t* dep = &params->copy_texture_writes[i];
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
        const xf_render_target_dependency_t* dep = &params->render_targets[i];
        xf_resource_texture_add_usage ( dep->texture, xg_texture_usage_bit_render_target_m );
    }

    if ( params->depth_stencil_target != xf_null_handle_m ) {
        std_assert_m ( params->depth_stencil_target != xf_null_handle_m );
        xf_texture_h texture = params->depth_stencil_target;
        xf_resource_texture_add_usage ( texture, xg_texture_usage_bit_depth_stencil_m );
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

static void xf_graph_add_node_dependencies ( xf_graph_h graph_handle, xf_resource_dependencies_t* deps ) {
    xf_node_h prev_write = xf_null_handle_m;
    xf_node_h prev_reads[xf_graph_max_nodes_m];
    uint32_t prev_reads_count = 0;

    for ( uint32_t i = 0; i < deps->count; ++i ) {
        xf_node_h node_handle = deps->array[i].node;
        xf_graph_resource_access_e access = deps->array[i].access;
        if ( xf_graph_resource_access_is_write ( access ) ) {
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
        } else if ( xf_graph_resource_access_is_read ( access ) ) {
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

        if ( info.view_access == xg_texture_view_access_default_only_m ) {
            xf_graph_add_node_dependencies ( graph_handle, &texture->deps.shared );
        } else if ( info.view_access == xg_texture_view_access_separate_mips_m ) {
            for ( uint32_t j = 0; j < std_static_array_capacity_m ( texture->deps.mips ); ++j ) {
                xf_graph_add_node_dependencies ( graph_handle, &texture->deps.mips[j] );
            }
        } else {
            std_not_implemented_m();
        }
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_graph_buffer_t* buffer = &graph->buffers_array[i];
        xf_graph_add_node_dependencies ( graph_handle, &buffer->deps );
    }

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        for ( uint32_t j = 0; j < node->params.node_dependencies_count; ++j ) {
            xf_node_t* source_node = &graph->nodes_array[node->params.node_dependencies[j]];
            source_node->next_nodes[source_node->next_nodes_count++] = i;
        }
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

static void xf_graph_fill_prev_nodes_from_next ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = i;
        xf_node_t* node = &graph->nodes_array[node_handle];
        node->prev_nodes_count = 0;        
    }

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = i;
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
        count[i] = graph->nodes_array[i].prev_nodes_count;
    }

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = i;
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

        for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; q++ ) {
            if ( node->params.queue == q ) {
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

                    if ( prev_node->params.queue == q ) {
                        if ( latest_prev_order < prev_node->execution_order ) {
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

    #if 0
    for ( xg_cmd_queue_e q = 0; q < xg_cmd_queue_count_m; ++q ) {
        char stack_buffer[128];
        std_stack_t stack = std_static_stack_m ( stack_buffer );
        char* queue_name = "";
        if ( q == xg_cmd_queue_graphics_m ) queue_name = "GRAPH";
        else if ( q == xg_cmd_queue_compute_m ) queue_name = "COMP "; 
        else if ( q == xg_cmd_queue_copy_m ) queue_name = "COPY ";
        else std_not_implemented_m();
        std_stack_string_append ( &stack, queue_name );
        std_stack_string_append ( &stack, " | " );
        for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
            char buffer[32] = " -";
            if ( graph->cross_queue_node_deps[i][q] != -1 ) {
                std_u32_to_str ( buffer, 32, graph->cross_queue_node_deps[i][q], 2 );
            }
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, " " );
        }
        std_log_info_m ( std_fmt_tab_m std_fmt_str_m, stack_buffer );
    }
    #else
    
    #endif
}

static void xf_graph_linearize ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    // TODO try to maximize distance between dependencies when determining execution order

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

    bool print_node_deps = false;
    if ( print_node_deps ) {
    std_log_info_m ( std_fmt_str_m " node dependencies:", graph->params.debug_name );
        for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
            xf_node_t* node = &graph->nodes_array[i];
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
                std_assert_m ( node->next_nodes[j] > i );
            }
            std_stack_string_append ( &stack, "] | prev: [" );
            for ( uint32_t j = 0; j < node->prev_nodes_count; ++j ) {
                if ( j > 0 ) {
                    std_stack_string_append ( &stack, " " );
                }
                xf_node_t* dep_node = &graph->nodes_array[node->prev_nodes[j]];
                std_stack_string_append ( &stack, dep_node->params.debug_name );
                std_assert_m ( node->prev_nodes[j] < i );
            }
            std_stack_string_append ( &stack, "]" );
            std_log_info_m ( std_fmt_tab_m std_fmt_str_m, buffer );
        }
    }

    xf_graph_build_cross_queue_deps ( graph_handle );
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

// TODO move this into xf_resource?
#if 0
std_unused_static_m()
static void xf_graph_build_texture ( xf_texture_h texture_handle, xg_device_h device, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

    if ( texture->is_external ) {
        xg_texture_info_t info;
        xg->get_texture_info ( &info, texture->xg_handle );
        xf_resource_texture_update_info ( texture_handle, &info );
        std_assert_m ( ( texture->required_usage & texture->allowed_usage ) == texture->required_usage );
    } else {
        bool create_new = false;

        if ( texture->xg_handle == xg_null_handle_m ) {
            create_new = true;
        }

        if ( texture->required_usage &~ texture->allowed_usage ) {
            create_new = true;
        }

        if ( create_new ) {
            if ( texture->xg_handle != xg_null_handle_m ) {
                std_assert_m ( false );
                //xg->cmd_destroy_texture ( resource_cmd_buffer, texture->xg_handle, xg_resource_cmd_buffer_time_workload_start_m );
            }

            xg_texture_params_t params = xf_graph_texture_params ( device, texture_handle );
            xg_texture_h new_texture = xg->cmd_create_texture ( resource_cmd_buffer, &params );
            xf_resource_texture_map_to_new ( texture_handle, new_texture, params.allowed_usage );

            if ( texture->params.clear_on_create ) {
                // TODO support depth stencil clears
                // TODO is hard coding 0 as key ok here?
                xg->cmd_clear_texture ( cmd_buffer, 0, new_texture, texture->params.clear.color );
            }
        }

        //xf_resource_texture_set_dirty ( texture_handle, false );
    }

    // TODO better way of doing this?
    //xf_resource_texture_alias ( texture_handle, xf_null_handle_m );
}
#endif

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
            new_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &params );
        }
#endif

        xf_resource_buffer_map_to_new ( buffer_handle, new_buffer, params.allowed_usage );
    }

    // TODO
    std_unused_m ( cmd_buffer );
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

static void xf_graph_gather_unique_buffers ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t buffer_hashes[xf_graph_max_buffers_m * 2];
    uint64_t buffer_values[xf_graph_max_buffers_m * 2];
    std_hash_map_t buffers_map = std_static_hash_map_m ( buffer_hashes, buffer_values );
    std_auto_m buffers_array = std_static_array_m ( xf_graph_buffer_t, graph->buffers_array );

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        xf_node_resource_params_t* params = &node->params.resources;

        for ( size_t resource_it = 0; resource_it < params->uniform_buffers_count; ++resource_it ) {
            xf_shader_buffer_dependency_t* resource = &params->uniform_buffers[resource_it];
            uint64_t ref = buffers_array.count;
            if ( std_hash_map_try_insert ( &ref, &buffers_map, std_hash_64_m ( resource->buffer ), ref ) ) {
                std_array_push_m ( &buffers_array, xf_graph_buffer_m ( .handle = resource->buffer ) );
            }
            node->resource_refs.uniform_buffers[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->storage_buffer_reads_count; ++resource_it ) {
            xf_shader_buffer_dependency_t* resource = &params->storage_buffer_reads[resource_it];
            uint64_t ref = buffers_array.count;
            if ( std_hash_map_try_insert ( &ref, &buffers_map, std_hash_64_m ( resource->buffer ), ref ) ) {
                std_array_push_m ( &buffers_array, xf_graph_buffer_m ( .handle = resource->buffer ) );
            }
            node->resource_refs.storage_buffer_reads[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->storage_buffer_writes_count; ++resource_it ) {
            xf_shader_buffer_dependency_t* resource = &params->storage_buffer_writes[resource_it];
            uint64_t ref = buffers_array.count;
            if ( std_hash_map_try_insert ( &ref, &buffers_map, std_hash_64_m ( resource->buffer ), ref ) ) {
                std_array_push_m ( &buffers_array, xf_graph_buffer_m ( .handle = resource->buffer ) );
            }
            node->resource_refs.storage_buffer_writes[resource_it] = ( uint32_t ) ref;
        }
        
        for ( size_t resource_it = 0; resource_it < params->copy_buffer_reads_count; ++resource_it ) {
            xf_buffer_h buffer_handle = params->copy_buffer_reads[resource_it];
            uint64_t ref = buffers_array.count;
            if ( std_hash_map_try_insert ( &ref, &buffers_map, std_hash_64_m ( buffer_handle ), ref ) ) {
                std_array_push_m ( &buffers_array, xf_graph_buffer_m ( .handle = buffer_handle ) );
            }
            node->resource_refs.copy_buffer_reads[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->copy_buffer_writes_count; ++resource_it ) {
            xf_buffer_h buffer_handle = params->copy_buffer_writes[resource_it];
            uint64_t ref = buffers_array.count;
            if ( std_hash_map_try_insert ( &ref, &buffers_map, std_hash_64_m ( buffer_handle ), ref ) ) {
                std_array_push_m ( &buffers_array, xf_graph_buffer_m ( .handle = buffer_handle ) );
            }
            node->resource_refs.copy_buffer_writes[resource_it] = ( uint32_t ) ref;
        }
    }

    graph->buffers_count = buffers_array.count;
}

static void xf_graph_gather_unique_textures ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    uint64_t texture_hashes[xf_graph_max_textures_m * 2];
    uint64_t texture_values[xf_graph_max_textures_m];
    std_hash_map_t textures_map = std_static_hash_map_m ( texture_hashes, texture_values );
    std_auto_m textures_array = std_static_array_m ( xf_graph_texture_t, graph->textures_array );

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        xf_node_resource_params_t* params = &node->params.resources;

        for ( size_t resource_it = 0; resource_it < params->render_targets_count; ++resource_it ) {
            xf_render_target_dependency_t* resource = &params->render_targets[resource_it];
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( resource->texture ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = resource->texture ) );
            }
            node->resource_refs.render_targets[resource_it] = ( uint32_t ) ref;
        }

        if ( params->depth_stencil_target != xf_null_handle_m ) {
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( params->depth_stencil_target ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = params->depth_stencil_target ) );
            }
            node->resource_refs.depth_stencil_target = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->sampled_textures_count; ++resource_it ) {
            xf_shader_texture_dependency_t* resource = &params->sampled_textures[resource_it];
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( resource->texture ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = resource->texture ) );
            }
            node->resource_refs.sampled_textures[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->storage_texture_reads_count; ++resource_it ) {
            xf_shader_texture_dependency_t* resource = &params->storage_texture_reads[resource_it];
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( resource->texture ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = resource->texture ) );
            }
            node->resource_refs.storage_texture_reads[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->storage_texture_writes_count; ++resource_it ) {
            xf_shader_texture_dependency_t* resource = &params->storage_texture_writes[resource_it];
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( resource->texture ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = resource->texture ) );
            }
            node->resource_refs.storage_texture_writes[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->copy_texture_reads_count; ++resource_it ) {
            xf_texture_h texture_handle = params->copy_texture_reads[resource_it].texture;
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( texture_handle ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = texture_handle ) );
            }
            node->resource_refs.copy_texture_reads[resource_it] = ( uint32_t ) ref;
        }

        for ( size_t resource_it = 0; resource_it < params->copy_texture_writes_count; ++resource_it ) {
            xf_texture_h texture_handle = params->copy_texture_writes[resource_it].texture;
            uint64_t ref = textures_array.count;
            if ( std_hash_map_try_insert ( &ref, &textures_map, std_hash_64_m ( texture_handle ), ref ) ) {
                std_array_push_m ( &textures_array, xf_graph_texture_m ( .handle = texture_handle ) );
            }
            node->resource_refs.copy_texture_writes[resource_it] = ( uint32_t ) ref;
        }
    }

    graph->textures_count = textures_array.count;
}

static void xf_graph_gather_unique_multi_textures ( xf_graph_h graph_handle ) {
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

static void xf_graph_scan_resources ( xf_graph_h graph_handle ) {
    xf_graph_gather_unique_textures ( graph_handle );
    xf_graph_gather_unique_buffers ( graph_handle );
    xf_graph_gather_unique_multi_textures ( graph_handle );
}

static void xf_graph_compute_buffer_lifespans ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        xf_node_resource_params_t* params = &node->params.resources;

        for ( size_t resource_it = 0; resource_it < params->uniform_buffers_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.uniform_buffers[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->buffers_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->storage_buffer_reads_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.storage_buffer_reads[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->buffers_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->storage_buffer_writes_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.storage_buffer_writes[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->buffers_array[ref].lifespan, node->execution_order, node->params.queue );
        }
        
        for ( size_t resource_it = 0; resource_it < params->copy_buffer_reads_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.copy_buffer_reads[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->buffers_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->copy_buffer_writes_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.copy_buffer_writes[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->buffers_array[ref].lifespan, node->execution_order, node->params.queue );
        }
    }
}

static void xf_graph_compute_texture_lifespans ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( size_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];
        xf_node_resource_params_t* params = &node->params.resources;

        for ( size_t resource_it = 0; resource_it < params->render_targets_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.render_targets[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        if ( params->depth_stencil_target != xf_null_handle_m ) {
            uint64_t ref = node->resource_refs.depth_stencil_target;
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->sampled_textures_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.sampled_textures[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->storage_texture_reads_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.storage_texture_reads[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->storage_texture_writes_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.storage_texture_writes[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->copy_texture_reads_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.copy_texture_reads[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
        }

        for ( size_t resource_it = 0; resource_it < params->copy_texture_writes_count; ++resource_it ) {
            uint64_t ref = node->resource_refs.copy_texture_writes[resource_it];
            xf_graph_resource_lifespan_extend ( &graph->textures_array[ref].lifespan, node->execution_order, node->params.queue );
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
    xf_device_texture_h handle;
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

#if 1
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
        std_unused_m ( texture );

#if 0
        if ( texture->is_external ) {
            xg_texture_info_t info;
            xg->get_texture_info ( &info, texture->xg_handle );
            xf_resource_texture_update_info ( texture_handle, &info );
            std_assert_m ( ( texture->required_usage & texture->allowed_usage ) == texture->required_usage );
            continue;
        }
#endif

        if ( xf_resource_texture_is_multi ( texture_handle ) ) {
            permanent_textures_array[permanent_textures_count++] = graph_texture;
            continue;
        }

        xf_device_texture_t* device_texture = xf_resource_texture_get_device_texture ( texture_handle );
        if ( device_texture ) {
            std_assert_m ( device_texture->handle != xg_null_handle_m );
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

    bool print_lifespans = true;
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

    bool alias_resources = true;
    bool alias_memory = true;

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
            //xf_device_texture_params_t device_texture_params = xf_device_texture_params_m ( .is_external = false );
            //std_str_copy_static_m ( device_texture_params.debug_name, params.debug_name );
            //xf_device_texture_h device_texture_handle = xf_resource_device_texture_create ( &device_texture_params );
            //xf_resource_texture_bind ( graph_texture->handle, device_texture_handle );

            committed_textures_array[committed_textures_count++] = ( xf_graph_committed_texture_t ) {
                .params = params,
                .req = transient_texture->req,
                //.handle = device_texture_handle,
                .lifespans_count = 1,
                .lifespans = { graph_texture->lifespan },
                .transient_textures_list = transient_texture,
            };
        }
    }

    // Debug print transient to committed aliasing
    bool print_alias_list = true;
    if ( print_alias_list ) {
        for ( uint32_t i = 0; i < committed_textures_count; ++i ) {
            xf_graph_committed_texture_t* committed_texture = &committed_textures_array[i];
            std_log_info_m ( std_fmt_str_m " alias list:", committed_texture->params.debug_name );
            
            xf_graph_transient_texture_t* transient_texture = committed_texture->transient_textures_list;
            while ( transient_texture ) {
                xf_texture_h texture_handle = graph->textures_array[transient_texture->handle].handle;
                xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
                std_log_info_m ( std_fmt_tab_m std_fmt_str_m, texture->params.debug_name );
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
        xg_texture_h xg_handle = xg->cmd_create_texture ( resource_cmd_buffer, &committed_texture->params );
        xg_texture_info_t texture_info;
        xg->get_texture_info ( &texture_info, xg_handle );
        xf_device_texture_h device_texture_handle = xf_resource_device_texture_create ( &xf_device_texture_params_m (
            .handle = xg_handle,
            .info = texture_info,
        ) );
        committed_texture->handle = device_texture_handle;

        xf_graph_transient_texture_t* transient_texture = committed_texture->transient_textures_list;
        while ( transient_texture ) {
            xf_texture_h texture_handle = graph->textures_array[transient_texture->handle].handle;
            xf_resource_texture_bind ( texture_handle, device_texture_handle );
            transient_texture = transient_texture->next;
        }
    }

    for ( uint32_t i = 0; i < permanent_textures_count; ++i ) {
        xf_graph_texture_t* graph_texture = permanent_textures_array[i];
        xf_texture_h texture_handle = graph_texture->handle;
        xf_texture_t* texture = xf_resource_texture_get ( texture_handle );

        bool create_new = false;

        if ( texture->device_texture_handle == xg_null_handle_m ) {
            create_new = true;
        } else {
            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            if ( texture->required_usage &~ device_texture->info.allowed_usage ) {
                // TODO free current device texture
                create_new = true;
            }
        }

        if ( create_new ) {
            xg_texture_params_t params = xf_graph_texture_params ( graph->params.device, texture_handle );
            xg_texture_h xg_handle = xg->cmd_create_texture ( resource_cmd_buffer, &params );
            xg_texture_info_t texture_info;
            xg->get_texture_info ( &texture_info, xg_handle );
            xf_device_texture_h device_texture_handle = xf_resource_device_texture_create ( &xf_device_texture_params_m (
                .handle = xg_handle,
                .info = texture_info,
            ) );
            xf_resource_texture_bind ( texture_handle, device_texture_handle );

            uint32_t graph_texture_idx = graph_texture - graph->textures_array;
            graph->owned_textures_array[graph->owned_textures_count++] = graph_texture_idx;
        }
    }

    // Store the heap info into the graph for later release
    graph->heap.memory_handle = heap_alloc.handle;
    for ( uint32_t i = 0; i < heap.textures_count; ++i ) {
        graph->heap.textures_array[graph->heap.textures_count++] = heap.textures_array[i].committed_texture->handle;
    }

#else
    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_build_texture ( graph->textures_array[i].handle, graph->params.device, xg, cmd_buffer, resource_cmd_buffer );
    }
#endif
}

static void xf_graph_build_resources ( xf_graph_h graph_handle, xg_i* xg, xg_cmd_buffer_h cmd_buffer, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xf_graph_build_textures ( graph_handle, xg, cmd_buffer, resource_cmd_buffer );

    // TODO
    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_graph_build_buffer ( graph->buffers_array[i].handle, graph->params.device, xg, cmd_buffer, resource_cmd_buffer );
    }
}

std_unused_static_m()
static void xf_graph_build_renderpasses ( xf_graph_t* graph, xg_i* xg, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];

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

#if 0
static void xf_graph_add_texture_transition ( xf_graph_h graph_handle, xf_node_h node_handle, xf_graph_texture_h texture_handle, xg_texture_view_t view, xf_texture_execution_state_t state ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_texture_t* texture = &graph->textures_array[texture_handle];

    xf_texture_info_t info;
    xf_resource_texture_get_info ( &info, texture->handle );

    if ( info.view_access == xg_texture_view_access_default_only_m ) {
        texture->transitions.shared.array[texture->transitions.shared.count++] = xf_graph_texture_transition_m ( .node = node_handle, .state = state, .texture = texture->handle );
    } else if ( info.view_access == xg_texture_view_access_separate_mips_m ) {
        uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? info.mip_levels - view.mip_base : view.mip_count;
        for ( uint32_t i = 0; i < mip_count; ++i ) {
            texture->transitions.mips[view.mip_base + i].array[texture->transitions.mips[view.mip_base + i].count++] = xf_graph_texture_transition_m ( .node = node_handle, .state = state, .texture = texture->handle );
        }
    } else {
        std_not_implemented_m();
    }
}

static void xf_graph_add_buffer_transition ( xf_graph_h graph_handle, xf_node_h node_handle, xf_graph_buffer_h buffer_handle, xf_buffer_execution_state_t state ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_graph_buffer_t* buffer = &graph->buffers_array[buffer_handle];
    buffer->transitions.array[buffer->transitions.count++] = xf_graph_buffer_transition_m ( .node = node_handle, .state = state );
}

static void xf_graph_node_accumulate_transitions ( xf_graph_h graph_handle, xf_node_h node_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    xf_node_t* node = &graph->nodes_array[node_handle];
    const xf_node_resource_params_t* params = &node->params.resources;
    xg_cmd_queue_e queue = node->params.queue;

    // Shader resources
    for ( size_t i = 0; i < params->sampled_textures_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->sampled_textures[i];
        const xf_texture_t* texture = xf_resource_texture_get ( dep->texture );
        
        xg_texture_layout_e layout = xg_texture_layout_shader_read_m;
        if ( texture->allowed_usage & xg_texture_usage_bit_depth_stencil_m ) {
            layout = xg_texture_layout_depth_stencil_read_m;
        }
        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = layout,
            .stage = dep->stage,
            .access = xg_memory_access_bit_shader_read_m,
            .queue = queue,
        );
        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.sampled_textures[i], dep->view, state );
    }

    for ( size_t i = 0; i < params->storage_texture_reads_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->storage_texture_reads[i];
        const xf_texture_t* texture = xf_resource_texture_get ( dep->texture );

        xg_texture_layout_e layout = xg_texture_layout_shader_read_m;
        if ( texture->allowed_usage & xg_texture_usage_bit_depth_stencil_m ) {
            layout = xg_texture_layout_depth_stencil_read_m;
        }
        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = layout,
            .stage = dep->stage,
            .access = xg_memory_access_bit_shader_read_m,
            .queue = queue,
        );

        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.storage_texture_reads[i], dep->view, state );
    }

    for ( size_t i = 0; i < params->storage_texture_writes_count; ++i ) {
        const xf_shader_texture_dependency_t* dep = &params->storage_texture_writes[i];

        xg_texture_layout_e layout = xg_texture_layout_shader_write_m;
        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = layout,
            .stage = dep->stage,
            .access = xg_memory_access_bit_shader_write_m,
            .queue = queue,
        );

        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.storage_texture_writes[i], dep->view, state );
    }

    for ( size_t i = 0; i < params->uniform_buffers_count; ++i ) {
        const xf_shader_buffer_dependency_t* dep = &params->uniform_buffers[i];

        xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
            .stage = dep->stage,
            .access = xg_memory_access_bit_shader_read_m,
            .queue = queue,
        );

        xf_graph_add_buffer_transition ( graph_handle, node_handle, node->resource_refs.uniform_buffers[i], state );
    }

    for ( size_t i = 0; i < params->storage_buffer_reads_count; ++i ) {
        const xf_shader_buffer_dependency_t* dep = &params->storage_buffer_reads[i];

        xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
            .stage = dep->stage,
            .access = xg_memory_access_bit_shader_read_m,
            .queue = queue,
        );

        xf_graph_add_buffer_transition ( graph_handle, node_handle, node->resource_refs.storage_buffer_reads[i], state );
    }

    for ( size_t i = 0; i < params->storage_buffer_writes_count; ++i ) {
        const xf_shader_buffer_dependency_t* dep = &params->storage_buffer_writes[i];

        xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
            .stage = dep->stage,
            .access = xg_memory_access_bit_shader_write_m,
            .queue = queue,
        );

        xf_graph_add_buffer_transition ( graph_handle, node_handle, node->resource_refs.storage_buffer_writes[i], state );
    }

    // Copy resources
    for ( size_t i = 0; i < params->copy_texture_reads_count; ++i ) {
        const xf_copy_texture_dependency_t* dep = &params->copy_texture_reads[i];

        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = xg_texture_layout_copy_source_m,
            .stage = xg_pipeline_stage_bit_transfer_m,
            .access = xg_memory_access_bit_transfer_read_m,
            .queue = queue,
        );

        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.copy_texture_reads[i], dep->view, state );
    }

    for ( size_t i = 0; i < params->copy_texture_writes_count; ++i ) {
        const xf_copy_texture_dependency_t* dep = &params->copy_texture_writes[i];

        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = xg_texture_layout_copy_dest_m,
            .stage = xg_pipeline_stage_bit_transfer_m,
            .access = xg_memory_access_bit_transfer_write_m,
            .queue = queue,
        );

        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.copy_texture_writes[i], dep->view, state );
    }

    for ( size_t i = 0; i < params->copy_buffer_reads_count; ++i ) {
        xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
            .stage = xg_pipeline_stage_bit_transfer_m,
            .access = xg_memory_access_bit_transfer_read_m,
            .queue = queue,
        );

        xf_graph_add_buffer_transition ( graph_handle, node_handle, node->resource_refs.copy_buffer_reads[i], state );
    }

    for ( size_t i = 0; i < params->copy_buffer_writes_count; ++i ) {
        xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
            .stage = xg_pipeline_stage_bit_transfer_m,
            .access = xg_memory_access_bit_transfer_write_m,
            .queue = queue,
        );

        xf_graph_add_buffer_transition ( graph_handle, node_handle, node->resource_refs.copy_buffer_writes[i], state );
    }

    // Render targets and depth stencil
    for ( size_t i = 0; i < params->render_targets_count; ++i ) {
        const xf_render_target_dependency_t* dep = &params->render_targets[i];

        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = xg_texture_layout_render_target_m,
            .stage = xg_pipeline_stage_bit_color_output_m,
            .access = xg_memory_access_bit_color_write_m,
            .queue = queue,
        );

        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.render_targets[i], dep->view, state );
    }

    if ( params->depth_stencil_target != xf_null_handle_m ) {
        std_assert_m ( params->depth_stencil_target != xf_null_handle_m );

        // TODO support early fragment test
        xf_texture_execution_state_t state = xf_texture_execution_state_m (
            .layout = xg_texture_layout_depth_stencil_target_m,
            .stage = xg_pipeline_stage_bit_late_fragment_test_m,
            .access = xg_memory_access_bit_depth_stencil_write_m,
            .queue = queue,
        );

        xf_graph_add_texture_transition ( graph_handle, node_handle, node->resource_refs.depth_stencil_target, xg_texture_view_m(), state );
    }
}
#endif

static void xf_graph_compute_segments ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xf_graph_segment_t* segments_array = graph->segments_array;
    uint32_t segments_count = 0;
    xf_graph_segment_t segment = xf_graph_segment_m( .begin = 0 );

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        uint32_t node_idx = graph->nodes_execution_order[i];
        xf_node_t* node = &graph->nodes_array[node_idx];

        if ( i == 0 ) {
            segment.queue = node->params.queue;
        } else if ( node->params.queue != segment.queue ) {
            segment.end = i - 1;
            segments_array[segments_count++] = segment;
        
            segment = xf_graph_segment_m ( .begin = i, .queue = node->params.queue );
        }

        node->segment = segments_count;
    }

    segment.end = graph->nodes_count - 1;
    segments_array[segments_count++] = segment;

    graph->segments_count = segments_count;

    // dependencies
    for ( uint32_t i = 0; i < segments_count; ++i ) {
        xf_graph_segment_t* segment = &graph->segments_array[i];
        for ( uint32_t j = segment->begin; j < segment->end; ++j ) {
            for ( xg_cmd_queue_e q = xg_cmd_queue_graphics_m; q < xg_cmd_queue_count_m; ++q ) {
                int32_t prev = graph->cross_queue_node_deps[j][q];
                if ( prev != -1 ) {
                    uint32_t prev_idx = graph->nodes_execution_order[prev];
                    xf_node_t* prev_node = &graph->nodes_array[prev_idx];
                    if ( prev_node->segment != i ) {
                        // TODO need to check that prev_node->segment is not already in the deps list?
                        segment->deps[segment->deps_count++] = prev_node->segment;
                        xf_graph_segment_t* prev_segment = &graph->segments_array[prev_node->segment];
                        prev_segment->is_depended = true;
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
        if ( segment->is_depended ) {
            segment->event = xg->create_queue_event ( &xg_queue_event_params_m ( .device = graph->params.device, .debug_name = "graph_event" ) );
        }
    }
}

static void xf_graph_print ( xf_graph_h graph_handle ) {
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
        xg_cmd_queue_e q = node->params.queue;
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

#if 0
static void xf_graph_compute_release_barriers ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_texture_t* texture = &graph->textures_array[i];

    }
}
#endif

void xf_graph_finalize ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xf_graph_scan_resources ( graph_handle );
    xf_graph_clear_resource_dependencies ( graph_handle );

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_h node_handle = i;
        xf_graph_node_accumulate_resource_usage ( graph_handle, node_handle );
        xf_graph_node_accumulate_dependencies ( graph_handle, node_handle );
    }

    xf_graph_accumulate_node_dependencies ( graph_handle );
    xf_graph_linearize ( graph_handle );
    xf_graph_compute_resource_lifespans ( graph_handle );
    xf_graph_compute_segments ( graph_handle );

    //for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
    //    xf_node_h node_handle = graph->nodes_execution_order[i];
    //    xf_graph_node_accumulate_transitions ( graph_handle, node_handle );
    //}
    
    xf_graph_add_resource_refs ( graph_handle );
    //xf_graph_sort_resource_dependencies ( graph_handle );
    //xf_graph_compute_release_barriers ( graph_handle );

    xf_graph_print ( graph_handle );

    graph->is_finalized = true;
}

void xf_graph_cache_texture_info ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    
    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_texture_t* graph_texture = &graph->textures_array[i];
        xf_texture_info_t info;
        xf_resource_texture_get_info ( &info, graph_texture->handle );
        graph_texture->view_access = info.view_access;
        graph_texture->mip_levels = info.mip_levels;
    }
}

void xf_graph_build ( xf_graph_h graph_handle, xg_workload_h workload ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xf_graph_build_resources ( graph_handle, xg, cmd_buffer, resource_cmd_buffer );
    xf_graph_cache_texture_info ( graph_handle );

    xf_graph_create_segment_events ( graph_handle, xg );

    graph->is_built = true;
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

static void xf_graph_clear_transition_idx ( xf_graph_h graph_handle ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    
    for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
        xf_graph_texture_t* texture = &graph->textures_array[i];
        for ( uint32_t i = 0; i < std_static_array_capacity_m ( texture->transitions.mips ); ++i ) {
            texture->transitions.mips[i].idx = 0;
        }
    }

    for ( uint32_t i = 0; i < graph->buffers_count; ++i ) {
        xf_graph_buffer_t* buffer = &graph->buffers_array[i];
        buffer->transitions.idx = 0;
    }
}

static void xf_graph_prepare_for_execute ( xf_graph_h graph_handle, xg_i* xg, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    if ( !graph->is_finalized ) {
        //std_log_warn_m ( "Graph " std_fmt_str_m " is not finalized", graph->params.debug_name );
        xf_graph_finalize ( graph_handle );
    }

    if ( !graph->is_built ) {
        xf_graph_build ( graph_handle, workload );
    }

    xf_graph_clear_transition_idx ( graph_handle );
    xf_graph_remap_upload_buffers ( graph_handle, xg, resource_cmd_buffer );
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
        
        if ( clear->type == xf_texture_clear_type_color_m ) {
            xg->cmd_clear_texture ( cmd_buffer, key, texture, clear->color );
        } else if ( clear->type == xf_texture_clear_type_depth_stencil_m ) {
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
        xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
            .source = xf_copy_texture_resource_m ( node_args->io->copy_texture_reads[i] ),
            .destination = xf_copy_texture_resource_m ( node_args->io->copy_texture_writes[i] ),
            .filter = xg_sampler_filter_point_m, // TODO use linear for not same-size textures? expose a param?
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

void xf_graph_destroy ( xf_graph_h graph_handle, xg_workload_h xg_workload ) {
    if ( graph_handle == xg_null_handle_m ) {
        return;
    }

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    //xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( xg_workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( xg_workload );

    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    for ( uint32_t i = 0; i < graph->nodes_count; ++i ) {
        xf_node_t* node = &graph->nodes_array[i];

        if ( node->params.type == xf_node_type_custom_pass_m) {
            xf_node_custom_pass_params_t* pass = &node->params.pass.custom;
            if ( pass->copy_args && pass->user_args.base ) {
                std_virtual_heap_free ( node->user_alloc );
            }
        } else if ( node->params.type == xf_node_type_compute_pass_m ) {
            xf_node_compute_pass_params_t* pass = &node->params.pass.compute;
            if ( pass->copy_uniform_data && pass->uniform_data.base ) {
                std_virtual_heap_free ( node->user_alloc );
            }
        }

        // Renderpass
        if ( node->renderpass != xg_null_handle_m ) {
            xg->cmd_destroy_renderpass ( resource_cmd_buffer, node->renderpass, xg_resource_cmd_buffer_time_workload_complete_m );
        }
    }

    if ( !xg_memory_handle_is_null_m ( graph->heap.memory_handle ) ) {
        for ( uint32_t i = 0; i < graph->heap.textures_count; ++i ) {
            //xf_device_texture_h device_texture_handle = graph->heap.textures_array[i];
            //xf_resource_texture_unbind ( texture_handle );
        }

        //xg->free_memory ( graph->heap.memory_handle );
    }

    //for ( uint32_t i = 0; i < graph->owned_textures_count; ++i ) {
    //    xf_graph_texture_t* graph_texture = &graph->textures_array[graph->owned_textures_array[i]];
    //    xf_resource_texture_unbind ( graph_texture->handle );
    //}

    for ( uint32_t i = 0; i < graph->segments_count; ++i ) {
        xg_queue_event_h event = graph->segments_array[i].event;
        if ( event != xg_null_handle_m ) {
            xg->cmd_destroy_queue_event ( resource_cmd_buffer, event, xg_resource_cmd_buffer_time_workload_complete_m );
        }
    }

    xf_graph_remove_resource_refs ( graph_handle );

    xf_resource_destroy_unreferenced ( xg, resource_cmd_buffer, xg_resource_cmd_buffer_time_workload_complete_m );

    if ( !xg_memory_handle_is_null_m ( graph->heap.memory_handle ) ) {
        xg->free_memory ( graph->heap.memory_handle );
    }

    std_list_push ( &xf_graph_state->graphs_freelist, graph );
    std_bitset_clear ( xf_graph_state->graphs_bitset, graph_handle );
}

uint64_t xf_graph_execute ( xf_graph_h graph_handle, xg_workload_h xg_workload, uint64_t base_key ) {
    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];
    std_assert_m ( graph );

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( xg_workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( xg_workload );

#if 0
    for ( size_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        uint32_t node_idx = graph->nodes_execution_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_idx];
        xf_node_params_t* params = &node->params;

        if ( !node->enabled ) {
            for ( uint32_t i = 0; i < params->resources.render_targets_count; ++i ) {
                if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_alias_m ) {
                    xf_resource_texture_alias ( params->resources.render_targets[i].texture, params->passthrough.render_targets[i].alias );
                    graph->textures_array[node->resource_refs.render_targets[i]].disable_aliasing = true;
                }
            }

            for ( uint32_t i = 0; i < params->resources.storage_texture_writes_count; ++i ) {
                if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_alias_m ) {
                    xf_resource_texture_alias ( params->resources.storage_texture_writes[i].texture, params->passthrough.storage_texture_writes[i].alias );
                    graph->textures_array[node->resource_refs.storage_texture_writes[i]].disable_aliasing = true;
                }
            }
        }
    }
#endif

    // Prepare
    xf_graph_prepare_for_execute ( graph_handle, xg, xg_workload, resource_cmd_buffer );

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
    
    xf_graph_build_renderpasses ( graph, xg, resource_cmd_buffer );

// TODO avoid this limit?
#define xf_graph_max_barriers_per_pass_m 128
#if 0
    // Initial queue barriers
    {
        xf_graph_texture_transition_t texture_transitions_arrays[xg_cmd_queue_count_m][xf_graph_max_barriers_per_pass_m];
        uint32_t texture_transitions_counts[xg_cmd_queue_count_m] = { 0 };

        for ( uint32_t i = 0; i < graph->textures_count; ++i ) {
            xf_graph_texture_t* graph_texture = &graph->textures_array[i];
            xf_texture_t* texutre = xf_resource_texture_get ( graph_texture->handle );

            if ( graph_texture->view_access == xg_texture_view_access_default_only_m ) {
                if ( graph_texture->transitions.shared.count > 0 ) {
                    xg_cmd_queue_e queue = texture->state.shared.queue;
                    xf_graph_texture_transition_t* transition = &graph_texture->transitions.shared.array[0];
                    if ( transition->state.queue != queue ) {
                        uint32_t idx = texture_transitions_counts[transition->state.queue]++;
                        texture_transitions_arrays[transition->state.queue][idx] = *transition;
                    }
                }
            } else if ( graph_texture->view_access == xg_texture_view_access_separate_mips_m ) {
                uint32_t mip_count = view.mip_count == xg_texture_all_mips_m ? graph_texture->mip_levels - view.mip_base : view.mip_count;
                for ( uint32_t i = 0; i < mip_count; ++i ) {
                    if ( graph_texture->transitions.mips[view.mip_base + i].count > 0 ) {
                        xg_cmd_queue_e queue = texture->state.mips[view.mip_base + i].queue;
                        xf_graph_texture_transition_t* transition = &graph_texture->transitions.mips[view.mip_base + i].array[0];
                        if ( transition->state.queue != queue ) {
                            uint32_t idx = texture_transitions_counts[transition->state.queue]++;
                            texture_transitions_arrays[transition->state.queue][idx] = *transition;
                        }
                    }
                }
            } else {
                std_not_implemented_m();
            }
        }

        for ( uint32_t i = 0; i < xg_cmd_queue_count_m; ++i ) {
            if ( texture_transitions_counts[i] > 0 ) {
                xg->cmd_bind_queue (  )
            }
        }
    }
#endif

    // Execute
    uint64_t sort_key = base_key;
    for ( size_t node_it = 0; node_it < graph->nodes_count; ++node_it ) {
        uint32_t node_idx = graph->nodes_execution_order[node_it];
        xf_node_t* node = &graph->nodes_array[node_idx];
        //xf_node_params_t* params = &node->params;

        xg_texture_memory_barrier_t texture_barriers[xf_graph_max_barriers_per_pass_m];
        std_stack_t texture_barriers_stack = std_static_stack_m ( texture_barriers );
        xg_buffer_memory_barrier_t buffer_barriers[xf_graph_max_barriers_per_pass_m]; // TODO split defines
        std_stack_t buffer_barriers_stack = std_static_stack_m ( buffer_barriers );

        // TODO try moving the aliasing before the build, leave the barriers here
#if 0
        if ( !node->enabled ) {
            for ( uint32_t i = 0; i < params->resources.render_targets_count; ++i ) {
                if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_clear_m ) {
                    xf_render_target_dependency_t* target = &node->params.resources.render_targets[i];

                    xf_texture_execution_state_t state = xf_texture_execution_state_m (
                        .layout = xg_texture_layout_copy_dest_m,
                        .stage = xg_pipeline_stage_bit_transfer_m,
                        .access = xg_memory_access_bit_transfer_write_m
                    );
                    xf_resource_texture_state_barrier ( &texture_barriers_stack, target->texture, target->view, &state );

                    xg->cmd_begin_debug_region ( cmd_buffer, sort_key, node->params.debug_name, node->params.debug_color );

                    {
                        xg_barrier_set_t barrier_set = xg_barrier_set_m();
                        barrier_set.texture_memory_barriers = texture_barriers;
                        barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                        xg->cmd_barrier_set ( cmd_buffer, sort_key, &barrier_set );
                    }

                    const xf_texture_t* texture = xf_resource_texture_get ( target->texture );
                    xg->cmd_clear_texture ( cmd_buffer, sort_key, texture->xg_handle, params->passthrough.render_targets[i].clear );

                    xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                } else if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_alias_m ) {
                    //xf_resource_texture_alias ( params->resources.render_targets[i].texture, params->passthrough.render_targets[i].alias );
                } else if ( params->passthrough.render_targets[i].mode == xf_passthrough_mode_copy_m ) {
                    xf_render_target_dependency_t* target = &node->params.resources.render_targets[i];
                    xf_copy_texture_dependency_t* source = &params->passthrough.render_targets[i].copy_source;
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

                    xg->cmd_begin_debug_region ( cmd_buffer, sort_key, node->params.debug_name, node->params.debug_color );

                    {
                        xg_barrier_set_t barrier_set = xg_barrier_set_m();
                        barrier_set.texture_memory_barriers = texture_barriers;
                        barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                        xg->cmd_barrier_set ( cmd_buffer, sort_key, &barrier_set );
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
                    xg->cmd_copy_texture ( cmd_buffer, sort_key, &copy_params );

                    xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                }
            }

            // TODO factor this into an outer function and call twice? need to share/wrap a bunch of local state...
            for ( uint32_t i = 0; i < params->resources.storage_texture_writes_count; ++i ) {
                if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_clear_m ) {
                    xf_shader_texture_dependency_t* texture_write = &node->params.resources.storage_texture_writes[i];

                    xf_texture_execution_state_t state = xf_texture_execution_state_m (
                        .layout = xg_texture_layout_copy_dest_m,
                        .stage = xg_pipeline_stage_bit_transfer_m,
                        .access = xg_memory_access_bit_transfer_write_m
                    );
                    xf_resource_texture_state_barrier ( &texture_barriers_stack, texture_write->texture, texture_write->view, &state );

                    xg->cmd_begin_debug_region ( cmd_buffer, sort_key, node->params.debug_name, node->params.debug_color );

                    {
                        xg_barrier_set_t barrier_set = xg_barrier_set_m();
                        barrier_set.texture_memory_barriers = texture_barriers;
                        barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                        xg->cmd_barrier_set ( cmd_buffer, sort_key, &barrier_set );
                    }

                    const xf_texture_t* texture = xf_resource_texture_get ( texture_write->texture );
                    xg->cmd_clear_texture ( cmd_buffer, sort_key, texture->xg_handle, params->passthrough.storage_texture_writes[i].clear );

                    xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                } else if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_alias_m ) {
                    //xf_resource_texture_alias ( params->resources.storage_texture_writes[i].texture, params->passthrough.storage_texture_writes[i].alias );
                } else if ( params->passthrough.storage_texture_writes[i].mode == xf_passthrough_mode_copy_m ) {
                    xf_shader_texture_dependency_t* target = &node->params.resources.storage_texture_writes[i];
                    xf_copy_texture_dependency_t* source = &params->passthrough.storage_texture_writes[i].copy_source;
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

                    xg->cmd_begin_debug_region ( cmd_buffer, sort_key, node->params.debug_name, node->params.debug_color );

                    {
                        xg_barrier_set_t barrier_set = xg_barrier_set_m();
                        barrier_set.texture_memory_barriers = texture_barriers;
                        barrier_set.texture_memory_barriers_count = std_stack_array_count_m ( &texture_barriers_stack, xg_texture_memory_barrier_t );
                        xg->cmd_barrier_set ( cmd_buffer, sort_key, &barrier_set );
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
                    xg->cmd_copy_texture ( cmd_buffer, sort_key, &copy_params );

                    xg->cmd_end_debug_region ( cmd_buffer, sort_key );
                }
            }

            continue;
        }
#endif

        xf_node_io_t io;

        for ( size_t i = 0; i < node->params.resources.sampled_textures_count; ++i ) {
            xf_shader_texture_dependency_t* resource = &node->params.resources.sampled_textures[i];
            xf_texture_t* texture = xf_resource_texture_get ( resource->texture );

            xg_texture_layout_e layout = xg_texture_layout_shader_read_m;
            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            if ( device_texture->info.allowed_usage & xg_texture_usage_bit_depth_stencil_m ) {
                layout = xg_texture_layout_depth_stencil_read_m;
            }

            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = layout,
                .stage = resource->stage,
                .access = xg_memory_access_bit_shader_read_m
            );

            xf_resource_texture_state_barrier ( &texture_barriers_stack, resource->texture, resource->view, &state );

            io.sampled_textures[i].texture = device_texture->handle;
            io.sampled_textures[i].view = resource->view;
            io.sampled_textures[i].layout = layout;
        }

        for ( size_t i = 0; i < node->params.resources.storage_texture_reads_count; ++i ) {
            xf_shader_texture_dependency_t* resource = &node->params.resources.storage_texture_reads[i];
            const xf_texture_t* texture = xf_resource_texture_get ( resource->texture );
            
            xg_texture_layout_e layout = xg_texture_layout_shader_read_m;
            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            if ( device_texture->info.allowed_usage & xg_texture_usage_bit_depth_stencil_m ) {
                layout = xg_texture_layout_depth_stencil_read_m;
            }

            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = layout,
                .stage = resource->stage,
                .access = xg_memory_access_bit_shader_read_m
            );

            xf_resource_texture_state_barrier ( &texture_barriers_stack, resource->texture, resource->view, &state );

            io.storage_texture_reads[i].texture = device_texture->handle;
            io.storage_texture_reads[i].view = resource->view;
            io.storage_texture_reads[i].layout = layout;
        }

        for ( size_t i = 0; i < node->params.resources.storage_texture_writes_count; ++i ) {
            xf_shader_texture_dependency_t* resource = &node->params.resources.storage_texture_writes[i];
            const xf_texture_t* texture = xf_resource_texture_get ( resource->texture );
            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );

            xg_texture_layout_e layout = xg_texture_layout_shader_write_m;
            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = layout,
                .stage = resource->stage,
                .access = xg_memory_access_bit_shader_write_m
            );

            xf_resource_texture_state_barrier ( &texture_barriers_stack, resource->texture, resource->view, &state );

            io.storage_texture_writes[i].texture = device_texture->handle;
            io.storage_texture_writes[i].view = resource->view;
            io.storage_texture_writes[i].layout = layout;
        }

        for ( size_t i = 0; i < node->params.resources.uniform_buffers_count; ++i ) {
            xf_shader_buffer_dependency_t* resource = &node->params.resources.uniform_buffers[i];
            const xf_buffer_t* buffer = xf_resource_buffer_get ( resource->buffer );
            xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                .stage = resource->stage,
                .access = xg_memory_access_bit_shader_read_m
            );

            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, resource->buffer, &state );

            io.uniform_buffers[i] = buffer->xg_handle;
        }

        for ( size_t i = 0; i < node->params.resources.storage_buffer_reads_count; ++i ) {
            xf_shader_buffer_dependency_t* resource = &node->params.resources.storage_buffer_reads[i];
            const xf_buffer_t* buffer = xf_resource_buffer_get ( resource->buffer );
            xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                .stage = resource->stage,
                .access = xg_memory_access_bit_shader_read_m
            );

            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, resource->buffer, &state );

            io.storage_buffer_reads[i] = buffer->xg_handle;
        }

        for ( size_t i = 0; i < node->params.resources.storage_buffer_writes_count; ++i ) {
            xf_shader_buffer_dependency_t* resource = &node->params.resources.storage_buffer_writes[i];
            const xf_buffer_t* buffer = xf_resource_buffer_get ( resource->buffer );
            xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                .stage = resource->stage,
                .access = xg_memory_access_bit_shader_write_m
            );

            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, resource->buffer, &state );

            io.storage_buffer_writes[i] = buffer->xg_handle;
        }

        for ( size_t i = 0; i < node->params.resources.copy_texture_reads_count; ++i ) {
            xf_copy_texture_dependency_t* copy = &node->params.resources.copy_texture_reads[i];
            const xf_texture_t* texture = xf_resource_texture_get ( copy->texture );
            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = xg_texture_layout_copy_source_m,
                .stage = xg_pipeline_stage_bit_transfer_m,
                .access = xg_memory_access_bit_transfer_read_m
            );

            xf_resource_texture_state_barrier ( &texture_barriers_stack, copy->texture, copy->view, &state );

            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            io.copy_texture_reads[i].texture = device_texture->handle;
            io.copy_texture_reads[i].view = copy->view;
            //io.copy_texture_reads[i].layout = xg_texture_layout_copy_source_m;
        }

        for ( size_t i = 0; i < node->params.resources.copy_texture_writes_count; ++i ) {
            xf_copy_texture_dependency_t* copy = &node->params.resources.copy_texture_writes[i];
            const xf_texture_t* texture = xf_resource_texture_get ( copy->texture );
            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = xg_texture_layout_copy_dest_m,
                .stage = xg_pipeline_stage_bit_transfer_m,
                .access = xg_memory_access_bit_transfer_write_m
            );

            xf_resource_texture_state_barrier ( &texture_barriers_stack, copy->texture, copy->view, &state );

            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            io.copy_texture_writes[i].texture = device_texture->handle;
            io.copy_texture_writes[i].view = copy->view;
            //io.copy_texture_writes[i].layout = xg_texture_layout_copy_dest_m;
        }

        for ( size_t i = 0; i < node->params.resources.copy_buffer_reads_count; ++i ) {
            xf_buffer_h buffer_handle = node->params.resources.copy_buffer_reads[i];
            const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
            xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                .stage = xg_pipeline_stage_bit_transfer_m,
                .access = xg_memory_access_bit_transfer_read_m
            );

            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, buffer_handle, &state );

            io.copy_buffer_reads[i] = buffer->xg_handle;
        }

        for ( size_t i = 0; i < node->params.resources.copy_buffer_writes_count; ++i ) {
            xf_buffer_h buffer_handle = node->params.resources.copy_buffer_writes[i];
            const xf_buffer_t* buffer = xf_resource_buffer_get ( buffer_handle );
            xf_buffer_execution_state_t state = xf_buffer_execution_state_m (
                .stage = xg_pipeline_stage_bit_transfer_m,
                .access = xg_memory_access_bit_transfer_write_m
            );

            xf_resource_buffer_state_barrier ( &buffer_barriers_stack, buffer_handle, &state );

            io.copy_buffer_writes[i] = buffer->xg_handle;
        }

        for ( size_t i = 0; i < node->params.resources.render_targets_count; ++i ) {
            xf_render_target_dependency_t* target = &node->params.resources.render_targets[i];
            const xf_texture_t* texture = xf_resource_texture_get ( target->texture );
            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = xg_texture_layout_render_target_m,
                .stage = xg_pipeline_stage_bit_color_output_m,
                .access = xg_memory_access_bit_color_write_m
            );

            xf_resource_texture_state_barrier ( &texture_barriers_stack, target->texture, target->view, &state );

            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            io.render_targets[i].texture = device_texture->handle;
            io.render_targets[i].view = target->view;
        }

        if ( node->params.resources.depth_stencil_target != xf_null_handle_m ) {
            std_assert_m ( node->params.resources.depth_stencil_target != xf_null_handle_m );
            xf_texture_h texture_handle = node->params.resources.depth_stencil_target;
            const xf_texture_t* texture = xf_resource_texture_get ( texture_handle );
            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = xg_texture_layout_depth_stencil_target_m,
                .stage = xg_pipeline_stage_bit_late_fragment_test_m,
                .access = xg_memory_access_bit_depth_stencil_write_m
            );

            // TODO support early fragment test
            xf_resource_texture_state_barrier ( &texture_barriers_stack, texture_handle, xg_texture_view_m(), &state );

            xf_device_texture_t* device_texture = xf_resource_device_texture_get ( texture->device_texture_handle );
            io.depth_stencil_target = device_texture->handle;
        } else {
            io.depth_stencil_target = xg_null_handle_m;
        }

        xf_graph_segment_t* segment = &graph->segments_array[node->segment];
        if ( node_it == segment->begin ) {
            xg_cmd_bind_queue_params_t bind_queue_params = xg_cmd_bind_queue_params_m (
                .queue = segment->queue,
                .signal_event = segment->event,
                .wait_count = segment->deps_count,
            );
            for ( uint32_t i = 0; i < bind_queue_params.wait_count; ++i ) {
                bind_queue_params.wait_stages[i] = xg_pipeline_stage_bit_top_of_pipe_m;
                bind_queue_params.wait_events[i] = graph->segments_array[segment->deps[i]].event;
            }
            xg->cmd_bind_queue ( cmd_buffer, sort_key, &bind_queue_params );
        }

        xg->cmd_begin_debug_region ( cmd_buffer, sort_key, node->params.debug_name, node->params.debug_color );

        {
            xg_barrier_set_t barrier_set = xg_barrier_set_m();
            barrier_set.texture_memory_barriers = texture_barriers;
            barrier_set.texture_memory_barriers_count = ( texture_barriers_stack.top - texture_barriers_stack.begin ) / sizeof ( xg_texture_memory_barrier_t );
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

            xg->cmd_begin_renderpass ( cmd_buffer, sort_key, &renderpass_params );
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
            xg->cmd_end_renderpass ( cmd_buffer, sort_key );
        }

        if ( node->params.resources.presentable_texture != xf_null_handle_m ) {
            xf_texture_h texture_handle = node->params.resources.presentable_texture;

            xg_texture_memory_barrier_t present_barrier[1];
            std_stack_t present_barrier_stack = std_static_stack_m ( present_barrier );

            xf_texture_execution_state_t state = xf_texture_execution_state_m (
                .layout = xg_texture_layout_present_m,
                .stage = xg_pipeline_stage_bit_bottom_of_pipe_m,
                .access = xg_memory_access_bit_none_m
            );

            xf_resource_texture_state_barrier ( &present_barrier_stack, texture_handle, xg_texture_view_m(), &state );
            std_assert_m ( present_barrier_stack.top == present_barrier + 1 );

            xg_barrier_set_t barrier_set = xg_barrier_set_m();
            barrier_set.texture_memory_barriers = present_barrier;
            barrier_set.texture_memory_barriers_count = 1;
            xg->cmd_barrier_set ( cmd_buffer, sort_key, &barrier_set );
        }

        xg->cmd_end_debug_region ( cmd_buffer, sort_key );
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
}

void xf_graph_debug_print ( xf_graph_h graph_handle ) {
    xf_graph_scan_resources ( graph_handle );
    xf_graph_linearize ( graph_handle );

    xf_graph_t* graph = &xf_graph_state->graphs_array[graph_handle];

    std_log_info_m ( "Graph execution order: (" std_fmt_u32_m " nodes)", graph->nodes_count );

    for ( uint64_t i = 0; i < graph->nodes_count; ++i ) {
        size_t node_idx = graph->nodes_execution_order[i];
        xf_node_t* node = &graph->nodes_array[node_idx];
        std_log_info_m ( std_fmt_tab_m std_fmt_str_m, node->params.debug_name );
    }
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
