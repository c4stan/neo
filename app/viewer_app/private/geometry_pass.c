#include "geometry_pass.h"

#include <std_log.h>

#include <se.h>
#include <se.inl>
#include <rv.h>
#include <sm_matrix.h>

#include <viewapp_state.h>

typedef struct {
    sm_mat_4x4f_t world;
} draw_cbuffer_vs_t;

typedef struct {
    float base_color[3];
    uint32_t object_id;
    float roughness;
    float metalness;
} draw_cbuffer_fs_t;

static void geometry_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // Bind render targets
    //xg_render_textures_binding_t render_textures = {
    //    .render_targets_count = 3,
    //    .render_targets = {
    //        xf_render_target_binding_m ( node_args->io->render_targets[0] ),
    //        xf_render_target_binding_m ( node_args->io->render_targets[1] ),
    //        xf_render_target_binding_m ( node_args->io->render_targets[2] ),
    //    },
    //    .depth_stencil.texture = node_args->io->depth_stencil_target,
    //};
    //xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    //xg_viewport_state_t viewport = xg_viewport_state_m (
    //    .width = pass_args->width,
    //    .height = pass_args->height,
    //);
    //xg->cmd_set_dynamic_viewport ( cmd_buffer, &viewport, key );

    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );

        // Set pipeline
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->geometry_pipeline );
        //xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        sm_vec_3f_t up = {
            .x = mesh_component->up[0],
            .y = mesh_component->up[1],
            .z = mesh_component->up[2],
        };

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

        std_assert_m ( i < 256 );
        draw_cbuffer_fs_t fs = {
            .base_color[0] = mesh_component->material.base_color[0],
            .base_color[1] = mesh_component->material.base_color[1],
            .base_color[2] = mesh_component->material.base_color[2],
            .object_id = mesh_component->object_id,
            .roughness = mesh_component->material.roughness,
            .metalness = mesh_component->material.metalness,
        };

        // Bind draw resources
        xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 2,
                .buffers = {
                    xg_buffer_resource_binding_m (
                        .shader_register = 0,
                        .range = xg->write_workload_uniform ( workload, &vs, sizeof ( vs ) ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 1,
                        .range = xg->write_workload_uniform ( workload, &fs, sizeof ( fs ) ),
                    )
                }
            )
        ) );

        //xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

        // Set vertex streams
        //xg_vertex_stream_binding_t vertex_bindings[2] = {
        //    xg_vertex_stream_binding_m (
        //        .buffer = mesh_component->pos_buffer,
        //        .stream_id = 0,
        //        .offset = 0,
        //    ),
        //    xg_vertex_stream_binding_m (
        //        .buffer = mesh_component->nor_buffer,
        //        .stream_id = 1,
        //        .offset = 0,
        //    )
        //};
        //xg->cmd_set_vertex_streams ( cmd_buffer, vertex_bindings, 2, key );

        // Draw
        //xg->cmd_draw_indexed ( cmd_buffer, mesh_component->idx_buffer, mesh_component->index_count, 0, key );
    
        xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
            .pipeline = pipeline_state,
            .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings,
            .index_buffer = mesh_component->idx_buffer,
            .primitive_count = mesh_component->index_count / 3,
            .vertex_buffers_count = 2,
            .vertex_buffers = { mesh_component->pos_buffer, mesh_component->nor_buffer },
        ) );
    }
}

xf_node_h add_geometry_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    xf_node_h node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "geometry",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = geometry_pass,
            .auto_renderpass = true,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 3,
            .render_targets = {
                xf_render_target_dependency_m ( .texture = color ),
                xf_render_target_dependency_m ( .texture = normal ),
                xf_render_target_dependency_m ( .texture = material ),
            },
            .depth_stencil_target = depth,
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = {
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
            }
        )
    ) );

    return node;
}

static void object_id_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );

        // Set pipeline
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->object_id_pipeline );

        sm_vec_3f_t up = {
            .x = mesh_component->up[0],
            .y = mesh_component->up[1],
            .z = mesh_component->up[2],
        };

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

        std_assert_m ( i < 256 );
        struct frag_uniforms {
            uint32_t object_id;
        } fs = {
            .object_id = mesh_component->object_id,
        };

        // Bind draw resources
        xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 2,
                .buffers = {
                    xg_buffer_resource_binding_m (
                        .shader_register = 0,
                        .range = xg->write_workload_uniform ( workload, &vs, sizeof ( vs ) ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 1,
                        .range = xg->write_workload_uniform ( workload, &fs, sizeof ( fs ) ),
                    )
                }
            )
        ) );

        xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
            .pipeline = pipeline_state,
            .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings,
            .index_buffer = mesh_component->idx_buffer,
            .primitive_count = mesh_component->index_count / 3,
            .vertex_buffers_count = 1,
            .vertex_buffers = { mesh_component->pos_buffer },
        ) );
    }
}

xf_node_h add_object_id_node ( xf_graph_h graph, xf_texture_h object_id, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_texture_info_t info;
    xf->get_texture_info ( &info, object_id );

    xf_node_h node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "object_id",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = object_id_pass,
            .auto_renderpass = true,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = {
                xf_render_target_dependency_m ( .texture = object_id ),
            },
            .depth_stencil_target = depth,
        ),
    ) );

    return node;
}
