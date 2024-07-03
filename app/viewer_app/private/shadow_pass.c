#include <shadow_pass.h>

#include <viewapp_state.h>

#include <sm_matrix.h>
#include <se.inl>

typedef struct {
    xs_pipeline_state_h pipeline_state;
} shadow_pass_args_t;

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

    {
        xg_render_textures_binding_t render_textures = xg_null_render_texture_bindings_m;
        render_textures.depth_stencil.texture = node_args->io->depth_stencil_target;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

#if 0
    se_query_h light_query;
    {
        se_query_params_t query_params = se_default_query_params_m;
        std_bitset_set ( query_params.request_component_flags, LIGHT_COMPONENT_ID );
        light_query = se->create_query ( &query_params );
    }

    se_query_h mesh_query;
    {
        se_query_params_t query_params = se_default_query_params_m;
        std_bitset_set ( query_params.request_component_flags, MESH_COMPONENT_ID );
        mesh_query = se->create_query ( &query_params );
    }

    se->resolve_pending_queries();
    const se_query_result_t* light_query_result = se->get_query_result ( light_query );
    const se_query_result_t* mesh_query_result = se->get_query_result ( mesh_query );
    uint64_t light_count = light_query_result->count;
    uint64_t mesh_count = mesh_query_result->count;
#else
    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m (
        .component_count = 1,
        .components = { LIGHT_COMPONENT_ID }
    ) );

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m (
        .component_count = 1,
        .components = { MESH_COMPONENT_ID }
    ) );

    se_component_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    uint64_t light_count = light_query_result.entity_count;
#endif
    for ( uint64_t i = 0; i < light_count; ++i ) {
#if 0
        se_entity_h light_entity = light_query_result->entities[i];
        se_component_h light_component_handle = se->get_component ( light_entity, LIGHT_COMPONENT_ID );
        std_auto_m light_component = ( viewapp_light_component_t* ) light_component_handle;
#else
        viewapp_light_component_t* light_component = se_component_iterator_next ( &light_iterator );
#endif

        xg_buffer_range_t view_buffer_range;
        {
            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, light_component->view );

            view_cbuffer_data_t view_data;
            view_data.view_from_world = view_info.view_matrix;
            view_data.proj_from_view = view_info.proj_matrix;
            view_data.world_from_view = view_info.inverse_view_matrix;
            view_data.view_from_proj = view_info.inverse_proj_matrix;
            view_data.prev_view_from_world = view_info.prev_frame_view_matrix;
            view_data.prev_proj_from_view = view_info.prev_frame_proj_matrix;
            view_data.z_near = view_info.proj_params.near_z;
            view_data.z_far = view_info.proj_params.far_z;

            view_buffer_range = xg->write_workload_uniform ( node_args->workload, &view_data, sizeof ( view_data ) );
        }

        {
            xg_buffer_resource_binding_t buffer;
            buffer.shader_register = 0;
            buffer.type = xg_buffer_binding_type_uniform_m;
            buffer.range = view_buffer_range;

            xg_pipeline_resource_bindings_t view_bindings = xg_default_pipeline_resource_bindings_m;
            view_bindings.set = xg_resource_binding_set_per_view_m;
            view_bindings.buffer_count = 1;
            view_bindings.buffers = &buffer;

            xg->cmd_set_pipeline_resources ( node_args->cmd_buffer, &view_bindings, node_args->base_key );
        }

        // TODO
        xg_viewport_state_t viewport = xg_default_viewport_state_m;
        viewport.width = 1024;
        viewport.height = 1024;
        xg->cmd_set_pipeline_viewport ( node_args->cmd_buffer, &viewport, key );

#if 0
        for ( uint64_t j = 0; j < mesh_count; ++j ) {
            se_entity_h mesh_entity = mesh_query_result->entities[j];
            se_component_h mesh_component_handle = se->get_component ( mesh_entity, MESH_COMPONENT_ID );
            std_auto_m mesh_component = ( viewapp_mesh_component_t* ) mesh_component_handle;
#else
        se_component_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
        uint64_t mesh_count = mesh_query_result.entity_count;
        for ( uint64_t j = 0; j < mesh_count; ++j ) {
            viewapp_mesh_component_t* mesh_component = se_component_iterator_next ( &mesh_iterator );
#endif
            xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->shadow_pipeline );
            xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

            xg_buffer_range_t vert_draw_buffer_range;
            {
                draw_cbuffer_vs_t vs;
                std_mem_zero_m ( &vs );
                sm_vec_3f_t up = sm_vec_3f ( 0, 1, 0 );
                sm_vec_3f_t dir;
                dir.x = mesh_component->orientation[0];
                dir.y = mesh_component->orientation[1];
                dir.z = mesh_component->orientation[2];
                dir = sm_vec_3f_norm ( dir );
                sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );

                sm_mat_4x4f_t trans;
                std_mem_zero_m ( &trans );
                trans.r0[0] = 1;
                trans.r1[1] = 1;
                trans.r2[2] = 1;
                trans.r3[3] = 1;
                trans.r0[3] = mesh_component->position[0];
                trans.r1[3] = mesh_component->position[1];
                trans.r2[3] = mesh_component->position[2];
                vs.world = sm_matrix_4x4f_mul ( trans, rot );

                vert_draw_buffer_range = xg->write_workload_uniform ( node_args->workload, &vs, sizeof ( vs ) );
            }

            // Bind draw resources
            {
                xg_buffer_resource_binding_t buffer;

                buffer.shader_register = 0;
                buffer.type = xg_buffer_binding_type_uniform_m;
                buffer.range = vert_draw_buffer_range;

                xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
                draw_bindings.set = xg_resource_binding_set_per_draw_m;
                draw_bindings.buffer_count = 1;
                draw_bindings.buffers = &buffer;

                xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );
            }

            // Set vertex streams
            {
                xg_vertex_stream_binding_t vertex_bindings[2];
                vertex_bindings[0].buffer = mesh_component->pos_buffer;
                vertex_bindings[0].stream_id = 0;
                vertex_bindings[0].offset = 0;
                vertex_bindings[1].buffer = mesh_component->nor_buffer;
                vertex_bindings[1].stream_id = 1;
                vertex_bindings[1].offset = 0;
                xg->cmd_set_vertex_streams ( cmd_buffer, vertex_bindings, 2, key );
            }

            // Draw
            xg->cmd_draw_indexed ( cmd_buffer, mesh_component->idx_buffer, mesh_component->index_count, 0, key );
        }
    }

#if 0
    se->dispose_query_results();
#endif
}

xf_node_h add_shadow_pass ( xf_graph_h graph, xf_texture_h target ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;

    shadow_pass_args_t args;
    args.pipeline_state = xs->lookup_pipeline_state ( "depth" );

    xf_node_h node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        //params.render_targets[params.render_targets_count++] = xf_render_target_dependency_m ( target, xg_default_texture_view_m );
        params.depth_stencil_target = target;
        params.passthrough.enable = true;
        params.passthrough.render_targets[0].mode = xf_node_passthrough_mode_clear_m;
        params.execute_routine = shadow_pass_routine;
        std_str_copy_static_m ( params.debug_name, "shadows" );
        node = xf->create_node ( graph, &params );
    }

    return node;
}
