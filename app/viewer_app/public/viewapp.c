#include <std_allocator.h>
#include <std_log.h>
#include <std_time.h>
#include <std_app.h>
#include <std_list.h>
#include <std_file.h>
#include <std_sort.h>

#include <viewapp.h>

#include <math.h>
#include <sm.h>

#include <geometry_pass.h>
#include <lighting_pass.h>
#include <hiz_pass.h>
#include <ssgi_pass.h>
#include <blur_pass.h>
#include <ssr_pass.h>
#include <ui_pass.h>
#include <shadow_pass.h>
#include <raytrace_pass.h>

#include <geometry.h>
#include <viewapp_state.h>

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <se.inl>

// ---

static viewapp_state_t* m_state;

// ---

static void viewapp_boot_workload_resources_layout ( void ) {
    xg_i* xg = m_state->modules.xg;

    m_state->render.workload_bindings_layout = xg->create_resource_layout ( &xg_resource_bindings_layout_params_m (
        .device = m_state->render.device,
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
    uint32_t _pad0[2];
    rv_matrix_4x4_t view_from_world;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t jittered_proj_from_view;
    rv_matrix_4x4_t view_from_proj;
    rv_matrix_4x4_t world_from_view;
    rv_matrix_4x4_t prev_view_from_world;
    rv_matrix_4x4_t prev_proj_from_view;
    float z_near;
    float z_far;
    uint32_t reload;
} workload_uniforms_t;

static void viewapp_update_workload_uniforms ( xg_workload_h workload ) {
    xg_i* xg = m_state->modules.xg;
    se_i* se = m_state->modules.se;
    xf_i* xf = m_state->modules.xf;

    bool camera_found = false;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );
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
            .frame_id = m_state->render.frame_id,
            .time_ms = m_state->render.time_ms,
            .resolution_x_u32 = ( uint32_t ) m_state->render.resolution_x,
            .resolution_y_u32 = ( uint32_t ) m_state->render.resolution_y,
            .resolution_x_f32 = ( float ) m_state->render.resolution_x,
            .resolution_y_f32 = ( float ) m_state->render.resolution_y,
            .view_from_world = view_info.view_matrix,
            .proj_from_view = view_info.proj_matrix,
            .jittered_proj_from_view = view_info.jittered_proj_matrix,
            .world_from_view = view_info.inverse_view_matrix,
            .view_from_proj = view_info.inverse_proj_matrix,
            .prev_view_from_world = view_info.prev_frame_view_matrix,
            .prev_proj_from_view = view_info.prev_frame_proj_matrix,
            .z_near = view_info.proj_params.perspective.near_z,
            .z_far = view_info.proj_params.perspective.far_z,
            .reload = m_state->render.graph_reload,
        };

        // if TAA is off disable jittering
        xf_node_info_t taa_node_info;
        xf->get_node_info ( &taa_node_info, m_state->render.raster_graph,  m_state->render.taa_node );
        if ( !taa_node_info.enabled ) {
            uniforms.jittered_proj_from_view = view_info.proj_matrix;
        }

        xg_buffer_range_t range = xg->write_workload_uniform ( workload, &uniforms, sizeof ( uniforms ) );
        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg_resource_bindings_h bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = m_state->render.workload_bindings_layout,
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
            .frame_id = m_state->render.frame_id,
            .time_ms = m_state->render.time_ms,
            .resolution_x_u32 = ( uint32_t ) m_state->render.resolution_x,
            .resolution_y_u32 = ( uint32_t ) m_state->render.resolution_y,
            .resolution_x_f32 = ( float ) m_state->render.resolution_x,
            .resolution_y_f32 = ( float ) m_state->render.resolution_y,
        };

        xg_buffer_range_t range = xg->write_workload_uniform ( workload, &uniforms, sizeof ( uniforms ) );
        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg_resource_bindings_h bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = m_state->render.workload_bindings_layout,
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 1,
                .buffers = { xg_buffer_resource_binding_m ( .shader_register = 0, .range = range ) }
            )
        ) );
        xg->set_workload_global_bindings ( workload, bindings );
    }
}

// ---

static void viewapp_boot_mouse_pick_graph ( void ) {
    xg_device_h device = m_state->render.device;    
    uint32_t resolution_x = m_state->render.resolution_x;
    uint32_t resolution_y = m_state->render.resolution_y;
    xg_i* xg = m_state->modules.xg;
    xf_i* xf = m_state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "mouse_pick_graph"
    ) );
    m_state->render.mouse_pick_graph = graph;

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
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m ( .depth = xg_depth_clear_regular_m ) ),
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
    m_state->render.object_id_readback_texture = readback_texture;

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

static void viewapp_boot_raytrace_graph ( void ) {
#if xg_enable_raytracing_m
    xg_device_h device = m_state->render.device;    
    xg_swapchain_h swapchain = m_state->render.swapchain;    
    uint32_t resolution_x = m_state->render.resolution_x;
    uint32_t resolution_y = m_state->render.resolution_y;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    xf_i* xf = m_state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "raytrace_graph"
    ) );
    m_state->render.raytrace_graph = graph;

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
        .format = xg_format_r16g16_sfloat_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,
            .debug_name = "depth_texture",
            .clear_on_create = true,
            .clear.depth_stencil = xg_depth_stencil_clear_m ()
        ),
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "geometry_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { 
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
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

    add_geometry_node ( graph, color_texture, normal_texture, material_texture, radiosity_texture, object_id_texture, velocity_texture, depth_texture );

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

    xf_texture_h reservoir_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .debug_name = "reservoir_texture",
            .format = xg_format_r16g16b16a16_sfloat_m,
            .clear_on_create = true,
            .clear.color = xg_color_clear_m()
        ),
        .multi_texture_count = 2,
    ) );

    add_raytrace_setup_pass ( graph, instance_buffer, light_buffer );
    add_raytrace_pass ( graph, color_texture, normal_texture, material_texture, radiosity_texture, depth_texture, lighting_texture, instance_buffer, light_buffer, reservoir_texture );

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
    //xf_node_h taa_node = 
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "taa",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "taa" ) ) ),
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
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
    //m_state->render.taa_node = taa_node;

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
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "tonemap" ) ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
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
    //m_state->render.export_dest = xf->create_texture_from_external ( m_state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, m_state->render.export_dest );
    //add_ui_pass ( graph, tonemap_texture, xf_null_handle_m );

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
#else
    m_state->render.raytrace_graph = xf_null_handle_m;
#endif
}

static void viewapp_boot_raster_graph ( void ) {
    xg_device_h device = m_state->render.device;    
    xg_swapchain_h swapchain = m_state->render.swapchain;    
    uint32_t resolution_x = m_state->render.resolution_x;
    uint32_t resolution_y = m_state->render.resolution_y;
    xs_database_h sdb = m_state->render.sdb;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    xf_i* xf = m_state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "raster_graph",
        .flags = xf_graph_flag_alias_memory_m | xf_graph_flag_alias_resources_m,// | xf_graph_flag_print_execution_order_m | xf_graph_flag_print_node_deps_m,
    ) );
    m_state->render.raster_graph = graph;

    uint32_t shadow_size = 1024 * 4;
    xf_texture_h shadow_texture = xf->create_texture ( &xf_texture_params_m (
        .width = shadow_size,
        .height = shadow_size,
        .format = xg_format_d16_unorm_m,
        .debug_name = "shadow_texture"
    ) );

    xf->create_node ( graph, &xf_node_params_m ( 
        .debug_name = "shadow_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = {
            .textures = { xf_texture_clear_m ( 
                .type = xf_texture_clear_depth_stencil_m,
                .depth_stencil = xg_depth_stencil_clear_m ( .depth = xg_depth_clear_regular_m )
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
        .format = xg_format_r16g16_sfloat_m,
        .debug_name = "velocity_texture",
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,//xg_format_d24_unorm_s8_uint_m,
            .debug_name = "depth_texture",
            .clear_on_create = true,
            .clear.depth_stencil = xg_depth_stencil_clear_m ()
        ),
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "geometry_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { 
                xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
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

    add_geometry_node ( graph, color_texture, normal_texture, material_texture, radiosity_texture, object_id_texture, velocity_texture, depth_texture );

    // lighting
    uint32_t light_grid_size[3] = { 16, 8, 24 };
    uint32_t light_cluster_count = light_grid_size[0] * light_grid_size[1] * light_grid_size[2];
    xf_texture_h lighting_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "lighting_texture",
    ) );

    // TODO rename these buffers more appropriately both here and in shader code
    xf_buffer_h light_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = light_data_size(),
        .debug_name = "light_data",
    ) );

    xf_buffer_h light_list_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( uint32_t ) * light_cluster_count * viewapp_max_lights_m,
        .debug_name = "light_list",
    ) );

    xf_buffer_h light_grid_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( uint32_t ) * 2 * light_cluster_count,
        .debug_name = "light_grid"
    ) );

    xf_buffer_h light_cluster_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( float ) * 4 * 2 * light_cluster_count,
        .debug_name = "light_clusters"
    ) );

    xf_node_h light_update_node = add_light_update_pass ( graph, light_buffer );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "light_cluster_build",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "light_cluster_build" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( light_cluster_count, 64 ), 1, 1 },
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
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "light_cull" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( light_cluster_count, 64 ), 1, 1 },
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
        .queue = xg_cmd_queue_compute_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "lighting" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .uniform_data = std_buffer_m ( &lighting_uniforms ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = { 
                xf_compute_buffer_dependency_m ( .buffer = light_buffer ), 
                xf_compute_buffer_dependency_m ( .buffer = light_list_buffer ), 
                xf_compute_buffer_dependency_m ( .buffer = light_grid_buffer ) 
            },
            .sampled_textures_count = 6,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = material_texture ),
                xf_compute_texture_dependency_m ( .texture = radiosity_texture ),
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
        std_stack_t stack = std_static_stack_m ( params.debug_name );
        std_stack_string_append_format ( &stack, "lighting_mip_" std_fmt_u32_m, i );
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
    uint32_t ssgi_scale = 1;
    xf_texture_h ssgi_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x / ssgi_scale,
        .height = resolution_y / ssgi_scale,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_raymarch_texture",
    ) );
    add_ssgi_raymarch_pass ( graph, "ssgi", ssgi_raymarch_texture, normal_texture, color_texture, lighting_texture, hiz_texture );

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
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssgi_ta" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x / ssgi_scale, 8 ), std_div_ceil_u32 ( resolution_y / ssgi_scale, 8 ), 1 },
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssgi_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 6,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssgi_raymarch_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_texture ), 
                xf_compute_texture_dependency_m ( .texture = hiz_texture ),
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
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
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
    xf_texture_h ssr_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_raymarch_texture",
    ) );
    xf_texture_h ssr_intersection_distance = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
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
        .queue = xg_cmd_queue_compute_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssr_ta" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
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
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "combine" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = combine_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 3,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = lighting_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssr_accumulation_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_accumulation_texture ), 
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
    xf_node_h taa_node = xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "taa",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "taa" ) ) ),
            .samplers_count = 2,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
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
    m_state->render.taa_node = taa_node;

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
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "tonemap" ) ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
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
    m_state->render.export_dest = xf->create_texture_from_external ( m_state->ui.export_texture );
    add_ui_pass ( graph, tonemap_texture, m_state->render.export_dest );

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

    m_state->render.graph_reload = true;

    xf->finalize_graph ( graph );
}

static void viewapp_build_mesh_raytrace_geo ( xg_workload_h workload, se_entity_h entity, viewapp_mesh_component_t* mesh ) {
    if ( !m_state->render.supports_raytrace ) {
        return;
    }

    xg_i* xg = m_state->modules.xg;
    se_i* se = m_state->modules.se;

    se_entity_properties_t entity_properties;
    se->get_entity_properties ( &entity_properties, entity );

    xg_raytrace_geometry_data_t rt_data = xg_raytrace_geometry_data_m (
        .vertex_buffer = mesh->geo_gpu_data.pos_buffer,
        .vertex_format = xg_format_r32g32b32_sfloat_m,
        .vertex_count = mesh->geo_data.vertex_count,
        .vertex_stride = 12,
        .index_buffer = mesh->geo_gpu_data.idx_buffer,
        .index_count = mesh->geo_data.index_count,
    );
    std_str_copy_static_m ( rt_data.debug_name, entity_properties.name );

    xg_raytrace_geometry_params_t rt_geo_params = xg_raytrace_geometry_params_m ( 
        .device = m_state->render.device,
        .geometries = &rt_data,
        .geometry_count = 1,
    );
    std_str_copy_static_m ( rt_geo_params.debug_name, entity_properties.name );
    xg_raytrace_geometry_h rt_geo = xg->create_raytrace_geometry ( workload, 0, &rt_geo_params );
    mesh->rt_geo = rt_geo;
}

static void viewapp_build_raytrace_geo ( xg_workload_h workload ) {
    if ( !m_state->render.supports_raytrace ) {
        return;
    }

    se_i* se = m_state->modules.se;

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t mesh_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        
        se_entity_h* entity_handle = se_stream_iterator_next ( &entity_iterator );
        viewapp_build_mesh_raytrace_geo ( workload, *entity_handle, mesh_component );
    }
}

static void viewapp_build_raytrace_world ( xg_workload_h workload ) {
#if xg_enable_raytracing_m
    if ( !m_state->render.supports_raytrace ) {
        return;
    }

    xg_device_h device = m_state->render.device;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    se_i* se = m_state->modules.se;

    if ( m_state->render.raytrace_world != xg_null_handle_m ) {
        xg->destroy_raytrace_world ( m_state->render.raytrace_world );
    }

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .component_count = 2, 
        .components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xg_raytrace_geometry_instance_t* rt_instances = std_virtual_heap_alloc_array_m ( xg_raytrace_geometry_instance_t, mesh_count );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        sm_vec_3f_t up = sm_quat_transform_f3 ( sm_quat ( transform_component->orientation ), sm_vec_3f_set ( 0, 1, 0 ) );
        sm_vec_3f_t dir = sm_quat_transform_f3 ( sm_quat ( transform_component->orientation ), sm_vec_3f_set ( 0, 0, 1 ) );
        //sm_vec_3f_t up = sm_vec_3f ( transform_component->up );
        //m_vec_3f_t dir = sm_vec_3f ( transform_component->orientation );
        dir = sm_vec_3f_norm ( dir );
        sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );
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

        sm_mat_4x4f_t world_matrix = sm_matrix_4x4f_mul ( trans, rot );
        xg_matrix_3x4_t transform;
        std_mem_copy ( transform.f, world_matrix.e, sizeof ( float ) * 12 );

        rt_instances[i] = xg_raytrace_geometry_instance_m (
            .geometry = mesh_component->rt_geo,
            .id = i,
            .transform = transform,
            .hit_shader_group_binding = 0,
        );
    }

    xs_database_pipeline_h rt_pipeline = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "raytrace" ) );
    xg_graphics_pipeline_state_h rt_pipeline_state = xs->get_pipeline_state ( rt_pipeline );

    xg_raytrace_world_h rt_world = xg->create_raytrace_world ( workload, 0, &xg_raytrace_world_params_m (
        .device = device,
        .pipeline = rt_pipeline_state,
        .instance_array = rt_instances,
        .instance_count = mesh_count,
        .debug_name = "rt_world"
    ) );

    std_virtual_heap_free ( rt_instances );

    m_state->render.raytrace_world = rt_world;
#endif
}

// ---

static sm_vec_3f_t viewapp_light_view_dir ( uint32_t idx ) {
    sm_vec_3f_t dir = { 0, 0, 0 };
    switch ( idx ) {
    case 0:
        dir = sm_vec_3f_set ( 0, -1, 0 ); // up
        break;
    case 1:
        dir = sm_vec_3f_set ( 0, 1, 0 ); // down
        break;
    case 2:
        dir = sm_vec_3f_set ( 0, 0, 1 ); // fw
        break;
    case 3:
        dir = sm_vec_3f_set ( 0, 0, -1 ); // bw
        break;
    case 4:
        dir = sm_vec_3f_set ( 1, 0, 0 ); // right
        break;
    case 5:
        dir = sm_vec_3f_set ( -1, 0, 0 ); // left
        break;
    default:
        std_assert_m ( false );
    }
    return dir;
}

static void viewapp_boot_scene_cornell_box ( xg_workload_h workload ) {
    se_i* se = m_state->modules.se;
    xs_i* xs = m_state->modules.xs;
    rv_i* rv = m_state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_device_h device = m_state->render.device;

    // sphere
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.01,
                .metalness = 0,
            )
        );

        viewapp_transform_component_t transform_component = viewapp_transform_component_m (
            .position = { -1.1, -1.45, 1 },
        );

        se->create_entity( &se_entity_params_m (
            .debug_name = "sphere",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform_component ) }
                    )
                }
            )
        ) );
    }

    // planes
    float plane_pos[5][3] = {
        { 0, 0, 2.5 },
        { 2.5, 0, 0 },
        { -2.5, 0, 0 },
        { 0, 2.5, 0 },
        { 0, -2.5, 0 }
    };

    sm_quat_t plane_rot[5] = {
        sm_quat_axis_rotation ( sm_vec_3f_set ( 1, 0, 0 ), -sm_rad_quad_m ),
        sm_quat_axis_rotation ( sm_vec_3f_set ( 0, 0, 1 ), sm_rad_quad_m ),
        sm_quat_axis_rotation ( sm_vec_3f_set ( 0, 0, 1 ), -sm_rad_quad_m ),
        sm_quat_axis_rotation ( sm_vec_3f_set ( 0, 0, 1 ), sm_rad_semi_m ),
        sm_quat_identity(),
    };

    float plane_col[5][3] = {
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 176 / 255.f, 2.2 ), powf (  40 / 255.f, 2.2 ), powf (  48 / 255.f, 2.2 ) },
        { powf (  67 / 255.f, 2.2 ), powf ( 149 / 255.f, 2.2 ), powf (  66 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
    };

    for ( uint32_t i = 0; i < 5; ++i ) {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 5.f );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    plane_col[i][0],
                    plane_col[i][1],
                    plane_col[i][2],
                },
                .ssr = true,
                .roughness = 0.01,
                .metalness = 0,
            ),
        );

        viewapp_transform_component_t transform_component = viewapp_transform_component_m (
            .position = {
                plane_pos[i][0],
                plane_pos[i][1],
                plane_pos[i][2],
            },
            .orientation = {
                plane_rot[i].e[0],
                plane_rot[i].e[1],
                plane_rot[i].e[2],
                plane_rot[i].e[3],
            },
        );

        se->create_entity ( &se_entity_params_m (
            .debug_name = "plane",
            .update = se_entity_update_m ( 
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform_component ) }
                    )
                }
            )
        ) );
    }

    // light
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 100, 100 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = false,
                .roughness = 0.01,
                .metalness = 0,
                .emissive = { 1, 1, 1 },
            )
        );

        viewapp_transform_component_t transform_component = viewapp_transform_component_m (
            .position = { 0, 1.5, 0 },
        );

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .position = { 0, 1.5, 0 },
            .intensity = 5,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
            .view_count = viewapp_light_max_views_m,
        );

        for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
            sm_vec_3f_t dir = viewapp_light_view_dir ( i );
            sm_quat_t orientation = sm_quat_from_vec ( dir );
            rv_view_params_t view_params = rv_view_params_m (
                .transform = rv_view_transform_m (
                    .position = {
                        light_component.position[0],
                        light_component.position[1],
                        light_component.position[2],
                    },
                    .orientation = {
                        orientation.e[0],
                        orientation.e[1],
                        orientation.e[2],
                        orientation.e[3],
                    },
                ),
                .proj_params.perspective = rv_perspective_projection_params_m (
                    .aspect_ratio = 1,
                    .near_z = 0.01,
                    .far_z = 1000,
                ),
            );
            light_component.views[i] = rv->create_view ( &view_params );
        }

        se->create_entity ( &se_entity_params_m (
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 3,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &light_component ) )
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &mesh_component ) )
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &transform_component ) )
                    )
                }
            )
        ) );
    }
}

#if 1
std_unused_static_m()
static void viewapp_boot_scene_field ( xg_workload_h workload ) {
    se_i* se = m_state->modules.se;
    xs_i* xs = m_state->modules.xs;
    rv_i* rv = m_state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_device_h device = m_state->render.device;

    // plane
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 100.f );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 ) 
                },
                .ssr = true,
                .roughness = 0.01,
            ),          
        );

        viewapp_transform_component_t transform_component = viewapp_transform_component_m ();

        se->create_entity ( &se_entity_params_m(
            .debug_name = "plane",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform_component ) }
                    ) 
                }
            )            
        ) );
    }

    float x = -50;

    // quads
    for ( uint32_t i = 0; i < 10; ++i ) {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 10.f );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 10 / 255.f, 2.2 ),
                    powf ( 10 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.01,
            )
        );

        sm_quat_t rot = sm_quat_from_vec ( sm_vec_3f_set ( 0, 1, 0 ) );
        viewapp_transform_component_t transform_component = viewapp_transform_component_m (
            .position = { x, 5, 0 },
            .orientation = { rot.x, rot.y, rot.z, rot.w }
            //.orientation = { 0, 1, 0 },
            //.up = { 0, 0, -1 },
        );

        se->create_entity ( &se_entity_params_m (
            .debug_name = "quad",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform_component ) }
                    ) 
                }
            )
        ) );

        x += 20;
    }

    // lights
    #if 1
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = false,
                .roughness = 0.01,
                .metalness = 0,
                .emissive = { 1, 1, 1 },
            )
        );

        viewapp_transform_component_t transform_component = viewapp_transform_component_m (
            .position = { 10, 10, -10 },
        );

        viewapp_light_component_t light_component = viewapp_light_component_m(
            .position = { 10, 10, -10 },
            .intensity = 20,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
            .view_count = viewapp_light_max_views_m,
        );
        for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
            sm_quat_t orientation = sm_quat_from_vec ( viewapp_light_view_dir ( i ) );
            rv_view_params_t view_params = rv_view_params_m (
                .transform = rv_view_transform_m (
                    .position = {
                        transform_component.position[0],
                        transform_component.position[1],
                        transform_component.position[2],
                    },
                    .orientation = {
                        orientation.e[0],
                        orientation.e[1],
                        orientation.e[2],
                        orientation.e[3],
                    },
                ),
                .proj_params.perspective = rv_perspective_projection_params_m (
                    .aspect_ratio = 1,
                    .near_z = 0.01,
                    .far_z = 100,
                    .reverse_z = false, // TODO ?
                ),
            );
            light_component.views[i] = rv->create_view ( &view_params );
        }

        se->create_entity ( &se_entity_params_m ( 
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 3,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = { se_stream_update_m ( .data = &light_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform_component ) }
                    )
                }
            )
        ) );
    }
    #endif

    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = false,
                .roughness = 0.01,
                .metalness = 0,
                .emissive = { 1, 1, 1 },
            )
        );

        viewapp_transform_component_t transform_component = viewapp_transform_component_m (
            .position = { -10, 10, -10 },
        );

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .position = { -10, 10, -10 },
            .intensity = 20,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
            .view_count = viewapp_light_max_views_m,
        );
        for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
            sm_quat_t orientation = sm_quat_from_vec ( viewapp_light_view_dir ( i ) );
            rv_view_params_t view_params = rv_view_params_m (
                .transform = rv_view_transform_m (
                    .position = {
                        transform_component.position[0],
                        transform_component.position[1],
                        transform_component.position[2],
                    },
                    .orientation = {
                        orientation.e[0],
                        orientation.e[1],
                        orientation.e[2],
                        orientation.e[3],
                    },
                ),
                .proj_params.perspective = rv_perspective_projection_params_m (
                    .aspect_ratio = 1,
                    .near_z = 0.01,
                    .far_z = 100,
                    .reverse_z = false, // TODO ?
                ),
            );
            light_component.views[i] = rv->create_view ( &view_params );
        }

        se->create_entity ( &se_entity_params_m ( 
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 3,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = { se_stream_update_m ( .data = &light_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform_component ) }
                    )
                }
            )
        ) );
    }
}
#endif

static void viewapp_create_cameras ( void ) {
    se_i* se = m_state->modules.se;
    rv_i* rv = m_state->modules.rv;
    uint32_t resolution_x = m_state->render.resolution_x;
    uint32_t resolution_y = m_state->render.resolution_y;

    // view
    rv_view_params_t view_params = rv_view_params_m (
        .transform = rv_view_transform_m (
            .position = { 0, 0, -8 },
        ),
        .proj_params.perspective = rv_perspective_projection_params_m (
            .aspect_ratio = ( float ) resolution_x / ( float ) resolution_y,
            .near_z = 0.1,
            .far_z = 10000,
            .fov_y = 50.f * rv_deg_to_rad_m,
            .jitter = { 1.f / resolution_x, 1.f / resolution_y },
            .reverse_z = false,
        ),
    );

    // cameras
    viewapp_camera_component_t arcball_camera_component = viewapp_camera_component_m (
        .view = rv->create_view ( &view_params ),
        .enabled = false,
        .type = viewapp_camera_type_arcball_m
    );

    viewapp_camera_component_t flycam_camera_component = viewapp_camera_component_m (
        .view = rv->create_view ( &view_params ),
        .enabled = true,
        .type = viewapp_camera_type_flycam_m
    );

    se->create_entity ( &se_entity_params_m (
        .debug_name = "arcball_camera",
        .update = se_entity_update_m (
            .components = { se_component_update_m (
                .id = viewapp_camera_component_id_m,
                .streams = { se_stream_update_m ( .data = &arcball_camera_component ) }
            ) }
        )
    ) );

    se->create_entity ( &se_entity_params_m (
        .debug_name = "flycam_camera",
        .update = se_entity_update_m (
            .components = { se_component_update_m (
                .id = viewapp_camera_component_id_m,
                .streams = { se_stream_update_m ( .data = &flycam_camera_component ) }
            ) }
        )
    ) );
}

#include <xg_enum.h>

typedef struct {
    char* data;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    xg_format_e format;
} viewapp_texture_t;

static void* viewapp_stbi_realloc ( void* p, size_t old_size, size_t new_size ) {
    void* new = std_virtual_heap_alloc_m ( new_size, 16 );
    std_mem_copy ( new, p, old_size );
    std_virtual_heap_free ( p );
    return new;
}

#define STBI_MALLOC(sz) std_virtual_heap_alloc_m ( sz, 16 )
#define STBI_FREE(p) std_virtual_heap_free ( p )
#define STBI_REALLOC_SIZED(p,oldsz,newsz) viewapp_stbi_realloc ( p, oldsz, newsz )
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
std_unused_static_m()
static viewapp_texture_t viewapp_import_texture ( const struct aiScene* scene, const char* path ) {
    void* data;
    xg_format_e format;
    int width, height, channels;

    uint64_t star_idx = std_str_find ( path, "*" );
    if ( star_idx != std_str_find_null_m ) {
        channels = 4; // TODO
        uint32_t texture_idx = std_str_to_u32 ( path + star_idx );
        std_assert_m ( texture_idx < scene->mNumTextures );
        struct aiTexture* embedded_texture = scene->mTextures[texture_idx];
        if ( embedded_texture->mHeight == 0 ) {
            // Compressed texture (e.g., PNG/JPG embedded)
            data = stbi_load_from_memory ( ( stbi_uc*) embedded_texture->pcData, embedded_texture->mWidth, &width, &height, NULL, channels);
        } else {
            // Raw uncompressed texture
            data = embedded_texture->pcData;
            width = embedded_texture->mWidth;
            height = embedded_texture->mHeight;
        }
    } else {
        if ( !stbi_info ( path, &width, &height, &channels ) ) {
            std_log_error_m ( "failed to read texture " std_fmt_str_m, path );
            return ( viewapp_texture_t ) {0};
        }
        if ( channels == 3 ) channels = 4;
        data = stbi_load ( path, &width, &height, NULL, channels );
        std_assert_m ( data );
    }

    if ( channels == 4 ) {
        format = xg_format_r8g8b8a8_unorm_m;
    } else if ( channels == 2 ) {
        format = xg_format_r8g8_unorm_m;
    } else if ( channels == 1 ) {
        format = xg_format_r8_unorm_m;
    } else {
        format = xg_format_undefined_m;
    }

    viewapp_texture_t texture = {
        .data = data,
        .width = width,
        .height = height,
        .channels = channels,
        .format = format
    };
    return texture;
}

static xg_texture_h viewapp_upload_texture_to_gpu ( xg_resource_cmd_buffer_h cmd_buffer, const viewapp_texture_t* texture, const char* name ) {
     xg_i* xg = m_state->modules.xg;
    xg_texture_params_t params = xg_texture_params_m ( 
        .memory_type = xg_memory_type_gpu_only_m,
        .device = m_state->render.device,
        .width = texture->width,
        .height = texture->height,
        .format = texture->format,
        .allowed_usage = xg_texture_usage_bit_sampled_m | xg_texture_usage_bit_copy_dest_m,
    );
    std_str_copy_static_m ( params.debug_name, name );
    //std_path_name ( params.debug_name, sizeof ( params.debug_name ), path );

    xg_texture_h texture_handle = xg->cmd_create_texture ( cmd_buffer, &params, &xg_texture_init_m (
        .mode = xg_texture_init_mode_upload_m,
        .upload_data = texture->data,
        .final_layout = xg_texture_layout_shader_read_m
    ) );

    return texture_handle;
}

static void viewapp_import_scene ( xg_workload_h workload, const char* input_path ) {
    se_i* se = m_state->modules.se;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;

    unsigned int flags = 0;
    flags |= aiProcess_ConvertToLeftHanded;
    flags |= aiProcess_JoinIdenticalVertices;
    //flags |= aiProcess_MakeLeftHanded;
    flags |= aiProcess_Triangulate;
    flags |= aiProcess_ValidateDataStructure;
    //flags |= aiProcess_GenSmoothNormals;
    flags |= aiProcess_FindInvalidData;
    //flags |= aiProcess_GenBoundingBoxes;
    //flags |= aiProcess_FlipWindingOrder;
    flags |= aiProcess_PreTransformVertices;
    flags |= aiProcess_CalcTangentSpace;

    std_tick_t start_tick = std_tick_now();

    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    std_log_info_m ( "Importing input scene " std_fmt_str_m, input_path );
    const struct aiScene* scene = aiImportFile ( input_path, flags );

    if ( scene == NULL ) {
        const char* error = aiGetErrorString();
        std_log_error_m ( "Error importing file: " std_fmt_str_m, error );
    } else {
        xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
        xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
        xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

        for ( uint32_t mesh_it = 0; mesh_it < scene->mNumMeshes; ++mesh_it ) {
            const struct aiMesh* mesh = scene->mMeshes[mesh_it];
            xg_geo_util_geometry_data_t geo;
            geo.vertex_count = mesh->mNumVertices;
            geo.index_count = mesh->mNumFaces * 3;
            geo.pos = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.nor = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.tan = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.bitan = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.uv = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 2 );
            geo.idx = std_virtual_heap_alloc_array_m ( uint32_t, geo.index_count );

            for ( uint32_t i = 0; i < mesh->mNumVertices; i++ ) {
                geo.pos[i * 3 + 0] = mesh->mVertices[i].x;
                geo.pos[i * 3 + 1] = mesh->mVertices[i].y;
                geo.pos[i * 3 + 2] = mesh->mVertices[i].z;

                geo.nor[i * 3 + 0] = mesh->mNormals[i].x;
                geo.nor[i * 3 + 1] = mesh->mNormals[i].y;
                geo.nor[i * 3 + 2] = mesh->mNormals[i].z;

                geo.tan[i * 3 + 0] = mesh->mTangents[i].x;
                geo.tan[i * 3 + 1] = mesh->mTangents[i].y;
                geo.tan[i * 3 + 2] = mesh->mTangents[i].z;
            
                geo.bitan[i * 3 + 0] = mesh->mBitangents[i].x;
                geo.bitan[i * 3 + 1] = mesh->mBitangents[i].y;
                geo.bitan[i * 3 + 2] = mesh->mBitangents[i].z;
            }

            if ( mesh->mTextureCoords[0] ) {
                for ( uint32_t i = 0; i < mesh->mNumVertices; i++ ) {
                    geo.uv[i * 2 + 0] = mesh->mTextureCoords[0][i].x;
                    geo.uv[i * 2 + 1] = mesh->mTextureCoords[0][i].y;
                }
            } else {
                for ( uint32_t i = 0; i < mesh->mNumVertices; i += 2 ) {
                    geo.uv[i * 2 + 0] = 0;
                    geo.uv[i * 2 + 1] = 0;
                }
            }

            for ( uint32_t i = 0; i < mesh->mNumFaces; i++ ) {
                struct aiFace face = mesh->mFaces[i];
                geo.idx[i * 3 + 0] = face.mIndices[0];
                geo.idx[i * 3 + 1] = face.mIndices[1];
                geo.idx[i * 3 + 2] = face.mIndices[2];
            }

            xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( m_state->render.device, workload, &geo );

            viewapp_material_data_t mesh_material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .roughness = 1,
                .metalness = 0,
                .ssr = false,
            );

            uint32_t mat_idx = mesh->mMaterialIndex;
            if ( mat_idx < scene->mNumMaterials ) {
                struct aiMaterial* material = scene->mMaterials[mat_idx];
                struct aiColor4D diffuse;
                if ( aiGetMaterialColor ( material, AI_MATKEY_BASE_COLOR, &diffuse ) == AI_SUCCESS ) {
                    mesh_material.base_color[0] = diffuse.r;
                    mesh_material.base_color[1] = diffuse.g;
                    mesh_material.base_color[2] = diffuse.b;
                }

                viewapp_texture_t color_texture = {};
                struct aiString color_texture_name;
                if ( aiGetMaterialTexture ( material, aiTextureType_DIFFUSE, 0, &color_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS ) {
                    char texture_path[256];
                    std_path_normalize ( texture_path, 256, input_path );
                    size_t len = std_path_pop ( texture_path );
                    std_path_append ( texture_path, 256 - len, color_texture_name.data );
                    color_texture = viewapp_import_texture ( scene, texture_path );
                }

                viewapp_texture_t normal_texture = {};
                struct aiString normal_texture_name;
                if ( aiGetMaterialTexture ( material, aiTextureType_NORMALS, 0, &normal_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS ) {
                    char texture_path[256];
                    std_path_normalize ( texture_path, 256, input_path );
                    size_t len = std_path_pop ( texture_path );
                    std_path_append ( texture_path, 256 - len, normal_texture_name.data );
                    normal_texture = viewapp_import_texture ( scene, texture_path );
                }

                struct aiString metalness_texture_name;
                struct aiString roughness_texture_name;
                bool has_metalness = aiGetMaterialTexture ( material, AI_MATKEY_METALLIC_TEXTURE, &metalness_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS;
                bool has_roughness = aiGetMaterialTexture ( material, AI_MATKEY_ROUGHNESS_TEXTURE, &roughness_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS;
                if ( has_roughness && has_metalness ) {
                    std_assert_m ( std_str_cmp ( metalness_texture_name.data, roughness_texture_name.data ) == 0 );
                    char texture_path[256];
                    std_path_normalize ( texture_path, 256, input_path );
                    size_t len = std_path_pop ( texture_path );
                    std_path_append ( texture_path, 256 - len, roughness_texture_name.data );
                    viewapp_texture_t material_texture = viewapp_import_texture ( scene, texture_path );
                    
                    for ( uint32_t i = 0; i < material_texture.width * material_texture.height; ++i ) {
                        // Following the glTF spec for metallicRoughnessTexture
                        char metalness = material_texture.data[i * 4 + 2];
                        char roughness = material_texture.data[i * 4 + 1];

                        if ( color_texture.data ) {
                            color_texture.data[i * 4 + 3] = metalness;
                        }
                        if ( normal_texture.data ) {
                            normal_texture.data[i * 4 + 3] = roughness;
                        }
                    }

                    std_virtual_heap_free ( material_texture.data );
                }

                if ( color_texture.data ) {
                    mesh_material.color_texture = viewapp_upload_texture_to_gpu ( resource_cmd_buffer, &color_texture, color_texture_name.data );
                    std_virtual_heap_free ( color_texture.data );
                }

                if ( normal_texture.data ) {
                    mesh_material.normal_texture = viewapp_upload_texture_to_gpu ( resource_cmd_buffer, &normal_texture, normal_texture_name.data );
                    std_virtual_heap_free ( normal_texture.data );
                }

                aiGetMaterialFloat ( material, AI_MATKEY_ROUGHNESS_FACTOR, &mesh_material.roughness );
                aiGetMaterialFloat ( material, AI_MATKEY_METALLIC_FACTOR, &mesh_material.metalness );
            }

            viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
                .geo_data = geo,
                .geo_gpu_data = gpu_data,
                .object_id_pipeline = object_id_pipeline_state,
                .geometry_pipeline = geometry_pipeline_state,
                .shadow_pipeline = shadow_pipeline_state,
                .object_id = m_state->render.next_object_id++,
                .material = mesh_material,
            );

            viewapp_transform_component_t transform_component = viewapp_transform_component_m (
                .position = { 0, 0, 0 },
            );

            se_entity_params_t entity_params = se_entity_params_m (
                .update = se_entity_update_m (
                    .component_count = 2,
                    .components = { 
                        se_component_update_m (
                            .id = viewapp_mesh_component_id_m,
                            .streams = { se_stream_update_m ( .data = &mesh_component ) }
                        ),
                        se_component_update_m (
                            .id = viewapp_transform_component_id_m,
                            .streams = { se_stream_update_m ( .data = &transform_component ) }
                        ) 
                    }
                )
            );
            std_str_copy_static_m ( entity_params.debug_name, mesh->mName.data );
            se->create_entity ( &entity_params );
        }

        for ( uint32_t light_it = 0; light_it < scene->mNumLights; ++light_it ) {
            const struct aiLight* light = scene->mLights[light_it];
            
            viewapp_light_component_t light_component = viewapp_light_component_m (
                .position = { light->mPosition.x, light->mPosition.y, light->mPosition.z },
                .intensity = 5,
                .color = { light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b },
                .shadow_casting = false,
                .view_count = 6,
            );

            // TOOD
            se_entity_params_t entity_params = se_entity_params_m (
                .update = se_entity_update_m (
                    .components = { se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &light_component ) )
                    ) }
                )
            );
            std_str_copy_static_m ( entity_params.debug_name, light->mName.data );
            se->create_entity ( &entity_params );
        }
    }

    //xg->submit_workload ( workload );

    aiReleaseImport ( scene );

    std_tick_t end_tick = std_tick_now();
    float time_ms = std_tick_to_milli_f32 ( end_tick - start_tick );
    std_log_info_m ( "Scene imported in " std_fmt_f32_dec_m(3) "s", time_ms / 1000.f );
}

static void update_raytrace_world ( void ) {
#if xg_enable_raytracing_m
    m_state->render.raytrace_world_update = true;
#endif
}

static void viewapp_load_scene ( uint32_t id ) {
    xg_i* xg = m_state->modules.xg;
    xg->wait_all_workload_complete();

    xg_workload_h workload = xg->create_workload ( m_state->render.device );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    se_i* se = m_state->modules.se;
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m() );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t entity_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < entity_count; ++i ) {
        se_entity_h* entity = se_stream_iterator_next ( &entity_iterator );
        viewapp_mesh_component_t* mesh_component = se->get_entity_component ( *entity, viewapp_mesh_component_id_m, 0 );
        if ( mesh_component ) {
            xg_geo_util_free_data ( &mesh_component->geo_data );
            xg_geo_util_free_gpu_data ( &mesh_component->geo_gpu_data, workload );

            viewapp_material_data_t* material = &mesh_component->material;
            if ( material->color_texture != xg_null_handle_m ) {
                xg->cmd_destroy_texture ( resource_cmd_buffer, material->color_texture, xg_resource_cmd_buffer_time_workload_start_m );
            }
            if ( material->normal_texture != xg_null_handle_m ) {
                xg->cmd_destroy_texture ( resource_cmd_buffer, material->normal_texture, xg_resource_cmd_buffer_time_workload_start_m );
            }
            if ( material->metalness_roughness_texture != xg_null_handle_m ) {
                xg->cmd_destroy_texture ( resource_cmd_buffer, material->metalness_roughness_texture, xg_resource_cmd_buffer_time_workload_start_m );
            }

            if ( mesh_component->rt_geo != xg_null_handle_m ) {
                xg->destroy_raytrace_geometry ( mesh_component->rt_geo );
            }
        }
        se->destroy_entity ( *entity );
    }

    viewapp_create_cameras();

    if ( id == 0 ) {
        viewapp_boot_scene_cornell_box ( workload );
    } else if ( id == 1 ) {
        viewapp_boot_scene_field ( workload );
    } else {
        viewapp_import_scene ( workload, m_state->scene.custom_scene_path );
    }

    m_state->scene.active_scene = id;
    viewapp_build_raytrace_geo ( workload );
    viewapp_build_raytrace_world ( workload );
    xg->submit_workload ( workload );
}

static void viewapp_boot_ui ( xg_device_h device ) {
    xg_i* xg = m_state->modules.xg;
    xi_i* xi = m_state->modules.xi;

    xg_workload_h workload = xg->create_workload ( device );

    xi->load_shaders ( device );
    xi->init_geos ( device, workload );

    std_file_h font_file = std_file_open ( "assets/ProggyVector-Regular.ttf", std_file_read_m );
    std_file_info_t font_file_info;
    std_file_info ( &font_file_info, font_file );
    void* font_data_alloc = std_virtual_heap_alloc_m ( font_file_info.size, 16 );
    std_file_read ( font_data_alloc, font_file_info.size, font_file );

    m_state->ui.font = xi->create_font ( 
        std_buffer ( font_data_alloc, font_file_info.size ),
        &xi_font_params_m (
            .xg_device = device,
            .pixel_height = 16,
            .debug_name = "proggy_clean"
        )
    );

    std_virtual_heap_free ( font_data_alloc );

    m_state->ui.window_state = xi_window_state_m (
        .title = "debug",
        .minimized = false,
        .x = 50,
        .y = 100,
        .width = 350,
        .height = 500,
        .padding_x = 10,
        .padding_y = 2,
        .style = xi_default_style_m (
            .font = m_state->ui.font,
            .color = xi_color_gray_m,
        )
    );

    m_state->ui.frame_section_state = xi_section_state_m ( .title = "frame" );
    m_state->ui.xg_alloc_section_state = xi_section_state_m ( .title = "memory" );
    m_state->ui.scene_section_state = xi_section_state_m ( .title = "scene" );
    m_state->ui.xf_graph_section_state = xi_section_state_m ( .title = "xf graph" );
    m_state->ui.entities_section_state = xi_section_state_m ( .title = "entities" );
    m_state->ui.xf_textures_state = xi_section_state_m ( .title = "xf textures" );

    m_state->ui.export_texture = xg->create_texture ( &xg_texture_params_m (
        .device = device,
        .width = m_state->render.resolution_x,
        .height = m_state->render.resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .allowed_usage = xg_texture_usage_bit_storage_m | xg_texture_usage_bit_sampled_m,
        .debug_name = "export",
    ) );

    xg->submit_workload ( workload );
}

static void viewapp_boot ( void ) {
    uint32_t resolution_x = 1920;//1024;
    uint32_t resolution_y = 1024;//768;

    m_state->render.resolution_x = resolution_x;
    m_state->render.resolution_y = resolution_y;

    wm_i* wm = m_state->modules.wm;
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
    m_state->render.window = window;
    wm->get_window_info ( window, &m_state->render.window_info );
    wm->get_window_input_state ( window, &m_state->render.input_state );

    xg_device_h device;
    xg_device_info_t device_info;
    xg_i* xg = m_state->modules.xg;
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
    ) );
    std_assert_m ( swapchain != xg_null_handle_m );

    m_state->render.device = device;
    m_state->render.swapchain = swapchain;

    se_i* se = m_state->modules.se;

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 1,
        .components = { se_component_layout_m (
            .id = viewapp_camera_component_id_m,
            .stream_count = 1,
            .streams = { sizeof ( viewapp_camera_component_t ) }
        ) }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 2,
        .components = { 
            se_component_layout_m (
                .id = viewapp_mesh_component_id_m,
                .streams = { sizeof ( viewapp_mesh_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_transform_component_id_m,
                .streams = { sizeof ( viewapp_transform_component_t ) }
            ),
        }
    ) );

    se->set_component_properties ( viewapp_transform_component_id_m, "Transform", &se_component_properties_params_m (
        .count = 3,
        .properties = {
            se_field_property_m ( 0, viewapp_transform_component_t, position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_transform_component_t, orientation, se_property_4f32_m ),
            se_field_property_m ( 0, viewapp_transform_component_t, scale, se_property_f32_m ),
        }
    ) );

    se->set_component_properties ( viewapp_mesh_component_id_m, "Mesh", &se_component_properties_params_m (
        .count = 5,
        .properties = {
            se_field_property_m ( 0, viewapp_mesh_component_t, material.base_color, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.emissive, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.roughness, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.metalness, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.ssr, se_property_bool_m ),
        }
    ) );

    se->set_component_properties ( viewapp_light_component_id_m, "Light", &se_component_properties_params_m (
        .count = 5,
        .properties = {
            se_field_property_m ( 0, viewapp_light_component_t, position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, intensity, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, color, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, radius, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, shadow_casting, se_property_bool_m ),
        }
    ) );

    se->set_component_properties ( viewapp_camera_component_id_m, "Camera", &se_component_properties_params_m (
        .count = 1, // TODO
        .properties = {
            se_field_property_m ( 0, viewapp_camera_component_t, enabled, se_property_bool_m ),
        }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 3,
        .components = { 
            se_component_layout_m (
                .id = viewapp_light_component_id_m,
                .streams = { sizeof ( viewapp_light_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_mesh_component_id_m,
                .streams = { sizeof ( viewapp_mesh_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_transform_component_id_m,
                .streams = { sizeof ( viewapp_transform_component_t ) }
            )
        }
    ) );

    xs_i* xs = m_state->modules.xs;
    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "viewapp_sdb" ) );
    m_state->render.sdb = sdb;
    xs->add_database_folder ( sdb, "shader/" );
    xs->set_output_folder ( sdb, "output/shader/" );
    xs_database_build_result_t build_result = xs->build_database ( sdb );
    std_assert_m ( build_result.failed_pipeline_states == 0 );

    viewapp_boot_ui ( device );

    xf_i* xf = m_state->modules.xf;
    xf->load_shaders ( device );

    m_state->render.supports_raytrace = device_info.supports_raytrace;

    viewapp_boot_workload_resources_layout();

    viewapp_load_scene ( m_state->scene.active_scene );

    viewapp_boot_raster_graph();
    if ( m_state->render.supports_raytrace ) {
        viewapp_boot_raytrace_graph();
    }
    viewapp_boot_mouse_pick_graph();

    m_state->render.active_graph = m_state->render.raster_graph;
}

static bool viewapp_get_camera_info ( rv_view_info_t* view_info ) {
    se_i* se = m_state->modules.se;
    rv_i* rv = m_state->modules.rv;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );

    se_stream_iterator_t camera_iterator = se_component_iterator_m ( camera_query_result.components, 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );

        if ( !camera_component->enabled ) {
            continue;
        }

        rv->get_view_info ( view_info, camera_component->view );
        return true;
    }

    return false;
}

static void viewapp_update_camera ( wm_input_state_t* input_state, wm_input_state_t* new_input_state, float dt ) {
    se_i* se = m_state->modules.se;
    xi_i* xi = m_state->modules.xi;
    rv_i* rv = m_state->modules.rv;

    if ( xi->get_active_element_id() != 0 ) {
        return;
    }

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );

    se_stream_iterator_t camera_iterator = se_component_iterator_m ( camera_query_result.components, 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );

        if ( !camera_component->enabled ) {
            continue;
        }

        rv->update_prev_frame_data ( camera_component->view );
        rv->update_proj_jitter ( camera_component->view, m_state->render.frame_id );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, camera_component->view );
        rv_view_transform_t xform = view_info.transform;
        bool dirty_xform = false;

        if ( camera_component->type == viewapp_camera_type_arcball_m ) {
            // drag
            if ( new_input_state->mouse[wm_mouse_state_left_m] ) {
                float drag_scale = -1.f / 400;
                sm_vec_3f_t v = sm_vec_3f ( xform.position );

                int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
                int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

                if ( delta_x != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_quat_t q = sm_quat_axis_rotation ( up, -delta_x * drag_scale );
                    v = sm_quat_transform_f3 ( q, v );
                }

                if ( delta_y != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_vec_3f_t axis = sm_vec_3f_cross ( up, v );
                    axis = sm_vec_3f_norm ( axis );
                    sm_quat_t q = sm_quat_axis_rotation ( axis, delta_y * drag_scale );
                    v = sm_quat_transform_f3 ( q, v );
                }

                if ( delta_x != 0 || delta_y != 0 ) {
                    xform.position[0] = v.x;
                    xform.position[1] = v.y;
                    xform.position[2] = v.z;

                    sm_vec_3f_t dir = sm_vec_3f ( xform.position );
                    dir = sm_vec_3f_neg ( dir );
                    dir = sm_vec_3f_norm ( dir );
                    sm_quat_t q = sm_quat_from_vec ( dir );
                    xform.orientation[0] = q.e[0];
                    xform.orientation[1] = q.e[1];
                    xform.orientation[2] = q.e[2];
                    xform.orientation[3] = q.e[3];

                    dirty_xform = true;
                }
            }

            // zoom
            if ( xi->get_hovered_element_id() == 0 ) {
                if ( new_input_state->mouse[wm_mouse_state_wheel_up_m] || new_input_state->mouse[wm_mouse_state_wheel_down_m] ) {
                    int8_t wheel = ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_up_m] - ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_down_m];
                    float zoom_step = -0.1;
                    float zoom_min = 0.001;

                    sm_vec_3f_t v = sm_vec_3f ( xform.position );
                    float dist = sm_vec_3f_len ( v );
                    float new_dist = fmaxf ( zoom_min, dist + ( zoom_step * wheel ) * dist );
                    v = sm_vec_3f_mul ( v, new_dist / dist );

                    xform.position[0] = v.x;
                    xform.position[1] = v.y;
                    xform.position[2] = v.z;

                    dirty_xform = true;
                }
            }
        } else if ( camera_component->type == viewapp_camera_type_flycam_m ) {
            bool speed_up_press = new_input_state->keyboard[wm_keyboard_state_e_m];
            bool speed_down_press = new_input_state->keyboard[wm_keyboard_state_q_m];
            float speed = camera_component->move_speed;
            if ( speed_up_press ) {
                speed *= 1.1f;
                camera_component->move_speed = speed;
            }
            if ( speed_down_press ) {
                speed *= 0.9f;
                camera_component->move_speed = speed;
            }

            bool forward_press = new_input_state->keyboard[wm_keyboard_state_w_m];
            bool backward_press = new_input_state->keyboard[wm_keyboard_state_s_m];
            bool right_press = new_input_state->keyboard[wm_keyboard_state_d_m];
            bool left_press = new_input_state->keyboard[wm_keyboard_state_a_m];
            if ( ( forward_press && !backward_press ) || ( backward_press && !forward_press ) 
                || ( right_press && !left_press ) || ( left_press && !right_press ) ) {
                sm_vec_3f_t z_axis = sm_quat_to_vec ( sm_quat ( xform.orientation ) );
                sm_vec_3f_t up = { 0, 1, 0 };
                sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
                //sm_vec_3f_t y_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( x_axis, z_axis ) );

                float forward = ( forward_press ? 1.f : 0.f ) - ( backward_press ? 1.f : 0.f );
                float right = ( right_press ? 1.f : 0.f ) - ( left_press ? 1.f : 0.f );
                sm_vec_3f_t move_dir = sm_vec_3f_norm ( sm_vec_3f_add ( sm_vec_3f_mul ( x_axis, right ), sm_vec_3f_mul ( z_axis, forward) ) );
                sm_vec_3f_t move = sm_vec_3f_mul ( move_dir, speed * dt );

                xform.position[0] += move.e[0];
                xform.position[1] += move.e[1];
                xform.position[2] += move.e[2];
                dirty_xform = true;
            }

            bool up_press = new_input_state->keyboard[wm_keyboard_state_z_m];
            bool down_press = new_input_state->keyboard[wm_keyboard_state_x_m];
            if ( ( up_press && !down_press ) || ( !up_press && down_press ) ) {
                float above = ( up_press ? 1.f : 0.f ) - ( down_press ? 1.f : 0.f );
                xform.position[1] += above * speed * dt;
                dirty_xform = true;
            }

            if ( new_input_state->mouse[wm_mouse_state_right_m] ) {
                int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
                int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

                sm_vec_3f_t z_axis = sm_quat_to_vec ( sm_quat ( xform.orientation ) );
                sm_vec_3f_t up = { 0, 1, 0 };
                sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
                sm_vec_3f_t dir = z_axis;
                float drag_scale = -1.f / 400;

                if ( delta_x != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_quat_t q = sm_quat_axis_rotation ( up, -delta_x * drag_scale );
                    dir = sm_quat_transform_f3 ( q, dir );
                }

                if ( delta_y != 0 ) {
                    sm_quat_t q = sm_quat_axis_rotation ( x_axis, -delta_y * drag_scale );
                    dir = sm_quat_transform_f3 ( q, dir );
                }

                sm_quat_t orientation = sm_quat_from_vec ( dir );

                xform.orientation[0] = orientation.e[0];
                xform.orientation[1] = orientation.e[1];
                xform.orientation[2] = orientation.e[2];
                xform.orientation[3] = orientation.e[3];
                dirty_xform = true;
            }

        }

        if ( dirty_xform ) {
            rv->update_view_transform ( camera_component->view, &xform );
        }
    }
}

// TODO this causes double frees on shared resources
#if 0
static void duplicate_selection ( void ) {
    se_i* se = m_state->modules.se;

    if ( m_state->ui.mouse_pick_entity == se_null_handle_m ) {
        return;
    }

    viewapp_mesh_component_t* mesh = se->get_entity_component ( m_state->ui.mouse_pick_entity, viewapp_mesh_component_id_m, 0 );
    viewapp_transform_component_t* transform = se->get_entity_component ( m_state->ui.mouse_pick_entity, viewapp_transform_component_id_m, 0 );

    viewapp_mesh_component_t new_mesh = *mesh;
    new_mesh.object_id = m_state->render.next_object_id++;
    viewapp_transform_component_t new_transform = *transform;
    new_transform.position[0] = 0;
    new_transform.position[1] = 0;
    new_transform.position[2] = 0;

    se->create_entity( &se_entity_params_m (
        .debug_name = "duplicate mesh", // TODO
        .update = se_entity_update_m (
            .component_count = 2,
            .components = { 
                se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &new_mesh ) }
                ),
                se_component_update_m (
                    .id = viewapp_transform_component_id_m,
                    .streams = { se_stream_update_m ( .data = &new_transform ) }
                )
            }
        )
    ) );
}
#endif

static se_entity_h spawn_plane ( xg_workload_h workload ) {
    xs_i* xs = m_state->modules.xs;
    se_i* se = m_state->modules.se;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 1.f );
    xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( m_state->render.device, workload, &geo );

    viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
        .geo_data = geo,
        .geo_gpu_data = gpu_data,
        .object_id_pipeline = object_id_pipeline_state,
        .geometry_pipeline = geometry_pipeline_state,
        .shadow_pipeline = shadow_pipeline_state,
        .object_id = m_state->render.next_object_id++,
        .material = viewapp_material_data_m (
            .base_color = {
                powf ( 240 / 255.f, 2.2 ),
                powf ( 240 / 255.f, 2.2 ),
                powf ( 250 / 255.f, 2.2 )
            },
            .ssr = false,
            .roughness = 0.01,
            .metalness = 0,
            .emissive = { 1, 1, 1 },
        )
    );

    viewapp_transform_component_t transform_component = viewapp_transform_component_m (
        .position = { 0, 0, 0 },
    );


    se_entity_h entity = se->create_entity( &se_entity_params_m (
        .debug_name = "plane",
        .update = se_entity_update_m (
            .component_count = 2,
            .components = {
                se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ),
                se_component_update_m (
                    .id = viewapp_transform_component_id_m,
                    .streams = { se_stream_update_m ( .data = &transform_component ) }
                ),
            }
        )
    ) );

    viewapp_mesh_component_t* mesh = se->get_entity_component ( entity, viewapp_mesh_component_id_m, 0 );
    viewapp_build_mesh_raytrace_geo ( workload, entity, mesh );
    update_raytrace_world();
    return entity;
}

static se_entity_h spawn_light ( xg_workload_h workload ) {
    xs_i* xs = m_state->modules.xs;
    se_i* se = m_state->modules.se;
    rv_i* rv = m_state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
    xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( m_state->render.device, workload, &geo );

    viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
        .geo_data = geo,
        .geo_gpu_data = gpu_data,
        .object_id_pipeline = object_id_pipeline_state,
        .geometry_pipeline = geometry_pipeline_state,
        .shadow_pipeline = shadow_pipeline_state,
        .object_id = m_state->render.next_object_id++,
        .material = viewapp_material_data_m (
            .base_color = { 
                powf ( 240 / 255.f, 2.2 ),
                powf ( 240 / 255.f, 2.2 ),
                powf ( 250 / 255.f, 2.2 )
            },
            .ssr = false,
            .roughness = 0.01,
            .metalness = 0,
            .emissive = { 1, 1, 1 },
        )
    );

    viewapp_transform_component_t transform_component = viewapp_transform_component_m (
        .position = { 0, 0, 0 },
    );

    viewapp_light_component_t light_component = viewapp_light_component_m (
        .position = { 0, 0, 0 },
        .intensity = 5,
        .color = { 1, 1, 1 },
        .shadow_casting = true,
        .view_count = viewapp_light_max_views_m,
    );

    for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
        sm_quat_t orientation = sm_quat_from_vec ( viewapp_light_view_dir ( i ) );
        rv_view_params_t view_params = rv_view_params_m (
            .transform = rv_view_transform_m (
                .position = {
                    light_component.position[0],
                    light_component.position[1],
                    light_component.position[2],
                },
                .orientation = {
                    orientation.e[0],
                    orientation.e[1],
                    orientation.e[2],
                    orientation.e[3],
                },
            ),
            .proj_params.perspective = rv_perspective_projection_params_m (
                .aspect_ratio = 1,
                .near_z = 0.01,
                .far_z = 100,
                .reverse_z = false, // TODO ?
            ),
        );
        light_component.views[i] = rv->create_view ( &view_params );
    }

    se_entity_h entity = se->create_entity( &se_entity_params_m (
        .debug_name = "sphere",
        .update = se_entity_update_m (
            .component_count = 3,
            .components = { 
                se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ),
                se_component_update_m (
                    .id = viewapp_transform_component_id_m,
                    .streams = { se_stream_update_m ( .data = &transform_component ) }
                ),
                se_component_update_m (
                    .id = viewapp_light_component_id_m,
                    .streams = { se_stream_update_m ( .data = &light_component ) }
                )
            }
        )
    ) );

    viewapp_mesh_component_t* mesh = se->get_entity_component ( entity, viewapp_mesh_component_id_m, 0 );
    viewapp_build_mesh_raytrace_geo ( workload, entity, mesh );
    update_raytrace_world();
    return entity;
}

static void mouse_pick ( uint32_t x, uint32_t y ) {
    xg_i* xg = m_state->modules.xg;
    xi_i* xi = m_state->modules.xi;
    xf_i* xf = m_state->modules.xf;

    if ( xi->get_active_element_id () != 0 ) {
        return;
    }

    xg_workload_h workload = xg->create_workload ( m_state->render.device );
    viewapp_update_workload_uniforms ( workload );
    xf->execute_graph ( m_state->render.mouse_pick_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->wait_all_workload_complete();

    uint32_t resolution_x = m_state->render.resolution_x;

    xg_texture_info_t info;
    xg->get_texture_info ( &info, m_state->render.object_id_readback_texture );
    std_auto_m data = ( uint8_t* ) info.allocation.mapped_address;

    uint8_t pixel = data[y * resolution_x + x];
    uint8_t id = pixel;

    se_i* se = m_state->modules.se;
    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &query_result.entities );
    uint32_t mesh_count = query_result.entity_count;

    for ( uint32_t i = 0; i < mesh_count; ++i ) {
        se_entity_h* entity = se_stream_iterator_next ( &entity_iterator );
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );

        if ( mesh_component->object_id == id ) {
            m_state->ui.mouse_pick_entity = *entity;
            se_entity_properties_t props;
            se->get_entity_properties ( &props, *entity );
            std_log_info_m ( "Mouse pick entity handle " std_fmt_u64_m " " std_fmt_str_m, m_state->ui.mouse_pick_entity, props.name );
            return;
        }
    }
 
    m_state->ui.mouse_pick_entity = se_null_handle_m;
}

static void viewapp_update_ui ( wm_window_info_t* window_info, wm_input_state_t* old_input_state, wm_input_state_t* input_state, xg_workload_h workload ) {
    wm_i* wm = m_state->modules.wm;
    xg_i* xg = m_state->modules.xg;
    xf_i* xf = m_state->modules.xf;
    xi_i* xi = m_state->modules.xi;
    se_i* se = m_state->modules.se;
    wm_window_h window = m_state->render.window;

    xi_workload_h xi_workload = xi->create_workload();
    set_ui_pass_xi_workload ( xi_workload );

    wm_input_buffer_t input_buffer;
    wm->get_window_input_buffer ( window, &input_buffer );

    rv_view_info_t view_info;
    viewapp_get_camera_info ( &view_info );

    // TODO remove
    xi->set_workload_view_info ( xi_workload, &view_info );

    xi->begin_update ( &xi_update_params_m (
        .window_info = window_info,
        .input_state = input_state,
        .input_buffer = &input_buffer,
        .view_info = &view_info
    ) );

    // export
    if ( m_state->ui.export_source != xf_null_handle_m ) {
        xi->draw_overlay_texture ( xi_workload, &xi_overlay_texture_state_m ( 
            .handle = m_state->ui.export_texture,
            .width = m_state->render.resolution_x,
            .height = m_state->render.resolution_y,
        ) );
    }

    // ui
    xi->begin_window ( xi_workload, &m_state->ui.window_state );
    
    // frame
    xi->begin_section ( xi_workload, &m_state->ui.frame_section_state );
    {
        xi_label_state_t frame_id_label = xi_label_state_m ( .text = "frame id" );
        xi_label_state_t frame_id_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        std_u32_to_str ( frame_id_value.text, xi_label_text_size, m_state->render.frame_id, 0 );
        xi->add_label ( xi_workload, &frame_id_label );
        xi->add_label ( xi_workload, &frame_id_value );
        xi->newline();

        xi_label_state_t frame_time_label = xi_label_state_m ( .text = "frame time" );
        xi_label_state_t frame_time_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        std_f32_to_str ( m_state->render.delta_time_ms, frame_time_value.text, xi_label_text_size );
        xi->add_label ( xi_workload, &frame_time_label );
        xi->add_label ( xi_workload, &frame_time_value );
        xi->newline();

        xi_label_state_t fps_label = xi_label_state_m ( .text = "fps" );
        xi_label_state_t fps_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        float fps = 1000.f / m_state->render.delta_time_ms;
        std_f32_to_str ( fps, fps_value.text, xi_label_text_size );
        xi->add_label ( xi_workload, &fps_label );
        xi->add_label ( xi_workload, &fps_value );
        xi->newline();
    }
    xi->end_section ( xi_workload );

    // xg allocator
    xi->begin_section ( xi_workload, &m_state->ui.xg_alloc_section_state );
    {
        std_allocator_info_t cpu_info;
        std_allocator_info ( &cpu_info );
        std_platform_memory_info_t memory_info = std_platform_memory_info();
        xi->add_label ( xi_workload, &xi_label_state_m ( .text = "cpu" ) );
        xi_label_state_t size_label = xi_label_state_m ( 
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );
        std_stack_t stack = std_static_stack_m ( size_label.text );
        char buffer[32];
        std_size_to_str_approx ( buffer, 32, cpu_info.used_heap_size );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, "/" );
        std_size_to_str_approx ( buffer, 32, cpu_info.total_heap_size );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, "/" );
        std_size_to_str_approx ( buffer, 32, memory_info.total_ram_size );
        std_stack_string_append ( &stack, buffer );
        xi->add_label ( xi_workload, &size_label );
    }
    xi->newline();
    for ( uint32_t i = 0; i < xg_memory_type_count_m; ++i ) {
        xg_allocator_info_t info;
        xg->get_allocator_info ( &info, m_state->render.device, i );
        {
            xi_label_state_t type_label = xi_label_state_m();
            std_stack_t stack = std_static_stack_m ( type_label.text );
            const char* memory_type_name;
            switch ( i ) {
                case xg_memory_type_gpu_only_m:     memory_type_name = "gpu"; break;
                case xg_memory_type_gpu_mapped_m:   memory_type_name = "mapped"; break;
                case xg_memory_type_upload_m:       memory_type_name = "upload"; break;
                case xg_memory_type_readback_m:     memory_type_name = "readback"; break;
            }
            std_stack_string_append ( &stack, memory_type_name );
            xi->add_label ( xi_workload, &type_label );
        }
        {
            xi_label_state_t size_label = xi_label_state_m ( 
                .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
            );
            std_stack_t stack = std_static_stack_m ( size_label.text );
            char buffer[32];
            std_size_to_str_approx ( buffer, 32, info.allocated_size );
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, "/" );
            std_size_to_str_approx ( buffer, 32, info.reserved_size );
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, "/" );
            std_size_to_str_approx ( buffer, 32, info.system_size );
            std_stack_string_append ( &stack, buffer );
            xi->add_label ( xi_workload, &size_label );
        }
        xi->newline();
    }
    xi->end_section ( xi_workload );

    // scene
    xi->begin_section ( xi_workload, &m_state->ui.scene_section_state );
    {
        xi_label_state_t scene_select_label = xi_label_state_m ( .text = "scene" );
        xi->add_label ( xi_workload, &scene_select_label );

        const char* scene_select_items[] = { "cornell box", "field", "..." };
        xi_select_state_t scene_select = xi_select_state_m (
            .items = scene_select_items,
            .item_count = std_static_array_capacity_m ( scene_select_items ),
            .item_idx = m_state->scene.active_scene,
            .width = 100,
            .sort_order = 2,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );
        if ( xi->add_select ( xi_workload, &scene_select ) ) {
            if ( scene_select.item_idx == 2 ) {
                xi->file_pick ( std_buffer_static_array_m ( m_state->scene.custom_scene_path ), NULL );
            }
            if ( scene_select.item_idx != 2 || m_state->scene.custom_scene_path[0] != '\0' ) {
                viewapp_load_scene ( scene_select.item_idx );
            }
        }
    }
    xi->end_section ( xi_workload );
    
    // xf graph
    xi->begin_section ( xi_workload, &m_state->ui.xf_graph_section_state );
    {
        xi_label_state_t graph_select_label = xi_label_state_m ( .text = "graph" );
        xi->add_label ( xi_workload, &graph_select_label );

#if xg_enable_raytracing_m
        const char* graph_select_items[] = { "raster", "raytrace" };
#else
        const char* graph_select_items[] = { "raster" };
#endif
        xi_select_state_t graph_select = xi_select_state_m (
            .items = graph_select_items,
            .item_count = std_static_array_capacity_m ( graph_select_items ),
            .item_idx = m_state->render.active_graph == m_state->render.raster_graph ? 0 : 1,
            .width = 100,
            .sort_order = 1,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );

        xi->add_select ( xi_workload, &graph_select );

        xf_graph_h active_graph = graph_select.item_idx == 0 ? m_state->render.raster_graph : m_state->render.raytrace_graph;
        if ( m_state->render.active_graph != active_graph ) {
            m_state->render.active_graph = active_graph;
        }

        xi->newline();

        xf_graph_info_t graph_info;
        xf->get_graph_info ( &graph_info, m_state->render.active_graph );
        uint32_t passthrough_nodes_count = 0; // TODO remove?

        const uint64_t* timings = xf->get_graph_timings ( m_state->render.active_graph );

        uint64_t timestamp_sum = 0;
        for ( uint32_t i = 0, node_id = -1; i < graph_info.node_count; ++i ) {
            xf_node_info_t node_info;
            xf->get_node_info ( &node_info, m_state->render.active_graph, graph_info.nodes[i] );

            if ( std_str_cmp ( node_info.debug_name, "export" ) == 0 ) {
                continue;
            }

            ++node_id;

            xi_label_state_t node_label = xi_label_state_m (
                .id = xi_mix_id_m ( i ),
            );
            std_str_copy_static_m ( node_label.text, node_info.debug_name );
            xi->add_label ( xi_workload, &node_label );

            bool any_resource = node_info.resources.render_targets_count + node_info.resources.storage_texture_writes_count + node_info.resources.copy_texture_writes_count > 0;
            bool hover = xi->test_layer_row_hover ( 14 );
            bool expanded = std_bitset_test ( m_state->ui.expanded_nodes_bitset, node_id );
            if ( any_resource && ( hover || expanded ) ) {
                xi_arrow_state_t node_arrow = xi_arrow_state_m (
                    .width = 14,
                    .height = 14,
                    .style = xi_style_m (
                        .horizontal_margin = 8
                    ),
                    .expanded = expanded,
                );
                bool changed = xi->add_arrow ( xi_workload, &node_arrow );
                if ( changed ) {
                    if ( node_arrow.expanded ) {
                        std_bitset_set ( m_state->ui.expanded_nodes_bitset, node_id );
                    } else {
                        std_bitset_clear ( m_state->ui.expanded_nodes_bitset, node_id );
                    }
                }
            }

            if ( node_info.passthrough ) {
                ++passthrough_nodes_count;
                bool node_enabled = node_info.enabled;
                
                xi_switch_state_t node_switch = xi_switch_state_m (
                    .width = 14,
                    .height = 14,
                    .value = node_enabled,
                    .style = xi_style_m (
                        .horizontal_alignment = xi_horizontal_alignment_right_to_left_m
                    ),
                    .id = xi_mix_id_m ( node_id ),
                );
                xi->add_switch ( xi_workload, &node_switch );

                if ( node_switch.value != node_enabled ) {
                    xf->node_set_enabled ( m_state->render.active_graph, graph_info.nodes[i], node_switch.value );
                    xf->invalidate_graph ( m_state->render.active_graph, workload );
                    m_state->render.graph_reload = true;
                }
            }

            uint64_t timestamp_diff = timings[i * 2 + 1] - timings[i * 2];
            timestamp_sum += timestamp_diff;
            float ms = xg->timestamp_to_ns ( m_state->render.device ) * timestamp_diff / 1000000.f;
            char buffer[32];
            std_f32_to_str ( ms, buffer, 32 );
            xi_label_state_t time_label = xi_label_state_m (
                .style = xi_style_m (
                    .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                    .horizontal_border_margin = 25,
                )
            );
            std_str_copy_static_m ( time_label.text, buffer );
            xi->add_label ( xi_workload, &time_label );

            xi->newline();

            if ( expanded ) {
                for ( uint32_t j = 0; j < node_info.texture_count; ++j ) {
                    xf_node_texture_info_t* node_tex_info = &node_info.texture_info[j];
                    if ( node_tex_info->access == xf_resource_access_render_target_m || node_tex_info->access == xf_resource_access_storage_write_m ) {
                        xf_texture_h texture_handle = node_tex_info->handle;
                        xf_texture_info_t texture_info;
                        xf->get_texture_info ( &texture_info, texture_handle );
                        xi_label_state_t texture_label = xi_label_state_m (
                            .style = xi_style_m (
                                .horizontal_margin = 16,
                            )
                        );
                        std_str_copy_static_m ( texture_label.text, texture_info.debug_name );
                        xi->add_label ( xi_workload, &texture_label );
                        uint64_t id = xi_mix_id_m ( j );
                        xi_switch_state_t export_switch = xi_switch_state_m (
                            .width = 14,
                            .height = 14,
                            .value = m_state->ui.export_id == id,
                            .id = id,
                            .style = xi_style_m (
                                .horizontal_margin = 8,
                            )
                        );
                        xi->add_switch ( xi_workload, &export_switch );

                        bool switch_change = export_switch.value != ( m_state->ui.export_id == id );

                        const char* channel_select_items[] = { "r", "g", "b", "a", "1", "0" };
                        xi_select_state_t channel_select_0 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = m_state->ui.export_channels[0],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi_select_state_t channel_select_1 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = m_state->ui.export_channels[1],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi_select_state_t channel_select_2 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = m_state->ui.export_channels[2],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi_select_state_t channel_select_3 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = m_state->ui.export_channels[3],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi->add_select ( xi_workload, &channel_select_0 );
                        xi->add_select ( xi_workload, &channel_select_1 );
                        xi->add_select ( xi_workload, &channel_select_2 );
                        xi->add_select ( xi_workload, &channel_select_3 );
                        bool channel_change = false;
                        channel_change |= m_state->ui.export_channels[0] != channel_select_0.item_idx;
                        channel_change |= m_state->ui.export_channels[1] != channel_select_1.item_idx;
                        channel_change |= m_state->ui.export_channels[2] != channel_select_2.item_idx;
                        channel_change |= m_state->ui.export_channels[3] != channel_select_3.item_idx;
                        bool reload = m_state->render.graph_reload && m_state->ui.export_node_id == node_id && m_state->ui.export_tex_id == j;

                        if ( switch_change || channel_change || reload ) {
                            m_state->ui.export_channels[0] = channel_select_0.item_idx;
                            m_state->ui.export_channels[1] = channel_select_1.item_idx;
                            m_state->ui.export_channels[2] = channel_select_2.item_idx;
                            m_state->ui.export_channels[3] = channel_select_3.item_idx;

                            if ( switch_change || reload ) {
                                if ( export_switch.value ) {
                                    m_state->ui.export_source = texture_handle;
                                    m_state->ui.export_node_id = node_id;
                                    m_state->ui.export_tex_id = j;
                                    m_state->ui.export_id = id;
                                    m_state->ui.export_node = graph_info.nodes[i];
                                } else {
                                    m_state->ui.export_source = xf_null_handle_m;
                                    m_state->ui.export_node_id = -1;
                                    m_state->ui.export_tex_id = -1;
                                    m_state->ui.export_id = 0;
                                    m_state->ui.export_node = xf_null_handle_m;
                                }
                            }

                            xf->invalidate_graph ( m_state->render.active_graph, workload );
                            xf->set_graph_texture_export ( m_state->render.active_graph, m_state->ui.export_node, m_state->ui.export_source, m_state->render.export_dest, m_state->ui.export_channels );
                        }

                        xi->newline();
                    }
                }
            }
        }

        xi->newline();

        {
            xi_label_state_t time_label = xi_label_state_m (
                .style = xi_style_m (
                    .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                    .horizontal_border_margin = 25,
                )
            );
            std_stack_t stack = std_static_stack_m ( time_label.text );
            char buffer[32];

            float sum_ms = xg->timestamp_to_ns ( m_state->render.device ) * timestamp_sum / 1000000.f;
            std_f32_to_str ( sum_ms, buffer, 32 );
            std_stack_string_append ( &stack, buffer );

            //std_stack_string_append ( &stack, "/" );
            //uint64_t timestamp_diff = timings[graph_info.node_count * 2 - 1] - timings[0];
            //float diff_ms = xg->timestamp_to_ns ( m_state->render.device ) * timestamp_diff / 1000000.f;
            //std_f32_to_str ( diff_ms, buffer, 32 );
            //std_stack_string_append ( &stack, buffer );

            xi->add_label ( xi_workload, &time_label );
        }

        xi->newline();

        if ( passthrough_nodes_count > 0 ) {
            if ( xi->add_button ( xi_workload, &xi_button_state_m ( 
                .text = "Disable all",
                .width = 100,
                .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m  
            ) ) ) {
                xf_graph_info_t info;
                xf->get_graph_info ( &info, m_state->render.active_graph );

                for ( uint32_t i = 0; i < info.node_count; ++i ) {
                    xf->disable_node ( m_state->render.active_graph, info.nodes[i] );
                }
                xf->invalidate_graph ( m_state->render.active_graph, workload );
            }
            if ( xi->add_button ( xi_workload, &xi_button_state_m ( 
                .text = "Enable all", 
                .width = 100,
                .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m  
            ) ) ) {
                xf_graph_info_t info;
                xf->get_graph_info ( &info, m_state->render.active_graph );

                for ( uint32_t i = 0; i < info.node_count; ++i ) {
                    xf->enable_node ( m_state->render.active_graph, info.nodes[i] );
                }
                xf->invalidate_graph ( m_state->render.active_graph, workload );
            }
        }
    }
    xi->end_section ( xi_workload );

    // se entities
    bool entity_edit = false;
    xi->begin_section ( xi_workload, &m_state->ui.entities_section_state );
    {
        se_i* se = m_state->modules.se;

        se_entity_h entity_list[se_max_entities_m];
        uint32_t destroy_list[se_max_entities_m];
        uint32_t destroy_count = 0;
        size_t entity_count = se->get_entity_list ( entity_list, se_max_entities_m );
        std_assert_m ( entity_count < 64 * 8 ); // expanded_entities_bitset

        bool delete_selected = false;
        if ( !old_input_state->keyboard[wm_keyboard_state_del_m] && input_state->keyboard[wm_keyboard_state_del_m] ) {
            delete_selected = true;
        }

        for ( uint32_t i = 0; i < entity_count; ++i ) {
            se_entity_h entity = entity_list[i];

            xi_color_t font_color = xi_color_invalid_m;
            if ( entity == m_state->ui.mouse_pick_entity ) {
                if ( delete_selected ) {
                    destroy_list[destroy_count++] = i;
                    m_state->ui.mouse_pick_entity = se_null_handle_m;
                    continue;
                }
                font_color = xi_color_yellow_m;
            }

            se_entity_properties_t props;
            se->get_entity_properties ( &props, entity );

            // entity label
            xi_label_state_t name_label = xi_label_state_m(
                .style = xi_style_m (
                    .font_color = font_color
                )
            );
            std_str_copy_static_m ( name_label.text, props.name );
            xi->add_label ( xi_workload, &name_label );

            // arrow
            bool hovered = xi->test_layer_row_hover ( 14 );
            bool expanded = std_bitset_test ( m_state->ui.expanded_entities_bitset, i );
            bool interacted = false;
            uint64_t arrow_id, button_id;
            if ( hovered || expanded ) {
                xi_arrow_state_t node_arrow = xi_arrow_state_m (
                    .width = 14,
                    .height = 14,
                    .style = xi_style_m (
                        .horizontal_margin = 8
                    ),
                    .expanded = expanded,
                    .id = xi_mix_id_m ( i ),
                );
                arrow_id = node_arrow.id;
                bool changed = xi->add_arrow ( xi_workload, &node_arrow );
                if ( changed ) {
                    if ( node_arrow.expanded ) {
                        std_bitset_set ( m_state->ui.expanded_entities_bitset, i );
                    } else {
                        std_bitset_clear ( m_state->ui.expanded_entities_bitset, i );
                    }
                }
                interacted |= changed;
            }

            if ( hovered ) {
                xi_button_state_t delete_button = xi_button_state_m (
                    .style = xi_style_m (
                        .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                    ),
                    .text = "X",
                    .width = 15,
                    .id = xi_mix_id_m ( i ),
                );
                button_id = delete_button.id;
                if ( xi->add_button ( xi_workload, &delete_button ) ) {
                    destroy_list[destroy_count++] = i;
                    interacted = true;
                    continue;
                }
            }

            uint64_t hovered_element = xi->get_hovered_element_id();
            if ( hovered_element != arrow_id && hovered_element != button_id && hovered && input_state->mouse[wm_mouse_state_left_m] ) {
                m_state->ui.mouse_pick_entity = entity;
            }

            xi->newline();

            if ( expanded ) {
                char buffer[1024];
                std_stack_t stack = std_static_stack_m ( buffer );

                for ( uint32_t j = 0; j < props.component_count; ++j ) {
                    se_component_properties_t* component = &props.components[j];

                    // component label
                    xi_label_state_t label;
                    std_stack_string_append ( &stack, "  " );
                    std_stack_string_append ( &stack, component->name );
                    std_str_copy_static_m ( label.text, buffer );
                    std_stack_clear ( &stack );
                    xi->add_label ( xi_workload, &label );
                    xi->newline();

                    for ( uint32_t k = 0; k < component->property_count; ++k ) {
                        se_property_t* property = &component->properties[k];

                        // property label
                        std_stack_string_append ( &stack, "  " );
                        std_stack_string_append ( &stack, "  " );
                        std_stack_string_append ( &stack, property->name );
                        std_str_copy_static_m ( label.text, buffer );
                        std_stack_clear ( &stack );
                        xi->add_label ( xi_workload, &label );

                        // property editor
                        void* component_data = se->get_entity_component ( entity, component->id, property->stream );
                        xi_property_e type = ( xi_property_e ) property->type; // This assumes the se and xi enums are laid out identical...
                        xi_property_editor_state_t property_editor_state = xi_property_editor_state_m (
                            .type = type,
                            .data = component_data + property->offset,
                            .property_width = type == xi_property_bool_m ? 14 : 64,
                            .property_height = type == xi_property_bool_m ? 14 : 0,
                            .id = ( ( ( uint64_t ) i ) << 32 ) + ( ( ( uint64_t ) j ) << 16 ) + ( ( uint64_t ) k + 1 ),
                            .style = xi_style_m ( .horizontal_alignment = xi_horizontal_alignment_right_to_left_m ),
                        );
                        entity_edit |= xi->add_property_editor ( xi_workload, &property_editor_state );
                        xi->newline();
                    }
                }
            }
        }

        // dispatch deletes and keep expanded entities bitset in sync
        uint32_t remaining_entities = entity_count;
        if ( destroy_count > 0 ) {
            update_raytrace_world();
        }
        for ( uint32_t i = 0; i < destroy_count; ++i ) {
            uint32_t entity_idx = destroy_list[i];
            se->destroy_entity ( entity_list[entity_idx] );
            --remaining_entities;
            #if 0
            for ( uint32_t i = entity_idx; i < remaining_entities; ++i ) {
                bool next = std_bitset_test ( m_state->ui.expanded_entities_bitset, i + 1 );
                if ( next ) {
                    std_bitset_set ( m_state->ui.expanded_entities_bitset, i );
                } else {
                    std_bitset_clear ( m_state->ui.expanded_entities_bitset, i );
                }
            }
            #else
            std_bitset_shift_left ( m_state->ui.expanded_entities_bitset, entity_idx, 1, std_static_array_capacity_m ( m_state->ui.expanded_entities_bitset ) );
            #endif
        }

        if ( xi->add_button ( xi_workload, &xi_button_state_m (
            .text = "Add light",
            .style = xi_default_style_m (
                .horizontal_padding = 8,
            ),
        ) ) ) {
            spawn_light ( workload );
        }

        if ( xi->add_button ( xi_workload, &xi_button_state_m (
            .text = "Add plane",
            .style = xi_default_style_m (
                .horizontal_padding = 8,
            ),
        ) ) ) {
            spawn_plane ( workload );
        }
    }
    xi->end_section ( xi_workload );

    xi->end_window ( xi_workload );

    // raytrace update
    if ( entity_edit ) {
        update_raytrace_world();
    }

    // geos
    bool transform_drag = false;
    if ( m_state->ui.mouse_pick_entity != se_null_handle_m ) {
        viewapp_transform_component_t* transform = se->get_entity_component ( m_state->ui.mouse_pick_entity, viewapp_transform_component_id_m, 0 );
        if ( transform ) {
            xi_transform_state_t xform = xi_transform_state_m (
                .position = { transform->position[0], transform->position[1], transform->position[2] },
                .rotation = { transform->orientation[0], transform->orientation[1], transform->orientation[2], transform->orientation[3] },
                .sort_order = 0,
                .mode = input_state->keyboard[wm_keyboard_state_alt_left_m] ? xi_transform_mode_rotation_m : xi_transform_mode_translation_m,
            );
            transform_drag = xi->draw_transform ( xi_workload, &xform );

            bool rtworld_needs_update = false;
            if ( ( transform->position[0] != xform.position[0]
                || transform->position[1] != xform.position[1]
                || transform->position[2] != xform.position[2]
                || transform->orientation[0] != xform.rotation[0]
                || transform->orientation[1] != xform.rotation[1]
                || transform->orientation[2] != xform.rotation[2]
                || transform->orientation[3] != xform.rotation[3]
                )
                && m_state->render.active_graph == m_state->render.raytrace_graph )
            {
                rtworld_needs_update = true;
            }

            transform->position[0] = xform.position[0];
            transform->position[1] = xform.position[1];
            transform->position[2] = xform.position[2];
            transform->orientation[0] = xform.rotation[0];
            transform->orientation[1] = xform.rotation[1];
            transform->orientation[2] = xform.rotation[2];
            transform->orientation[3] = xform.rotation[3];

            if ( rtworld_needs_update ) {
                update_raytrace_world();
            }
        }
    }

    // mouse pick
    if ( !transform_drag && !old_input_state->mouse[wm_mouse_state_left_m] && input_state->mouse[wm_mouse_state_left_m] ) {
        mouse_pick ( input_state->cursor_x, input_state->cursor_y );
    }

    xi->end_update();
}

static void viewapp_update_lights ( void ) {
    se_i* se = m_state->modules.se;
    rv_i* rv = m_state->modules.rv;

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( 
        .component_count = 2,
        .components = { viewapp_light_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &query_result.components[1], 0 );

    for ( uint32_t light_it = 0; light_it < query_result.entity_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        for ( uint32_t view_it = 0; view_it < viewapp_light_max_views_m; ++view_it ) {
            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, light_component->views[view_it] );
            rv_view_transform_t transform = rv_view_transform_m (
                .position = { 
                    transform_component->position[0],
                    transform_component->position[1],
                    transform_component->position[2],
                },
                .orientation = {
                    view_info.transform.orientation[0],
                    view_info.transform.orientation[1],
                    view_info.transform.orientation[2],
                    view_info.transform.orientation[3],
                }
            );
            rv->update_view_transform ( light_component->views[view_it], &transform );
        }
    }
}

static void viewapp_update_meshes ( void ) {
    se_i* se = m_state->modules.se;

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .component_count = 2,
        .components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 0 );

    for ( uint32_t i = 0; i < mesh_query_result.entity_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );
        mesh_component->prev_transform = *transform_component;
        // TODO update transform...
    }
}

static std_app_state_e viewapp_update ( void ) {
    wm_window_h window = m_state->render.window;

    wm_i* wm = m_state->modules.wm;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    xf_i* xf = m_state->modules.xf;

    if ( !wm->is_window_alive ( window ) ) {
        return std_app_state_exit_m;
    }

    float target_fps = 120.f;
    float target_frame_period = target_fps > 0.f ? 1.f / target_fps * 1000.f : 0.f;
    std_tick_t frame_tick = m_state->render.frame_tick;
    float time_ms = m_state->render.time_ms;

    std_tick_t new_tick = std_tick_now();
    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

    if ( delta_ms < target_frame_period ) {
        //std_thread_yield();
        std_thread_this_sleep( 0 );
        return std_app_state_tick_m;
    }

    time_ms += delta_ms;
    m_state->render.time_ms = time_ms;
    m_state->render.delta_time_ms = delta_ms;

    frame_tick = new_tick;
    m_state->render.frame_tick = frame_tick;

    wm->update_window ( window );

    wm_input_state_t* input_state = &m_state->render.input_state;
    wm_input_state_t new_input_state;
    wm->get_window_input_state ( window, &new_input_state );

    if ( new_input_state.keyboard[wm_keyboard_state_esc_m] ) {
        return std_app_state_exit_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
        xs->rebuild_databases();
        m_state->reload = true;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
        return std_app_state_reload_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f3_m] && new_input_state.keyboard[wm_keyboard_state_f3_m] ) {
        return std_app_state_reboot_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f4_m] && new_input_state.keyboard[wm_keyboard_state_f4_m] ) {
        m_state->render.capture_frame = true;
    }

#if 0
    if ( !input_state->keyboard[wm_keyboard_state_f5_m] && new_input_state.keyboard[wm_keyboard_state_f5_m] ) {
        duplicate_selection();
    }

    if ( !input_state->keyboard[wm_keyboard_state_f6_m] && new_input_state.keyboard[wm_keyboard_state_f6_m] ) {
        spawn_light (  );
    }
#endif

    m_state->render.frame_id += 1;

    xg_workload_h workload = xg->create_workload ( m_state->render.device );
    if ( m_state->render.capture_frame ) {
        xg->debug_capture_workload ( workload );
        m_state->render.capture_frame = false;
    }

    if ( m_state->reload ) {
        bool enabled[xf_graph_max_nodes_m] = {};
        xf_graph_info_t graph_info;
        xf->get_graph_info ( &graph_info, m_state->render.active_graph );
        for ( uint32_t i = 0; i < graph_info.node_count; ++i ) {
            xf_node_info_t node_info;
            xf->get_node_info ( &node_info, m_state->render.active_graph, graph_info.nodes[i] );
            enabled[i] = node_info.enabled;
        }

        bool active_graph[2] = {};
        if ( m_state->render.active_graph == m_state->render.raster_graph ) active_graph[0] = 1;
        if ( m_state->render.active_graph == m_state->render.raytrace_graph ) active_graph[1] = 1;

        xf->destroy_graph ( m_state->render.raster_graph, workload );
        xf->destroy_graph ( m_state->render.raytrace_graph, workload );
        xf->destroy_graph ( m_state->render.mouse_pick_graph, workload );

        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg->cmd_destroy_texture ( resource_cmd_buffer, m_state->render.object_id_readback_texture, xg_resource_cmd_buffer_time_workload_complete_m );
 
        viewapp_boot_raster_graph();
        viewapp_boot_raytrace_graph();
        viewapp_boot_mouse_pick_graph();

        viewapp_build_raytrace_world ( workload );

        if ( active_graph[0] ) m_state->render.active_graph = m_state->render.raster_graph;
        if ( active_graph[1] ) m_state->render.active_graph = m_state->render.raytrace_graph;

        bool any_passthrough = false;
        for ( uint32_t i = 0; i < graph_info.node_count; ++i ) {
            if ( !enabled[i] ) {
                xf->node_set_enabled ( m_state->render.active_graph, graph_info.nodes[i], false );
                any_passthrough = true;
            }
        }

        if ( any_passthrough ) {
            xf->invalidate_graph ( m_state->render.active_graph, workload );
        }

        m_state->reload = false;
        m_state->render.graph_reload = true;
    }

    wm_window_info_t new_window_info;
    wm->get_window_info ( window, &new_window_info );

    viewapp_update_camera ( input_state, &new_input_state, delta_ms * 1000 );

    viewapp_update_meshes();
    viewapp_update_lights();
    viewapp_update_ui ( &new_window_info, input_state, &new_input_state, workload );

    m_state->render.window_info = new_window_info;
    m_state->render.input_state = new_input_state;

    viewapp_update_workload_uniforms ( workload );

    m_state->render.graph_reload = false;

    xf->execute_graph ( m_state->render.active_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->present_swapchain ( m_state->render.swapchain, workload );

    xs->update_pipeline_states ( workload );
    //xf->destroy_unreferenced_resources();

    if ( m_state->render.raytrace_world_update ) {
        xg->wait_all_workload_complete();
        xg_workload_h workload = xg->create_workload ( m_state->render.device );
        viewapp_build_raytrace_world ( workload );
        xf->destroy_graph ( m_state->render.raytrace_graph, workload );
        viewapp_boot_raytrace_graph();
        xg->submit_workload ( workload );
        m_state->render.raytrace_world_update = false;
    }

    std_tick_t update_tick = std_tick_now();
    float update_ms = std_tick_to_milli_f32 ( update_tick - new_tick );
    m_state->render.update_time_ms = update_ms;

    return std_app_state_tick_m;
}

std_app_state_e viewapp_tick ( void ) {
    if ( m_state->render.frame_id == 0 ) {
        viewapp_boot();
    }

    return viewapp_update();
}

void* viewer_app_load ( void* runtime ) {
    std_runtime_bind ( runtime );

    viewapp_state_t* state = viewapp_state_alloc();

    state->api.tick = viewapp_tick;
    state->reload = false;

    state->modules = ( viewapp_modules_state_t ) {
        .wm = std_module_load_m ( wm_module_name_m ),
        .xg = std_module_load_m ( xg_module_name_m ),
        .xs = std_module_load_m ( xs_module_name_m ),
        .xf = std_module_load_m ( xf_module_name_m ),
        .se = std_module_load_m ( se_module_name_m ),
        .rv = std_module_load_m ( rv_module_name_m ),
        .xi = std_module_load_m ( xi_module_name_m ),
    };
    state->render = viewapp_render_state_m();
    state->ui = viewapp_ui_state_m();
    state->scene = viewapp_scene_state_m();

    m_state = state;
    return state;
}

void viewer_app_unload ( void ) {
    xg_i* xg = m_state->modules.xg;
    xg->wait_all_workload_complete();

    xg_workload_h workload = xg->create_workload ( m_state->render.device );

    se_i* se = m_state->modules.se;
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg_geo_util_free_data ( &mesh_component->geo_data );
        xg_geo_util_free_gpu_data ( &mesh_component->geo_gpu_data, workload );
    }

    xg->submit_workload ( workload );

    xg->destroy_resource_layout ( m_state->render.workload_bindings_layout );

    std_module_unload_m ( xi_module_name_m );
    std_module_unload_m ( rv_module_name_m );
    std_module_unload_m ( se_module_name_m );
    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );

    viewapp_state_free();
}

void viewer_app_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );

    std_auto_m state = ( viewapp_state_t* ) api;
    state->api.tick = viewapp_tick;
    state->reload = true;
    m_state = state;

    viewapp_state_bind ( state );
}
