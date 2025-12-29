#include "viewapp_render.h"

#include <geometry_pass.h>
#include <lighting_pass.h>
#include <hiz_pass.h>
#include <ssgi_pass.h>
#include <blur_pass.h>
#include <ssr_pass.h>
#include <ui_pass.h>
#include <shadow_pass.h>
#include <raytrace_pass.h>
#include <tessellation_pass.h>

#include "viewapp_state.h"

#include <std_file.h>

void viewapp_boot_render ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    uint32_t resolution_x = 1920;
    uint32_t resolution_y = 1024;

    state->render.resolution_x = resolution_x;
    state->render.resolution_y = resolution_y;

    wm_i* wm = state->modules.wm;
    wm_window_h window;
    {
        wm_window_params_t window_params = {
            .name = "viewer_app",
            .x = 0,
            .y = 0,
            .width = resolution_x,
            .height = resolution_y,
            .gain_focus = true,
            .borderless = false
        };
        std_log_info_m ( "Creating window "std_fmt_str_m, window_params.name );
        window = wm->create_window ( &window_params );
    }
    state->render.window = window;
    wm->get_window_info ( &state->render.window_info, window );
    wm->get_window_input_state ( window, &state->render.input_state );

    xg_device_h device;
    xg_device_info_t device_info;
    xg_i* xg = state->modules.xg;
    {
        size_t device_count = xg->get_devices_count();
        std_assert_m ( device_count > 0 );
        xg_device_h devices[16];
        xg->get_devices ( devices, 16 );
        device = devices[0];
        bool activate_result = xg->activate_device ( device );
        std_assert_m ( activate_result );
        xg->get_device_info ( &device_info, device );
        std_log_info_m ( "Picking device 0 (" std_fmt_str_m ") as default device", device_info.name );
    }
    xg_swapchain_h swapchain = xg->create_window_swapchain ( &xg_swapchain_window_params_m (
        .window = window,
        .device = device,
        .texture_count = 3,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m,
        .debug_name = "swapchain",
    ) );
    std_assert_m ( swapchain != xg_null_handle_m );

    state->render.device = device;
    state->render.swapchain = swapchain;
    state->render.supports_raytrace = device_info.supports_raytrace;

    xs_i* xs = state->modules.xs;
    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "viewapp_sdb" ) );
    state->render.sdb = sdb;
    {
        char path_buffer[128];
        std_string_t path = std_static_string_m ( path_buffer );
        std_path_append_dir ( &path, std_source_data_path_m );
        std_path_append_dir ( &path, "shader" );
        xs->add_data_folder ( sdb, path_buffer );
    }
    xs->set_output_folder ( sdb, "shader" );
    xs->set_build_params ( sdb, &xs_database_build_params_m (
        .pipeline_flags = xg_pipeline_flag_bit_capture_statistics_m,
        .build_flags = xs_database_build_flag_bit_output_statistics_m,
    ) );
    xs_database_build_result_t build_result = xs->build_database ( sdb );
    std_assert_m ( build_result.failed_pipeline_states == 0 );

    xf_i* xf = state->modules.xf;
    xf->load_shaders ( device );

    viewapp_boot_workload_resources_layout();
}

void viewapp_boot_workload_resources_layout ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;

    state->render.workload_bindings_layout = xg->create_resource_layout ( &xg_resource_bindings_layout_params_m (
        .device = state->render.device,
        .resource_count = 1,
        .resources = { xg_resource_binding_layout_m ( .shader_register = 0, .type = xg_resource_binding_buffer_uniform_m, .stages = xg_shading_stage_bit_all_m ) },
        .debug_name = "workload_globals"
    ) );
}

typedef struct {
    float resolution_x_f32;
    float resolution_y_f32;
    uint32_t resolution_x_u32;
    uint32_t resolution_y_u32;
    uint32_t frame_id;
    float time_ms;
    uint32_t clear_history;
    uint32_t _pad0[1];
    rv_matrix_4x4_t view_from_world;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t jittered_proj_from_view;
    rv_matrix_4x4_t view_from_proj;
    rv_matrix_4x4_t world_from_view;
    rv_matrix_4x4_t prev_view_from_world;
    rv_matrix_4x4_t prev_proj_from_view;
    float cam_pos[3];
    float z_near;
    float z_far;
    float v_fov;
    uint32_t _pad1[2];
    float frustum_planes[6][4];
} workload_uniforms_t;

void viewapp_update_workload_uniforms ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;
    xf_i* xf = state->modules.xf;

    bool camera_found = false;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( 
        .include_component_count = 1, 
        .include_components = { viewapp_camera_component_id_m } 
    ) );
    se_stream_iterator_t camera_iterator = se_component_iterator_m ( &camera_query_result.components[0], 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );

        if ( !camera_component->enabled ) {
            continue;
        }

        rv_i* rv = std_module_get_m ( rv_module_name_m );
        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, camera_component->view );

        if ( view_info.proj_params.type == rv_projection_orthographic_m ) {
            continue;
        }

        workload_uniforms_t uniforms = {
            .frame_id = state->render.frame_id,
            .time_ms = state->render.time_ms,
            .resolution_x_u32 = ( uint32_t ) state->render.resolution_x,
            .resolution_y_u32 = ( uint32_t ) state->render.resolution_y,
            .resolution_x_f32 = ( float ) state->render.resolution_x,
            .resolution_y_f32 = ( float ) state->render.resolution_y,
            .clear_history = state->render.clear_history,
            .view_from_world = view_info.view_matrix,
            .proj_from_view = view_info.proj_matrix,
            .jittered_proj_from_view = view_info.jittered_proj_matrix,
            .world_from_view = view_info.inverse_view_matrix,
            .view_from_proj = view_info.inverse_proj_matrix,
            .prev_view_from_world = view_info.prev_frame_view_matrix,
            .prev_proj_from_view = view_info.prev_frame_proj_matrix,
            .z_near = view_info.proj_params.perspective.near_z,
            .z_far = view_info.proj_params.perspective.far_z,
            .v_fov = view_info.proj_params.perspective.fov_y,
            .cam_pos = {
                view_info.transform.position[0], 
                view_info.transform.position[1], 
                view_info.transform.position[2], 
            },
            .frustum_planes = {
                { view_info.frustum_planes[0][0], view_info.frustum_planes[0][1], view_info.frustum_planes[0][2], view_info.frustum_planes[0][3] },
                { view_info.frustum_planes[1][0], view_info.frustum_planes[1][1], view_info.frustum_planes[1][2], view_info.frustum_planes[1][3] },
                { view_info.frustum_planes[2][0], view_info.frustum_planes[2][1], view_info.frustum_planes[2][2], view_info.frustum_planes[2][3] },
                { view_info.frustum_planes[3][0], view_info.frustum_planes[3][1], view_info.frustum_planes[3][2], view_info.frustum_planes[3][3] },
                { view_info.frustum_planes[4][0], view_info.frustum_planes[4][1], view_info.frustum_planes[4][2], view_info.frustum_planes[4][3] },
                { view_info.frustum_planes[5][0], view_info.frustum_planes[5][1], view_info.frustum_planes[5][2], view_info.frustum_planes[5][3] },
            }
        };

        // disable jittering if TAA is off 
        xf_node_h taa_node = xf->get_node_by_name ( state->render.render_graph, "taa" );
        if ( taa_node != xf_null_handle_m ) {
            xf_node_info_t taa_node_info;
            xf->get_node_info ( &taa_node_info, state->render.render_graph, taa_node );
            if ( !taa_node_info.enabled ) {
                uniforms.jittered_proj_from_view = view_info.proj_matrix;
            }
        }

        xg_buffer_range_t range = xg->write_workload_uniform ( workload, &uniforms, sizeof ( uniforms ) );
        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg_resource_bindings_h bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = state->render.workload_bindings_layout,
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 1,
                .buffers = { xg_buffer_resource_binding_m ( .shader_register = 0, .range = range ) }
            )
        ) );
        xg->set_workload_global_bindings ( workload, bindings );

        camera_found = true;
        break;
    }

    if ( !camera_found ) {
        workload_uniforms_t uniforms = {
            .frame_id = state->render.frame_id,
            .time_ms = state->render.time_ms,
            .resolution_x_u32 = ( uint32_t ) state->render.resolution_x,
            .resolution_y_u32 = ( uint32_t ) state->render.resolution_y,
            .resolution_x_f32 = ( float ) state->render.resolution_x,
            .resolution_y_f32 = ( float ) state->render.resolution_y,
        };

        xg_buffer_range_t range = xg->write_workload_uniform ( workload, &uniforms, sizeof ( uniforms ) );
        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg_resource_bindings_h bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = state->render.workload_bindings_layout,
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 1,
                .buffers = { xg_buffer_resource_binding_m ( .shader_register = 0, .range = range ) }
            )
        ) );
        xg->set_workload_global_bindings ( workload, bindings );
    }

    state->render.clear_history = false;
}

static void viewapp_bind_mouse_pick_graph_routines ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_graph_h graph = state->render.mouse_pick_graph;
    if ( graph == xf_null_handle_m ) return;
    bind_object_id_routine ( graph );
}

void viewapp_load_mouse_pick_graph ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_device_h device = state->render.device;    
    uint32_t resolution_x = state->render.resolution_x;
    uint32_t resolution_y = state->render.resolution_y;
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "mouse_pick_graph"
    ) );
    state->render.mouse_pick_graph = graph;

    xf_texture_h object_id_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8_uint_m,
        .debug_name = "mouse_pick_object_id"
    ) );

    xf_texture_h depth_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_d32_sfloat_m,
        .debug_name = "mouse_pick_depth"
    ) );

    xf->create_node ( graph, &xf_node_params_m ( 
        .debug_name = "mouse_pick_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = {
            .textures = { 
                xf_texture_clear_m (),
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m ) ),
            }
        },
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 2,
            .copy_texture_writes = { 
                xf_copy_texture_dependency_m ( .texture = object_id_texture ),
                xf_copy_texture_dependency_m ( .texture = depth_texture ),
            }
        )
    ) );

    add_object_id_node ( graph, object_id_texture, depth_texture );

    xg_texture_h readback_texture = xg->create_texture ( &xg_texture_params_m (
        .memory_type = xg_memory_type_readback_m,
        .device = device,
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8_uint_m,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m,
        .tiling = xg_texture_tiling_linear_m,
        .debug_name = "object_id_readback",
    ) );
    state->render.object_id_readback_texture = readback_texture;

    xf_texture_h copy_dest = xf->create_texture_from_external ( readback_texture );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "mouse_pick_object_id_copy",
        .type = xf_node_type_copy_pass_m,
        .pass.copy = xf_node_copy_pass_params_m(),
        .resources = xf_node_resource_params_m (
            .copy_texture_reads_count = 1,
            .copy_texture_reads = {
                xf_copy_texture_dependency_m ( .texture = object_id_texture )
            },
            .copy_texture_writes_count = 1,
            .copy_texture_writes = {
                xf_copy_texture_dependency_m ( .texture = copy_dest )
            }
        )
    ) );

    xf->finalize_graph ( graph );
}

static void viewapp_bind_restir_di_graph_routines ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_graph_h graph = state->render.render_graph;
    bind_raytrace_setup_routine ( graph );
    bind_ui_routine ( graph );
}

static void viewapp_boot_restir_di_graph ( void ) {
#if xg_enable_raytracing_m
    viewapp_state_t* state = viewapp_state_get();
    xg_device_h device = state->render.device;    
    xg_swapchain_h swapchain = state->render.swapchain;    
    uint32_t resolution_x = state->render.resolution_x;
    uint32_t resolution_y = state->render.resolution_y;
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "restir_di"
    ) );
    state->render.render_graph = graph;

    // gbuffer laydown
    xf_texture_h color_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture",
    ) );

    xf_texture_h normal_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h material_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "material_texture"
    ) );

    xf_texture_h radiosity_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "radiosity_texture"
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );

    xf_texture_h velocity_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,
            .debug_name = "depth_texture",
            .clear_on_create = true,
            .clear.depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m )
        ),
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "geometry_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { 
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m ) ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m( .f32 = { 0.5, 0.5, 0, 0 } ) ),
            }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 7,
            .copy_texture_writes = {
                xf_copy_texture_dependency_m ( .texture = depth_texture ),
                xf_copy_texture_dependency_m ( .texture = color_texture ),
                xf_copy_texture_dependency_m ( .texture = normal_texture ),
                xf_copy_texture_dependency_m ( .texture = material_texture ),
                xf_copy_texture_dependency_m ( .texture = radiosity_texture ),
                xf_copy_texture_dependency_m ( .texture = object_id_texture ),
                xf_copy_texture_dependency_m ( .texture = velocity_texture ),
            }
        ),
    ) );

    gbuffer_textures_t gbuffer = {
        .color = color_texture,
        .normal = normal_texture,
        .material = material_texture,
        .radiosity = radiosity_texture,
        .object_id = object_id_texture,
        .velocity = velocity_texture,
    };
    add_geometry_node ( graph, &gbuffer, depth_texture );

    // restir di
    xf_texture_h lighting_texture = xf->create_texture ( &xf_texture_params_m ( 
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "lighting_texture"
    ));

    xf_buffer_h instance_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = raytrace_instance_data_size(),
        .debug_name = "instance_buffer",
    ) );

    xf_buffer_h light_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = raytrace_light_data_size(),
        .debug_name = "light_buffer",
    ) );
    
    add_raytrace_setup_pass ( graph, instance_buffer, light_buffer );

    xf_texture_h reservoir_buffer = xf->create_multi_buffer ( &xf_multi_buffer_params_m (
        .buffer = xf_buffer_params_m (
            .size = 32 * resolution_x * resolution_y, // TODO
            .debug_name = "reservoir_buffer",
            .init = &xg_buffer_init_m (
                .mode = xg_buffer_init_mode_clear_m,
                .clear = 0,
            ),
        ),
        .multi_buffer_count = 2,
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "restir_di_sample",
        .type = xf_node_type_raytrace_pass_m,
        .pass.raytrace = xf_node_raytrace_pass_params_m (
            .thread_count = { resolution_x, resolution_y, 1 },
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "restir_di_sample" ) ),
            .raytrace_worlds_count = 1,
            .raytrace_worlds = { state->render.raytrace_world },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = {
                xf_shader_buffer_dependency_m ( .buffer = instance_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = light_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = reservoir_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .sampled_textures_count = 4,
            .sampled_textures = {
                xf_shader_texture_dependency_m ( .texture = color_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = normal_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = material_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = depth_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { 
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = color_texture ) ), 
            }
        ),
    ) );

    xf_buffer_h prev_reservoir_buffer = xf->get_multi_buffer ( reservoir_buffer, -1 );
    xf_texture_h prev_object_id_texture = xf->get_multi_texture ( object_id_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "restir_di_temporal",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "restir_di_temporal" ) ),
            .samplers_count = 2,
            .samplers = { 
                xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ),
                xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ),
            },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 2,
            .storage_buffer_reads = {
                xf_shader_buffer_dependency_m ( .buffer = light_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = prev_reservoir_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .storage_buffer_writes_count = 1,
            .storage_buffer_writes = {
                xf_shader_buffer_dependency_m ( .buffer = reservoir_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .sampled_textures_count = 7,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = color_texture ), 
                xf_compute_texture_dependency_m ( .texture = normal_texture ), 
                xf_compute_texture_dependency_m ( .texture = material_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture ),
                xf_compute_texture_dependency_m ( .texture = object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = prev_object_id_texture ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_buffer_writes = {
                xf_buffer_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
            }
        )
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "restir_di_spatial",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "restir_di_spatial" ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 1,
            .storage_buffer_reads = {
                xf_shader_buffer_dependency_m ( .buffer = light_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .storage_buffer_writes_count = 1,
            .storage_buffer_writes = {
                xf_shader_buffer_dependency_m ( .buffer = reservoir_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .sampled_textures_count = 4,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = color_texture ), 
                xf_compute_texture_dependency_m ( .texture = normal_texture ), 
                xf_compute_texture_dependency_m ( .texture = material_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_buffer_writes = {
                xf_buffer_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
            }
        )
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "restir_di_lighting",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "restir_di_lighting" ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 1,
            .storage_buffer_reads = {
                xf_shader_buffer_dependency_m ( .buffer = light_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .storage_buffer_writes_count = 1,
            .storage_buffer_writes = {
                xf_shader_buffer_dependency_m ( .buffer = reservoir_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .sampled_textures_count = 5,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = color_texture ), 
                xf_compute_texture_dependency_m ( .texture = normal_texture ), 
                xf_compute_texture_dependency_m ( .texture = material_texture ), 
                xf_compute_texture_dependency_m ( .texture = radiosity_texture ),
                xf_compute_texture_dependency_m ( .texture = depth_texture ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { 
                xf_shader_texture_dependency_m ( .texture = lighting_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { 
                xf_texture_passthrough_m ( 
                    .mode = xf_passthrough_mode_clear_m, 
                ) 
            }
        )
    ) );

    // taa
    xf_texture_h taa_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "taa_accumulation_texture",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );
    xg_texture_h taa_history_texture = xf->get_multi_texture ( taa_accumulation_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "taa",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "taa" ) ),
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = taa_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 5,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = lighting_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = taa_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture ) 
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { 
                xf_texture_passthrough_m ( 
                    .mode = xf_passthrough_mode_copy_m, 
                    .copy_source = xf_copy_texture_dependency_m ( .texture = lighting_texture ) 
                ) 
            }
        )
    ) );

    // tonemap
    xf_texture_h tonemap_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "tonemap_texture",
    ) );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tonemap",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "tonemap" ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = tonemap_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 1,
            .sampled_textures = { xf_compute_texture_dependency_m ( .texture = taa_accumulation_texture ) },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_copy_m,
                    .copy_source = xf_copy_texture_dependency_m ( .texture = taa_accumulation_texture ),
                )
            },
        ),
    ) );

    // ui
    state->render.export_dest = xf->create_texture_from_external ( state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, state->render.export_dest );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "present",
        .type = xf_node_type_copy_pass_m,
        .pass.copy = xf_node_copy_pass_params_m(),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
            .copy_texture_reads_count = 1,
            .copy_texture_reads = { xf_copy_texture_dependency_m ( .texture = tonemap_texture ) },
            .presentable_texture = swapchain_multi_texture,
        )
    ) );

    xf->finalize_graph ( graph );
#endif
}

static void viewapp_bind_raytrace_graph_routines ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_graph_h graph = state->render.render_graph;
    bind_raytrace_setup_routine ( graph );
    bind_ui_routine ( graph );
}

static void viewapp_boot_raytrace_graph ( void ) {
#if xg_enable_raytracing_m
    viewapp_state_t* state = viewapp_state_get();
    xg_device_h device = state->render.device;    
    xg_swapchain_h swapchain = state->render.swapchain;    
    uint32_t resolution_x = state->render.resolution_x;
    uint32_t resolution_y = state->render.resolution_y;
    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;
    xg_i* xg = state->modules.xg;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "raytrace"
    ) );
    state->render.render_graph = graph;

    // gbuffer laydown
    xf_texture_h color_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture",
    ) );

    xf_texture_h normal_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h material_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "material_texture"
    ) );

    xf_texture_h radiosity_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "radiosity_texture"
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );

    xf_texture_h velocity_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,
            .debug_name = "depth_texture",
            .clear_on_create = true,
            .clear.depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m )
        ),
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "geometry_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { 
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m ) ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m( .f32 = { 0.5, 0.5, 0, 0 } ) ),
            }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 7,
            .copy_texture_writes = {
                xf_copy_texture_dependency_m ( .texture = depth_texture ),
                xf_copy_texture_dependency_m ( .texture = color_texture ),
                xf_copy_texture_dependency_m ( .texture = normal_texture ),
                xf_copy_texture_dependency_m ( .texture = material_texture ),
                xf_copy_texture_dependency_m ( .texture = radiosity_texture ),
                xf_copy_texture_dependency_m ( .texture = object_id_texture ),
                xf_copy_texture_dependency_m ( .texture = velocity_texture ),
            }
        ),
    ) );

    gbuffer_textures_t gbuffer = {
        .color = color_texture,
        .normal = normal_texture,
        .material = material_texture,
        .radiosity = radiosity_texture,
        .object_id = object_id_texture,
        .velocity = velocity_texture,
    };
    add_geometry_node ( graph, &gbuffer, depth_texture );

    // raytrace
    xf_texture_h lighting_texture = xf->create_texture ( &xf_texture_params_m ( 
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "lighting_texture"
    ));

    xf_buffer_h instance_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = raytrace_instance_data_size(),
        .debug_name = "instance_buffer",
    ) );

    xf_buffer_h light_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = raytrace_light_data_size(),
        .debug_name = "light_buffer",
    ) );
    
    add_raytrace_setup_pass ( graph, instance_buffer, light_buffer );

    xf_texture_h reservoir_buffer = xf->create_multi_buffer ( &xf_multi_buffer_params_m (
        .buffer = xf_buffer_params_m (
            .size = 32 * resolution_x * resolution_y, // TODO
            .debug_name = "reservoir_buffer",
            .init = &xg_buffer_init_m (
                .mode = xg_buffer_init_mode_clear_m,
                .clear = 0,
            ),
        ),
        .multi_buffer_count = 2,
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "raytrace",
        .type = xf_node_type_raytrace_pass_m,
        .pass.raytrace = xf_node_raytrace_pass_params_m (
            .thread_count = { resolution_x, resolution_y, 1 },
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "raytrace" ) ),
            .raytrace_worlds_count = 1,
            .raytrace_worlds = { state->render.raytrace_world },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = {
                xf_shader_buffer_dependency_m ( .buffer = instance_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = light_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = reservoir_buffer, .stage = xg_pipeline_stage_bit_raytrace_shader_m ), 
            },
            .sampled_textures_count = 5,
            .sampled_textures = {
                xf_shader_texture_dependency_m ( .texture = color_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = normal_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = material_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = radiosity_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
                xf_shader_texture_dependency_m ( .texture = depth_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { 
                xf_shader_texture_dependency_m ( .texture = lighting_texture, .stage = xg_pipeline_stage_bit_raytrace_shader_m ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { 
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = color_texture ) ), 
            }
        ),
    ) );

    // taa
    xf_texture_h taa_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "taa_accumulation_texture",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );
    xg_texture_h taa_history_texture = xf->get_multi_texture ( taa_accumulation_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "taa",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "taa" ) ),
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = taa_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 5,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = lighting_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = taa_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture ) 
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { 
                xf_texture_passthrough_m ( 
                    .mode = xf_passthrough_mode_copy_m, 
                    .copy_source = xf_copy_texture_dependency_m ( .texture = lighting_texture ) 
                ) 
            }
        )
    ) );

    // tonemap
    xf_texture_h tonemap_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "tonemap_texture",
    ) );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tonemap",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "tonemap" ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = tonemap_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 1,
            .sampled_textures = { xf_compute_texture_dependency_m ( .texture = taa_accumulation_texture ) },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_copy_m,
                    .copy_source = xf_copy_texture_dependency_m ( .texture = taa_accumulation_texture ),
                )
            },
        ),
    ) );

    // ui
    state->render.export_dest = xf->create_texture_from_external ( state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, state->render.export_dest );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "present",
        .type = xf_node_type_copy_pass_m,
        .pass.copy = xf_node_copy_pass_params_m(),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
            .copy_texture_reads_count = 1,
            .copy_texture_reads = { xf_copy_texture_dependency_m ( .texture = tonemap_texture ) },
            .presentable_texture = swapchain_multi_texture,
        )
    ) );
#else
    state->render.render_graph = xf_null_handle_m;
#endif
}

static void viewapp_bind_raster_graph_routines ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_graph_h graph = state->render.render_graph;
    bind_geometry_routine ( graph );
    bind_shadow_routine ( graph );
    bind_hiz_mip0_gen_routine ( graph );
    bind_light_update_routine ( graph );
    bind_ui_routine ( graph );
}

static void viewapp_boot_raster_graph ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_device_h device = state->render.device;    
    xg_swapchain_h swapchain = state->render.swapchain;    
    uint32_t resolution_x = state->render.resolution_x;
    uint32_t resolution_y = state->render.resolution_y;
    xs_database_h sdb = state->render.sdb;
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "raster",
        .flags = xf_graph_flag_alias_memory_m | xf_graph_flag_alias_resources_m,// | xf_graph_flag_print_execution_order_m | xf_graph_flag_print_node_deps_m,
    ) );
    state->render.render_graph = graph;

    uint32_t shadow_size = 1024 * 8;
    xf_texture_h shadow_texture = xf->create_texture ( &xf_texture_params_m (
        .width = shadow_size,
        .height = shadow_size,
        .format = xg_format_d32_sfloat_m,
        .debug_name = "shadow_texture"
    ) );

    xf->create_node ( graph, &xf_node_params_m ( 
        .debug_name = "shadow_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = {
            .textures = { xf_texture_clear_m ( 
                .type = xf_texture_clear_depth_stencil_m,
                .depth_stencil = xg_depth_stencil_clear_m()
            ) }
        },
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = shadow_texture ) }
        )
    ) );

    // shadows
    add_shadow_pass ( graph, shadow_texture );

    // gbuffer laydown
    xf_texture_h color_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture",
    ) );

    xf_texture_h normal_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h material_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "material_texture"
    ) );

    xf_texture_h radiosity_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "radiosity_texture"
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );
    xf_texture_h prev_object_id_texture = xf->get_multi_texture ( object_id_texture, -1 );

    xf_texture_h velocity_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,//xg_format_d24_unorm_s8_uint_m,
            .debug_name = "depth_texture",
            .clear_on_create = true,
            .clear.depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m )
        ),
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "geometry_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { 
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m ( .depth = viewapp_main_view_depth_clear_m ) ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m( .f32 = { 0.5, 0.5, 0, 0 } ) ),
            }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 7,
            .copy_texture_writes = {
                xf_copy_texture_dependency_m ( .texture = depth_texture ),
                xf_copy_texture_dependency_m ( .texture = color_texture ),
                xf_copy_texture_dependency_m ( .texture = normal_texture ),
                xf_copy_texture_dependency_m ( .texture = material_texture ),
                xf_copy_texture_dependency_m ( .texture = radiosity_texture ),
                xf_copy_texture_dependency_m ( .texture = object_id_texture ),
                xf_copy_texture_dependency_m ( .texture = velocity_texture ),
            }
        ),
    ) );

    gbuffer_textures_t gbuffer = {
        .color = color_texture,
        .normal = normal_texture,
        .material = material_texture,
        .radiosity = radiosity_texture,
        .object_id = object_id_texture,
        .velocity = velocity_texture,
    };
    add_geometry_node ( graph, &gbuffer, depth_texture );

    // tessellation
    // TODO remove the setup pass, have a proper susystem that suballocates multiple meshes vertex/index/meta data into buffers, properly render on gbuffer, shadows, ...
    xg_workload_h tess_workload = xg->create_workload ( device );
    xf_buffer_h tess_vertex_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = 1024 * 1024 * 16,
        .debug_name = "tessellation_vbuffer"
    ) );
    xf_buffer_h tess_index_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = 1024 * 1024 * 32,
        .debug_name = "tessellation_ibuffer"
    ) );
    float tess_instance_vertex_data[6] = {
        0, 0,
        0, 1,
        1, 0,
    };
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( tess_workload );
    xg_buffer_h tess_instance_vertex_buffer = xg->cmd_create_buffer ( resource_cmd_buffer, &xg_buffer_params_m (
        .size = sizeof ( tess_instance_vertex_data ),
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device,
        .allowed_usage = xg_buffer_usage_bit_copy_dest_m 
            | xg_buffer_usage_bit_vertex_buffer_m,
        .debug_name = "tess_instance_vertex_buffer",
    ), &xg_buffer_init_m (
        .mode = xg_buffer_init_mode_upload_m,
        .upload_data = tess_instance_vertex_data,
    ) );

    xg->submit_workload ( tess_workload );
    xg->wait_all_workload_complete();
    
    uint64_t tess_subdivision_buffer_size = sizeof ( uint32_t ) * 1024 * 1024 * 32;
    xf_buffer_h tess_subdivision_buffer = xf->create_multi_buffer ( &xf_multi_buffer_params_m (
        .buffer = xf_buffer_params_m (
            .size = tess_subdivision_buffer_size,
            .debug_name = "tessellation_subdivision_buffer",
            .init = &xg_buffer_init_m (
                .mode = xg_buffer_init_mode_clear_m,
                .clear = 0,
            ),
        ),
    ) );
    xf_buffer_h tess_prev_subdivision_buffer = xf->get_multi_buffer ( tess_subdivision_buffer, -1 );

    xf_buffer_h tess_culled_subdivision_buffer = xf->create_buffer ( &xf_buffer_params_m ( 
        .size = tess_subdivision_buffer_size,
        .debug_name = "tessellation_culled_buffer",
        .init = &xg_buffer_init_m (
            .mode = xg_buffer_init_mode_clear_m,
            .clear = 0,
        ),
    ) );

    xf_buffer_h tess_update_indirect_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( xg_compute_indirect_gpu_args_t ) + 4,
        .debug_name = "tessellation_indirect_update",
    ) );

    add_tessellation_setup_pass ( graph, tess_vertex_buffer, tess_index_buffer, tess_update_indirect_buffer, tess_prev_subdivision_buffer, tess_culled_subdivision_buffer );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tessellation_update",
        .type = xf_node_type_compute_indirect_pass_m,
        .pass.compute_indirect = xf_node_compute_indirect_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "tessellation_update" ) ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = {
                xf_compute_buffer_dependency_m ( .buffer = tess_vertex_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = tess_index_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = tess_prev_subdivision_buffer ),
            },
            .storage_buffer_writes_count = 2,
            .storage_buffer_writes = {
                xf_compute_buffer_dependency_m ( .buffer = tess_subdivision_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = tess_culled_subdivision_buffer ),
            },
            .indirect_command_read = tess_update_indirect_buffer,
        ),
    ) );

    xf_buffer_h tess_draw_indirect_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( xg_draw_indirect_gpu_args_t ),
        .debug_name = "tessellation_indirect_draw"
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tessellation_prepare",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "tessellation_prepare" ) ),
            .workgroup_count = { 1, 1, 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 2,
            .storage_buffer_reads = {
                xf_compute_buffer_dependency_m ( .buffer = tess_culled_subdivision_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = tess_subdivision_buffer ),
            },
            .storage_buffer_writes_count = 3,
            .storage_buffer_writes = {
                xf_compute_buffer_dependency_m ( .buffer = tess_prev_subdivision_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = tess_draw_indirect_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = tess_update_indirect_buffer ),
            },
        ),
    ) );

    add_tessellation_draw_pass ( graph, tess_vertex_buffer, tess_index_buffer, tess_culled_subdivision_buffer, tess_draw_indirect_buffer, tess_instance_vertex_buffer, &gbuffer, depth_texture, sdb );

    // lighting
    xf_texture_h lighting_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "lighting_texture",
    ) );

    // see lighting_common.glsl for data types

    // the frustum is split in 16 x 8 x 24 partitions (clusters)
    // cluster buffer contains one enclosing view-space aabb per cluster each defined as <min,max> (light_cluster_t)
    // filled by light_cluster_build
    uint32_t light_grid_size[3] = { 16, 8, 24 };
    uint32_t light_cluster_count = light_grid_size[0] * light_grid_size[1] * light_grid_size[2];
    xf_buffer_h light_cluster_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( float ) * 4 * 2 * light_cluster_count,
        .debug_name = "light_clusters"
    ) );

    // light buffer contains the lights and their data (light_t)
    // filled by the light update pass
    xf_buffer_h light_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = light_data_size(),
        .debug_name = "light_data",
    ) );

    // light list buffer contains lists of relevant lights (u32 indices to light data), one per cluster but stored out of order
    // light grid buffer contains for each cluster an <offset,count> pair (light_grid_t) that identifies the cluster's list of lights inside the light list buffer
    // both are filled by light_cull
    xf_buffer_h light_list_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( uint32_t ) * light_cluster_count * viewapp_max_lights_m,
        .debug_name = "light_list",
    ) );
    xf_buffer_h light_grid_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( uint32_t ) * 2 * light_cluster_count,
        .debug_name = "light_grid"
    ) );

    xf_node_h light_update_node = add_light_update_pass ( graph, light_buffer );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "light_cluster_build",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "light_cluster_build" ) ),
            .workgroup_count = { std_div_round_up_u32 ( light_cluster_count, 64 ), 1, 1 },
            .uniform_data = std_buffer_static_array_m ( light_grid_size ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_writes_count = 1,
            .storage_buffer_writes = {
                xf_compute_buffer_dependency_m ( .buffer = light_cluster_buffer )
            }
        ),
        .node_dependencies_count = 1,
        .node_dependencies = { light_update_node }
    ) );

    xf->create_node ( graph, &xf_node_params_m ( 
        .debug_name = "light_cull",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "light_cull" ) ),
            .workgroup_count = { std_div_round_up_u32 ( light_cluster_count, 64 ), 1, 1 },
            .uniform_data = std_buffer_static_array_m ( light_grid_size ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 2,
            .storage_buffer_reads = {
                xf_compute_buffer_dependency_m ( .buffer = light_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = light_cluster_buffer ),
            },
            .storage_buffer_writes_count = 2,
            .storage_buffer_writes =  {
                xf_compute_buffer_dependency_m ( .buffer = light_list_buffer ),
                xf_compute_buffer_dependency_m ( .buffer = light_grid_buffer ),
            }
        )
    ) );

    struct {
        uint32_t grid_size[3];
        float z_scale;
        float z_bias;
        uint32_t shadow_size;
    } lighting_uniforms = {
        .grid_size[0] = light_grid_size[0],
        .grid_size[1] = light_grid_size[1],
        .grid_size[2] = light_grid_size[2],
        .z_scale = 0, // TODO
        .z_bias = 0,
        .shadow_size = shadow_size,
    };

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "lighting",
        .type = xf_node_type_compute_pass_m,
        //.queue = xg_cmd_queue_compute_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "lighting" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .uniform_data = std_buffer_struct_m ( &lighting_uniforms ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = { 
                xf_compute_buffer_dependency_m ( .buffer = light_buffer ), 
                xf_compute_buffer_dependency_m ( .buffer = light_list_buffer ), 
                xf_compute_buffer_dependency_m ( .buffer = light_grid_buffer ) 
            },
            .sampled_textures_count = 5,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = material_texture ),
                //xf_compute_texture_dependency_m ( .texture = radiosity_texture ),
                xf_compute_texture_dependency_m ( .texture = depth_texture ),
                xf_compute_texture_dependency_m ( .texture = shadow_texture ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_compute_texture_dependency_m ( .texture = lighting_texture )
            }
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = color_texture ) ) },
        ),
    ) );

    // hi-z
    uint32_t hiz_mip_count = 8;
    std_assert_m ( resolution_x % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    std_assert_m ( resolution_y % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    xf_texture_h hiz_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r32_sfloat_m,
        .mip_levels = hiz_mip_count,
        .view_access = xg_texture_view_access_separate_mips_m,
        .debug_name = "hiz_texture",
    ) );

    add_hiz_mip0_gen_pass ( graph, hiz_texture, depth_texture );

    for ( uint32_t i = 1; i < hiz_mip_count; ++i ) {
        add_hiz_submip_gen_pass ( graph, hiz_texture, i );
    }

    // downsample lighting result
    uint32_t lighting_mip_count = 8;
    std_assert_m ( resolution_x % ( 1 << ( lighting_mip_count - 1 ) ) == 0 );
    std_assert_m ( resolution_y % ( 1 << ( lighting_mip_count - 1 ) ) == 0 );
    xf_texture_h downsampled_lighting_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / 2,
        .height = resolution_y / 2,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "downsampled_lighting_texture",
        .mip_levels = lighting_mip_count,
        .view_access = xg_texture_view_access_separate_mips_m,
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "lighting_mip_0",
        .type = xf_node_type_copy_pass_m,
        .pass.copy = xf_node_copy_pass_params_m (
            .filter = xg_sampler_filter_linear_m,
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { 
                xf_copy_texture_dependency_m ( .texture = downsampled_lighting_texture, .view = xg_texture_view_m ( .mip_base = 0, .mip_count = 1 ) ),
            },
            .copy_texture_reads_count = 1,
            .copy_texture_reads = { 
                xf_copy_texture_dependency_m ( .texture = lighting_texture ),
            },
        ),
    ) );

    for ( uint32_t i = 1; i < lighting_mip_count; ++i ) {
        xf_node_params_t params = xf_node_params_m (
            .type = xf_node_type_copy_pass_m,
            .pass.copy = xf_node_copy_pass_params_m (
                .filter = xg_sampler_filter_linear_m,
            ),
            .resources = xf_node_resource_params_m (
                .copy_texture_writes_count = 1,
                .copy_texture_writes = { 
                    xf_copy_texture_dependency_m ( .texture = downsampled_lighting_texture, .view = xg_texture_view_m ( .mip_base = i, .mip_count = 1 ) ),
                },
                .copy_texture_reads_count = 1,
                .copy_texture_reads = { 
                    xf_copy_texture_dependency_m ( .texture = downsampled_lighting_texture, .view = xg_texture_view_m ( .mip_base = i-1, .mip_count = 1 ) ),
                },
            ),
        );
        std_string_t string = std_static_string_m ( params.debug_name );
        std_string_append_format ( &string, "lighting_mip_" std_fmt_u32_m, i );
        xf->create_node ( graph, &params );
    }

    // ssgi
    // TODO use prev frame final color texture?
    //xf_texture_h prev_color_texture = color_texture;//xf->get_multi_texture ( color_texture, -1 );
    //xf_texture_h ssgi_raymarch_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
    //    .texture = xf_texture_params_m (
    //        .width = resolution_x,
    //        .height = resolution_y,
    //        .format = xg_format_b10g11r11_ufloat_pack32_m,
    //        .debug_name = "ssgi_raymarch_texture",
    //    ),
    //) );
    uint32_t ssgi_scale = 2;
    xf_texture_h ssgi_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / ssgi_scale,
        .height = resolution_y / ssgi_scale,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        //.format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "ssgi_raymarch_texture",
    ) );
    add_ssgi_raymarch_pass ( graph, "ssgi", ssgi_raymarch_texture, normal_texture, color_texture, downsampled_lighting_texture, hiz_texture );

    // ssgi blur
#if 0
    xf_texture_h ssgi_blur_x_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / ssgi_scale,
        .height = resolution_y / ssgi_scale,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_x_texture",
    ) );
    add_bilateral_blur_pass ( graph, ssgi_blur_x_texture, ssgi_raymarch_texture, normal_texture, depth_texture, 5, 3, blur_pass_direction_horizontal_m, "ssgi_blur_x" );

    xf_texture_h ssgi_blur_y_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / ssgi_scale,
        .height = resolution_y / ssgi_scale,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_y_texture",
    ) );
    add_bilateral_blur_pass ( graph, ssgi_blur_y_texture, ssgi_blur_x_texture, normal_texture, depth_texture, 5, 3, blur_pass_direction_vertical_m, "ssgi_blur_y" );
#endif

    // ssgi temporal accumulation
    xf_texture_h ssgi_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x / ssgi_scale,
            .height = resolution_y / ssgi_scale,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            //.format = xg_format_r8g8b8a8_unorm_m,
            .debug_name = "ssgi_accumulation_texture",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m(),
        ),
    ) );
    xf_texture_h prev_depth_texture = xf->get_multi_texture ( depth_texture, -1 );
    xg_texture_h ssgi_history_texture = xf->get_multi_texture ( ssgi_accumulation_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssgi_ta",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssgi_ta" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x / ssgi_scale, 8 ), std_div_round_up_u32 ( resolution_y / ssgi_scale, 8 ), 1 },
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssgi_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 7,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssgi_raymarch_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = prev_object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture )
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = ssgi_raymarch_texture ) ) }
        )
    ) );

#if 0
    // ssgi2
    xf_texture_h ssgi_2_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_raymarch_texture",
    ) );
    add_ssgi_raymarch_pass ( graph, "ssgi_2", ssgi_2_raymarch_texture, normal_texture, color_texture, ssgi_raymarch_texture, hiz_texture );

    // ssgi2 blur
    xf_texture_h ssgi_2_blur_x_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_x_texture",
    ) );
    add_bilateral_blur_pass ( graph, ssgi_2_blur_x_texture, ssgi_2_raymarch_texture, normal_texture, depth_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_2_blur_x" );

    xf_texture_h ssgi_2_blur_y_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_y_texture",
    ) );
    add_bilateral_blur_pass ( graph, ssgi_2_blur_y_texture, ssgi_2_blur_x_texture, normal_texture, depth_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_2_blur_y" );

    // ssgi2 temporal accumulation
    xf_texture_h ssgi_2_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_2_accumulation_texture",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m(),
        ),
    ) );
    xg_texture_h ssgi_2_history_texture = xf->get_multi_texture ( ssgi_2_accumulation_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssgi_2_ta",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssgi_ta" ) ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssgi_2_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 5,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssgi_2_blur_y_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_2_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = hiz_texture ), 
                xf_compute_texture_dependency_m ( .texture = velocity_texture )
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = ssgi_2_blur_y_texture ) ) }
        ),
    ) );
#endif

    // ssr
    uint32_t ssr_scale = 1;
    xf_texture_h ssr_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / ssr_scale,
        .height = resolution_y / ssr_scale,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_raymarch_texture",
    ) );
    xf_texture_h ssr_intersection_distance = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / ssr_scale,
        .height = resolution_y / ssr_scale,
        .format = xg_format_r16_sfloat_m,
        .debug_name = "ssr_intersect_distance",
    ) );
    add_ssr_raymarch_pass ( graph, ssr_raymarch_texture, ssr_intersection_distance, normal_texture, material_texture, lighting_texture, hiz_texture );

    // ssr blur
#if 0
    xf_texture_h ssr_blur_x_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_x_texture",
    ) );
    add_bilateral_blur_pass ( graph, ssr_blur_x_texture, ssr_raymarch_texture, normal_texture, depth_texture, 11, 3, blur_pass_direction_horizontal_m, "ssr_blur_x" );

    xf_texture_h ssr_blur_y_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format  = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_y_texture",
    ) );
    add_bilateral_blur_pass ( graph, ssr_blur_y_texture, ssr_blur_x_texture, normal_texture, depth_texture, 11, 3, blur_pass_direction_vertical_m, "ssr_blur_y" );
#endif

    // ssr ta
    xf_texture_h ssr_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x / ssr_scale,
            .height = resolution_y / ssr_scale,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssr_accumulation_texture",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m(),
        ),
    ) );
    xf_texture_h ssr_history_texture = xf->get_multi_texture ( ssr_accumulation_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssr_ta",
        //.queue = xg_cmd_queue_compute_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssr_ta" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x / ssr_scale, 8 ), std_div_round_up_u32 ( resolution_y / ssr_scale, 8 ), 1 },
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssr_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 9,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssr_raymarch_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssr_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = normal_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture ),
                xf_compute_texture_dependency_m ( .texture = ssr_intersection_distance ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = ssr_raymarch_texture ) ) }
        )
    ) );

    // combine
    xf_texture_h combine_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "combine_texture",
    ) );
    xf->create_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "combine",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "combine" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = combine_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 4,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = lighting_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssr_accumulation_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_accumulation_texture ), 
                xf_compute_texture_dependency_m ( .texture = radiosity_texture ), // temp hack to avoid having emissive objects blow up ssgi
                //xf_compute_texture_dependency_m ( .texture = ssgi_2_accumulation_texture ) 
            },
        ),
    ) );

    // taa
    xf_texture_h taa_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "taa_accumulation_texture",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );
    xg_texture_h taa_history_texture = xf->get_multi_texture ( taa_accumulation_texture, -1 );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "taa",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "taa" ) ),
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = taa_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 5,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = combine_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = taa_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture ) 
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { 
                xf_texture_passthrough_m ( 
                    .mode = xf_passthrough_mode_copy_m, 
                    .copy_source = xf_copy_texture_dependency_m ( .texture = combine_texture ) 
                ) 
            }
        )
    ) );

    // tonemap
    xf_texture_h tonemap_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "tonemap_texture",
    ) );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tonemap",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "tonemap" ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = tonemap_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 1,
            .sampled_textures = { xf_compute_texture_dependency_m ( .texture = taa_accumulation_texture ) },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_copy_m,
                    .copy_source = xf_copy_texture_dependency_m ( .texture = taa_accumulation_texture ),
                )
            },
        ),
    ) );

    // ui
    state->render.export_dest = xf->create_texture_from_external ( state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, state->render.export_dest );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "present",
        .type = xf_node_type_copy_pass_m,
        .pass.copy = xf_node_copy_pass_params_m(),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
            .copy_texture_reads_count = 1,
            .copy_texture_reads = { xf_copy_texture_dependency_m ( .texture = tonemap_texture ) },
            .presentable_texture = swapchain_multi_texture,
        ),
    ) );

    xf->finalize_graph ( graph );
}

void viewapp_load_render_graph ( viewapp_render_graph_e graph, xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;
    
    if ( state->render.render_graph != xf_null_handle_m ) {
        xf->destroy_graph ( state->render.render_graph, workload );
    }

    state->render.active_render_graph = graph;

    if ( viewapp_render_graph_is_raytrace ( graph ) ) {
        viewapp_build_raytrace_world();
    }

    switch ( graph ) {
    case viewapp_render_graph_raster_m:
        viewapp_boot_raster_graph();
        break;
    case viewapp_render_graph_restir_di_m:
        viewapp_boot_restir_di_graph();
        break;
    case viewapp_render_graph_raytrace_m:
        viewapp_boot_raytrace_graph();
        break;
    default:
        std_assert_m ( false );
    }

    state->render.load_render_graph = false;

    //if ( viewapp_render_graph_is_raytrace ( graph ) ) {
    //    state->render.raytrace_world_update = true;
    //}
}

void viewapp_reload_graphs ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    viewapp_render_graph_e graph = state->render.active_render_graph;

    switch ( graph ) {
    case viewapp_render_graph_raster_m:
        viewapp_bind_raster_graph_routines();
        break;
    case viewapp_render_graph_restir_di_m:
        viewapp_bind_restir_di_graph_routines();
        break;
    case viewapp_render_graph_raytrace_m:
        viewapp_bind_raytrace_graph_routines();
        break;
    default:
        std_assert_m ( false );
    }

    viewapp_bind_mouse_pick_graph_routines();
}

bool viewapp_render_graph_is_raytrace ( viewapp_render_graph_e render_graph ) {
    switch ( render_graph ) {
    case viewapp_render_graph_raster_m:
        return false;
    case viewapp_render_graph_restir_di_m:
    case viewapp_render_graph_raytrace_m:
        return true;
    default:
        std_assert_m ( false );
    }

    return false;
}

xs_database_pipeline_h viewapp_get_render_graph_raytrace_pipeline ( viewapp_render_graph_e render_graph ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;

    switch ( render_graph ) {
    case viewapp_render_graph_raster_m:
        return xf_null_handle_m;
    case viewapp_render_graph_restir_di_m:
        return xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "restir_di_sample" ) );
    case viewapp_render_graph_raytrace_m:
        return xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "raytrace" ) );
    default:
        std_assert_m ( false );
    }

    return false;
}
