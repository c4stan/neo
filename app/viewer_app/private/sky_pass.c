#include "sky_pass.h"

#include "viewapp_state.h"

typedef struct {
    xs_database_pipeline_h pipeline;
} sky_pass_args_t;

static void sky_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;
    std_auto_m pass_args = ( sky_pass_args_t* ) user_args;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    se_i* se = state->modules.se;

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( 
        .include_component_count = 1, 
        .include_components = { viewapp_sky_component_id_m } 
    ) );
    if ( query_result.entity_count == 0 ) {
        return;
    }
    std_assert_m ( query_result.entity_count == 1 );
    se_stream_iterator_t sky_component_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    viewapp_sky_component_t* sky_component = se_stream_iterator_next ( &sky_component_iterator );
    if ( sky_component->sky_texture == xg_null_handle_m ) {
        return;
    }

    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );
    xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
        .bindings = xg_pipeline_resource_bindings_m (
            .texture_count = 1,
            .textures = {
                xg_texture_resource_binding_m (
                    .shader_register = 0,
                    .layout = xg_texture_layout_shader_read_m,
                    .texture = sky_component->sky_texture,
                ),
            },
            .sampler_count = 1,
            .samplers = {
                xg_sampler_resource_binding_m (
                    .shader_register = 1,
                    .sampler = xg->get_default_sampler ( node_args->device, xg_default_sampler_linear_wrap_m ),
                )
            }
        ),
    ) );

    xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
        .pipeline = pipeline_state,
        .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings,
        .index_buffer = sky_component->geo_gpu_data.idx_buffer,
        .primitive_count = sky_component->geo_data.index_count / 3,
        .vertex_buffers_count = 5,
        .vertex_buffers = { 
            sky_component->geo_gpu_data.pos_buffer, 
            sky_component->geo_gpu_data.nor_buffer, 
            sky_component->geo_gpu_data.tan_buffer, 
            sky_component->geo_gpu_data.bitan_buffer, 
            sky_component->geo_gpu_data.uv_buffer 
        },
    ) );
}

xf_node_h add_sky_node ( xf_graph_h graph, const gbuffer_textures_t* gbuffer, xf_texture_h depth ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    sky_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "sky" ) ),
    };

    xf_node_h node = xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "sky",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = sky_pass_routine,
            .auto_renderpass = true,
            .user_args = std_buffer_struct_m ( &args ),
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = {
                xf_render_target_dependency_m ( .texture = gbuffer->radiosity ),
            },
            .depth_stencil_target = depth,
        ),
    ) );
    return node;
}
