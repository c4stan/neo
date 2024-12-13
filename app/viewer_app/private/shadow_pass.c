#include <shadow_pass.h>

#include <viewapp_state.h>

#include <sm_matrix.h>
#include <se.inl>

typedef struct {
    sm_mat_4x4f_t world;
} draw_cbuffer_vs_t;

// TODO this is duplicate with viewapp.c
// move into a header
typedef struct {
    rv_matrix_4x4_t view_from_world;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t view_from_proj;
    rv_matrix_4x4_t world_from_view;
    rv_matrix_4x4_t prev_view_from_world;
    rv_matrix_4x4_t prev_proj_from_view;
    float z_near;
    float z_far;
} view_cbuffer_data_t;

static void shadow_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;
    xs_i* xs = state->modules.xs;
    rv_i* rv = state->modules.rv;

    xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
        .depth_stencil.texture = node_args->io->depth_stencil_target
    );
    xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m (
        .component_count = 1,
        .components = { viewapp_light_component_id_m }
    ) );
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m (
        .component_count = 1,
        .components = { viewapp_mesh_component_id_m }
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    uint64_t light_count = light_query_result.entity_count;

    for ( uint64_t i = 0; i < light_count; ++i ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, light_component->view );
        view_cbuffer_data_t view_data = {
            .view_from_world = view_info.view_matrix,
            .proj_from_view = view_info.proj_matrix,
            .world_from_view = view_info.inverse_view_matrix,
            .view_from_proj = view_info.inverse_proj_matrix,
            .prev_view_from_world = view_info.prev_frame_view_matrix,
            .prev_proj_from_view = view_info.prev_frame_proj_matrix,
            .z_near = view_info.proj_params.near_z,
            .z_far = view_info.proj_params.far_z,
        };

        xg_pipeline_resource_bindings_t view_bindings = xg_pipeline_resource_bindings_m (
            .set = xg_resource_binding_set_per_view_m,
            .buffer_count = 1,
            .buffers = xg_buffer_resource_binding_m (
                .shader_register = 0,
                .type = xg_buffer_binding_type_uniform_m,
                .range = xg->write_workload_uniform ( node_args->workload, &view_data, sizeof ( view_data ) )
            )
        );

        xg->cmd_set_pipeline_resources ( node_args->cmd_buffer, &view_bindings, node_args->base_key );

        // TODO
        xg_viewport_state_t viewport = xg_viewport_state_m (
            .width = 1024,
            .height = 1024,
        );
        xg->cmd_set_dynamic_viewport ( node_args->cmd_buffer, &viewport, key );

        se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
        uint64_t mesh_count = mesh_query_result.entity_count;
        for ( uint64_t j = 0; j < mesh_count; ++j ) {
            viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
            xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->shadow_pipeline );
            xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

            sm_vec_3f_t up = { 0, 1, 0 };
            
            sm_vec_3f_t dir = {
                .x = mesh_component->orientation[0],
                .y = mesh_component->orientation[1],
                .z = mesh_component->orientation[2],
            };
            dir = sm_vec_3f_norm ( dir );
            
            sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );

            sm_mat_4x4f_t trans = {
                .r0[0] = 1,
                .r1[1] = 1,
                .r2[2] = 1,
                .r3[3] = 1,
                .r0[3] = mesh_component->position[0],
                .r1[3] = mesh_component->position[1],
                .r2[3] = mesh_component->position[2],
            };

            draw_cbuffer_vs_t vs = {
                .world = sm_matrix_4x4f_mul ( trans, rot ),
            };

            // Bind draw resources
            xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
                .set = xg_resource_binding_set_per_draw_m,
                .buffer_count = 1,
                .buffers = xg_buffer_resource_binding_m (
                    .shader_register = 0,
                    .type = xg_buffer_binding_type_uniform_m,
                    .range = xg->write_workload_uniform ( node_args->workload, &vs, sizeof ( vs ) ),
                )
            );

            xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

            // Set vertex streams
            xg_vertex_stream_binding_t vertex_bindings[2] = {
                xg_vertex_stream_binding_m (
                    .buffer = mesh_component->pos_buffer,
                    .stream_id = 0,
                    .offset = 0,
                ),
                xg_vertex_stream_binding_m (
                    .buffer = mesh_component->nor_buffer,
                    .stream_id = 1,
                    .offset = 0,
                )
            };
            xg->cmd_set_vertex_streams ( cmd_buffer, vertex_bindings, 2, key );

            // Draw
            xg->cmd_draw_indexed ( cmd_buffer, mesh_component->idx_buffer, mesh_component->index_count, 0, key );
        }
    }
}

xf_node_h add_shadow_pass ( xf_graph_h graph, xf_texture_h target ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    xf_node_h node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "shadows",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = shadow_pass_routine
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
