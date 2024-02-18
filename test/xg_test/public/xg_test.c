#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <xg.h>

#include <math.h>

std_warnings_ignore_m ( "-Wunused-function" )

static void xg_test2_frame ( xg_device_h device, xg_swapchain_h swapchain, bool capture ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_workload_h workload = xg->create_workload ( device );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xg->acquire_next_swapchain_texture ( swapchain, workload );

    xg_texture_h texture = xg->get_swapchain_texture ( swapchain );

    if ( capture ) {
        xg->cmd_start_debug_capture ( cmd_buffer, xg_debug_capture_stop_time_workload_present_m, 0 );
    }

    xg->cmd_begin_debug_region ( cmd_buffer, "Test region", xg_debug_region_color_yellow_m, 0 );

    {
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = texture;
        texture_barrier.layout.old = xg_texture_layout_undefined_m;
        texture_barrier.layout.new = xg_texture_layout_copy_dest_m;
        texture_barrier.memory.flushes = xg_memory_access_none_m;
        texture_barrier.memory.invalidations = xg_memory_access_transfer_write_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_transfer_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_transfer_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    {
        xg_color_clear_t color_clear;
        uint64_t t = 500;
        color_clear.f32[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        color_clear.f32[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
        color_clear.f32[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        xg->cmd_clear_texture ( cmd_buffer, texture, color_clear, 2 );
    }

    {
        xg_color_clear_t color_clear;
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        xg->cmd_clear_texture ( cmd_buffer, texture, color_clear, 1 );
    }

    {
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = texture;
        texture_barrier.layout.old = xg_texture_layout_copy_dest_m;
        texture_barrier.layout.new = xg_texture_layout_present_m;
        texture_barrier.memory.flushes = xg_memory_access_transfer_write_m;
        texture_barrier.memory.invalidations = xg_memory_access_none_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_transfer_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_bottom_of_pipe_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 3 );
    }

    xg_texture_h temp_texture = xg->create_texture ( &xg_texture_params_m (
        .allocator = xg->get_default_allocator ( device, xg_memory_type_gpu_only_m ),
        .device = device,
        .width = 600,
        .height = 400,
        .format = xg_format_b8g8r8a8_unorm_m,
        .debug_name = "temp_texture",
        .allowed_usage = xg_texture_usage_render_target_m | xg_texture_usage_copy_dest_m,
    ) );

    {
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = temp_texture;
        texture_barrier.layout.old = xg_texture_layout_undefined_m;
        texture_barrier.layout.new = xg_texture_layout_copy_dest_m;
        texture_barrier.memory.flushes = xg_memory_access_none_m;
        texture_barrier.memory.invalidations = xg_memory_access_transfer_write_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_transfer_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_transfer_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    {
        xg_color_clear_t color_clear;
        uint64_t t = 500;
        color_clear.f32[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        color_clear.f32[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
        color_clear.f32[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        xg->cmd_clear_texture ( cmd_buffer, temp_texture, color_clear, 2 );
    }

    {
        xg_color_clear_t cbuffer_data;
        cbuffer_data.f32[0] = 0.0;
        cbuffer_data.f32[1] = 0.0;
        cbuffer_data.f32[2] = 1.0;
        xg_buffer_range_t cbuffer_range;
        cbuffer_range = xg->write_workload_uniform ( workload, &cbuffer_data, sizeof ( cbuffer_data ) );
    }

    xg->cmd_end_debug_region ( cmd_buffer, 4 );

    xg->cmd_destroy_texture ( resource_cmd_buffer, temp_texture, xg_resource_cmd_buffer_time_workload_complete_m );

    //xg->close_cmd_buffers ( &cmd_buffer, 1 );
    xg->submit_workload ( workload );
    xg->present_swapchain ( swapchain, workload );
}

static void xg_test2_run ( void ) {
    wm_i* wm = std_module_get_m ( wm_module_name_m );
    wm_window_params_t window_params = wm_window_params_m (
        .name = "xg_test",
        .x = 0,
        .y = 0,
        .width = 600,
        .height = 400,
        .gain_focus = true,
        .borderless = false,
    );
    wm_window_h window = wm->create_window ( &window_params );

    xg_device_h device;
    xg_swapchain_h swapchain;
    xg_i* xg = std_module_get_m ( xg_module_name_m );
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

        xg_swapchain_window_params_t swapchain_params;
        swapchain_params.window = window;
        swapchain_params.device = device;
        swapchain_params.texture_count = 3;
        swapchain_params.format = xg_format_b8g8r8a8_unorm_m;//xg_format_b8g8r8a8_srgb_m;
        swapchain_params.color_space = xg_colorspace_srgb_m;
        swapchain_params.present_mode = xg_present_mode_mailbox_m;
        swapchain_params.debug_name = "swapchain";
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    wm_window_info_t info = {0};
    wm->get_window_info ( window, &info );

    std_log_info_m ( "Press F1 to reload modules" );
    std_log_info_m ( "Press F2 to queue a frame capture" );

    wm_input_state_t input_state;
    wm->get_window_input_state ( window, &input_state );

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

        wm_window_info_t new_info;
        wm->get_window_info ( window, &new_info );

        if ( !std_mem_cmp ( &info, &new_info, sizeof ( wm_window_info_t ) ) ) {
            if ( info.width != new_info.width || info.height != new_info.height ) {
                xg->resize_swapchain ( swapchain, new_info.width, new_info.height );
            }

            info = new_info;
        }

        bool capture = false;

        wm_input_state_t new_input_state;
        wm->get_window_input_state ( window, &new_input_state );

        if ( !input_state.keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
            std_module_reload_m();
        }

        if ( !input_state.keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
            capture = true;
        }

        if ( input_state.keyboard[wm_keyboard_state_esc_m] ) {
            break;
        }

        input_state = new_input_state;

        xg_test2_frame ( device, swapchain, capture );
    }
}

#if std_enabled_m(xg_debug_simple_frame_test_m)
static void xg_test1_run ( void ) {
    wm_i* wm = std_module_get_m ( wm_module_name_m );
    wm_window_params_t window_params;
    window_params.name = "xg_test";
    window_params.x = 0;
    window_params.y = 0;
    window_params.width = 600;
    window_params.height = 400;
    window_params.gain_focus = true;
    window_params.borderless = false;
    wm_window_h window = wm->create_window ( &window_params );

    xg_device_h device;
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        size_t device_count = xg->get_devices_count();
        std_assert_m ( device_count > 0 );
        xg_device_h devices[16];
        xg->get_devices ( devices, 16 );
        device = devices[0];
        bool activate_result = xg->activate_device ( device );
        std_assert_m ( activate_result );
        xg_device_info_t device_info;
        xg->get_device_info ( device, &device_info );
        std_log_info_m ( "Picking device 0 (" std_fmt_str_m ") as default device", device_info.name );
    }

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

        xg->debug_simple_frame ( window, device );
    }
}
#endif

void std_main ( void ) {
    xg_test2_run();
    std_log_info_m ( "XG_TEST COMPLETE!" );
}
