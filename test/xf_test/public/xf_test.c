#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <xs.h>
#include <xf.h>

#include <math.h>

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

    xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
        .pipeline = pipeline_state,
        .primitive_count = 1,
    ) );
}

static void xf_test ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    std_assert_m ( wm );

    uint32_t resolution_x = 600;
    uint32_t resolution_y = 400;

    wm_window_params_t window_params = { .name = "xf_test", .x = 0, .y = 0, .width = resolution_x, .height = resolution_y, .gain_focus = true, .borderless = false };
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
            .allowed_usage = xg_texture_usage_bit_copy_dest_m,
            .debug_name = "swapchain",
        );
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    xs_i* xs = std_module_load_m ( xs_module_name_m );
    xs_database_h sdb = xs->create_database ( &xs_database_params_m (
        .device = device,
        .debug_name = "shader_db"
    ) );
    xs->add_database_folder ( sdb, "shader/" );
    xs->set_output_folder ( sdb, "output/shader/" );
    xs->build_database ( sdb );
    xs_database_pipeline_h pipeline_state = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "triangle" ) );
    xs_database_pipeline_h compute_pipeline_state = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "clear" ) );

    xf_i* xf = std_module_load_m ( xf_module_name_m );

    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain(swapchain);

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m ( 
        .device = device,
        .debug_name = "test_graph_1" 
    ) );

    xf_texture_h color_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = 600,
            .height = 400,
            .format = xg_format_r8g8b8a8_unorm_m,
            .debug_name = "color_texture",
        ),
        .multi_texture_count = 1,
        .auto_advance = false,
    ) );

    xf_texture_h depth_texture = xf->create_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = 600,
            .height = 400,
            .format = xg_format_d32_sfloat_m,
            .debug_name = "depth_texture",
        ),
        .multi_texture_count = 1,
        .auto_advance = false,
    ) );
    
    bool compute_clear = true;

    if ( compute_clear ) {
#if 1
        float color[3] = { 1, 0, 0 };

        xf->create_node ( graph, &xf_node_params_m ( 
            .debug_name = "clear1",
            .type = xf_node_type_clear_pass_m,
            .pass.clear = xf_node_clear_pass_params_m (
                .textures = { 
                    xf_texture_clear_m ( .color = xg_color_clear_m ( .f32 = { color[0], color[1], color[2], 1 } ) ),
                    xf_texture_clear_m ( .type = xf_texture_clear_depth_stencil_m, .depth_stencil = xg_depth_stencil_clear_m() ),
                },
            ),
            .resources = xf_node_resource_params_m (
                .copy_texture_writes_count = 2,
                .copy_texture_writes = { 
                    xf_copy_texture_dependency_m ( .texture = color_texture ), 
                    xf_copy_texture_dependency_m ( .texture = depth_texture ), 
                },
            ),
        ) );

        xf->create_node ( graph, &xf_node_params_m ( 
            .debug_name = "clear2",
            .type = xf_node_type_compute_pass_m,
            .queue = xg_cmd_queue_compute_m,
            .pass.compute = xf_node_compute_pass_params_m (
                .pipeline = xs->get_pipeline_state ( compute_pipeline_state ),
                .workgroup_count = { resolution_x, resolution_y, 1 },
            ),
            .resources = xf_node_resource_params_m (
                .storage_texture_writes_count = 1,
                .storage_texture_writes = { xf_compute_texture_dependency_m ( .texture = color_texture ) },
            ),
        ) );
#endif
    } else {
        float color[3] = { 0, 1, 0 };
        //uint64_t t = 500;
        //color[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        //color[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
        //color[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );

        xf->create_node ( graph, &xf_node_params_m ( 
            .debug_name = "clear",
            .type = xf_node_type_clear_pass_m,
            .pass.clear = xf_node_clear_pass_params_m (
                .textures = { xf_texture_clear_m ( .color = xg_color_clear_m ( .f32 = { color[0], color[1], color[2], 1 } ) ) }
            ),
            .resources = xf_node_resource_params_m (
                .copy_texture_writes_count = 1,
                .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = color_texture ) },
            ),
        ) );
    }

    xf_triangle_pass_args_t triangle_pass_args = {
        .pipeline_state = pipeline_state,
    };
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "triangle",
        .debug_color = xg_debug_region_color_green_m,
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = xf_triangle_pass,
            .user_args = std_buffer_struct_m ( &triangle_pass_args ),
            .auto_renderpass = true,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = color_texture ) },
            .depth_stencil_target = depth_texture,
        ),
    ) );

    xf->create_node ( graph, &xf_node_params_m (
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

        window_info = new_window_info;

        xg_workload_h workload = xg->create_workload ( device );

        uint64_t id = 0;
        id = xf->execute_graph ( graph, workload, id );
        //id = xf->execute_graph ( graph2, workload, id );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );

        xs->update_pipeline_states ( workload );
    }

    //xf->destroy_unreferenced_resources();

    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
}

void std_main ( void ) {
    xf_test();
    std_log_info_m ( "xf_test_m COMPLETE!" );
}
