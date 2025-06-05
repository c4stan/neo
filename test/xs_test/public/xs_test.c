#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <wm.h>
#include <xg.h>
#include <xs.h>

#include <math.h>

std_warnings_ignore_m ( "-Wunused-function" )

typedef struct {
    float color[3];
    float pad;
} draw_uniforms_t;

typedef struct {
    xg_device_h device;
    xg_workload_h workload;
    xg_swapchain_h swapchain;
    xg_compute_pipeline_state_h compute_pipeline;
    xg_graphics_pipeline_state_h graphics_pipeline;
    xg_renderpass_h renderpass;
} xs_test_frame_params_t;

static void xs_test_frame ( xs_test_frame_params_t frame ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( frame.workload );

    xg_swapchain_acquire_result_t acquire;
    xg->acquire_swapchain ( &acquire, frame.swapchain, frame.workload );
    xg_texture_h swapchain_texture = acquire.texture;

    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( frame.workload );

    xg_texture_info_t swapchain_texture_info;
    xg->get_texture_info ( &swapchain_texture_info, swapchain_texture );

    xg_device_info_t device_info;
    xg->get_device_info ( &device_info, frame.device );

    // create temp texture
    xg_texture_params_t params = xg_texture_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = frame.device,
        .width = swapchain_texture_info.width,
        .height = swapchain_texture_info.height,
        .format = swapchain_texture_info.format,
        .allowed_usage = xg_texture_usage_bit_copy_source_m | xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_storage_m,
        .debug_name = "temp_texture",
    );
    xg_texture_h temp_texture = xg->cmd_create_texture ( resource_cmd_buffer, &params, NULL );

    xg->cmd_destroy_texture ( resource_cmd_buffer, temp_texture, xg_resource_cmd_buffer_time_workload_complete_m );

    // create events
    xg_queue_event_h events[2] = {
        xg->create_queue_event ( &xg_queue_event_params_m ( .device = frame.device ) ),
        xg->create_queue_event ( &xg_queue_event_params_m ( .device = frame.device ) )
    };
    xg->cmd_destroy_queue_event ( resource_cmd_buffer, events[0], xg_resource_cmd_buffer_time_workload_complete_m );
    xg->cmd_destroy_queue_event ( resource_cmd_buffer, events[1], xg_resource_cmd_buffer_time_workload_complete_m );

    xg->cmd_bind_queue ( cmd_buffer, 0, &xg_cmd_bind_queue_params_m (
        .queue = xg_cmd_queue_graphics_m,
        .signal_events = { events[0] },
        .signal_count = 1,
    ) );

    // transition to copy_dest
    xg_texture_memory_barrier_t texture_barrier = xg_texture_memory_barrier_m (
        .texture = temp_texture,
        .layout.old = xg_texture_layout_undefined_m,
        .layout.new = xg_texture_layout_copy_dest_m,
        .memory.flushes = xg_memory_access_bit_none_m,
        .memory.invalidations = xg_memory_access_bit_none_m,
        .execution.blocker = xg_pipeline_stage_bit_none_m,
        .execution.blocked = xg_pipeline_stage_bit_transfer_m,
    );

    xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
        .texture_memory_barriers_count = 1,
        .texture_memory_barriers = &texture_barrier,
    ) );

    // copy clear
    xg_color_clear_t color_clear;
    uint64_t t = 500;
    color_clear.f32[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
    color_clear.f32[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
    color_clear.f32[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
    xg->cmd_clear_texture ( cmd_buffer, 0, temp_texture, color_clear );

    // transition to storage
    texture_barrier = xg_texture_memory_barrier_m (
        .texture = temp_texture,
        .layout.old = xg_texture_layout_copy_dest_m,
        .layout.new = xg_texture_layout_shader_write_m,
        .memory.flushes = xg_memory_access_bit_transfer_write_m,
        .memory.invalidations = xg_memory_access_bit_none_m,
        .execution.blocker = xg_pipeline_stage_bit_transfer_m,
        .execution.blocked = xg_pipeline_stage_bit_compute_shader_m,
    );

    xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
        .texture_memory_barriers_count = 1,
        .texture_memory_barriers = &texture_barrier,
    ) );

    if ( device_info.dedicated_compute_queue ) {
        // release
        texture_barrier = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_shader_write_m,
            .layout.new = xg_texture_layout_shader_write_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_none_m,
            .execution.blocked = xg_pipeline_stage_bit_none_m,
            .queue.old = xg_cmd_queue_graphics_m,
            .queue.new = xg_cmd_queue_compute_m,
        );

        xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
            .texture_memory_barriers_count = 1,
            .texture_memory_barriers = &texture_barrier,
        ) );

        xg->cmd_bind_queue ( cmd_buffer, 0, &xg_cmd_bind_queue_params_m (
            .queue = xg_cmd_queue_compute_m,
            .signal_count = 1,
            .signal_events = { events[1] },
            .wait_count = 1,
            .wait_events = { events[0] },
            .wait_stages = { xg_pipeline_stage_bit_top_of_pipe_m }
        ) );

        // acquire
        texture_barrier = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_shader_write_m,
            .layout.new = xg_texture_layout_shader_write_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_compute_shader_m,
            .queue.old = xg_cmd_queue_graphics_m,
            .queue.new = xg_cmd_queue_compute_m,
        );

        xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
            .texture_memory_barriers_count = 1,
            .texture_memory_barriers = &texture_barrier,
        ) );
    }

    // compute clear
    // TODO upload color_clear to compute cbuffer...
    xg_resource_bindings_layout_h compute_layouts[xg_shader_binding_set_count_m];
    xg->get_pipeline_resource_layouts ( compute_layouts, frame.compute_pipeline );

    xg->cmd_compute ( cmd_buffer, 0, &xg_cmd_compute_params_m (
        .workgroup_count_x = swapchain_texture_info.width,
        .workgroup_count_y = swapchain_texture_info.height,
        .pipeline = frame.compute_pipeline,
        .bindings[xg_shader_binding_set_dispatch_m] = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = compute_layouts[xg_shader_binding_set_dispatch_m],
            .bindings = xg_pipeline_resource_bindings_m (
                .texture_count = 1,
                .textures = {
                    xg_texture_resource_binding_m (
                        .texture = temp_texture,
                        .layout = xg_texture_layout_shader_write_m,
                        .shader_register = 0,
                    )
                }
            )
        ) ),
    ) );

    if ( device_info.dedicated_compute_queue ) {
        // transition to copy source, release
        texture_barrier = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_shader_write_m,
            .layout.new = xg_texture_layout_copy_source_m,
            .memory.flushes = xg_memory_access_bit_shader_write_m,
            .memory.invalidations = xg_memory_access_bit_transfer_read_m,
            .execution.blocker = xg_pipeline_stage_bit_compute_shader_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
            .queue.old = xg_cmd_queue_compute_m,
            .queue.new = xg_cmd_queue_graphics_m
        );

        xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
            .texture_memory_barriers_count = 1,
            .texture_memory_barriers = &texture_barrier,
        ) );

        xg->cmd_bind_queue ( cmd_buffer, 0, &xg_cmd_bind_queue_params_m (
            .queue = xg_cmd_queue_graphics_m,
            .wait_count = 1,
            .wait_events = { events[1] },
            .wait_stages = { xg_pipeline_stage_bit_top_of_pipe_m },
        ) );

        // Copy temp texture on swapchain texture
        xg_texture_memory_barrier_t texture_barriers[2];

        // flush and make visible the clear
        // transition to copy_source
        texture_barriers[0] = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_shader_write_m,
            .layout.new = xg_texture_layout_copy_source_m,
            .memory.flushes = xg_memory_access_bit_shader_write_m,
            .memory.invalidations = xg_memory_access_bit_transfer_read_m,
            .execution.blocker = xg_pipeline_stage_bit_compute_shader_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
            .queue.old = xg_cmd_queue_compute_m,
            .queue.new = xg_cmd_queue_graphics_m
        );

        // transition swapchain to copy_dest
        texture_barriers[1] = xg_texture_memory_barrier_m (
            .texture = swapchain_texture,
            .layout.old = xg_texture_layout_undefined_m, //xg_texture_layout_present_m;
            .layout.new = xg_texture_layout_copy_dest_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );

        xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
            .texture_memory_barriers_count = 2,
            .texture_memory_barriers = texture_barriers,
        ) );
    } else {
        xg_texture_memory_barrier_t texture_barriers[2];

        // flush and make visible the clear
        // transition to copy_source
        texture_barriers[0] = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_shader_write_m,
            .layout.new = xg_texture_layout_copy_source_m,
            .memory.flushes = xg_memory_access_bit_shader_write_m,
            .memory.invalidations = xg_memory_access_bit_transfer_read_m,
            .execution.blocker = xg_pipeline_stage_bit_compute_shader_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );

        // transition swapchain to copy_dest
        texture_barriers[1] = xg_texture_memory_barrier_m (
            .texture = swapchain_texture,
            .layout.old = xg_texture_layout_undefined_m, //xg_texture_layout_present_m;
            .layout.new = xg_texture_layout_copy_dest_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );

        xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
            .texture_memory_barriers_count = 2,
            .texture_memory_barriers = texture_barriers,
        ) );
    }

    xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
        .source.texture = temp_texture,
        .destination.texture = swapchain_texture,
    );
    xg->cmd_copy_texture ( cmd_buffer, 0, &copy_params );

    // Transition swpachain texture to render target
    // make copy on swapchain available and visible to
    texture_barrier = xg_texture_memory_barrier_m ( 
        .texture = swapchain_texture,
        .layout.old = xg_texture_layout_copy_dest_m,
        .layout.new = xg_texture_layout_render_target_m,
        .memory.flushes = xg_memory_access_bit_transfer_write_m,
        .memory.invalidations = xg_memory_access_bit_color_write_m,
        .execution.blocker = xg_pipeline_stage_bit_transfer_m,
        .execution.blocked = xg_pipeline_stage_bit_color_output_m,
    );

    xg->cmd_barrier_set ( cmd_buffer, 0, &xg_barrier_set_m (
        .texture_memory_barriers_count = 1,
        .texture_memory_barriers = &texture_barrier,
    ) );

    // Bind swapchain texture as render target
    xg->cmd_begin_renderpass ( cmd_buffer, 0, &xg_cmd_renderpass_params_m (
        .renderpass = frame.renderpass,
        .render_targets_count = 1,
        .render_targets = { xg_render_target_binding_m (
            .texture = swapchain_texture,
            .view = xg_texture_view_m(),
        ) },
    ) );

    xg_buffer_range_t uniform_buffer_range;
    {
        draw_uniforms_t uniforms;
        uniforms.color[0] = 0.0;
        uniforms.color[1] = 0.0;
        uniforms.color[2] = 1.0;
        xg_buffer_h buffer_handle = xg->create_buffer ( & xg_buffer_params_m (
            .memory_type = xg_memory_type_gpu_mapped_m,
            .device = frame.device,
            .size = 128,
            .allowed_usage = xg_buffer_usage_bit_uniform_m,
            .debug_name = "uniform buffer"
        ) );

        xg_buffer_info_t buffer_info;
        xg->get_buffer_info ( &buffer_info, buffer_handle );
        std_mem_copy ( buffer_info.allocation.mapped_address, &uniforms, sizeof ( uniforms ) );
        uniform_buffer_range.handle = buffer_handle;
        uniform_buffer_range.offset = 0;
        uniform_buffer_range.size = sizeof ( uniforms );

        xg->cmd_destroy_buffer ( resource_cmd_buffer, buffer_handle, xg_resource_cmd_buffer_time_workload_complete_m );
    }

    xg_resource_bindings_layout_h graphics_layouts[xg_shader_binding_set_count_m];
    xg->get_pipeline_resource_layouts ( graphics_layouts, frame.graphics_pipeline );

    // Bind resources
    xg_resource_bindings_h group = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = graphics_layouts[xg_shader_binding_set_dispatch_m],
        .bindings = xg_pipeline_resource_bindings_m (
            .buffer_count = 1,
            .buffers = {
                xg_buffer_resource_binding_m (
                    .shader_register = 0,
                    .range = uniform_buffer_range,
                )
            }
        )
    ) );

    // Draw
    xg->cmd_draw ( cmd_buffer, 0, &xg_cmd_draw_params_m (
        .pipeline = frame.graphics_pipeline,
        .bindings[xg_shader_binding_set_dispatch_m] = group,
        .primitive_count = 1,
    ) );

    xg->cmd_end_renderpass ( cmd_buffer, 0 );

    // Transition swapchain texture to present mode
    {
        xg_texture_memory_barrier_t texture_barrier = xg_texture_memory_barrier_m (
            .texture = swapchain_texture,
            .layout.old = xg_texture_layout_render_target_m,
            .layout.new = xg_texture_layout_present_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_color_output_m,
            .execution.blocked = xg_pipeline_stage_bit_bottom_of_pipe_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m (
            .texture_memory_barriers_count = 1,
            .texture_memory_barriers = &texture_barrier,
        );

        xg->cmd_barrier_set ( cmd_buffer, 0, &barrier_set );
    }

    xg->submit_workload ( frame.workload );
    xg->present_swapchain ( frame.swapchain, frame.workload );
}

static void xs_test ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    std_assert_m ( wm );

    uint32_t resolution_x = 600;
    uint32_t resolution_y = 400;
    wm_window_params_t window_params = { .name = "xs_test", .x = 0, .y = 0, .width = resolution_x, .height = resolution_y, .gain_focus = true, .borderless = false };
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
            .format = xg_format_b8g8r8a8_unorm_m,//xg_format_b8g8r8a8_srgb_m
            .color_space = xg_colorspace_srgb_m,
            .present_mode = xg_present_mode_fifo_m,
            .allowed_usage = xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_render_target_m,
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
    xs->set_build_params ( sdb, &xs_database_build_params_m (
        .base_graphics_state = &xg_graphics_pipeline_state_m ( 
            .viewport_state.width = 600,
            .viewport_state.height = 400,
        ),
    ) );
    xs->build_database ( sdb );

    xs_database_pipeline_h graphics_database_pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "triangle" ) );
    xs_database_pipeline_h compute_database_pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "clear" ) );

    xg_renderpass_h renderpass = xg->create_renderpass ( &xg_renderpass_params_m ( 
        .device = device,
        .render_textures_layout = xg_render_textures_layout_m (
            .render_targets_count = 1,
            .render_targets = { xg_render_target_layout_m ( .format = xg_format_b8g8r8a8_unorm_m ) }
        ),
        .render_textures_usage = xg_render_textures_usage_m (
            .render_targets = { xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_render_target_m }
        ),
        .resolution_x = resolution_x,
        .resolution_y = resolution_y,
        .debug_name = "xs_test_renderpass"
    ) );

    wm_window_info_t window_info;
    wm->get_window_info ( window, &window_info );
    wm_input_state_t input_state;
    wm->get_window_input_state ( window, &input_state );

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

        wm_input_state_t new_input_state;
        wm->get_window_input_state ( window, &new_input_state );

        if ( new_input_state.keyboard[wm_keyboard_state_esc_m] ) {
            break;
        }

        if ( !input_state.keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
            xs->rebuild_databases();
        }

        if ( !input_state.keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
            std_module_reload_m();
        }

        wm_window_info_t new_window_info;
        wm->get_window_info ( window, &new_window_info );

        if ( window_info.width != new_window_info.width || window_info.height != new_window_info.height ) {
            xg->resize_swapchain ( swapchain, new_window_info.width, new_window_info.height );
            xg->destroy_renderpass ( renderpass );
            renderpass = xg->create_renderpass ( &xg_renderpass_params_m ( 
                .device = device,
                .render_textures_layout = xg_render_textures_layout_m (
                    .render_targets_count = 1,
                    .render_targets = { xg_render_target_layout_m ( .format = xg_format_b8g8r8a8_unorm_m ) }
                ),
                .render_textures_usage = xg_render_textures_usage_m (
                    .render_targets = { xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_render_target_m }
                ),
                .resolution_x = new_window_info.width,
                .resolution_y = new_window_info.height,
                .debug_name = "xs_test_renderpass"
            ) );
            xs->set_build_params ( sdb, &xs_database_build_params_m (
                .base_graphics_state = &xg_graphics_pipeline_state_m ( 
                    .viewport_state.width = new_window_info.width,
                    .viewport_state.height = new_window_info.height,
                ),
            ) );
            xs->rebuild_databases();
        }

        window_info = new_window_info;
        input_state = new_input_state;

        xg_workload_h workload = xg->create_workload ( device );

        xg_graphics_pipeline_state_h graphics_pipeline = xs->get_pipeline_state ( graphics_database_pipeline );
        xg_compute_pipeline_state_h compute_pipeline = xs->get_pipeline_state ( compute_database_pipeline );
        xs_test_frame ( ( xs_test_frame_params_t ) {
            .device = device,
            .workload = workload,
            .swapchain = swapchain,
            .compute_pipeline = compute_pipeline,
            .graphics_pipeline = graphics_pipeline,
            .renderpass = renderpass
        } );

        xs->update_pipeline_states ( workload );
    }

    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
}

void std_main ( void ) {
    xs_test();
    std_log_info_m ( "xs_test_m COMPLETE!" );
}
