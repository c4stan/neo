#include <std_allocator.h>
#include <std_log.h>
#include <std_time.h>
#include <std_app.h>
#include <std_list.h>

#include <viewapp.h>

#include <math.h>
#include <sm_matrix.h>

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

#include <se.inl>

std_warnings_ignore_m ( "-Wunused-function" )
std_warnings_ignore_m ( "-Wunused-variable" )

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
            .z_near = view_info.proj_params.near_z,
            .z_far = view_info.proj_params.far_z,
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

// TODO avoid this, instead raycast into scene. maybe use a bvh or a grid to speed up if needed.
static void viewapp_boot_mouse_pick_graph ( void ) {
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
        .debug_name = "mouse_pick_graph"
    ) );
    m_state->render.mouse_pick_graph = graph;

    // TODO
    xg_format_e object_id_format = xg_format_r8g8b8a8_uint_m;

    // TODO redo an object id pass here to remove dependency on the raster graph?

    xg_texture_h readback_texture = xg->create_texture ( &xg_texture_params_m (
        .memory_type = xg_memory_type_readback_m,
        .device = device,
        .width = resolution_x,
        .height = resolution_y,
        .format = object_id_format,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m,
        .tiling = xg_texture_tiling_linear_m,
        .debug_name = "object_id_readback",
    ) );
    m_state->render.object_id_readback_texture = readback_texture;

    xf_texture_h copy_dest = xf->create_texture_from_external ( readback_texture );

    xf_node_h copy_node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "mouse_pick_object_id_copy",
        .type = xf_node_type_copy_pass_m,
        .resources = xf_node_resource_params_m (
            .copy_texture_reads_count = 1,
            .copy_texture_reads = {
                xf_copy_texture_dependency_m ( .texture = m_state->render.object_id_texture )
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
    xs_database_h sdb = m_state->render.sdb;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    xf_i* xf = m_state->modules.xf;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "raytrace_graph"
    ) );
    m_state->render.raytrace_graph = graph;

    xf_texture_h color_texture = xf->create_texture ( &xf_texture_params_m ( 
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "color_texture"
    ));

    // raytrace
    xf_node_h raytrace_node = add_raytrace_pass ( graph, color_texture );

    // ui
    xf_node_h ui_node = add_ui_pass ( graph, color_texture );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );
    xf_node_h present_pass = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "present",
        .type = xf_node_type_copy_pass_m,
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
            .copy_texture_reads_count = 1,
            .copy_texture_reads = { xf_copy_texture_dependency_m ( .texture = color_texture ) },
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
        .sort = true
    ) );
    m_state->render.raster_graph = graph;

    xf_texture_h shadow_texture = xf->create_texture ( &xf_texture_params_m (
        .width = 1024,
        .height = 1024,
        .format = xg_format_d16_unorm_m,
        .debug_name = "shadow_texture"
    ) );

    xf_node_h shadow_clear_node = xf->add_node ( graph, &xf_node_params_m ( 
        .debug_name = "shadow_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = {
            .textures = { xf_texture_clear_m ( 
                .type = xf_texture_clear_type_depth_stencil_m,
                .depth_stencil = xg_depth_stencil_clear_m ( .depth = xg_depth_clear_regular_m )
            ) }
        },
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = shadow_texture ) }
        )
    ) );

    // shadows
    // TODO support more than one shadow...
    xf_node_h shadow_node = add_shadow_pass ( graph, shadow_texture );

    // gbuffer laydown
    // TODO remove multi?
    xf_texture_h color_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_unorm_m,
            .debug_name = "color_texture",
        ),
        .multi_texture_count = 1,
    ) );

    xf_texture_h normal_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h object_id_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
        ),
        .auto_advance = false,
    ) );
    xf_texture_h prev_object_id_texture = xf->get_multi_texture ( object_id_texture, -1 );
    m_state->render.object_id_texture = object_id_texture;

    xf_texture_h depth_stencil_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,//xg_format_d24_unorm_s8_uint_m,
            .debug_name = "depth_stencil_texture",
        ),
    ) );

    xf_node_h geometry_clear_node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "geometry_clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { 
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .color = xg_color_clear_m() ),
                xf_texture_clear_m ( .type = xf_texture_clear_type_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m() )
            }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 4,
            .copy_texture_writes = {
                xf_copy_texture_dependency_m ( .texture = color_texture ),
                xf_copy_texture_dependency_m ( .texture = normal_texture ),
                xf_copy_texture_dependency_m ( .texture = object_id_texture ),
                xf_copy_texture_dependency_m ( .texture = depth_stencil_texture ),
            }
        ),
    ) );

    xf_node_h geometry_pass = add_geometry_node ( graph, color_texture, normal_texture, object_id_texture, depth_stencil_texture );

    // lighting
    uint32_t light_grid_size[3] = { 16, 8, 24 };
    uint32_t light_cluster_count = light_grid_size[0] * light_grid_size[1] * light_grid_size[2];
    xf_texture_h lighting_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "lighting_texture",
    ) );

#if 1
    // TODO rename these buffers more appropriately both here and in shader code
    xf_buffer_h light_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = viewapp_max_lights_m * uniform_light_size(),
        .debug_name = "lights",
    ) );

    xf_buffer_h light_upload_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = viewapp_max_lights_m * uniform_light_size(),
        .debug_name = "lights_upload",
        .upload = true,
    ) );

    xf_buffer_h light_list_buffer = xf->create_buffer ( &xf_buffer_params_m (
        .size = sizeof ( uint32_t ) * light_cluster_count * 8,
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

    xf_node_h light_update_node = add_light_update_pass ( graph, light_upload_buffer, light_buffer );

    xf_node_h light_clusters_build_node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "light_cluster_build",
        .type = xf_node_type_compute_pass_m,
        .queue = xg_cmd_queue_compute_m,
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

    xf_node_h light_cull_node = xf->add_node ( graph, &xf_node_params_m ( 
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
#endif

    struct {
        uint32_t grid_size[3];
        float z_scale;
        float z_bias;
    } lighting_uniforms = {
        .grid_size[0] = light_grid_size[0],
        .grid_size[1] = light_grid_size[1],
        .grid_size[2] = light_grid_size[2],
        .z_scale = 0, // TODO
        .z_bias = 0,
    };

    xf_node_h lighting_node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "lighting",
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "lighting" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .uniform_data = std_buffer_m ( &lighting_uniforms ),
        ),
        .resources = xf_node_resource_params_m (
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = { 
                xf_compute_buffer_dependency_m ( .buffer = light_buffer ), 
                xf_compute_buffer_dependency_m ( .buffer = light_list_buffer ), 
                xf_compute_buffer_dependency_m ( .buffer = light_grid_buffer ) 
            },
            .sampled_textures_count = 4,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = color_texture ),
                xf_compute_texture_dependency_m ( .texture = normal_texture ),
                xf_compute_texture_dependency_m ( .texture = depth_stencil_texture ),
                xf_compute_texture_dependency_m ( .texture = shadow_texture ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_compute_texture_dependency_m ( .texture = lighting_texture )
            }
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_alias_m, .alias = color_texture ) },
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

    xf_node_h hiz_mip0_gen_node = add_hiz_mip0_gen_pass ( graph, hiz_texture, depth_stencil_texture );

    for ( uint32_t i = 1; i < hiz_mip_count; ++i ) {
        add_hiz_submip_gen_pass ( graph, hiz_texture, i );
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
    xf_texture_h ssgi_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_raymarch_texture",
    ) );
    xf_node_h ssgi_raymarch_node = add_ssgi_raymarch_pass ( graph, "ssgi", ssgi_raymarch_texture, normal_texture, color_texture, lighting_texture, hiz_texture );

    // ssgi blur
    xf_texture_h ssgi_blur_x_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_x_texture",
    ) );
    xf_node_h ssgi_blur_x_node = add_bilateral_blur_pass ( graph, ssgi_blur_x_texture, ssgi_raymarch_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_blur_x" );

    xf_texture_h ssgi_blur_y_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_y_texture",
    ) );
    xf_node_h ssgi_blur_y_node = add_bilateral_blur_pass ( graph, ssgi_blur_y_texture, ssgi_blur_x_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_blur_y" );

    // ssgi temporal accumulation
    xf_texture_h ssgi_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_accumulation_texture",
        ),
    ) );
    xf_texture_h prev_depth_stencil_texture = xf->get_multi_texture ( depth_stencil_texture, -1 );
    xg_texture_h ssgi_history_texture = xf->get_multi_texture ( ssgi_accumulation_texture, -1 );
    xf_node_h ssgi_temporal_accumulation_node = xf->add_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssgi_ta",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ta" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssgi_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 4,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssgi_blur_y_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_stencil_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_stencil_texture )
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_alias_m, .alias = ssgi_blur_y_texture ) }
        )
    ) );

    // ssgi2
    xf_texture_h ssgi_2_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_raymarch_texture",
    ) );
    xf_node_h ssgi_2_raymarch_node = add_ssgi_raymarch_pass ( graph, "ssgi_2", ssgi_2_raymarch_texture, normal_texture, color_texture, ssgi_raymarch_texture, hiz_texture );

    // ssgi2 blur
    xf_texture_h ssgi_2_blur_x_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_x_texture",
    ) );
    xf_node_h ssgi_2_blur_x_node = add_bilateral_blur_pass ( graph, ssgi_2_blur_x_texture, ssgi_2_raymarch_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_2_blur_x" );

    xf_texture_h ssgi_2_blur_y_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_y_texture",
    ) );
    xf_node_h ssgi_2_blur_y_node = add_bilateral_blur_pass ( graph, ssgi_2_blur_y_texture, ssgi_2_blur_x_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_2_blur_y" );

    // ssgi2 temporal accumulation
    xf_texture_h ssgi_2_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_2_accumulation_texture",
        ),
    ) );
    xg_texture_h ssgi_2_history_texture = xf->get_multi_texture ( ssgi_2_accumulation_texture, -1 );
    xf_node_h ssgi_2_temporal_accumulation_node = xf->add_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssgi_2_ta",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ta" ) ) ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssgi_2_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 4,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssgi_2_blur_y_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_2_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_stencil_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_stencil_texture ) 
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_alias_m, .alias = ssgi_2_blur_y_texture ) }
        ),
    ) );

    // ssr
    xf_texture_h ssr_raymarch_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_raymarch_texture",
    ) );
    xf_node_h ssr_trace_node = add_ssr_raymarch_pass ( graph, ssr_raymarch_texture, normal_texture, lighting_texture, hiz_texture );

    // ssr blur
    xf_texture_h ssr_blur_x_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_x_texture",
    ) );
    xf_node_h ssr_blur_x_node = add_bilateral_blur_pass ( graph, ssr_blur_x_texture, ssr_raymarch_texture, normal_texture, depth_stencil_texture, 1, 5, blur_pass_direction_horizontal_m, "ssr_blur_x" );

    xf_texture_h ssr_blur_y_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format  = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_y_texture",
    ) );
    xf_node_h ssr_blur_y_node = add_bilateral_blur_pass ( graph, ssr_blur_y_texture, ssr_blur_x_texture, normal_texture, depth_stencil_texture, 1, 5, blur_pass_direction_vertical_m, "ssr_blur_y" );

    // ssr ta
    xf_texture_h ssr_accumulation_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssr_accumulation_texture",
        ),
    ) );
    xf_texture_h ssr_history_texture = xf->get_multi_texture ( ssr_accumulation_texture, -1 );
    xf_node_h ssr_ta_node = xf->add_node ( graph, &xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .debug_name = "ssr_ta",
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = xs->get_pipeline_state ( xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssr_ta" ) ) ),
            .workgroup_count = { std_div_ceil_u32 ( resolution_x, 8 ), std_div_ceil_u32 ( resolution_y, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .storage_texture_writes_count = 1,
            .storage_texture_writes = { xf_shader_texture_dependency_m ( .texture = ssr_accumulation_texture, .stage = xg_pipeline_stage_bit_compute_shader_m ) },
            .sampled_textures_count = 7,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = ssr_blur_y_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssr_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_stencil_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_depth_stencil_texture ), 
                xf_compute_texture_dependency_m ( .texture = normal_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ), 
                xf_compute_texture_dependency_m ( .texture = prev_object_id_texture ) },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_alias_m, .alias = ssr_blur_y_texture, ) }
        )
    ) );

    // combine
    xf_texture_h combine_texture = xf->create_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "combine_texture",
    ) );
    xf_node_h combine_node = xf->add_node ( graph, &xf_node_params_m (
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
            .sampled_textures_count = 4,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = lighting_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssr_accumulation_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_accumulation_texture ), 
                xf_compute_texture_dependency_m ( .texture = ssgi_2_accumulation_texture ) 
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
        ),
    ) );
    xg_texture_h taa_history_texture = xf->get_multi_texture ( taa_accumulation_texture, -1 );
    xf_node_h taa_node = xf->add_node ( graph, &xf_node_params_m (
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
            .sampled_textures_count = 4,
            .sampled_textures = { 
                xf_compute_texture_dependency_m ( .texture = combine_texture ), 
                xf_compute_texture_dependency_m ( .texture = depth_stencil_texture ), 
                xf_compute_texture_dependency_m ( .texture = taa_history_texture ), 
                xf_compute_texture_dependency_m ( .texture = object_id_texture ) 
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_alias_m,
                    .alias = combine_texture
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
    xf_node_h tonemap_node = xf->add_node ( graph, &xf_node_params_m (
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
            .sampled_textures = { xf_compute_texture_dependency_m ( .texture = taa_accumulation_texture ) }, // taa_accumulation_texture
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
    xf_node_h ui_node = add_ui_pass ( graph, tonemap_texture );

    // present
    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );
    xf_node_h present_pass = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "present",
        .type = xf_node_type_copy_pass_m,
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

static void viewapp_build_raytrace_geo ( void ) {
    xg_device_h device = m_state->render.device;
    xg_i* xg = m_state->modules.xg;
    se_i* se = m_state->modules.se;

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t mesh_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        
        se_entity_h* entity_handle = se_stream_iterator_next ( &entity_iterator );
        se_entity_properties_t entity_properties;
        se->get_entity_properties ( &entity_properties, *entity_handle );

        xg_raytrace_geometry_data_t rt_data = xg_raytrace_geometry_data_m (
            .vertex_buffer = mesh_component->pos_buffer,
            .vertex_format = xg_format_r32g32b32_sfloat_m,
            .vertex_count = mesh_component->vertex_count,
            .vertex_stride = 12,
            .index_buffer = mesh_component->idx_buffer,
            .index_count = mesh_component->index_count,
        );
        std_str_copy_static_m ( rt_data.debug_name, entity_properties.name );

        xg_raytrace_geometry_params_t rt_geo_params = xg_raytrace_geometry_params_m ( 
            .device = device,
            .geometries = &rt_data,
            .geometry_count = 1,
        );
        std_str_copy_static_m ( rt_geo_params.debug_name, entity_properties.name );
        xg_raytrace_geometry_h rt_geo = xg->create_raytrace_geometry ( &rt_geo_params );
        mesh_component->rt_geo = rt_geo;
    }
}

static void viewapp_build_raytrace_world ( void ) {
#if xg_enable_raytracing_m
    xg_device_h device = m_state->render.device;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    se_i* se = m_state->modules.se;

    if ( m_state->render.raytrace_world != xg_null_handle_m ) {
        xg->destroy_raytrace_world ( m_state->render.raytrace_world );
    }

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xg_raytrace_geometry_instance_t* rt_instances = std_virtual_heap_alloc_array_m ( xg_raytrace_geometry_instance_t, mesh_count );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        
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

    xg_raytrace_world_h rt_world = xg->create_raytrace_world ( &xg_raytrace_world_params_m (
        .device = device,
        .pipeline = rt_pipeline_state,
        .instance_array = rt_instances,
        .instance_count = mesh_count,
        .debug_name = "rt_world"
    ) );

    m_state->render.raytrace_world = rt_world;
#endif
}

static void viewapp_boot_scene_raytrace ( void ) {
    xg_device_h device = m_state->render.device;
    xg_i* xg = m_state->modules.xg;

    geometry_data_t geo = generate_plane ( 1 );
    geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

    xg_raytrace_geometry_data_t rt_data = xg_raytrace_geometry_data_m (
        .vertex_buffer = gpu_data.pos_buffer,
        .vertex_format = xg_format_r32g32b32_sfloat_m,
        .vertex_count = geo.vertex_count,
        .vertex_stride = 12,
        .index_buffer = gpu_data.idx_buffer,
        .index_count = geo.index_count,
        .debug_name = "rt_geo_data"
    );

    xg_raytrace_geometry_h rt_geo = xg->create_raytrace_geometry ( &xg_raytrace_geometry_params_m (
        .device = device,
        .geometries = &rt_data,
        .geometry_count = 1,
        .debug_name = "rt_geo"
    ) );

    xg_raytrace_geometry_instance_t instance = xg_raytrace_geometry_instance_m ( 
        .geometry = rt_geo,
        .id = 0,
    );

    xg_raytrace_world_h world = xg->create_raytrace_world ( &xg_raytrace_world_params_m (
        .device = m_state->render.device,
        .instance_array = &instance,
        .instance_count = 1,
        .debug_name = "rt_world",
    ) );

    free_gpu_data ( &gpu_data );

    m_state->render.raytrace_world = world;
}

// ---

static void viewapp_boot_scene_cornell_box ( void ) {
    se_i* se = m_state->modules.se;
    xs_i* xs = m_state->modules.xs;
    rv_i* rv = m_state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );

    xg_device_h device = m_state->render.device;

    // sphere
    {
        geometry_data_t geo = generate_sphere ( 1.f, 300, 300 );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { -1.1, -1.45, 1 },
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
            )
        );

        se_entity_h sphere = se->create_entity( &se_entity_params_m (
            .debug_name = "sphere",
            .update = se_entity_update_m (
                .components = { se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ) }
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

    float plane_dir[5][3] = {
        { 0, 1, 0 },
        { 0, 1, 0 },
        { 0, 1, 0 },
        { 0, 0, 1 },
        { 0, 0, 1 }
    };

    float plane_up[5][3] = {
        { 0, 0, -1 },
        { -1, 0, 0 },
        { 1, 0, 0 },
        { 0, -1, 0 },
        { 0, 1, 0 },
    };

    float plane_col[5][3] = {
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 176 / 255.f, 2.2 ), powf (  40 / 255.f, 2.2 ), powf (  48 / 255.f, 2.2 ) },
        { powf (  67 / 255.f, 2.2 ), powf ( 149 / 255.f, 2.2 ), powf (  66 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
    };

    for ( uint32_t i = 0; i < 5; ++i ) {
        geometry_data_t geo = generate_plane ( 5.f );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = {
                plane_pos[i][0],
                plane_pos[i][1],
                plane_pos[i][2],
            },
            .orientation = {
                plane_dir[i][0],
                plane_dir[i][1],
                plane_dir[i][2],
            },
            .up = {
                plane_up[i][0],
                plane_up[i][1],
                plane_up[i][2],
            },
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    plane_col[i][0],
                    plane_col[i][1],
                    plane_col[i][2],
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
            ),
        );

        if ( i == 3 ) { // top plane
            mesh_component.material.emissive = 1;
        }

        se_entity_h plane = se->create_entity ( &se_entity_params_m (
            .debug_name = "plane",
            .update = se_entity_update_m ( 
                .components = { se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ) }
            )
        ) );
    }

    // light
    {
        geometry_data_t geo = generate_sphere ( 1.f, 100, 100 );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { 0, 1.5, 0 },
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
                .emissive = 1,
            )
        );

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .position = { 0, 1.5, 0 },
            .intensity = 5,
            .color = { 1, 1, 1 },
            .shadow_casting = true
        );

        rv_view_params_t view_params = rv_view_params_m (
            .position = {
                light_component.position[0],
                light_component.position[1],
                light_component.position[2],
            },
            .focus_point = {
                view_params.position[0],
                view_params.position[1] - 1,
                view_params.position[2],
            },
            .proj_params = rv_projection_params_m (
                .aspect_ratio = 1,
                .near_z = 0.01,
                .far_z = 100,
                .reverse_z = false, // TODO ?
            ),
        );
        light_component.view = rv->create_view ( &view_params );

        se_entity_h light = se->create_entity ( &se_entity_params_m (
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &light_component ) )
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &mesh_component ) )
                    )
                }
            )
        ) );
    }
}

#if 1
static void viewapp_boot_scene_field ( void ) {
    se_i* se = m_state->modules.se;
    xs_i* xs = m_state->modules.xs;
    rv_i* rv = m_state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( m_state->render.sdb, xs_hash_static_string_m ( "shadow" ) );

    xg_device_h device = m_state->render.device;

    // plane
    {
        geometry_data_t geo = generate_plane ( 100.f );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 ) 
                },
                .ssr = true,
                .roughness = 0,
            ),          
        );

        se_entity_h entity = se->create_entity ( &se_entity_params_m(
            .debug_name = "plane",
            .update = se_entity_update_m (
                .components = { se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ) }
            )            
        ) );
    }

    float x = -50;

    // quads
    for ( uint32_t i = 0; i < 10; ++i ) {
        geometry_data_t geo = generate_plane ( 10.f );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { x, 5, 0 },
            .orientation = { 0, 1, 0 },
            .up = { 0, 0, -1 },
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

        se_entity_h entity = se->create_entity ( &se_entity_params_m (
            .debug_name = "quad",
            .update = se_entity_update_m (
                .components = { se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ) }
            )
        ) );

        x += 20;
    }

    // lights
    {
        geometry_data_t geo = generate_sphere ( 1.f, 300, 300 );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { 10, 10, -10 },
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
                .emissive = 1,
            )
        );

        viewapp_light_component_t light_component = viewapp_light_component_m(
            .position = { 10, 10, -10 },
            .intensity = 20,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
        );
        light_component.view = rv->create_view( &rv_view_params_m (
            .position = {
                light_component.position[0],
                light_component.position[1],
                light_component.position[2],
            },
            .focus_point = {
                light_component.position[0],
                light_component.position[1] - 1.f,
                light_component.position[2],
            },
            .proj_params = rv_projection_params_m ( 
                .aspect_ratio = 1,
                .near_z = 0.1,
                .far_z = 100,
                .reverse_z = false,
            ),
        ) );

        se_entity_h entity = se->create_entity ( &se_entity_params_m ( 
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = { se_stream_update_m ( .data = &light_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    )
                }
            )
        ) );
    }

    {
        geometry_data_t geo = generate_sphere ( 1.f, 300, 300 );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { -10, 10, -10 },
            .object_id = m_state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
                .emissive = 1,
            )
        );

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .position = { -10, 10, -10 },
            .intensity = 20,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
        );        
        light_component.view = rv->create_view ( &rv_view_params_m (
            .position = {
                light_component.position[0],
                light_component.position[1],
                light_component.position[2],
            },
            .focus_point = {
                light_component.position[0],
                light_component.position[1] - 1.f,
                light_component.position[2],
            },
            .proj_params = rv_projection_params_m (
                .aspect_ratio = 1,
                .near_z = 0.1f,
                .far_z = 100.f,
                .reverse_z = false,
            ),
        ) );

        se_entity_h entity = se->create_entity ( &se_entity_params_m ( 
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = { se_stream_update_m ( .data = &light_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    )
                }
            )
        ) );
    }
}
#endif

//#include <stdio.h>
static void viewapp_boot ( void ) {
    //{
    //    int c;
    //    do {
    //        c = getchar();
    //    } while ((c != '\n') && (c != EOF));
    //}

    uint32_t resolution_x = 1024;//1920;
    uint32_t resolution_y = 768;//1024;

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
    xg_i* xg = m_state->modules.xg;
    {
        size_t device_count = xg->get_devices_count();
        std_assert_m ( device_count > 0 );
        xg_device_h devices[16];
        xg->get_devices ( devices, 16 );
        device = devices[0];
        bool activate_result = xg->activate_device ( device );
        std_assert_m ( activate_result );
        xg_device_info_t device_info;
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
        .component_count = 1,
        .components = { se_component_layout_m (
            .id = viewapp_mesh_component_id_m,
            .stream_count = 1,
            .streams = { sizeof ( viewapp_mesh_component_t ) }
        ) }
    ) );

    se->set_component_properties ( viewapp_mesh_component_id_m, "Mesh component", &se_component_properties_params_m (
        .count = 3,
        .properties = {
            se_field_property_m ( 0, viewapp_mesh_component_t, position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, orientation, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, up, se_property_3f32_m ),
        }
    ) );

    se->set_component_properties ( viewapp_light_component_id_m, "Light component", &se_component_properties_params_m (
        .count = 3,
        .properties = {
            se_field_property_m ( 0, viewapp_light_component_t, position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, intensity, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, color, se_property_3f32_m ),
        }
    ) );

    se->set_component_properties ( viewapp_camera_component_id_m, "Camera component", &se_component_properties_params_m (
        .count = 1, // TODO
        .properties = {
            se_field_property_m ( 0, viewapp_camera_component_t, enabled, se_property_bool_m ),
        }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 2,
        .components = { 
            se_component_layout_m (
                .id = viewapp_light_component_id_m,
                .stream_count = 1,
                .streams = { sizeof ( viewapp_light_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_mesh_component_id_m,
                .stream_count = 1,
                .streams = { sizeof ( viewapp_mesh_component_t ) }
            )
        }
    ) );

    xs_i* xs = m_state->modules.xs;
    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "viewapp_sdb" ) );
    m_state->render.sdb = sdb;
    xs->add_database_folder ( sdb, "shader/" );
    xs->set_output_folder ( sdb, "output/shader/" );
    xs->build_database ( sdb );

    xi_i* xi = m_state->modules.xi;
    xi->load_shaders ( device );
    xi->init_geos ( device );

    {
        fs_i* fs = m_state->modules.fs;
        fs_file_h font_file = fs->open_file ( "assets/ProggyVector-Regular.ttf", fs_file_read_m );
        fs_file_info_t font_file_info;
        fs->get_file_info ( &font_file_info, font_file );
        void* font_data_alloc = std_virtual_heap_alloc ( font_file_info.size, 16 );
        fs->read_file ( font_data_alloc, font_file_info.size, font_file );

        m_state->ui.font = xi->create_font ( 
            std_buffer ( font_data_alloc, font_file_info.size ),
            &xi_font_params_m (
                .xg_device = device,
                .pixel_height = 16,
            )
        );

        m_state->ui.window_state = xi_window_state_m (
            .title = "debug",
            .minimized = false,
            .x = 50,
            .y = 100,
            .width = 350,
            .height = 500,
            .padding_x = 10,
            .padding_y = 2,
            .style = xi_style_m (
                .font = m_state->ui.font,
            )
        );

        m_state->ui.frame_section_state = xi_section_state_m ( .title = "frame" );
        m_state->ui.xg_alloc_section_state = xi_section_state_m ( .title = "memory" );
        m_state->ui.xf_graph_section_state = xi_section_state_m ( .title = "xf graph" );
        m_state->ui.entities_section_state = xi_section_state_m ( .title = "entities" );
    }

    rv_i* rv = m_state->modules.rv;
    {
        // view
        rv_view_params_t view_params = rv_view_params_m (
            .position = { 0, 0, -8 },
            .proj_params = rv_projection_params_m (
                .aspect_ratio = ( float ) resolution_x / ( float ) resolution_y,
                .near_z = 0.1,
                .far_z = 1000,
                .fov_y = 50.f * rv_deg_to_rad_m,
                .jitter = { 1.f / resolution_x, 1.f / resolution_y },
                .reverse_z = false,
            ),
        );

        // cameras
        viewapp_camera_component_t arcball_camera_component = viewapp_camera_component_m (
            .view = rv->create_view ( &view_params ),
            .enabled = true,
            .type = viewapp_camera_type_arcball_m
        );

        viewapp_camera_component_t flycam_camera_component = viewapp_camera_component_m (
            .view = rv->create_view ( &view_params ),
            .enabled = false,
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

    viewapp_boot_workload_resources_layout();

    viewapp_boot_scene_cornell_box();
    //viewapp_boot_scene_field();
    viewapp_build_raytrace_geo();
    viewapp_build_raytrace_world();

    viewapp_boot_raster_graph();
    viewapp_boot_raytrace_graph();
    viewapp_boot_mouse_pick_graph();

    m_state->render.active_graph = m_state->render.raster_graph;

    //xf_i* xf = m_state->modules.xf;
    //xf->debug_print_graph ( m_state->render.active_graph );
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
                sm_vec_3f_t v = {
                    xform.position[0] - xform.focus_point[0],
                    xform.position[1] - xform.focus_point[1],
                    xform.position[2] - xform.focus_point[2],
                };

                int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
                int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

                if ( delta_x != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( up, delta_x * drag_scale );

                    v = sm_matrix_4x4f_transform_f3_dir ( mat, v );
                }

                if ( delta_y != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_vec_3f_t axis = sm_vec_3f_cross ( up, v );
                    axis = sm_vec_3f_norm ( axis );

                    sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( axis, -delta_y * drag_scale );
                    v = sm_matrix_4x4f_transform_f3_dir ( mat, v );
                }

                if ( delta_x != 0 || delta_y != 0 ) {
                    xform.position[0] = xform.focus_point[0] + v.x;
                    xform.position[1] = xform.focus_point[1] + v.y;
                    xform.position[2] = xform.focus_point[2] + v.z;

                    dirty_xform = true;
                }
            }

            // zoom
            if ( xi->get_hovered_element_id() == 0 ) {
                if ( new_input_state->mouse[wm_mouse_state_wheel_up_m] || new_input_state->mouse[wm_mouse_state_wheel_down_m] ) {
                    int8_t wheel = ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_up_m] - ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_down_m];
                    float zoom_step = -0.1;
                    float zoom_min = 0.001;

                    sm_vec_3f_t v = {
                        xform.position[0] - xform.focus_point[0],
                        xform.position[1] - xform.focus_point[1],
                        xform.position[2] - xform.focus_point[2],
                    };

                    float dist = sm_vec_3f_len ( v );
                    float new_dist = fmaxf ( zoom_min, dist + ( zoom_step * wheel ) * dist );
                    v = sm_vec_3f_mul ( v, new_dist / dist );

                    xform.position[0] = xform.focus_point[0] + v.x;
                    xform.position[1] = xform.focus_point[1] + v.y;
                    xform.position[2] = xform.focus_point[2] + v.z;

                    dirty_xform = true;
                }
            }
        } else if ( camera_component->type == viewapp_camera_type_flycam_m ) {
            bool forward_press = new_input_state->keyboard[wm_keyboard_state_w_m];
            bool backward_press = new_input_state->keyboard[wm_keyboard_state_s_m];
            bool right_press = new_input_state->keyboard[wm_keyboard_state_d_m];
            bool left_press = new_input_state->keyboard[wm_keyboard_state_a_m];

            float speed = camera_component->move_speed;
            if ( ( forward_press && !backward_press ) || ( backward_press && !forward_press ) 
                || ( right_press && !left_press ) || ( left_press && !right_press ) ) {
                sm_vec_3f_t z_axis = sm_vec_3f_norm ( sm_vec_3f_sub ( sm_vec_3f ( xform.focus_point ), sm_vec_3f ( xform.position ) ) );
                sm_vec_3f_t up = { 0, 1, 0 };
                sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
                sm_vec_3f_t y_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( x_axis, z_axis ) );

                float forward = ( forward_press ? 1.f : 0.f ) - ( backward_press ? 1.f : 0.f );
                float right = ( right_press ? 1.f : 0.f ) - ( left_press ? 1.f : 0.f );
                sm_vec_3f_t move_dir = sm_vec_3f_norm ( sm_vec_3f_add ( sm_vec_3f_mul ( x_axis, right ), sm_vec_3f_mul ( z_axis, forward) ) );
                sm_vec_3f_t move = sm_vec_3f_mul ( move_dir, speed * dt );

                xform.position[0] += move.e[0];
                xform.position[1] += move.e[1];
                xform.position[2] += move.e[2];
                xform.focus_point[0] += move.e[0];
                xform.focus_point[1] += move.e[1];
                xform.focus_point[2] += move.e[2];
                dirty_xform = true;
            }

            bool up_press = new_input_state->keyboard[wm_keyboard_state_shift_left_m];
            bool down_press = new_input_state->keyboard[wm_keyboard_state_ctrl_left_m];
            if ( ( up_press && !down_press ) || ( !up_press && down_press ) ) {
                float above = ( up_press ? 1.f : 0.f ) - ( down_press ? 1.f : 0.f );
                xform.position[1] += above * speed * dt;
                xform.focus_point[1] += above * speed * dt;
                dirty_xform = true;
            }

            if ( new_input_state->mouse[wm_mouse_state_right_m] ) {
                int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
                int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

                sm_vec_3f_t z_axis = sm_vec_3f_norm ( sm_vec_3f_sub ( sm_vec_3f ( xform.focus_point ), sm_vec_3f ( xform.position ) ) );
                sm_vec_3f_t up = { 0, 1, 0 };
                sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
                sm_vec_3f_t dir = z_axis;
                float drag_scale = -1.f / 400;

                if ( delta_x != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( up, delta_x * drag_scale );
                    dir = sm_matrix_4x4f_transform_f3_dir ( mat, dir );
                }

                if ( delta_y != 0 ) {
                    sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( x_axis, delta_y * drag_scale );
                    dir = sm_matrix_4x4f_transform_f3_dir ( mat, dir );
                }

                xform.focus_point[0] = xform.position[0] + dir.x;
                xform.focus_point[1] = xform.position[1] + dir.y;
                xform.focus_point[2] = xform.position[2] + dir.z;
                dirty_xform = true;
            }

        }

        if ( dirty_xform ) {
            rv->update_view_transform ( camera_component->view, &xform );
        }
    }
}

static void duplicate_selection ( void ) {
    se_i* se = m_state->modules.se;

    if ( m_state->ui.mouse_pick_entity == se_null_handle_m ) {
        return;
    }

    viewapp_mesh_component_t* mesh = se->get_entity_component ( m_state->ui.mouse_pick_entity, viewapp_mesh_component_id_m, 0 );

    viewapp_mesh_component_t new_mesh = *mesh;
    new_mesh.object_id = m_state->render.next_object_id++;
    new_mesh.position[0] = 0;
    new_mesh.position[1] = 0;
    new_mesh.position[2] = 0;
    new_mesh.orientation[0] = 0;
    new_mesh.orientation[1] = 0;
    new_mesh.orientation[2] = 1;
    new_mesh.up[0] = 0;
    new_mesh.up[1] = 1;
    new_mesh.up[2] = 0;

    se->create_entity( &se_entity_params_m (
        .debug_name = "duplicate mesh", // TODO
        .update = se_entity_update_m (
            .component_count = 1,
            .components = se_component_update_m (
                .id = viewapp_mesh_component_id_m,
                .stream_count = 1,
                .streams = {
                    se_stream_update_m ( .id = 0, .data = &new_mesh )
                }
            )
        )
    ) );
}
    
static void mouse_pick ( uint32_t x, uint32_t y ) {
    xg_i* xg = m_state->modules.xg;
    xi_i* xi = m_state->modules.xi;
    xf_i* xf = m_state->modules.xf;

    if ( xi->get_active_element_id () != 0 ) {
        return;
    }

    xg_workload_h workload = xg->create_workload ( m_state->render.device );
    xf->execute_graph ( m_state->render.mouse_pick_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->wait_all_workload_complete();

    uint32_t resolution_x = m_state->render.resolution_x;
    uint32_t resolution_y = m_state->render.resolution_y;

    xg_texture_info_t info;
    xg->get_texture_info ( &info, m_state->render.object_id_readback_texture );
    std_auto_m data = ( uint32_t* ) info.allocation.mapped_address;

    // TODO stall the gpu for the latest accurate value before reading?
    uint32_t pixel = data[y * resolution_x + x];
    uint32_t id = pixel & 0xff;

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
            std_log_info_m ( "Mouse pick entity handle " std_fmt_u64_m, m_state->ui.mouse_pick_entity );
            return;
        }
    }
 
    m_state->ui.mouse_pick_entity = se_null_handle_m;
}

// TODO
static void update_raytrace_world ( xg_workload_h workload ) {
#if xg_enable_raytracing_m
    xg_i* xg = m_state->modules.xg;
    xf_i* xf = m_state->modules.xf;
    xg->wait_all_workload_complete();
    viewapp_build_raytrace_world();
    xf->destroy_graph ( m_state->render.raytrace_graph, workload );
    viewapp_boot_raytrace_graph();
#endif
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

    // ui
    xi->begin_window ( xi_workload, &m_state->ui.window_state );
    
    // frame
    xi->begin_section ( xi_workload, &m_state->ui.frame_section_state );
    {
        xi_label_state_t frame_id_label = xi_label_state_m ( .text = "frame id:" );
        xi_label_state_t frame_id_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        std_u32_to_str ( frame_id_value.text, xi_label_text_size, m_state->render.frame_id, 0 );
        xi->add_label ( xi_workload, &frame_id_label );
        xi->add_label ( xi_workload, &frame_id_value );
        xi->newline();

        xi_label_state_t frame_time_label = xi_label_state_m ( .text = "frame time:" );
        xi_label_state_t frame_time_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        std_f32_to_str ( m_state->render.delta_time_ms, frame_time_value.text, xi_label_text_size );
        xi->add_label ( xi_workload, &frame_time_label );
        xi->add_label ( xi_workload, &frame_time_value );
        xi->newline();

        xi_label_state_t fps_label = xi_label_state_m ( .text = "fps:" );
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
        std_virtual_heap_allocator_info ( &cpu_info );
        std_platform_memory_info_t memory_info = std_platform_memory_info();
        xi->add_label ( xi_workload, &xi_label_state_m ( .text = "cpu" ) );
        xi_label_state_t size_label = xi_label_state_m ( 
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );
        std_stack_t stack = std_static_stack_m ( size_label.text );
        char buffer[32];
        std_size_to_str_approx ( buffer, 32, cpu_info.allocated_size );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, "/" );
        std_size_to_str_approx ( buffer, 32, cpu_info.reserved_size );
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
    
    // xf graph
    xi->begin_section ( xi_workload, &m_state->ui.xf_graph_section_state );
    {
        xi_label_state_t graph_select_label = xi_label_state_m (
            .text = "graph"
        );
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
            xf->debug_print_graph ( active_graph );
            m_state->render.active_graph = active_graph;
        }

        xi->newline();

        xf_graph_info_t graph_info;
        xf->get_graph_info ( &graph_info, m_state->render.active_graph );
        uint32_t passthrough_nodes_count = 0;

        for ( uint32_t i = 0; i < graph_info.node_count; ++i ) {
            xf_node_info_t node_info;
            xf->get_node_info ( &node_info, m_state->render.active_graph, graph_info.nodes[i] );

            if ( node_info.passthrough ) {
                ++passthrough_nodes_count;
                bool node_enabled = node_info.enabled;

                xi_label_state_t node_label = xi_label_state_m ();
                std_str_copy_static_m ( node_label.text, node_info.debug_name );
                xi->add_label ( xi_workload, &node_label );

                xi_switch_state_t node_switch = xi_switch_state_m (
                    .width = 14,
                    .height = 14,
                    .value = node_enabled,
                    .style = xi_style_m (
                        .horizontal_alignment = xi_horizontal_alignment_right_to_left_m
                    ),
                );
                xi->add_switch ( xi_workload, &node_switch );

                xi->newline();

                if ( node_switch.value != node_enabled ) {
                    xf->node_set_enabled ( m_state->render.active_graph, graph_info.nodes[i], node_switch.value );
                }
            }
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
        size_t entity_count = se->get_entity_list ( entity_list, se_max_entities_m );
        
        for ( uint32_t i = 0; i < entity_count; ++i ) {
            se_entity_properties_t props;
            se->get_entity_properties ( &props, entity_list[i] );

            // entity label
            xi_label_state_t label = xi_label_state_m();
            std_str_copy_static_m ( label.text, props.name );
            xi->add_label ( xi_workload, &label );
            xi->newline();

            char buffer[1024];
            std_stack_t stack = std_static_stack_m ( buffer );

            for ( uint32_t j = 0; j < props.component_count; ++j ) {
                se_component_properties_t* component = &props.components[j];

                // component label
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
                    void* component_data = se->get_entity_component ( entity_list[i], component->id, property->stream );
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
    xi->end_section ( xi_workload );

    if ( entity_edit ) {
        update_raytrace_world ( workload );
    }

    xi->end_window ( xi_workload );

    if ( !old_input_state->mouse[wm_mouse_state_left_m] && input_state->mouse[wm_mouse_state_left_m] ) {
        mouse_pick ( input_state->cursor_x, input_state->cursor_y );
    }

    // geos
    if ( m_state->ui.mouse_pick_entity != se_null_handle_m ) {
        viewapp_mesh_component_t* mesh = se->get_entity_component ( m_state->ui.mouse_pick_entity, viewapp_mesh_component_id_m, 0 );
        std_assert_m ( mesh );

        xi_transform_state_t xform = xi_transform_state_m (
            .position = { mesh->position[0], mesh->position[1], mesh->position[2] }
        );
        xi->draw_transform ( xi_workload, &xform );

        bool rtworld_needs_update = false;
        if ( ( mesh->position[0] != xform.position[0] 
            || mesh->position[1] != xform.position[1] 
            || mesh->position[2] != xform.position[2] )
            && m_state->render.active_graph == m_state->render.raytrace_graph )
        {
            rtworld_needs_update = true;
        }

        mesh->position[0] = xform.position[0];
        mesh->position[1] = xform.position[1];
        mesh->position[2] = xform.position[2];
    
        if ( rtworld_needs_update ) {
            update_raytrace_world ( workload );
        }
    }

    xi->end_update();
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

    wm_window_info_t* window_info = &m_state->render.window_info;
    wm_input_state_t* input_state = &m_state->render.input_state;

    float target_fps = 30.f;
    float target_frame_period = target_fps > 0.f ? 1.f / target_fps * 1000.f : 0.f;
    std_tick_t frame_tick = m_state->render.frame_tick;
    float time_ms = m_state->render.time_ms;

    std_tick_t new_tick = std_tick_now();
    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

    if ( delta_ms < target_frame_period ) {
        //std_log_info_m ( "pass" );
        //std_thread_yield();
        std_thread_this_sleep( 1 );
        return std_app_state_tick_m;
    }

    time_ms += delta_ms;
    m_state->render.time_ms = time_ms;
    m_state->render.delta_time_ms = delta_ms;

    frame_tick = new_tick;
    m_state->render.frame_tick = frame_tick;

    wm->update_window ( window );

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

    if ( !input_state->keyboard[wm_keyboard_state_f5_m] && new_input_state.keyboard[wm_keyboard_state_f5_m] ) {
        duplicate_selection();
    }

    m_state->render.frame_id += 1;

    viewapp_update_camera ( input_state, &new_input_state, delta_ms * 1000 );

    wm_window_info_t new_window_info;
    wm->get_window_info ( window, &new_window_info );

    xg_workload_h workload = xg->create_workload ( m_state->render.device );

    viewapp_update_ui ( &new_window_info, input_state, &new_input_state, workload );

    m_state->render.window_info = new_window_info;
    m_state->render.input_state = new_input_state;

    if ( m_state->reload ) {
        xf->destroy_graph ( m_state->render.raster_graph, workload );
        xf->destroy_graph ( m_state->render.raytrace_graph, workload );
        xf->destroy_graph ( m_state->render.mouse_pick_graph, workload );

        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg->cmd_destroy_texture ( resource_cmd_buffer, m_state->render.object_id_readback_texture, xg_resource_cmd_buffer_time_workload_complete_m );

        //xf->destroy_unreferenced_resources();

        viewapp_boot_raster_graph();
        viewapp_boot_raytrace_graph();
        viewapp_boot_mouse_pick_graph();

        m_state->render.active_graph = m_state->render.raster_graph;

        m_state->reload = false;
    }

    viewapp_update_workload_uniforms ( workload );

    uint64_t key = 0;
    key = xf->execute_graph ( m_state->render.active_graph, workload, key );
    xf->advance_multi_texture ( m_state->render.object_id_texture );
    xg->submit_workload ( workload );
    xg->present_swapchain ( m_state->render.swapchain, workload );

    xs->update_pipeline_states ( workload );
    //xf->destroy_unreferenced_resources();

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
        .tk = std_module_load_m ( tk_module_name_m ),
        .fs = std_module_load_m ( fs_module_name_m ),
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

    m_state = state;
    return state;
}

void viewer_app_unload ( void ) {
    std_module_unload_m ( xi_module_name_m );
    std_module_unload_m ( rv_module_name_m );
    std_module_unload_m ( se_module_name_m );
    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
    std_module_unload_m ( fs_module_name_m );
    std_module_unload_m ( tk_module_name_m );

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
