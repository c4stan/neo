#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <xg.h>

#include <math.h>

static void xg_test_frame ( xg_device_h device, xg_swapchain_h swapchain, bool capture ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_workload_h workload = xg->create_workload ( device );
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xg_swapchain_acquire_result_t acquire;
    xg->acquire_swapchain ( &acquire, swapchain, workload );
    xg_texture_h texture = acquire.texture;

    if ( capture ) {
        xg->debug_capture_workload ( workload );
    }

    xg_queue_event_h event = xg->create_queue_event ( &xg_queue_event_params_m ( .device = device ) );
    xg->cmd_destroy_queue_event ( resource_cmd_buffer, event, xg_resource_cmd_buffer_time_workload_complete_m );

    xg->cmd_bind_queue ( cmd_buffer, 0, &xg_cmd_bind_queue_params_m (
        .queue = xg_cmd_queue_graphics_m,
        .signal_events =  { event },
        .signal_count = 1,
    ) );

    uint64_t key = 0;

    xg->cmd_begin_debug_region ( cmd_buffer, key, "Test region", xg_debug_region_color_green_m );

    xg->cmd_barrier_set ( cmd_buffer, key, &xg_barrier_set_m (
        .texture_memory_barriers_count = 1,
        .texture_memory_barriers = &xg_texture_memory_barrier_m (
            .texture = texture,
            .layout.old = xg_texture_layout_undefined_m,
            .layout.new = xg_texture_layout_copy_dest_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_transfer_write_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        ),
    ) );

    xg_color_clear_t color_clear;
    const uint64_t t = 500;
    uint64_t tick = std_tick_now();
    color_clear.f32[0] = ( sinf ( std_tick_to_milli_f32 ( tick / t ) ) + 1 ) / 2.f;
    color_clear.f32[1] = ( sinf ( std_tick_to_milli_f32 ( tick / t ) + 3.14f ) + 1 ) / 2.f;
    color_clear.f32[2] = ( cosf ( std_tick_to_milli_f32 ( tick / t ) ) + 1 ) / 2.f;
    xg->cmd_clear_texture ( cmd_buffer, key + 2, texture, color_clear );

    xg->cmd_clear_texture ( cmd_buffer, key + 1, texture, xg_color_clear_m (
        .f32[0] = 0,
        .f32[1] = 0,
        .f32[2] = 0,
    ) );

    key += 2;

    xg->cmd_barrier_set ( cmd_buffer, key, &xg_barrier_set_m(
        .texture_memory_barriers_count = 1,
        .texture_memory_barriers = &xg_texture_memory_barrier_m (
            .texture = texture,
            .layout.old = xg_texture_layout_copy_dest_m,
            .layout.new = xg_texture_layout_present_m,
            .memory.flushes = xg_memory_access_bit_transfer_write_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_bottom_of_pipe_m,
        ),
    ) );

    xg->cmd_end_debug_region ( cmd_buffer, key );

    xg->submit_workload ( workload );
    xg->present_swapchain ( swapchain, workload );
}

static void xg_test_run ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    wm_window_h window = wm->create_window ( &wm_window_params_m (
        .name = "xg_test",
        .width = 600,
        .height = 400,
        .gain_focus = true,
    ) );

    xg_i* xg = std_module_load_m ( xg_module_name_m );
    xg_device_h devices[16];
    size_t device_count = xg->get_devices ( devices, 16 );
    std_assert_m ( device_count > 0 );
    xg_device_h device = devices[0];
    std_verify_m ( xg->activate_device ( device ) );
    xg_device_info_t device_info;
    xg->get_device_info ( &device_info, device );
    std_log_info_m ( "Picking device 0 (" std_fmt_str_m ") as default device", device_info.name );

    xg_swapchain_h swapchain = xg->create_window_swapchain ( &xg_swapchain_window_params_m (
        .window = window,
        .device = device,
        .texture_count = 3,
        .format = xg_format_b8g8r8a8_unorm_m,
        .color_space = xg_colorspace_srgb_m,
        .present_mode = xg_present_mode_fifo_m,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m,
        .debug_name = "swapchain",
    ) );

    wm_input_state_t input_state;
    wm->get_window_input_state ( window, &input_state );

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

        bool capture = false;

        wm_input_state_t new_input_state;
        wm->get_window_input_state ( window, &new_input_state );

        if ( !input_state.keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
            capture = true;
        }

        if ( input_state.keyboard[wm_keyboard_state_esc_m] ) {
            break;
        }

        input_state = new_input_state;

        xg_test_frame ( device, swapchain, capture );
    }

    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
}

void std_main ( void ) {
    xg_test_run();
    std_log_info_m ( "XG_TEST COMPLETE!" );
}
