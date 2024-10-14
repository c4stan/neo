#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <xs.h>
#include <xf.h>
#include <fs.h>

#include <math.h>

std_warnings_ignore_m ( "-Wunused-function" )

#if 0
static void xf_clear_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );
    xg_texture_h texture = node_args->io->copy_texture_writes[0].texture;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    // TODO pass this with node_args?
    //      it is possible that the right solution is to have separate APIs in xg for command buffer recording vs the rest,
    //      and here is passed the cmd buffer API instead of the whole xg api
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    {
        xg_color_clear_t color_clear;
        uint64_t t = 500;
        color_clear.f32[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        color_clear.f32[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
        color_clear.f32[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        xg->cmd_clear_texture ( cmd_buffer, texture, color_clear, key++ );
    }
}
#endif

typedef struct {
    xs_database_pipeline_h pipeline_state;
} xf_triangle_pass_args_t;
static void xf_triangle_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    xf_triangle_pass_args_t* args = ( xf_triangle_pass_args_t* ) user_args;
    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( args->pipeline_state );

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // Bind swapchain texture as render target
    {
        xg_color_clear_t color_clear;
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        xg_render_textures_binding_t render_textures;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        render_textures.depth_stencil.texture = xg_null_handle_m;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    // Set pipeline
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = 400,
        .height = 200,
    );
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    // Draw
    xg->cmd_draw ( cmd_buffer, 3, 0, key );
}

#if 0
static void copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
            .source = xf_copy_texture_resource_m ( node_args->io->copy_texture_reads[0] ),
            .destination = xf_copy_texture_resource_m ( node_args->io->copy_texture_writes[0] ),
            .filter = xg_sampler_filter_point_m,
        );

        xg->cmd_copy_texture ( cmd_buffer, &copy_params, key );
    }
}
#endif

static void xf_test ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    std_assert_m ( wm );

    wm_window_params_t window_params = { .name = "xf_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
    wm_window_h window = wm->create_window ( &window_params );
    std_log_info_m ( "Creating window "std_fmt_str_m std_fmt_newline_m, window_params.name );

    xg_device_h device;
    xg_swapchain_h swapchain;
    xg_i* xg = std_module_load_m ( xg_module_name_m );
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

        xg_swapchain_window_params_t swapchain_params = xg_swapchain_window_params_m (
            .window = window,
            .device = device,
            .texture_count = 3,
            .format = xg_format_b8g8r8a8_unorm_m,//xg_format_b8g8r8a8_srgb_m;
            .color_space = xg_colorspace_srgb_m,
            .present_mode = xg_present_mode_fifo_m,
            .debug_name = "swapchain",
        );
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    std_module_load_m ( fs_module_name_m );

    xs_i* xs = std_module_load_m ( xs_module_name_m );
    xs_database_h sdb = xs->create_database ( &xs_database_params_m (
        .device = device,
        std_debug_string_assign_m ( .debug_name, "shader_db" )
    ) );
    xs->add_database_folder ( sdb, "shader/" );
    xs->set_output_folder ( sdb, "output/shader/" );
    xs->build_database ( sdb );
    xs_database_pipeline_h pipeline_state = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "test_state" ) );

    xf_i* xf = std_module_load_m ( xf_module_name_m );
    xf_graph_h graph = xf->create_graph ( device, swapchain );

    xf_texture_h color_texture;
    {
        xf_texture_params_t color_texture_params = xf_texture_params_m (
            .width = 400,
            .height = 200,
            .format = xg_format_r8g8b8a8_unorm_m,
            .debug_name = "color_texture",
        );
        color_texture = xf->declare_texture ( &color_texture_params );
    }

    xf->add_node ( graph, &xf_node_params_m ( 
        .debug_name = "clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { xf_texture_clear_m ( .color = xg_color_clear_m ( .f32 = { 0, 0, 1, 1 } ) ) }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = color_texture ) },
        ),
    ) );

    xf_triangle_pass_args_t triangle_pass_args;
    triangle_pass_args.pipeline_state = pipeline_state;
    xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "triangle",
        .debug_color = xg_debug_region_color_green_m,
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = xf_triangle_pass,
            .user_args = std_buffer_m ( &triangle_pass_args ),
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = color_texture ) },            
        ),
    ) );

    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );
    xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "present",
        .type = xf_node_type_copy_pass_m,
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
            .copy_texture_reads_count = 1,
            .copy_texture_reads = { xf_copy_texture_dependency_m ( .texture = color_texture ) },
            .presentable_texture = swapchain_multi_texture,
        ),
    ) );

    xf->debug_print_graph ( graph );

    wm_window_info_t window_info;
    wm->get_window_info ( window, &window_info );

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

        wm_input_state_t input_state;
        wm->get_window_input_state ( window, &input_state );

        if ( input_state.keyboard[wm_keyboard_state_esc_m] ) {
            break;
        }

        if ( input_state.keyboard[wm_keyboard_state_f1_m] ) {
            xs->rebuild_databases();
        }

        wm_window_info_t new_window_info;
        wm->get_window_info ( window, &new_window_info );

        if ( window_info.width != new_window_info.width || window_info.height != new_window_info.height ) {
            // TODO change xf to support the resize without having to call any of this manually 
            //xg->resize_swapchain ( swapchain, new_window_info.width, new_window_info.height );
            //xf->resize_swapchain ( swapchain_multi_texture );
        }

        window_info = new_window_info;

        xg_workload_h workload = xg->create_workload ( device );

        //xg->acquire_next_swapchain_texture ( swapchain, workload );

        xf->execute_graph ( graph, workload );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );

        xs->update_pipeline_states ( workload );

        //xf->advance_multi_texture ( swapchain_multi_texture );
    }

    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
}

void std_main ( void ) {
    xf_test();
    std_log_info_m ( "xf_test_m COMPLETE!" );
}
