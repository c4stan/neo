#include "viewapp_render.h"

#include <geometry_pass.h>
#include <sky_pass.h>
#include <lighting_pass.h>
#include <hiz_pass.h>
#include <blur_pass.h>
#include <ui_pass.h>
#include <shadow_pass.h>
#include <raytrace_pass.h>
#include <tessellation_pass.h>

#include "viewapp_state.h"

#include <std_file.h>

void viewapp_gen_brdf_lut ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    xg_texture_h lut_texture = xg->create_texture ( &xg_texture_params_m (
        .device = state->render.device,
        .width = 512,
        .height = 512,
        .format = xg_format_r16g16_sfloat_m,
        .allowed_usage = xg_texture_usage_bit_sampled_m | xg_texture_usage_bit_storage_m,
        .debug_name = "brdf_lut",
    ) );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xg_texture_memory_barrier_t pre_barrier = xg_texture_memory_barrier_m (
        .texture = lut_texture,
        .execution = xg_execution_dependency_m (
            .blocker = xg_pipeline_stage_bit_top_of_pipe_m,
            .blocked = xg_pipeline_stage_bit_compute_shader_m,
        ),
        .memory = xg_memory_dependency_m (
            .flushes = xg_memory_access_bit_none_m,
            .invalidations = xg_memory_access_bit_shader_write_m,
        ),
        .layout = xg_layout_dependency_m (
            .old = xg_texture_layout_undefined_m,
            .new = xg_texture_layout_shader_write_m,
        ),
    );

    xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
        .texture_memory_barriers_count = 1,
        .texture_memory_barriers = &pre_barrier
    ) );

    xs_database_pipeline_h db_pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "ibl_split_sum_brdf_lut" ) );
    xg_compute_pipeline_state_h pipeline = xs->get_pipeline_state ( db_pipeline );

    xg_resource_bindings_layout_h bindings_layouts[xg_shader_binding_set_count_m];
    xg->get_pipeline_resource_layouts ( bindings_layouts, pipeline );

    struct {
        uint32_t sample_count;
    } uniform_data = {
        .sample_count = 1024,
    };

    xg->cmd_compute ( cmd_buffer, 0, &xg_cmd_compute_params_m (
        .pipeline = pipeline,
        .bindings[xg_shader_binding_set_dispatch_m] = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = bindings_layouts[xg_shader_binding_set_dispatch_m],
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 1,
                .buffers = {
                    xg_buffer_resource_binding_m (
                        .shader_register = 0,
                        .range = xg->write_workload_uniform ( workload, &uniform_data, sizeof ( uniform_data ) ),
                    ),
                },
                .texture_count = 1,
                .textures = {
                    xg_texture_resource_binding_m (
                        .texture = lut_texture,
                        .layout = xg_texture_layout_shader_write_m,
                        .shader_register = 1,
                    )
                }
            ),
        ) ),
        .workgroup_count_x = std_div_round_up_u32 ( 512, 8 ),
        .workgroup_count_y = std_div_round_up_u32 ( 512, 8 ),
        .workgroup_count_z = 1,
    ) );

    state->render.ibl_lut_texture = lut_texture;
}

void viewapp_unload_render ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;

    viewapp_destroy_render_graph ( workload, resource_cmd_buffer );
    viewapp_destroy_mouse_pick_graph ( workload, resource_cmd_buffer );
    viewapp_destroy_ibl_cubemap_gen_graph ( workload, resource_cmd_buffer );

    if ( state->render.ibl_cubemap_texture != xg_null_handle_m ) {
        xg->cmd_destroy_texture ( resource_cmd_buffer, state->render.ibl_cubemap_texture, xg_resource_cmd_buffer_time_workload_complete_m );
        state->render.ibl_cubemap_texture = xg_null_handle_m;
    }

    xf->destroy_texture ( state->render.ibl_cubemap );

    xg->cmd_destroy_texture ( resource_cmd_buffer, state->render.ibl_lut_texture, xg_resource_cmd_buffer_time_workload_complete_m );

    xg->destroy_swapchain ( state->render.swapchain );
    xg->destroy_resource_layout ( state->render.workload_bindings_layout );
}

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

    state->render.ibl_cubemap_resolution_x = 512;
    state->render.ibl_cubemap_resolution_y = 512;

    state->render.ibl_cubemap = xf->create_texture_from_external ( xf_null_handle_m, xg->get_default_texture ( state->render.device, xg_default_texture_r16g16b16a16_float_cube_black_m ) );

    xg_workload_h workload = xg->create_workload ( device );
    viewapp_update_workload_uniforms ( workload );
    viewapp_gen_brdf_lut ( workload );
    xg->submit_workload ( workload );
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
    if ( state->render.active_render_graph != viewapp_render_graph_raster_m ) {
        bind_object_id_routine ( graph );
    }
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

    xf_texture_h object_id_texture;
    xg_format_e object_id_texture_format;
    if ( state->render.active_render_graph != viewapp_render_graph_raster_m ) {
        object_id_texture_format = xg_format_r16_uint_m;
        object_id_texture = xf->create_texture ( graph, &xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = object_id_texture_format,
            .debug_name = "mouse_pick_object_id"
        ) );

        xf_texture_h depth_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    } else {
        object_id_texture_format = xg_format_r8g8b8a8_uint_m;
        object_id_texture = state->render.object_id_texture;
    }

    xg_texture_h readback_texture = xg->create_texture ( &xg_texture_params_m (
        .memory_type = xg_memory_type_readback_m,
        .device = device,
        .width = resolution_x,
        .height = resolution_y,
        .format = object_id_texture_format,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m,
        .tiling = xg_texture_tiling_linear_m,
        .debug_name = "object_id_readback",
    ) );
    state->render.object_id_readback_texture = readback_texture;

    xf_texture_h copy_dest = xf->create_texture_from_external ( graph, readback_texture );

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
}

void viewapp_load_ibl_cubemap_gen_graph ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_device_h device = state->render.device;
    uint32_t resolution_x = state->render.ibl_cubemap_resolution_x;
    uint32_t resolution_y = state->render.ibl_cubemap_resolution_y;

    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;
    xg_i* xg = state->modules.xg;
    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "ibl_cubemap_gen_graph",
    ) );
    state->render.ibl_cubemap_gen_graph = graph;

    xf_texture_h cubemap_texture = state->render.ibl_cubemap;

    add_sky_cubemap_gen_node ( graph, cubemap_texture, 0 );
    add_sky_cubemap_gen_node ( graph, cubemap_texture, 1 );
    add_sky_cubemap_gen_node ( graph, cubemap_texture, 2 );
    add_sky_cubemap_gen_node ( graph, cubemap_texture, 3 );
    add_sky_cubemap_gen_node ( graph, cubemap_texture, 4 );
    add_sky_cubemap_gen_node ( graph, cubemap_texture, 5 );

    uint32_t mip_levels = 10; // TODO
    for ( uint32_t face_it = 0; face_it < 6; ++face_it ) {        
        for ( uint32_t mip_it = 1; mip_it < mip_levels; ++mip_it ) {
            struct {
                uint32_t face;
                uint32_t sample_count;
                float roughness;
                uint32_t _pad0;
            } uniform_data = {
                .face = face_it,
                .sample_count = 1024,
                .roughness = ( float ) mip_it / mip_levels,
            };
            xf_node_params_t params = xf_node_params_m (
                .type = xf_node_type_compute_pass_m,
                .pass.compute = xf_node_compute_pass_params_m (
                    .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "ibl_split_sum_prefilter" ) ),
                    .samplers_count = 1,
                    .samplers = {
                        xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ),
                    },
                    .workgroup_count = { std_div_round_up_u32 ( resolution_x >> mip_it, 8 ), std_div_round_up_u32 ( resolution_y >> mip_it, 8 ), 1 },
                    .uniform_data = std_buffer_struct_m ( &uniform_data ),
                ),
                .resources = xf_node_resource_params_m (
                    .sampled_textures_count = 1,
                    .sampled_textures = { 
                        xf_compute_texture_dependency_m ( .texture = cubemap_texture, .view = xg_texture_view_m (
                            .mip_base = 0,
                            .mip_count = 1,
                            .array_base = 0,
                            .array_count = 6,
                            .cube = 1,
                        ) ),
                    },
                    .storage_texture_writes_count = 1,
                    .storage_texture_writes = { 
                        xf_compute_texture_dependency_m ( .texture = cubemap_texture, .view = xg_texture_view_m ( 
                            .mip_base = mip_it, 
                            .mip_count = 1,
                            .array_base = face_it,
                            .array_count = 1,
                        ) ),
                    },
                ),
            );
            std_string_t string = std_static_string_m ( params.debug_name );
            std_string_append_format ( &string, "cube_prefilter_" std_fmt_u32_m "_" std_fmt_u32_m, face_it, mip_it );
            xf->create_node ( graph, &params );
        }
    }
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
    xf_texture_h color_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture",
    ) );

    xf_texture_h normal_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h material_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "material_texture"
    ) );

    xf_texture_h radiosity_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "radiosity_texture"
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );

    xf_texture_h velocity_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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
    xf_texture_h lighting_texture = xf->create_texture ( graph, &xf_texture_params_m ( 
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "lighting_texture"
    ));

    xf_buffer_h instance_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = raytrace_instance_data_size(),
        .debug_name = "instance_buffer",
    ) );

    xf_buffer_h light_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = raytrace_light_data_size(),
        .debug_name = "light_buffer",
    ) );
    
    add_raytrace_setup_pass ( graph, instance_buffer, light_buffer );

    xf_texture_h reservoir_buffer = xf->create_multi_buffer ( graph, &xf_multi_buffer_params_m (
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
    xf_texture_h taa_accumulation_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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
    xf_texture_h tonemap_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    state->render.export_dest = xf->create_texture_from_external ( graph, state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, state->render.export_dest );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( graph, swapchain );
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
    xf_texture_h color_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture",
    ) );

    xf_texture_h normal_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h material_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "material_texture"
    ) );

    xf_texture_h radiosity_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "radiosity_texture"
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
    ) );

    xf_texture_h velocity_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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
    xf_texture_h lighting_texture = xf->create_texture ( graph, &xf_texture_params_m ( 
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "lighting_texture"
    ));

    xf_buffer_h instance_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = raytrace_instance_data_size(),
        .debug_name = "instance_buffer",
    ) );

    xf_buffer_h light_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = raytrace_light_data_size(),
        .debug_name = "light_buffer",
    ) );
    
    add_raytrace_setup_pass ( graph, instance_buffer, light_buffer );

    xf_texture_h reservoir_buffer = xf->create_multi_buffer ( graph, &xf_multi_buffer_params_m (
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
    xf_texture_h taa_accumulation_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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
    xf_texture_h tonemap_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    state->render.export_dest = xf->create_texture_from_external ( graph, state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, state->render.export_dest );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( graph, swapchain );
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

typedef struct {
    uint32_t grid_size[3];
    float z_scale;
    float z_bias;
    uint32_t shadow_size;
    uint32_t _pad0[2];
    float sky_irradiance_sh[9][4];
} viewapp_raster_lighting_uniform_data_t;

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
    xf_texture_h shadow_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    xf_texture_h color_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture",
    ) );

    xf_texture_h normal_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h material_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "material_texture"
    ) );

    xf_texture_h radiosity_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "radiosity_texture"
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
            .clear_on_create = true,
            .clear.color = xg_color_clear_m(),
            .usage = xg_texture_usage_bit_copy_source_m,
        ),
    ) );
    xf_texture_h prev_object_id_texture = xf->get_multi_texture ( object_id_texture, -1 );
    state->render.object_id_texture = object_id_texture;

    xf_texture_h velocity_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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

    add_sky_node ( graph, &gbuffer, depth_texture );

    // tessellation
    // TODO remove the setup pass, have a proper subsystem that suballocates multiple meshes vertex/index/meta data into buffers, shadows, ...
    xg_workload_h tess_workload = xg->create_workload ( device );
    xf_buffer_h tess_vertex_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = 1024 * 1024 * 16,
        .debug_name = "tessellation_vbuffer"
    ) );
    xf_buffer_h tess_index_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = 1024 * 1024 * 32,
        .debug_name = "tessellation_ibuffer"
    ) );
    float tess_instance_vertex_data[6] = {
        0, 0,   // bottom left
        1, 0,   // bottom right
        0, 1,   // top left
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
    state->render.tessellation_instance_vertex_buffer = tess_instance_vertex_buffer;

    xg->submit_workload ( tess_workload );
    xg->wait_all_workload_complete();
    
    uint64_t tess_subdivision_buffer_size = sizeof ( uint32_t ) * 1024 * 1024 * 32;
    xf_buffer_h tess_subdivision_buffer = xf->create_multi_buffer ( graph, &xf_multi_buffer_params_m (
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

    xf_buffer_h tess_culled_subdivision_buffer = xf->create_buffer ( graph, &xf_buffer_params_m ( 
        .size = tess_subdivision_buffer_size,
        .debug_name = "tessellation_culled_buffer",
        .init = &xg_buffer_init_m (
            .mode = xg_buffer_init_mode_clear_m,
            .clear = 0,
        ),
    ) );

    xf_buffer_h tess_update_indirect_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
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

    xf_buffer_h tess_draw_indirect_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
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
    xf_texture_h lighting_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    xf_buffer_h light_cluster_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = sizeof ( float ) * 4 * 2 * light_cluster_count,
        .debug_name = "light_clusters"
    ) );

    // light buffer contains the lights and their data (light_t)
    // filled by the light update pass
    xf_buffer_h light_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = light_data_size(),
        .debug_name = "light_data",
    ) );

    // light list buffer contains lists of relevant lights (u32 indices to light data), one per cluster but stored out of order
    // light grid buffer contains for each cluster an <offset,count> pair (light_grid_t) that identifies the cluster's list of lights inside the light list buffer
    // both are filled by light_cull
    xf_buffer_h light_list_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
        .size = sizeof ( uint32_t ) * light_cluster_count * viewapp_max_lights_m,
        .debug_name = "light_list",
    ) );
    xf_buffer_h light_grid_buffer = xf->create_buffer ( graph, &xf_buffer_params_m (
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

    viewapp_raster_lighting_uniform_data_t lighting_uniforms = {
        .grid_size[0] = light_grid_size[0],
        .grid_size[1] = light_grid_size[1],
        .grid_size[2] = light_grid_size[2],
        .z_scale = 0, // TODO
        .z_bias = 0,
        .shadow_size = shadow_size,
    };

    xf_texture_h lut_texture = xf->create_texture_from_external ( graph, state->render.ibl_lut_texture );

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
            .sampled_textures_count = 7,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = material_texture ),
                xf_compute_texture_dependency_m ( .texture = depth_texture ),
                xf_compute_texture_dependency_m ( .texture = shadow_texture ),
                xf_compute_texture_dependency_m ( 
                    .texture = state->render.ibl_cubemap,
                    .view = xg_texture_view_m ( .cube = 1 ) ),
                xf_compute_texture_dependency_m ( .texture = lut_texture ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_compute_texture_dependency_m ( .texture = lighting_texture )
            }
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( 
                .mode = xf_passthrough_mode_copy_m, 
                .copy_source = xf_copy_texture_dependency_m ( .texture = color_texture ) 
            ) 
        },
        ),
    ) );

    // hi-z
    uint32_t hiz_mip_count = 8;
    std_assert_m ( resolution_x % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    std_assert_m ( resolution_y % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    xf_texture_h hiz_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    xf_texture_h downsampled_lighting_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    xf_texture_h ssgi_raymarch_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x / ssgi_scale,
        .height = resolution_y / ssgi_scale,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        //.format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "ssgi_raymarch_texture",
    ) );

    struct {
        float resolution_x_f32;
        float resolution_y_f32;
        uint32_t hiz_mip_count;
    } ssgi_uniform_data = {
        .resolution_x_f32 = ( float ) resolution_x / ssgi_scale,
        .resolution_y_f32 = ( float ) resolution_y / ssgi_scale,
        .hiz_mip_count = ( uint32_t ) hiz_mip_count,
    };

    xf->create_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssgi",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "ssgi" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x / ssgi_scale, 8 ), std_div_round_up_u32 ( resolution_y / ssgi_scale, 8 ), 1 },
            .uniform_data = std_buffer_struct_m ( &ssgi_uniform_data ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .sampled_textures_count = 4,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = downsampled_lighting_texture ),
                xf_compute_texture_dependency_m ( .texture = hiz_texture ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_shader_texture_dependency_m ( .texture = ssgi_raymarch_texture, .stage = xg_pipeline_stage_bit_compute_shader_m )
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_clear_m ) }
        ),
    ) );

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
    xf_texture_h ssgi_accumulation_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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
    xf_texture_h ssr_distance_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x / ssr_scale,
        .height = resolution_y / ssr_scale,
        .format = xg_format_r16_sfloat_m,
        .debug_name = "ssr_distance",
    ) );
    xf_texture_h ssr_pdf_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x / ssr_scale,
        .height = resolution_y / ssr_scale,
        .format = xg_format_r16_sfloat_m,
        .debug_name = "ssr_pdf",
    ) );
    xf_texture_h ssr_wi_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x / ssr_scale,
        .height = resolution_y / ssr_scale,
        .format = xg_format_r8g8_unorm_m,
        .debug_name = "ssr_wi",
    ) );
    xf_texture_h ssr_uv_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x / ssr_scale,
        .height = resolution_y / ssr_scale,
        .format = xg_format_r16g16_unorm_m,
        .debug_name = "ssr_uv",
    ) );

    struct {
        float resolution_x_f32;
        float resolution_y_f32;
        float scale_f32;
        uint32_t hiz_mip_count;
    } ssr_uniforms = {
        .resolution_x_f32 = ( float ) resolution_x / ssr_scale,
        .resolution_y_f32 = ( float ) resolution_y / ssr_scale,
        .scale_f32 = ( float ) ssr_scale,
        .hiz_mip_count = hiz_mip_count,
    };

    // ssr color resolve
    xf_texture_h ssr_color_texture = xf->create_texture ( graph, &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_color_texture",
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "ssr_trace",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "ssr" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x / ssr_scale, 8 ), std_div_round_up_u32 ( resolution_y / ssr_scale, 8 ), 1 },
            .uniform_data = std_buffer_struct_m ( &ssr_uniforms ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) }
        ),
        .resources = xf_node_resource_params_m (
            .sampled_textures_count = 5,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = material_texture ),
                xf_compute_texture_dependency_m ( .texture = hiz_texture ),
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = lighting_texture ),
            },
            .storage_texture_writes_count = 5,
            .storage_texture_writes = {
                xf_shader_texture_dependency_m ( .texture = ssr_uv_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ),
                xf_shader_texture_dependency_m ( .texture = ssr_wi_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ),
                xf_shader_texture_dependency_m ( .texture = ssr_pdf_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ),
                xf_shader_texture_dependency_m ( .texture = ssr_distance_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ),
                xf_shader_texture_dependency_m ( .texture = ssr_color_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_clear_m ) },
        ),
    ) );

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

#if 1    
    xf->create_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssr_color",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssr_color" ) ),
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_compute_texture_dependency_m ( .texture = ssr_color_texture ) },
            .sampled_textures_count = 8,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = material_texture ),
                xf_compute_texture_dependency_m ( .texture = depth_texture ),
                xf_compute_texture_dependency_m ( .texture = lighting_texture ),
                xf_compute_texture_dependency_m ( .texture = ssr_uv_texture ),
                xf_compute_texture_dependency_m ( .texture = ssr_wi_texture ),
                xf_compute_texture_dependency_m ( .texture = ssr_pdf_texture ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_clear_m ) },
        )
    ) );
#endif

    // ssr ta
    xf_texture_h ssr_accumulation_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
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
            .workgroup_count = { std_div_round_up_u32 ( resolution_x, 8 ), std_div_round_up_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_compute_texture_dependency_m ( .texture = ssr_accumulation_texture ) },
            .sampled_textures_count = 9,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssr_color_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssr_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = normal_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_object_id_texture ),
                xf_compute_texture_dependency_m ( .texture = velocity_texture ),
                xf_compute_texture_dependency_m ( .texture = ssr_distance_texture ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = ssr_color_texture ) ) }
        )
    ) );

    // combine
    xf_texture_h combine_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    xf_texture_h taa_accumulation_texture = xf->create_multi_texture ( graph, &xf_multi_texture_params_m (
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
    xf_texture_h tonemap_texture = xf->create_texture ( graph, &xf_texture_params_m (
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
    state->render.export_dest = xf->create_texture_from_external ( graph, state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, state->render.export_dest );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( graph, swapchain );
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
}

static void viewapp_destroy_render_graph ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;
    viewapp_render_graph_e active_graph = state->render.active_render_graph;

    if ( state->render.render_graph != xf_null_handle_m ) {
        xf->destroy_graph ( state->render.render_graph, workload );
    }

    switch ( active_graph ) {
    case viewapp_render_graph_raster_m:
        xg->cmd_destroy_buffer ( resource_cmd_buffer, state->render.tessellation_instance_vertex_buffer, xg_resource_cmd_buffer_time_workload_complete_m );
        break;
    default:
        break;
    }

    state->render.active_render_graph = viewapp_render_graph_count_m;
}

static void viewapp_destroy_mouse_pick_graph ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;

    xf->destroy_graph ( state->render.mouse_pick_graph, workload );
    xg->cmd_destroy_texture ( resource_cmd_buffer, state->render.object_id_readback_texture, xg_resource_cmd_buffer_time_workload_start_m );
}

static void viewapp_destroy_ibl_cubemap_gen_graph ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    xf->destroy_graph ( state->render.ibl_cubemap_gen_graph, workload );
}

void viewapp_load_render_graph ( viewapp_render_graph_e graph, xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg_null_handle_m;
    if ( workload != xg_null_handle_m ) {
        resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
    }
    viewapp_destroy_render_graph ( workload, resource_cmd_buffer );

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

    if ( state->render.mouse_pick_graph != xf_null_handle_m ) {
        viewapp_destroy_mouse_pick_graph ( workload, resource_cmd_buffer );
        viewapp_load_mouse_pick_graph();
    }

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

uint64_t viewapp_render_update_sky ( viewapp_sky_component_t* sky_component, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, uint64_t key ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;
    xf_graph_h graph = state->render.render_graph;

    xf_node_h lighting_node = xf->get_node_by_name ( graph, "lighting" );
    std_auto_m lighting_uniforms = ( viewapp_raster_lighting_uniform_data_t* ) ( xf->get_node_uniform_data ( graph, lighting_node ) );
    for ( uint32_t i = 0; i < 9; ++i ) {
        lighting_uniforms->sky_irradiance_sh[i][0] = sky_component->irradiance_sh[i * 3 + 0];
        lighting_uniforms->sky_irradiance_sh[i][1] = sky_component->irradiance_sh[i * 3 + 1];
        lighting_uniforms->sky_irradiance_sh[i][2] = sky_component->irradiance_sh[i * 3 + 2];
    }

    if ( state->render.ibl_cubemap_texture != xg_null_handle_m ) {
        xg->cmd_destroy_texture ( resource_cmd_buffer, state->render.ibl_cubemap_texture, xg_resource_cmd_buffer_time_workload_complete_m );
        state->render.ibl_cubemap_texture = xg_null_handle_m;
    }

    if ( sky_component->radiance_texture != xg_null_handle_m ) {
        state->render.ibl_cubemap_texture = xg->create_texture ( &xg_texture_params_m (
            .memory_type = xg_memory_type_gpu_only_m,
            .device = state->render.device,
            .width = state->render.ibl_cubemap_resolution_x,
            .height = state->render.ibl_cubemap_resolution_y,
            .mip_levels = 10,
            .array_layers = 6,
            .format = xg_format_r16g16b16a16_sfloat_m,
            .allowed_usage = xg_texture_usage_bit_sampled_m | xg_texture_usage_bit_storage_m | xg_texture_usage_bit_render_target_m,
            .flags = xg_texture_create_flag_bit_cubemap_e,
            .view_access = xg_texture_view_access_dynamic_m,
            .debug_name = "sky_cubemap",
        ) );

        xf->bind_texture_to_external ( state->render.ibl_cubemap, state->render.ibl_cubemap_texture );
        key = xf->execute_graph ( state->render.ibl_cubemap_gen_graph, workload, key );
    } else {
        xf->bind_texture_to_external ( state->render.ibl_cubemap, xg->get_default_texture ( state->render.device, xg_default_texture_r16g16b16a16_float_cube_black_m ) );
    }

    return key;
}
