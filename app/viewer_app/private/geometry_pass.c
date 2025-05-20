#include "geometry_pass.h"

#include <std_log.h>

#include <se.h>
#include <se.inl>
#include <rv.h>
#include <sm_matrix.h>
#include <sm_quat.h>

#include <viewapp_state.h>

typedef struct {
    sm_mat_4x4f_t world;
    sm_mat_4x4f_t prev_world;
} geometry_vertex_uniforms_t;

typedef struct {
    float base_color[3];
    float roughness;
    float emissive[3];
    float metalness;
    uint32_t object_id;
    uint32_t mat_id;
} geometry_fragment_uniforms_t;

static void geometry_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .component_count = 2, 
        .components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->geometry_pipeline );

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

        //sm_vec_3f_t prev_up = sm_vec_3f ( mesh_component->prev_transform.up );
        //sm_vec_3f_t prev_dir = sm_vec_3f ( mesh_component->prev_transform.orientation );
        //prev_dir = sm_vec_3f_norm ( prev_dir );
        //sm_mat_4x4f_t prev_rot = sm_matrix_4x4f_dir_rotation ( prev_dir, prev_up );
        sm_mat_4x4f_t prev_rot = sm_quat_to_4x4f ( sm_quat ( mesh_component->prev_transform.orientation ) );
        float prev_scale = mesh_component->prev_transform.scale;
        sm_mat_4x4f_t prev_trans = {
            .r0[0] = prev_scale,
            .r1[1] = prev_scale,
            .r2[2] = prev_scale,
            .r3[3] = 1,
            .r0[3] = mesh_component->prev_transform.position[0],
            .r1[3] = mesh_component->prev_transform.position[1],
            .r2[3] = mesh_component->prev_transform.position[2],
        };

        geometry_vertex_uniforms_t vs = {
            .world = sm_matrix_4x4f_mul ( trans, rot ),
            .prev_world = sm_matrix_4x4f_mul ( prev_trans, prev_rot ),
        };

        geometry_fragment_uniforms_t fs = {
            .base_color[0] = mesh_component->material.base_color[0],
            .base_color[1] = mesh_component->material.base_color[1],
            .base_color[2] = mesh_component->material.base_color[2],
            .object_id = mesh_component->object_id,
            .roughness = mesh_component->material.roughness,
            .metalness = mesh_component->material.metalness,
            .mat_id = mesh_component->material.ssr ? 1 : 0,
            .emissive[0] = mesh_component->material.emissive[0],
            .emissive[1] = mesh_component->material.emissive[1],
            .emissive[2] = mesh_component->material.emissive[2],
        };

        xg_texture_h color_texture = mesh_component->material.color_texture;
        if ( color_texture == xg_null_handle_m ) {
            color_texture = xg->get_default_texture ( node_args->device, xg_default_texture_r8g8b8a8_unorm_white_m );
        }

        xg_texture_h normal_texture = mesh_component->material.normal_texture;
        if ( normal_texture == xg_null_handle_m ) {
            normal_texture = xg->get_default_texture ( node_args->device, xg_default_texture_r8g8b8a8_unorm_tbn_up_m );
        }

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
                },
                .texture_count = 2,
                .textures = {
                    xg_texture_resource_binding_m (
                        .shader_register = 2,
                        .layout = xg_texture_layout_shader_read_m,
                        .texture = color_texture,
                    ),
                    xg_texture_resource_binding_m (
                        .shader_register = 3,
                        .layout = xg_texture_layout_shader_read_m,
                        .texture = normal_texture,
                    ),
                },
                .sampler_count = 1,
                .samplers = {
                    xg_sampler_resource_binding_m (
                        .shader_register = 4,
                        .sampler = xg->get_default_sampler ( node_args->device, xg_default_sampler_linear_wrap_m ),
                    ),
                }
            )
        ) );

        xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
            .pipeline = pipeline_state,
            .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings,
            .index_buffer = mesh_component->geo_gpu_data.idx_buffer,
            .primitive_count = mesh_component->geo_data.index_count / 3,
            .vertex_buffers_count = 5,
            .vertex_buffers = { 
                mesh_component->geo_gpu_data.pos_buffer, 
                mesh_component->geo_gpu_data.nor_buffer, 
                mesh_component->geo_gpu_data.tan_buffer, 
                mesh_component->geo_gpu_data.bitan_buffer, 
                mesh_component->geo_gpu_data.uv_buffer 
            },
        ) );
    }
}

xf_node_h add_geometry_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h radiosity, xf_texture_h object_id, xf_texture_h velocity, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    xf_node_h node = xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "geometry",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = geometry_pass,
            .auto_renderpass = true,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 6,
            .render_targets = {
                xf_render_target_dependency_m ( .texture = color ),
                xf_render_target_dependency_m ( .texture = normal ),
                xf_render_target_dependency_m ( .texture = material ),
                xf_render_target_dependency_m ( .texture = radiosity ),
                xf_render_target_dependency_m ( .texture = object_id ),
                xf_render_target_dependency_m ( .texture = velocity ),
            },
            .depth_stencil_target = depth,
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = {
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
            }
        )
    ) );

    return node;
}

typedef struct {
    sm_mat_4x4f_t world;
} object_id_vertex_uniforms_t;

typedef struct {
    uint32_t object_id;
} object_id_fragment_uniforms_t;

static void object_id_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .component_count = 2, 
        .components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->object_id_pipeline );

        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        //sm_vec_3f_t up = {
        //    .x = transform_component->up[0],
        //    .y = transform_component->up[1],
        //    .z = transform_component->up[2],
        //};

        //sm_vec_3f_t dir = {
        //    .x = transform_component->orientation[0],
        //    .y = transform_component->orientation[1],
        //    .z = transform_component->orientation[2],
        //};
        //dir = sm_vec_3f_norm ( dir );
        
        //sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );
        sm_mat_4x4f_t rot = sm_quat_to_4x4f ( sm_quat ( transform_component->orientation ) );

        sm_mat_4x4f_t trans = {
            .r0[0] = transform_component->scale,
            .r1[1] = transform_component->scale,
            .r2[2] = transform_component->scale,
            .r3[3] = 1,
            .r0[3] = transform_component->position[0],
            .r1[3] = transform_component->position[1],
            .r2[3] = transform_component->position[2],
        };

        object_id_vertex_uniforms_t vs = {
            .world = sm_matrix_4x4f_mul ( trans, rot ),
        };

        object_id_fragment_uniforms_t fs = {
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
            .index_buffer = mesh_component->geo_gpu_data.idx_buffer,
            .primitive_count = mesh_component->geo_data.index_count / 3,
            .vertex_buffers_count = 1,
            .vertex_buffers = { mesh_component->geo_gpu_data.pos_buffer },
        ) );
    }
}

xf_node_h add_object_id_node ( xf_graph_h graph, xf_texture_h object_id, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_texture_info_t info;
    xf->get_texture_info ( &info, object_id );

    xf_node_h node = xf->create_node ( graph, &xf_node_params_m (
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
