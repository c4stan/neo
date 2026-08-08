#include <xf.h>

#include <viewapp_state.h>

typedef struct {
    uint64_t idx_buffer;
    uint64_t pos_buffer;
    uint64_t nor_buffer;
    float albedo[3];
    float emissive[3];
    uint32_t id;
} instance_data_t;

typedef struct {
    float pos[3];
    float radius;
    float color[3];
    float intensity;
    uint32_t id;
} light_data_t;

typedef struct {
    uint32_t light_count;
    uint32_t _pad0[3];
    light_data_t lights[];
} light_buffer_t;

uint32_t raytrace_light_data_size ( void ) {
    return sizeof ( light_buffer_t ) + sizeof ( light_data_t ) * viewapp_max_lights_m;
}

uint32_t raytrace_instance_data_size ( void ) {
    return sizeof ( instance_data_t ) * 1024; // TODO
}

static void raytrace_setup_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;

    // Fill instance buffer
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .include_component_count = 1, 
        .include_components = { viewapp_mesh_component_id_m } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;
    size_t instance_data_size = sizeof ( instance_data_t ) * mesh_count;
    std_auto_m instance_data = ( instance_data_t* ) std_virtual_heap_alloc_m ( instance_data_size, 16 );

    for ( uint32_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg_buffer_info_t pos_buffer_info, nor_buffer_info, idx_buffer_info;
        xg->get_buffer_info ( &pos_buffer_info, mesh_component->geo_gpu_data.pos_buffer );
        xg->get_buffer_info ( &nor_buffer_info, mesh_component->geo_gpu_data.nor_buffer );
        xg->get_buffer_info ( &idx_buffer_info, mesh_component->geo_gpu_data.idx_buffer );

        instance_data[i].pos_buffer = pos_buffer_info.gpu_address;
        instance_data[i].nor_buffer = nor_buffer_info.gpu_address;
        instance_data[i].idx_buffer = idx_buffer_info.gpu_address;

        instance_data[i].albedo[0] = mesh_component->material.base_color[0];
        instance_data[i].albedo[1] = mesh_component->material.base_color[1];
        instance_data[i].albedo[2] = mesh_component->material.base_color[2];

        instance_data[i].emissive[0] = mesh_component->material.emissive[0];
        instance_data[i].emissive[1] = mesh_component->material.emissive[1];
        instance_data[i].emissive[2] = mesh_component->material.emissive[2];

        instance_data[i].id = mesh_component->object_id;
    }

    xg_buffer_range_t instance_buffer_range = xg->write_workload_staging ( node_args->workload, std_buffer_m ( .base = instance_data, .size = instance_data_size ), 1 );
    std_virtual_heap_free ( instance_data );

    // Fill light buffer
    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m ( 
        .include_component_count = 3, 
        .include_components = { viewapp_light_component_id_m, viewapp_transform_component_id_m, viewapp_mesh_component_id_m } 
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &light_query_result.components[1], 1 );
    se_stream_iterator_t light_mesh_iterator = se_component_iterator_m ( &light_query_result.components[2], 0 );
    uint64_t light_count = light_query_result.entity_count;
    light_count = std_min ( light_count, viewapp_max_lights_m );
    size_t light_data_size = sizeof ( light_buffer_t ) + sizeof ( light_data_t ) * light_count;
    std_auto_m light_data = ( light_buffer_t* ) std_virtual_heap_alloc_m ( light_data_size, 16 );

    for ( uint64_t light_it = 0; light_it < light_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        viewapp_transform_t* transform_component = se_stream_iterator_next ( &transform_iterator );
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &light_mesh_iterator );
    
        light_data->lights[light_it] = ( light_data_t ) {
            .pos = {
                transform_component->position[0],
                transform_component->position[1],
                transform_component->position[2],
            },
            .radius = light_component->radius,
            .color = { 
                light_component->color[0],
                light_component->color[1],
                light_component->color[2],
            },
            .intensity = light_component->intensity,
            .id = mesh_component->object_id,
        };
    }
    light_data->light_count = light_count;

    xg_buffer_range_t light_buffer_range = xg->write_workload_staging ( node_args->workload, std_buffer_m ( .base = light_data, .size = light_data_size ), 1 );
    std_virtual_heap_free ( light_data );

    xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
        .destination = node_args->io->copy_buffer_writes[0], 
        .source = instance_buffer_range.handle,
        .source_offset = instance_buffer_range.offset,
        .size = instance_buffer_range.size,
    ) );

    xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
        .destination = node_args->io->copy_buffer_writes[1], 
        .source = light_buffer_range.handle,
        .source_offset = light_buffer_range.offset,
        .size = light_buffer_range.size,
    ) );
}

xf_node_h add_raytrace_setup_pass ( xf_graph_h graph, xf_buffer_h instances, xf_buffer_h lights ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    xf_node_params_t node_params = xf_node_params_m (
        .debug_name = "raytrace_setup",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = raytrace_setup_pass,
        ),
        .resources = xf_node_resource_params_m (
            .copy_buffer_writes_count = 2,
            .copy_buffer_writes = { instances, lights },
        ),
    );
    xf_node_h node = xf->create_node ( graph, &node_params );
    return node;
}

void bind_raytrace_setup_routine ( xf_graph_h graph ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );
    xf_node_h node = xf->get_node_by_name ( graph, "raytrace_setup" );
    std_assert_m ( node != xf_null_handle_m );
    xf->bind_custom_node_routine ( graph, node, raytrace_setup_pass );
}
