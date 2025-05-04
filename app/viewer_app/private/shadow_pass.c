#include <shadow_pass.h>

#include <viewapp_state.h>

#include <sm_matrix.h>
#include <sm_quat.h>
#include <se.inl>

typedef struct {
    sm_mat_4x4f_t world;
} draw_uniforms_t;

typedef struct {
    rv_matrix_4x4_t view_from_world;
    rv_matrix_4x4_t proj_from_view;
} pass_uniforms_t;

typedef struct {
    xg_resource_bindings_layout_h pass_layout;
    uint64_t width;
    uint64_t height;
} shadow_pass_args_t;

static void shadow_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( shadow_pass_args_t* ) user_args;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;
    xs_i* xs = state->modules.xs;
    rv_i* rv = state->modules.rv;

    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m (
        .component_count = 1,
        .components = { viewapp_light_component_id_m }
    ) );
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m (
        .component_count = 2,
        .components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    uint64_t light_count = light_query_result.entity_count;

    uint32_t light_view_count = 0;
    for ( uint64_t i = 0; i < light_count; ++i ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        if ( !light_component->shadow_casting ) {
            continue;
        }
        light_view_count += light_component->view_count;
    }

    light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );

    uint64_t size = std_pow2_round_down_u64 ( std_min_u64 ( pass_args->width, pass_args->height ) );
    uint64_t cols = pass_args->width / size;
    uint64_t rows = pass_args->height / size;
    while ( size >= 1 ) {
        if ( cols * rows >= light_view_count ) {
            break;
        }
        size >>= 1;
        cols = pass_args->width / size;
        rows = pass_args->height / size;
    }

    uint32_t global_view_it = 0;
    for ( uint64_t light_it = 0; light_it < light_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        if ( !light_component->shadow_casting ) {
            continue;
        }

        for ( uint32_t view_it = 0; view_it < light_component->view_count; ++view_it ) {
            uint32_t row = global_view_it / cols;
            uint32_t col = global_view_it % cols;
            ++global_view_it;

            // TODO better way to pass this from here to lighting
            light_component->shadow_tiles[view_it].x = col * size;
            light_component->shadow_tiles[view_it].y = row * size;
            light_component->shadow_tiles[view_it].size = size;

            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, light_component->views[view_it] );
            pass_uniforms_t pass_uniforms = {
                .view_from_world = view_info.view_matrix,
                .proj_from_view = view_info.proj_matrix,
            };
            xg_buffer_range_t range = xg->write_workload_uniform ( node_args->workload, &pass_uniforms, sizeof ( pass_uniforms ) );

            xg_resource_bindings_h pass_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m ( 
                .layout = pass_args->pass_layout,
                .bindings = xg_pipeline_resource_bindings_m (
                    .buffer_count = 1,
                    .buffers = xg_buffer_resource_binding_m (
                        .shader_register = 0,
                        .range = range
                    )
                )
            ) );

            xg_viewport_state_t viewport = xg_viewport_state_m (
                .x = size * col,
                .y = size * row,
                .width = size,
                .height = size,
            );
            xg->cmd_set_dynamic_viewport ( node_args->cmd_buffer, key, &viewport );

            se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
            se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 0 );
            uint64_t mesh_count = mesh_query_result.entity_count;
            for ( uint64_t j = 0; j < mesh_count; ++j ) {
                viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
                xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->shadow_pipeline );

                viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );

                //sm_vec_3f_t up = sm_vec_3f ( transform_component->up );
                //sm_vec_3f_t dir = sm_vec_3f ( transform_component->orientation );
                //dir = sm_vec_3f_norm ( dir );
                //sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );
                sm_mat_4x4f_t rot = sm_quat_to_4x4f ( sm_quat ( transform_component->orientation ) );
                float scale = transform_component->scale;
                sm_mat_4x4f_t trans = {
                    .r0[0] = scale,
                    .r1[1] = scale,
                    .r2[2] = scale,
                    .r3[3] = 1,
                    .r0[3] = transform_component->position[0],
                    .r1[3] = transform_component->position[1],
                    .r2[3] = transform_component->position[2],
                };

                draw_uniforms_t draw_uniforms = {
                    .world = sm_matrix_4x4f_mul ( trans, rot ),
                };

                xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
                    .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
                    .bindings = xg_pipeline_resource_bindings_m (
                        .buffer_count = 1,
                        .buffers = xg_buffer_resource_binding_m (
                            .shader_register = 0,
                            .range = xg->write_workload_uniform ( node_args->workload, &draw_uniforms, sizeof ( draw_uniforms ) ),
                        )
                    )
                ) );

                xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
                    .pipeline = pipeline_state,
                    .bindings = { xg_null_handle_m, pass_bindings, xg_null_handle_m, draw_bindings },
                    .vertex_buffers_count = 2,
                    .vertex_buffers = { mesh_component->geo_gpu_data.pos_buffer, mesh_component->geo_gpu_data.nor_buffer },
                    .index_buffer = mesh_component->geo_gpu_data.idx_buffer,
                    .primitive_count = mesh_component->geo_data.index_count / 3,
                ) );
            }
        }
    }
}

xf_node_h add_shadow_pass ( xf_graph_h graph, xf_texture_h target ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t texture_info;
    xf->get_texture_info ( &texture_info, target );

    xg_resource_bindings_layout_h pass_layout = xg->create_resource_layout ( &xg_resource_bindings_layout_params_m (
        .device = graph_info.device,
        .resource_count = 1,
        .resources = { xg_resource_binding_layout_m ( .shader_register = 0, .stages = xg_shading_stage_bit_vertex_m, .type = xg_resource_binding_buffer_uniform_m ) },
    ) );

    shadow_pass_args_t args = {
        .pass_layout = pass_layout,
        .width = texture_info.width,
        .height = texture_info.height,
    };

    xf_node_h node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "shadows",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = shadow_pass_routine,
            .user_args = std_buffer_m ( &args ),
            .auto_renderpass = true
        ),
        .resources = xf_node_resource_params_m (
            .depth_stencil_target = target,
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_clear_m ) }
        ),
    ) );

    return node;
}
