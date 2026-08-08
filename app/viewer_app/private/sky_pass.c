#include "sky_pass.h"

#include "viewapp_state.h"

#include <sm_vector.h>
#include <sm_matrix.h>
#include <sm_quat.h>

typedef struct {
    xs_database_pipeline_h pipeline;
} sky_pass_args_t;

static viewapp_sky_component_t* sky_pass_get_sky_component ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( 
        .include_component_count = 1, 
        .include_components = { viewapp_sky_component_id_m } 
    ) );
    if ( query_result.entity_count == 0 ) {
        return NULL;
    }
    std_assert_m ( query_result.entity_count == 1 );
    se_stream_iterator_t sky_component_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    viewapp_sky_component_t* sky_component = se_stream_iterator_next ( &sky_component_iterator );
    return sky_component;
}

static void sky_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;
    std_auto_m pass_args = ( sky_pass_args_t* ) user_args;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    viewapp_sky_component_t* sky_component = sky_pass_get_sky_component();
    if ( sky_component->radiance_texture == xg_null_handle_m ) {
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
                    .texture = sky_component->radiance_texture,
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

typedef struct {
    xs_database_pipeline_h pipeline;
    uint32_t face;
} sky_cubemap_gen_args_t;

typedef struct {
    sm_mat_4x4f_t view_from_world;
    sm_mat_4x4f_t proj_from_view;
} sky_cubemap_gen_uniforms_t;

static void sky_cubemap_gen_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;
    std_auto_m pass_args = ( sky_cubemap_gen_args_t* ) user_args;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    viewapp_sky_component_t* sky_component = sky_pass_get_sky_component();
    std_assert_m ( sky_component->radiance_texture != xg_null_handle_m );

    sm_quat_t dir;
    switch ( pass_args->face ) {
    case 0:
        dir = sm_quat_from_vec ( sm_vec_3f_set ( 1, 0, 0 ) );
        break;
    case 1:
        dir = sm_quat_from_vec ( sm_vec_3f_set ( -1, 0, 0 ) );
        break;
    case 2:
        dir = sm_quat_from_vec ( sm_vec_3f_set ( 0, 1, 0 ) );
        break;
    case 3:
        dir = sm_quat_from_vec ( sm_vec_3f_set ( 0, -1, 0 ) );
        break;
    case 4:
        dir = sm_quat_from_vec ( sm_vec_3f_set ( 0, 0, 1 ) );
        break;
    case 5:
        dir = sm_quat_from_vec ( sm_vec_3f_set ( 0, 0, -1 ) );
        break;
    }

    sky_cubemap_gen_uniforms_t uniforms = {
        .view_from_world = sm_matrix_view ( sm_vec_3f_set (0,0,0), dir ),
        .proj_from_view = sm_matrix_perspective_proj ( &sm_perspective_projection_params_m (
            .aspect_ratio = 1,
            .fov_y = 90 * sm_deg_to_rad_m,
            .infinite_far_z = true,
        ) ),
    };

    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );
    xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
        .bindings = xg_pipeline_resource_bindings_m (
            .buffer_count = 1,
            .buffers = {
                xg_buffer_resource_binding_m (
                    .shader_register = 0,
                    .range = xg->write_workload_uniform ( workload, std_buffer_struct_m ( &uniforms ) ),
                ),
            },
            .texture_count = 1,
            .textures = {
                xg_texture_resource_binding_m (
                    .shader_register = 1,
                    .layout = xg_texture_layout_shader_read_m,
                    .texture = sky_component->radiance_texture,
                ),
            },
            .sampler_count = 1,
            .samplers = {
                xg_sampler_resource_binding_m (
                    .shader_register = 2,
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

xf_node_h add_sky_cubemap_gen_node ( xf_graph_h graph, xf_texture_h cubemap, uint32_t face ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    sky_cubemap_gen_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "cubemap_sky_gen" ) ),
        .face = face,
    };

    xf_node_params_t node_params = xf_node_params_m (
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = sky_cubemap_gen_pass_routine,
            .auto_renderpass = true,
            .user_args = std_buffer_struct_m ( &args ),
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = {
                xf_render_target_dependency_m ( .texture = cubemap, .view = xg_texture_view_m ( 
                    .array_base = face, 
                    .array_count = 1,
                    .mip_base = 0,
                    .mip_count = 1,
                ) ),
            },
        ),
    );
    std_string_t name_string = std_static_string_m ( node_params.debug_name );
    std_string_append_format ( &name_string, "cubemap_sky_gen" std_fmt_u32_m, face );
    xf_node_h node = xf->create_node ( graph, &node_params );
    return node;
}
