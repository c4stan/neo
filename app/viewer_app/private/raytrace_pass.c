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
    float emissive;
} raytrace_shader_instance_t;

#define MAX_LIGHTS_COUNT 32

typedef struct {
    float pos[3];
    float intensity;
    float color[3];
    uint32_t _pad0;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t view_from_world;
} light_uniform_t;;

typedef struct {
    uint32_t light_count;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    light_uniform_t lights[MAX_LIGHTS_COUNT];
} lights_uniform_buffer_t;

static void raytrace_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    std_auto_m pass_args = ( raytrace_pass_args_t* ) user_args;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    rv_i* rv = state->modules.rv;
    se_i* se = state->modules.se;

    // Fill lights buffer
    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_light_component_id_m } ) );
    uint64_t light_count = light_query_result.entity_count;
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    std_assert_m ( light_count <= MAX_LIGHTS_COUNT );

    lights_uniform_buffer_t cbuffer;
    std_mem_zero_m ( &cbuffer );

    cbuffer.light_count = light_count;

    for ( uint64_t i = 0; i < light_count; ++i ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, light_component->view );

        cbuffer.lights[i] = ( light_uniform_t ) {
            .pos =  {
                light_component->position[0],
                light_component->position[1],
                light_component->position[2],
            },
            .intensity = light_component->intensity,
            .color = { 
                light_component->color[0],
                light_component->color[1],
                light_component->color[2],
            },
            .proj_from_view = view_info.proj_matrix,
            .view_from_world = view_info.view_matrix,
        };
    }

    // Fill raytrace instance buffer
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    // TODO avoid creating and destroying this here...
    size_t shader_instance_buffer_size = sizeof ( raytrace_shader_instance_t ) * mesh_count;
    xg_buffer_h shader_instance_buffer = xg->create_buffer ( &xg_buffer_params_m ( 
        .memory_type = xg_memory_type_gpu_mapped_m,
        .device = node_args->device,
        .size = shader_instance_buffer_size,
        .allowed_usage = xg_buffer_usage_bit_storage_m,
        .debug_name = "rt_shader_instance_data"
    ) );
    xg_buffer_info_t shader_instance_buffer_info;
    xg->get_buffer_info ( &shader_instance_buffer_info, shader_instance_buffer );
    raytrace_shader_instance_t* instances = shader_instance_buffer_info.allocation.mapped_address;

    for ( uint32_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg_buffer_info_t pos_buffer_info, nor_buffer_info, idx_buffer_info;
        xg->get_buffer_info ( &pos_buffer_info, mesh_component->pos_buffer );
        xg->get_buffer_info ( &nor_buffer_info, mesh_component->nor_buffer );
        xg->get_buffer_info ( &idx_buffer_info, mesh_component->idx_buffer );

        instances[i].pos_buffer = pos_buffer_info.gpu_address;
        instances[i].nor_buffer = nor_buffer_info.gpu_address;
        instances[i].idx_buffer = idx_buffer_info.gpu_address;

        instances[i].albedo[0] = mesh_component->material.base_color[0];
        instances[i].albedo[1] = mesh_component->material.base_color[1];
        instances[i].albedo[2] = mesh_component->material.base_color[2];

        instances[i].emissive = mesh_component->material.emissive;
    }

    // Bind
    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_shader_binding_set_per_draw_m,
        .raytrace_world_count = 1,
        .raytrace_worlds = { xg_raytrace_world_resource_binding_m (
            .shader_register = 0,
            .world = pass_args->world,
        )},
        .texture_count = 1,
        .textures = {  
            xf_shader_texture_binding_m ( node_args->io->storage_texture_writes[0], 1 ),
        },
        .buffer_count = 2,
        .buffers = {
            xg_buffer_resource_binding_m ( 
                .shader_register = 2,
                .type = xg_buffer_binding_type_storage_m,
                .range = xg_buffer_range_m ( .handle = shader_instance_buffer, .offset = 0, .size = shader_instance_buffer_size ),
            ),
            xg_buffer_resource_binding_m ( 
                .shader_register = 3,
                .type = xg_buffer_binding_type_uniform_m,
                .range = xg->write_workload_uniform ( node_args->workload, &cbuffer, sizeof ( cbuffer ) ),
            )
        }
    );

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );
    xg->cmd_set_raytrace_pipeline_state ( cmd_buffer, xs->get_pipeline_state ( pass_args->pipeline ), key );

    // Draw
    xg->cmd_trace_rays ( cmd_buffer, pass_args->width, pass_args->height, 1, key );

    xg->cmd_destroy_buffer ( resource_cmd_buffer, shader_instance_buffer, xg_resource_cmd_buffer_time_workload_complete_m );
}

xf_node_h add_raytrace_pass ( xf_graph_h graph, xf_texture_h target, xf_node_h dep ) {
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
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = target, .stage = xg_pipeline_stage_bit_raytrace_shader_m ) },
        ),
        .node_dependencies_count = 1,
        .node_dependencies = { dep }
    );
    xf_node_h raytrace_node = xf->add_node ( graph, &node_params );
    return raytrace_node;
}
