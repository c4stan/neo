#include <xf.h>

#include <viewapp_state.h>

typedef struct {
    xs_database_pipeline_h pipeline;
    xg_raytrace_world_h world;
    xg_texture_h target;
    uint32_t width;
    uint32_t height;
} raytrace_pass_args_t;

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
        .component_count = 1, 
        .components = { viewapp_mesh_component_id_m } 
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

    xg_buffer_range_t instance_buffer_range = xg->write_workload_staging ( node_args->workload, instance_data, instance_data_size );
    std_virtual_heap_free ( instance_data );

    // Fill light buffer
    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m ( 
        .component_count = 3, 
        .components = { viewapp_light_component_id_m, viewapp_transform_component_id_m, viewapp_mesh_component_id_m } 
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &light_query_result.components[1], 0 );
    se_stream_iterator_t light_mesh_iterator = se_component_iterator_m ( &light_query_result.components[2], 0 );
    uint64_t light_count = light_query_result.entity_count;
    light_count = std_min ( light_count, viewapp_max_lights_m );
    size_t light_data_size = sizeof ( light_buffer_t ) + sizeof ( light_data_t ) * light_count;
    std_auto_m light_data = ( light_buffer_t* ) std_virtual_heap_alloc_m ( light_data_size, 16 );

    for ( uint64_t light_it = 0; light_it < light_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );
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

    xg_buffer_range_t light_buffer_range = xg->write_workload_staging ( node_args->workload, light_data, light_data_size );
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

static void raytrace_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    std_auto_m pass_args = ( raytrace_pass_args_t* ) user_args;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    // Bind
    xg_pipeline_state_h pipeline = xs->get_pipeline_state ( pass_args->pipeline );
    xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = xg->get_pipeline_resource_layout ( pipeline, xg_shader_binding_set_dispatch_m ),
        .bindings = xg_pipeline_resource_bindings_m (
            .raytrace_world_count = 1,
            .raytrace_worlds = { xg_raytrace_world_resource_binding_m (
                .shader_register = 0,
                .world = pass_args->world,
            )},
            .texture_count = 1,
            .textures = {  
                xf_shader_texture_binding_m ( node_args->io->storage_texture_writes[0], 1 ),
            },
            .buffer_count = 3,
            .buffers = {
                xg_buffer_resource_binding_m ( 
                    .shader_register = 2,
                    .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[0] ),
                ),
                xg_buffer_resource_binding_m ( 
                    .shader_register = 3,
                    .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[1] ),
                ),
                xg_buffer_resource_binding_m ( 
                    .shader_register = 4,
                    .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_writes[0] ),
                ),
            }
        )
    ) );

    // Draw
    xg->cmd_raytrace ( cmd_buffer, key, &xg_cmd_raytrace_params_m (
        .ray_count_x = pass_args->width,
        .ray_count_y = pass_args->height,
        .pipeline = pipeline,
        .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings
    ) );
}

xf_node_h add_raytrace_pass ( xf_graph_h graph, xf_texture_h target, xf_buffer_h instances, xf_buffer_h lights, xf_buffer_h reservoir ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_texture_info_t target_info;
    xf->get_texture_info ( &target_info, target );

    raytrace_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "raytrace" ) ),
        .world = state->render.raytrace_world,
        .target = target,
        .width = target_info.width,
        .height = target_info.height,
    };

    xf_node_params_t node_params = xf_node_params_m (
        .debug_name = "raytrace",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = raytrace_pass,
            .user_args = std_buffer_m ( &args ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 2,
            .storage_buffer_reads = { 
                xf_shader_buffer_dependency_m ( .buffer = instances, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = lights, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .storage_buffer_writes_count = 1,
            .storage_buffer_writes = { xf_shader_buffer_dependency_m ( .buffer = reservoir, .stage = xg_pipeline_stage_bit_raytrace_shader_m ) },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = target, .stage = xg_pipeline_stage_bit_raytrace_shader_m ) },
        ),
    );
    xf_node_h node = xf->create_node ( graph, &node_params );
    return node;
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
