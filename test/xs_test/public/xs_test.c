#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <wm.h>
#include <xg.h>
#include <xs.h>
#include <fs.h>

#include <math.h>

std_warnings_ignore_m ( "-Wunused-function" )

typedef struct {
    float color[3];
    float pad;
} frame_cbuffer_t;

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

#if 1
    // Allocate temp texture
    xg_texture_params_t params = xg_texture_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = frame.device,
        .width = swapchain_texture_info.width,
        .height = swapchain_texture_info.height,
        .format = swapchain_texture_info.format,
        .allowed_usage = xg_texture_usage_bit_copy_source_m | xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_storage_m,
        .initial_layout = xg_texture_layout_undefined_m,
        .debug_name = "temp_texture",
    );
    xg_texture_h temp_texture = xg->cmd_create_texture ( resource_cmd_buffer, &params );

    xg->cmd_destroy_texture ( resource_cmd_buffer, temp_texture, xg_resource_cmd_buffer_time_workload_complete_m );

#define COMPUTE_CLEAR 1

    // Clear temp texture
    {
#if !COMPUTE_CLEAR
        // transition to copy_dest
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = temp_texture;
        texture_barrier.layout.old = xg_texture_layout_undefined_m;
        texture_barrier.layout.new = xg_texture_layout_copy_dest_m;
        texture_barrier.memory.flushes = xg_memory_access_bit_none_m;
        texture_barrier.memory.invalidations = xg_memory_access_bit_none_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_bit_transfer_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_bit_transfer_m;

        xg_barrier_set_t barrier_set;
        barrier_set.memory_barriers_count = 0;
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );

#else
        // transition to storage image
        xg_texture_memory_barrier_t texture_barrier = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_undefined_m,
            .layout.new = xg_texture_layout_shader_write_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_compute_shader_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m (
            .texture_memory_barriers_count = 1,
            .texture_memory_barriers = &texture_barrier,
        );

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
#endif
    }

    {
        xg_color_clear_t color_clear;
        uint64_t t = 500;
        color_clear.f32[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        color_clear.f32[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
        color_clear.f32[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
#if !COMPUTE_CLEAR
        xg->cmd_clear_texture ( cmd_buffer, temp_texture, color_clear, 0 );
#else
        xg->cmd_set_compute_pipeline_state ( cmd_buffer, frame.compute_pipeline, 0 );

#if 0
        xg_pipeline_resource_bindings_t bindings = xg_pipeline_resource_bindings_m (
            .set = xg_shader_binding_set_per_frame_m,
            .texture_count = 1,
            .textures = {
                xg_texture_resource_binding_m (
                    .shader_register = 0,
                    .texture = temp_texture,
                    .view = xg_texture_view_m(),
                    .layout = xg_texture_layout_shader_write_m,
                )
            }
        );
        xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, 0 );
#else
        xg_pipeline_resource_group_h group = xg->cmd_create_workload_resource_group ( resource_cmd_buffer, frame.workload, &xg_pipeline_resource_group_params_m (
            .device = frame.device,
            .pipeline = frame.compute_pipeline,
            .bindings = xg_pipeline_resource_bindings_m (
                .set = xg_shader_binding_set_per_frame_m,
                .texture_count = 1,
                .textures = {
                    xg_texture_resource_binding_m (
                        .shader_register = 0,
                        .texture = temp_texture,
                        .view = xg_texture_view_m(),
                        .layout = xg_texture_layout_shader_write_m,
                    )
                }
            )
        ) );
        xg->cmd_set_resource_group ( cmd_buffer, xg_shader_binding_set_per_frame_m, group, 0 );
#endif

        xg->cmd_dispatch_compute ( cmd_buffer, swapchain_texture_info.width, swapchain_texture_info.height, 1, 0 );
#endif
    }

    // Copy temp texture on swapchain texture
    {
        // flush and make visible the clear
        // transition to copy_source
        xg_texture_memory_barrier_t texture_barrier[2];
#if !COMPUTE_CLEAR
        texture_barrier[0] = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_copy_dest_m,
            .layout.new = xg_texture_layout_copy_source_m,
            .memory.flushes = xg_memory_access_bit_transfer_write_m,
            .memory.invalidations = xg_memory_access_bit_transfer_write_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );
#else
        texture_barrier[0] = xg_texture_memory_barrier_m (
            .texture = temp_texture,
            .layout.old = xg_texture_layout_shader_write_m,
            .layout.new = xg_texture_layout_copy_source_m,
            .memory.flushes = xg_memory_access_bit_shader_write_m,
            .memory.invalidations = xg_memory_access_bit_transfer_write_m, // xg_memory_access_bit_transfer_read_m?
            .execution.blocker = xg_pipeline_stage_bit_compute_shader_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );
#endif
        // transition to to copy_dest
        texture_barrier[1] = xg_texture_memory_barrier_m (
            .texture = swapchain_texture,
            .layout.old = xg_texture_layout_undefined_m, //xg_texture_layout_present_m;
            .layout.new = xg_texture_layout_copy_dest_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_none_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m (
            .texture_memory_barriers_count = 2,
            .texture_memory_barriers = texture_barrier,
        );
        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
        .source.texture = temp_texture,
        .destination.texture = swapchain_texture,
    );
    xg->cmd_copy_texture ( cmd_buffer, &copy_params, 0 );
#endif

    // Bind swapchain texture as render target
    {
        xg_color_clear_t color_clear;
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
            .render_targets_count = 1,
            .render_targets[0].texture = swapchain_texture,
            .render_targets[0].view = xg_texture_view_m(),
            .depth_stencil.texture = xg_null_handle_m,
        );
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, 0 );
    }

    // Transition swpachain texture to render target
    {
        // make copy on swapchain available and visible to
        xg_texture_memory_barrier_t texture_barrier = xg_texture_memory_barrier_m ( 
            .texture = swapchain_texture,
            .layout.old = xg_texture_layout_copy_dest_m,
            .layout.new = xg_texture_layout_render_target_m,
            .memory.flushes = xg_memory_access_bit_transfer_write_m,
            .memory.invalidations = xg_memory_access_bit_color_write_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_color_output_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m (
            .texture_memory_barriers_count = 1,
            .texture_memory_barriers = &texture_barrier,
        );

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    // Set pipeline
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, frame.graphics_pipeline, 0 );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = 600,
        .height = 400,
    );
    xg->cmd_set_dynamic_viewport ( cmd_buffer, &viewport, 0 );

    xg_buffer_range_t cbuffer_range;
    {
        frame_cbuffer_t cbuffer_data;
        cbuffer_data.color[0] = 0.0;
        cbuffer_data.color[1] = 0.0;
        cbuffer_data.color[2] = 1.0;
        //cbuffer_range = xg->write_workload_uniform ( workload, &cbuffer_data, sizeof ( cbuffer_data ) );
        xg_buffer_h buffer_handle = xg->create_buffer ( & xg_buffer_params_m (
            .memory_type = xg_memory_type_gpu_mapped_m,
            .device = frame.device,
            .size = 128,
            .allowed_usage = xg_buffer_usage_bit_uniform_m,
            .debug_name = "uniform buffer"
        ) );

        xg_buffer_info_t buffer_info;
        xg->get_buffer_info ( &buffer_info, buffer_handle );
        std_mem_copy ( buffer_info.allocation.mapped_address, &cbuffer_data, sizeof ( cbuffer_data ) );
        cbuffer_range.handle = buffer_handle;
        cbuffer_range.offset = 0;
        cbuffer_range.size = sizeof ( cbuffer_data );

        xg->cmd_destroy_buffer ( resource_cmd_buffer, buffer_handle, xg_resource_cmd_buffer_time_workload_complete_m );
    }

    // Bind resources
#if 0
    xg_pipeline_resource_bindings_t bindings = xg_pipeline_resource_bindings_m (
        .set = xg_shader_binding_set_per_draw_m,
        .buffer_count = 1,
        .buffers = {
            xg_buffer_resource_binding_m (
                .shader_register = 0,
                .type = xg_buffer_binding_type_uniform_m,
                .range = cbuffer_range,
            )
        }
    );

    xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, 0 );
#else
    xg_pipeline_resource_group_h group = xg->cmd_create_workload_resource_group ( resource_cmd_buffer, frame.workload, &xg_pipeline_resource_group_params_m (
        .device = frame.device,
        .pipeline = frame.graphics_pipeline,
        .bindings = xg_pipeline_resource_bindings_m (
            .set = xg_shader_binding_set_per_draw_m,
            .buffer_count = 1,
            .buffers = {
                xg_buffer_resource_binding_m (
                    .shader_register = 0,
                    .type = xg_buffer_binding_type_uniform_m,
                    .range = cbuffer_range,
                )
            }
        )
    ) );

    xg->cmd_set_resource_group ( cmd_buffer, xg_shader_binding_set_per_draw_m, group, 0 );
#endif

    xg->cmd_begin_renderpass ( cmd_buffer, frame.renderpass, 0 );

    // Draw
    xg->cmd_draw ( cmd_buffer, 3, 0, 0 );

    xg->cmd_end_renderpass ( cmd_buffer, frame.renderpass, 0 );

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

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    //xg->close_resource_cmd_buffers ( &resource_cmd_buffer, 1 );

    //xg->close_cmd_buffers ( &cmd_buffer, 1 );
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
    xs->set_build_params ( sdb, &xs_database_build_params_m (
        .base_graphics_state = &xg_graphics_pipeline_state_m ( 
            .viewport_state.width = 600,
            .viewport_state.height = 400,
        ),
    ) );
    xs->build_database ( sdb );

    xs_database_pipeline_h graphics_database_pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "test_state" ) );
    xs_database_pipeline_h compute_database_pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "compute_state" ) );

    xg_renderpass_h renderpass = xg->create_renderpass ( &xg_graphics_renderpass_params_m ( 
        .device = device,
        .render_textures = xg_render_textures_layout_m (
            .render_targets_count = 1,
            .render_targets = { xg_render_target_layout_m ( .format = xg_format_b8g8r8a8_unorm_m ) }
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
}

void std_main ( void ) {
    xs_test();
    std_log_info_m ( "xs_test_m COMPLETE!" );
}
