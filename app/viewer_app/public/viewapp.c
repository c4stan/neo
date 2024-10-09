#include <std_allocator.h>
#include <std_log.h>
#include <std_time.h>
#include <std_app.h>
#include <std_list.h>

#include <viewapp.h>

#include <math.h>
#include <sm_matrix.h>

#include <geometry_pass.h>
#include <downsample_pass.h>
#include <lighting_pass.h>
#include <hiz_pass.h>
#include <ssgi_pass.h>
#include <temporal_accumulation_pass.h>
#include <blur_pass.h>
#include <ssr_pass.h>
#include <ui_pass.h>
#include <shadow_pass.h>
#include <depth_pass.h>
#include <simple_pass.h>
#include <raytrace_pass.h>

#include <geometry.h>
#include <viewapp_state.h>

#include <se.inl>

std_warnings_ignore_m ( "-Wunused-function" )
std_warnings_ignore_m ( "-Wunused-variable" )

// ---

static viewapp_state_t* m_state;

// ---

typedef struct {
    float resolution_x_f32;
    float resolution_y_f32;
    uint32_t resolution_x_u32;
    uint32_t resolution_y_u32;
    uint32_t frame_id;
    float time_ms;
} frame_cbuffer_data_t;

typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    uint32_t frame_id;
    float time_ms;
    bool capture_frame;
} frame_setup_pass_args_t;

static void frame_setup_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( node_args );
    std_unused_m ( user_args );

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    if ( m_state->render.capture_frame ) {
        xg->cmd_start_debug_capture ( cmd_buffer, xg_debug_capture_stop_time_workload_present_m, key );
        m_state->render.capture_frame = false;
    }

    frame_cbuffer_data_t frame_data = {
        .frame_id = m_state->render.frame_id,
        .time_ms = m_state->render.time_ms,
        .resolution_x_u32 = ( uint32_t ) m_state->render.resolution_x,
        .resolution_y_u32 = ( uint32_t ) m_state->render.resolution_y,
        .resolution_x_f32 = ( float ) m_state->render.resolution_x,
        .resolution_y_f32 = ( float ) m_state->render.resolution_y,
    };

    xg_pipeline_resource_bindings_t frame_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_frame_m,
        .buffer_count = 1,
        .buffers = {
            xg_buffer_resource_binding_m (
                .shader_register = 0,
                .type = xg_buffer_binding_type_uniform_m,
                .range = xg->write_workload_uniform ( node_args->workload, &frame_data, sizeof ( frame_data ) ),
            ),
        }
    );
    xg->cmd_set_pipeline_resources ( cmd_buffer, &frame_bindings, key );
}

typedef struct {
    rv_matrix_4x4_t view_from_world;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t jittered_proj_from_view;
    rv_matrix_4x4_t view_from_proj;
    rv_matrix_4x4_t world_from_view;
    rv_matrix_4x4_t prev_view_from_world;
    rv_matrix_4x4_t prev_proj_from_view;
    float z_near;
    float z_far;
} view_cbuffer_data_t;

static void view_setup_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );

    xg_i* xg = m_state->modules.xg;
    se_i* se = m_state->modules.se;
    xf_i* xf = m_state->modules.xf;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );
    std_assert_m ( camera_query_result.entity_count == 1 );
    se_component_iterator_t camera_iterator = se_component_iterator_m ( &camera_query_result.components[0], 0 );
    viewapp_camera_component_t* camera_component = se_component_iterator_next ( &camera_iterator );

    rv_i* rv = std_module_get_m ( rv_module_name_m );
    rv_view_info_t view_info;
    rv->get_view_info ( &view_info, camera_component->view );

    view_cbuffer_data_t view_data = {
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
    xf->get_node_info ( &taa_node_info, m_state->render.taa_node );
    if ( !taa_node_info.enabled ) {
        view_data.jittered_proj_from_view = view_info.proj_matrix;
    }

    // Bind view resources
    xg_pipeline_resource_bindings_t view_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_view_m,
        .buffer_count = 1,
        .buffers = {
            xg_buffer_resource_binding_m ( 
                .shader_register = 0,
                .type = xg_buffer_binding_type_uniform_m,
                .range = xg->write_workload_uniform ( node_args->workload, &view_data, sizeof ( view_data ) ),
            ),
        }
    );

    xg->cmd_set_pipeline_resources ( node_args->cmd_buffer, &view_bindings, node_args->base_key );

}

// TODO move out into simple_pass
typedef struct {
    xg_sampler_filter_e filter;
} copy_pass_args_t;

static void copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    copy_pass_args_t* args = ( copy_pass_args_t* ) user_args;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        xf_copy_texture_resource_t source = node_args->io->copy_texture_reads[0];
        xf_copy_texture_resource_t dest = node_args->io->copy_texture_writes[0];

        xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
            .source = xf_copy_texture_resource_m ( node_args->io->copy_texture_reads[0] ),
            .destination = xf_copy_texture_resource_m ( node_args->io->copy_texture_writes[0] ),
            .filter = args ? args->filter : xg_sampler_filter_point_m,
        );

        xg->cmd_copy_texture ( cmd_buffer, &copy_params, key );
    }
}

typedef struct {
    xg_graphics_pipeline_state_h pipeline;
    xg_sampler_h sampler;
} combine_pass_args_t;

static void combine_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    combine_pass_args_t* args = ( combine_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
            .render_targets_count = 1,
            .render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] ),
        );
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    xs_i* xs = std_module_get_m ( xs_module_name_m );
    {
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( args->pipeline );
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
            .set = xg_resource_binding_set_per_draw_m,
            .texture_count = 3,
            .textures = {
                xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 ),
                xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[1], 1 ),
                xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[2], 2 )
            },
            .sampler_count = 1,
            .samplers = xg_sampler_resource_binding_m (
                .shader_register = 3,
                .sampler = args->sampler,
            )
        );

        xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

        xg->cmd_draw ( cmd_buffer, 3, 0, key );
    }
}

// ---

static void viewapp_boot_raytrace_graph ( void ) {
    xg_device_h device = m_state->render.device;    
    xg_swapchain_h swapchain = m_state->render.swapchain;    
    uint32_t resolution_x = m_state->render.resolution_x;
    uint32_t resolution_y = m_state->render.resolution_y;
    xs_database_h sdb = m_state->render.sdb;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    xf_i* xf = m_state->modules.xf;

    xf_graph_h graph = xf->create_graph ( device, xg_null_handle_m );
    m_state->render.graph = graph;

    // frame setup
    xf_node_h frame_setup_node = xf->create_node ( graph, &xf_node_params_m (
        .execute_routine = frame_setup_pass,
        .debug_name = "frame_setup"
    ) );

    // view setup
    xf_node_h view_setup_node = xf->create_node ( graph, &xf_node_params_m (
        .execute_routine = view_setup_pass,
        .debug_name = "view_setup",
    ) );

    xf_texture_h color_texture = xf->declare_texture ( &xf_texture_params_m ( 
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
    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );
    xf_node_h present_pass = xf->create_node ( graph, &xf_node_params_m (
        .copy_texture_writes_count = 1,
        .copy_texture_writes = { xf_copy_texture_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m ) },
        .copy_texture_reads_count = 1,
        .copy_texture_reads = { xf_copy_texture_dependency_m ( color_texture, xg_default_texture_view_m ) },
        .presentable_texture = swapchain_multi_texture,
        .execute_routine = copy_pass,
        .debug_name = "present",
    ) );

    xf->debug_print_graph ( graph );
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

    xf_graph_h graph = xf->create_graph ( device, xg_null_handle_m );
    m_state->render.graph = graph;

    // frame setup
    xf_node_h frame_setup_node = xf->create_node ( graph, &xf_node_params_m (
        .execute_routine = frame_setup_pass,
        .debug_name = "frame_setup"
    ) );

    xf_texture_h shadow_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = 1024,
        .height = 1024,
        .format = xg_format_d16_unorm_m,
        .debug_name = "shadow_texture"
    ) );

    //xf_node_h shadow_clear_node = add_depth_clear_pass ( graph, shadow_texture, "shadow_clear" );
    xf_node_h shadow_clear_node = add_simple_clear_pass ( graph, "shadow_clear", &simple_clear_pass_params_m ( 
        .depth_stencil_textures_count = 1,
        .depth_stencil_textures = { shadow_texture },
        .depth_stencil_clears = { xg_depth_stencil_clear_m ( .depth = xg_depth_clear_regular_m ) }
    ) );

    // shadows
    // TODO support more than one shadow...
    xf_node_h shadow_node = add_shadow_pass ( graph, shadow_texture );

    // view setup
    xf_node_h view_setup_node = xf->create_node ( graph, &xf_node_params_m (
        .execute_routine = view_setup_pass,
        .debug_name = "view_setup",
    ) );

    // gbuffer laydown
    // TODO remove multi?
    xf_texture_h color_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_unorm_m,
            .debug_name = "color_texture",
        ),
        .multi_texture_count = 1,
    ) );

    xf_texture_h normal_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h object_id_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
        ),
    ) );
    xf_texture_h prev_object_id_texture = xf->get_multi_texture ( object_id_texture, -1 );

    xf_texture_h depth_stencil_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,//xg_format_d24_unorm_s8_uint_m,
            .debug_name = "depth_stencil_texture",
        ),
    ) );

    //xf_node_h geometry_clear_node = add_geometry_clear_node ( graph, color_texture, normal_texture, object_id_texture, depth_stencil_texture );
    xf_node_h geometry_clear_node = add_simple_clear_pass ( graph, "geometry_clear", &simple_clear_pass_params_m (
        .color_textures_count = 3,
        .color_textures = { color_texture, normal_texture, object_id_texture },
        .color_clears = { xg_color_clear_m(), xg_color_clear_m(), xg_color_clear_m() },
        .depth_stencil_textures_count = 1,
        .depth_stencil_textures = { depth_stencil_texture },
        .depth_stencil_clears = { xg_depth_stencil_clear_m ( .depth = xg_depth_clear_regular_m ) }
    ) );

    xf_node_h geometry_pass = add_geometry_node ( graph, color_texture, normal_texture, object_id_texture, depth_stencil_texture );

    // lighting
    xf_texture_h lighting_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "lighting_texture",
    ) );
    // todo remove extra normal texture param
    xf_node_h lighting_node = add_lighting_pass ( graph, lighting_texture, color_texture, normal_texture, normal_texture, depth_stencil_texture, shadow_texture );

    // hi-z
    uint32_t hiz_mip_count = 8;
    std_assert_m ( resolution_x % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    std_assert_m ( resolution_y % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    xf_texture_h hiz_texture = xf->declare_texture ( &xf_texture_params_m (
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
    xf_texture_h ssgi_raymarch_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_raymarch_texture",
        ),
    ) );
    xf_node_h ssgi_raymarch_node = add_ssgi_raymarch_pass ( graph, "ssgi", ssgi_raymarch_texture, normal_texture, color_texture, lighting_texture, hiz_texture );

    // ssgi blur
    xf_texture_h ssgi_blur_x_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_x_texture",
    ) );
    xf_node_h ssgi_blur_x_node = add_bilateral_blur_pass ( graph, ssgi_blur_x_texture, ssgi_raymarch_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_blur_x" );

    xf_texture_h ssgi_blur_y_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_y_texture",
    ) );
    xf_node_h ssgi_blur_y_node = add_bilateral_blur_pass ( graph, ssgi_blur_y_texture, ssgi_blur_x_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_blur_y" );

    // ssgi temporal accumulation
    xf_texture_h ssgi_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_accumulation_texture",
        ),
    ) );
    xf_texture_h prev_depth_stencil_texture = xf->get_multi_texture ( depth_stencil_texture, -1 );
    xg_texture_h ssgi_history_texture = xf->get_multi_texture ( ssgi_accumulation_texture, -1 );
    xf_node_h ssgi_temporal_accumulation_node = add_simple_screen_pass ( graph, "ssgi_ta", &simple_screen_pass_params_m (
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ta" ) ),
        .render_targets_count = 1,
        .render_targets = { ssgi_accumulation_texture },
        .texture_reads_count = 4,
        .texture_reads = { ssgi_blur_y_texture, ssgi_history_texture, depth_stencil_texture, prev_depth_stencil_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .shader_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_alias_m,
                    .alias = ssgi_blur_y_texture
                )
            }
        )
    ) );

    // ssgi2
    xf_texture_h ssgi_2_raymarch_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_2_raymarch_texture",
        ),
    ) );
    xf_node_h ssgi_2_raymarch_node = add_ssgi_raymarch_pass ( graph, "ssgi_2", ssgi_2_raymarch_texture, normal_texture, color_texture, ssgi_raymarch_texture, hiz_texture );

    // ssgi2 blur
    xf_texture_h ssgi_2_blur_x_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_x_texture",
    ) );
    xf_node_h ssgi_2_blur_x_node = add_bilateral_blur_pass ( graph, ssgi_2_blur_x_texture, ssgi_2_raymarch_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_2_blur_x" );

    xf_texture_h ssgi_2_blur_y_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_y_texture",
    ) );
    xf_node_h ssgi_2_blur_y_node = add_bilateral_blur_pass ( graph, ssgi_2_blur_y_texture, ssgi_2_blur_x_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_2_blur_y" );

    // ssgi2 temporal accumulation
    xf_texture_h ssgi_2_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_2_accumulation_texture",
        ),
    ) );
    xg_texture_h ssgi_2_history_texture = xf->get_multi_texture ( ssgi_2_accumulation_texture, -1 );
    xf_node_h ssgi_2_temporal_accumulation_node = add_simple_screen_pass ( graph, "ssgi_2_ta", &simple_screen_pass_params_m (
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ta" ) ),
        .render_targets_count = 1,
        .render_targets = { ssgi_2_accumulation_texture },
        .texture_reads_count = 4,
        .texture_reads = { ssgi_2_blur_y_texture, ssgi_2_history_texture, depth_stencil_texture, prev_depth_stencil_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .shader_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_alias_m,
                    .alias = ssgi_2_blur_y_texture
                )
            }
        )
    ) );

    // ssr
    xf_texture_h ssr_raymarch_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_raymarch_texture",
    ) );
    xf_node_h ssr_trace_node = add_ssr_raymarch_pass ( graph, ssr_raymarch_texture, normal_texture, lighting_texture, hiz_texture );

    // ssr blur
    xf_texture_h ssr_blur_x_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_y_texture",
    ) );
    xf_node_h ssr_blur_x_node = add_bilateral_blur_pass ( graph, ssr_blur_x_texture, ssr_raymarch_texture, normal_texture, depth_stencil_texture, 1, 5, blur_pass_direction_horizontal_m, "ssr_blur_x" );

    xf_texture_h ssr_blur_y_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format  = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_y_texture",
    ) );
    xf_node_h ssr_blur_y_node = add_bilateral_blur_pass ( graph, ssr_blur_y_texture, ssr_blur_x_texture, normal_texture, depth_stencil_texture, 1, 5, blur_pass_direction_vertical_m, "ssr_blur_y" );

    // ssr ta
    xf_texture_h ssr_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssr_accumulation_texture",
        ),
    ) );
    xf_texture_h ssr_history_texture = xf->get_multi_texture ( ssr_accumulation_texture, -1 );
    xf_node_h ssr_ta_node = add_simple_screen_pass ( graph, "ssr_ta", &simple_screen_pass_params_m (
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "ssr_ta" ) ),
        .render_targets_count = 1,
        .render_targets = { ssr_accumulation_texture },
        .texture_reads_count = 7,
        .texture_reads = { ssr_blur_y_texture, ssr_history_texture, depth_stencil_texture, prev_depth_stencil_texture, normal_texture, object_id_texture, prev_object_id_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .shader_texture_writes = { 
                xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_alias_m,
                    .alias = ssr_blur_y_texture,
                )
            }
        )
    ) );

    // combine
    xf_texture_h combine_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "combine_texture",
    ) );
    xf_node_h combine_node = add_simple_screen_pass ( graph, "combine", &simple_screen_pass_params_m (
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "combine" ) ),
        .render_targets_count = 1,
        .render_targets = { combine_texture },
        .texture_reads_count = 4,
        .texture_reads = { lighting_texture, ssr_accumulation_texture, ssgi_accumulation_texture, ssgi_2_accumulation_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
    ) );

    // taa
    xf_texture_h taa_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "taa_accumulation_texture",
        ),
    ) );
    xg_texture_h taa_history_texture = xf->get_multi_texture ( taa_accumulation_texture, -1 );
    xf_node_h taa_node = add_simple_screen_pass ( graph, "taa", &simple_screen_pass_params_m (
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "taa" ) ),
        .render_targets_count = 1,
        .render_targets = { taa_accumulation_texture },
        .texture_reads_count = 4,
        .texture_reads = { combine_texture, depth_stencil_texture, taa_history_texture, object_id_texture },
        .samplers_count = 2,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .shader_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_alias_m,
                    .alias = combine_texture
                )
            }
        )
        ) );
    m_state->render.taa_node = taa_node;

    // tonemap
    xf_texture_h tonemap_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "tonemap_texture",
    ) );
    xf_node_h tonemap_node = add_simple_screen_pass ( graph, "tonemap", &simple_screen_pass_params_m (
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "tonemap" ) ),
        .render_targets_count = 1,
        .render_targets = { tonemap_texture },
        .texture_reads_count = 1,
        .texture_reads = { taa_accumulation_texture }, // taa_accumulation_texture
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .shader_texture_writes = { xf_texture_passthrough_m (
                    .mode = xf_passthrough_mode_copy_m,
                    .copy_source = xf_copy_texture_dependency_m ( taa_accumulation_texture, xg_default_texture_view_m ),
                )
            },
        ),
    ) );

    // ui
    xf_node_h ui_node = add_ui_pass ( graph, tonemap_texture );

    // present
    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );
    xf_node_h present_pass = xf->create_node ( graph, &xf_node_params_m (
        .copy_texture_writes_count = 1,
        .copy_texture_writes = { xf_copy_texture_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m ) },
        .copy_texture_reads_count = 1,
        .copy_texture_reads = { xf_copy_texture_dependency_m ( tonemap_texture, xg_default_texture_view_m ) },
        .presentable_texture = swapchain_multi_texture,
        .execute_routine = copy_pass,
        .debug_name = "present",
    ) );

    xf->debug_print_graph ( graph );
}

#if 1
static void viewapp_boot_build_raytrace_world ( void ) {
    xg_device_h device = m_state->render.device;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    se_i* se = m_state->modules.se;

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_component_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_component_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xg_raytrace_geometry_instance_t* rt_instances = std_virtual_heap_alloc_array_m ( xg_raytrace_geometry_instance_t, mesh_count );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_component_iterator_next ( &mesh_iterator );
        
        se_entity_h* entity_handle = se_component_iterator_next ( &entity_iterator );
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
            .geometry = rt_geo,
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
}
#endif

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

    //std_virtual_stack_t stack = std_virtual_stack_create ( 1024 * 32 );
    //se_entity_params_allocator_t allocator = se_entity_params_allocator ( &stack );

    // sphere
    {
        //se->set_entity_name ( sphere, "sphere" );
        //se_entity_params_alloc_entity ( &allocator, sphere );
        
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
        
        //se_entity_params_alloc_monostream_component_inline_m ( &allocator, viewapp_mesh_component_id_m, &mesh_component );
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
        //se_entity_h plane = se->create_entity();
        //se->set_entity_name ( plane, "plane" );
        //se_entity_params_alloc_entity ( &allocator, plane );

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

        //se_entity_params_alloc_monostream_component_inline_m ( &allocator, viewapp_mesh_component_id_m, &mesh_component );
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
            .position = { 0, 1.45, 0 },
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
            .position = { 0, 1.45, 0 },
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
                .near_z = 0.1,
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

        //se_entity_params_alloc_monostream_component_inline_m ( &allocator, viewapp_light_component_id_m, &light_component );
    }

    //se->init_entities ( allocator.entities );
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
        //se->add_component ( entity, viewapp_mesh_component_id_m, ( se_component_h ) mesh_component );

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

#if 1
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
        .count = 0,
        .properties = {
        }
    ) );
#endif

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
            .minimized = true,
            .x = 50,
            .y = 100,
            .width = 350,
            .height = 500,
            .padding_x = 10,
            .padding_y = 2,
            .style = xi_style_m (
                .font = m_state->ui.font
            )
        );

        m_state->ui.frame_section_state = xi_section_state_m ( .title = "frame" );
        m_state->ui.xg_alloc_section_state = xi_section_state_m ( .title = "xg allocator" );
        m_state->ui.xf_graph_section_state = xi_section_state_m ( .title = "xf graph" );
        m_state->ui.entities_section_state = xi_section_state_m ( .title = "entities" );
    }

    rv_i* rv = m_state->modules.rv;
    {
        viewapp_camera_component_t camera_component;

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
        camera_component.view = rv->create_view ( &view_params );

        // camera
        //se_entity_h camera_entity = se->create_entity();
        //se->set_entity_name ( camera_entity, "camera" );

        //std_virtual_stack_t stack = std_virtual_stack_create ( 1024 * 32 );
        //se_entity_params_allocator_t allocator = se_entity_params_allocator ( &stack );
        //se_entity_params_alloc_entity ( &allocator, camera_entity );
        //se_entity_params_alloc_monostream_component_inline_m ( &allocator, viewapp_camera_component_id_m, &camera_component );

        //se->init_entities ( allocator.entities );
        se->create_entity ( &se_entity_params_m (
            .debug_name = "camera",
            .update = se_entity_update_m (
                .components = { se_component_update_m (
                    .id = viewapp_camera_component_id_m,
                    .streams = { se_stream_update_m ( .data = &camera_component ) }
                ) }
            )
        ) );
    }

    //viewapp_boot_scene_cornell_box();
    viewapp_boot_scene_field();
    //viewapp_boot_build_raytrace_world();

    viewapp_boot_raster_graph();
    //viewapp_boot_raytrace_graph();
}

static void viewapp_update_cameras ( wm_input_state_t* input_state, wm_input_state_t* new_input_state ) {
    se_i* se = m_state->modules.se;
    xi_i* xi = m_state->modules.xi;
    rv_i* rv = m_state->modules.rv;

    if ( xi->get_active_element_id() != 0 ) {
        return;
    }

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );

    se_component_iterator_t camera_iterator = se_component_iterator_m ( &camera_query_result.components[0], 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_component_iterator_next ( &camera_iterator );

        rv->update_prev_frame_data ( camera_component->view );
        rv->update_proj_jitter ( camera_component->view, m_state->render.frame_id );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, camera_component->view );
        rv_view_transform_t xform = view_info.transform;
        bool dirty_xform = false;

        if ( new_input_state->mouse[wm_mouse_state_left_m] ) {
            float drag_scale = -1.f / 400;
            sm_vec_3f_t v;
            v.x = xform.position[0] - xform.focus_point[0];
            v.y = xform.position[1] - xform.focus_point[1];
            v.z = xform.position[2] - xform.focus_point[2];

            int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
            int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

            if ( delta_x != 0 ) {
                sm_vec_3f_t up = sm_vec_3f ( 0, 1, 0 );
                sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( up, delta_x * drag_scale );

                v = sm_matrix_4x4f_transform_f3 ( mat, v );
            }

            if ( delta_y != 0 ) {
                sm_vec_3f_t up = sm_vec_3f ( 0, 1, 0 );
                sm_vec_3f_t axis = sm_vec_3f_cross ( up, v );
                axis = sm_vec_3f_norm ( axis );

                sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( axis, -delta_y * drag_scale );
                v = sm_matrix_4x4f_transform_f3 ( mat, v );
            }

            if ( delta_x != 0 || delta_y != 0 ) {
                xform.position[0] = view_info.transform.focus_point[0] + v.x;
                xform.position[1] = view_info.transform.focus_point[1] + v.y;
                xform.position[2] = view_info.transform.focus_point[2] + v.z;

                dirty_xform = true;
            }
        }

        if ( xi->get_hovered_element_id() == 0 ) {
            if ( new_input_state->mouse[wm_mouse_state_wheel_up_m] || new_input_state->mouse[wm_mouse_state_wheel_down_m] ) {
                int8_t wheel = ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_up_m] - ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_down_m];
                float zoom_step = -0.1;
                float zoom_min = 0.001;

                sm_vec_3f_t v;
                v.x = xform.position[0] - xform.focus_point[0];
                v.y = xform.position[1] - xform.focus_point[1];
                v.z = xform.position[2] - xform.focus_point[2];

                float dist = sm_vec_3f_len ( v );
                float new_dist = fmaxf ( zoom_min, dist + ( zoom_step * wheel ) * dist );
                v = sm_vec_3f_mul ( v, new_dist / dist );

                xform.position[0] = view_info.transform.focus_point[0] + v.x;
                xform.position[1] = view_info.transform.focus_point[1] + v.y;
                xform.position[2] = view_info.transform.focus_point[2] + v.z;

                dirty_xform = true;
            }
        }

        if ( dirty_xform ) {
            rv->update_view_transform ( camera_component->view, &xform );
        }
    }
}

static void viewapp_update_ui ( wm_window_info_t* window_info, wm_input_state_t* input_state ) {
    wm_i* wm = m_state->modules.wm;
    xg_i* xg = m_state->modules.xg;
    xf_i* xf = m_state->modules.xf;
    xi_i* xi = m_state->modules.xi;
    wm_window_h window = m_state->render.window;

    xi_workload_h xi_workload = xi->create_workload();
    set_ui_pass_xi_workload ( xi_workload );

    wm_input_buffer_t input_buffer;
    wm->get_window_input_buffer ( window, &input_buffer );
    
    xi->begin_update ( window_info, input_state, &input_buffer );
    xi->begin_window ( xi_workload, &m_state->ui.window_state );
    
    // frame
    xi->begin_section ( xi_workload, &m_state->ui.frame_section_state );
    {
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
    xf->debug_ui_graph ( xi, xi_workload, m_state->render.graph );
    if ( xi->add_button ( xi_workload, &xi_button_state_m ( 
        .text = "Disable all",
        .height = 20, 
        .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m  
    ) ) ) {
        xf_graph_info_t info;
        xf->get_graph_info ( &info, m_state->render.graph );

        for ( uint32_t i = 0; i < info.node_count; ++i ) {
            xf->disable_node ( info.nodes[i] );
        }
    }
    if ( xi->add_button ( xi_workload, &xi_button_state_m ( 
        .text = "Enable all", 
        .height = 20,
        .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m  
    ) ) ) {
        xf_graph_info_t info;
        xf->get_graph_info ( &info, m_state->render.graph );

        for ( uint32_t i = 0; i < info.node_count; ++i ) {
            xf->enable_node ( info.nodes[i] );
        }
    }
    xi->end_section ( xi_workload );

    // se entities
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
                    xi_property_editor_state_t property_editor_state = xi_property_editor_state_m ( 
                        .type = ( xi_property_e ) property->type,
                        .data = component_data + property->offset,
                        .property_width = 64,
                        .id = ( ( ( uint64_t ) i ) << 32 ) + ( ( ( uint64_t ) j ) << 16 ) + ( ( uint64_t ) k ),
                        .style = xi_style_m ( .horizontal_alignment = xi_horizontal_alignment_right_to_left_m ),
                    );
                    xi->add_property_editor ( xi_workload, &property_editor_state );
                    xi->newline();
                }
            }
        }
    }
    xi->end_section ( xi_workload );
    
    xi->end_window ( xi_workload );
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

    float target_fps = 60.f;
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
    }

    if ( !input_state->keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
        return std_app_state_reload_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f3_m] && new_input_state.keyboard[wm_keyboard_state_f3_m] ) {
        m_state->render.capture_frame = true;
    }

    m_state->render.frame_id += 1;

    viewapp_update_cameras ( input_state, &new_input_state );

    wm_window_info_t new_window_info;
    wm->get_window_info ( window, &new_window_info );

    viewapp_update_ui ( &new_window_info, &new_input_state );

    m_state->render.window_info = new_window_info;
    m_state->render.input_state = new_input_state;

    xg_workload_h workload = xg->create_workload ( m_state->render.device );

    xf->execute_graph ( m_state->render.graph, workload );
    xg->submit_workload ( workload );
    xg->present_swapchain ( m_state->render.swapchain, workload );

    xs->update_pipeline_states ( workload );

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

    state->modules.tk = std_module_load_m ( tk_module_name_m );
    state->modules.fs = std_module_load_m ( fs_module_name_m );
    state->modules.wm = std_module_load_m ( wm_module_name_m );
    state->modules.xg = std_module_load_m ( xg_module_name_m );
    state->modules.xs = std_module_load_m ( xs_module_name_m );
    state->modules.xf = std_module_load_m ( xf_module_name_m );
    state->modules.se = std_module_load_m ( se_module_name_m );
    state->modules.rv = std_module_load_m ( rv_module_name_m );
    state->modules.xi = std_module_load_m ( xi_module_name_m );

    std_mem_zero_m ( &state->render );
    state->render.frame_id = 0;

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

    std_log_info_m ( "viewapp unloaded" );
}

void viewer_app_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );

    std_auto_m state = ( viewapp_state_t* ) api;
    state->api.tick = viewapp_tick;
    m_state = state;

    viewapp_state_bind ( state );
}
