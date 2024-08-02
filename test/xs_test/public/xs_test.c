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

static void xs_test2_frame ( xg_device_h device, xg_workload_h workload, xg_swapchain_h swapchain, xg_graphics_pipeline_state_h graphics_pipeline_state, xg_compute_pipeline_state_h compute_pipeline_state ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );

    xg->acquire_next_swapchain_texture ( swapchain, workload );

    xg_texture_h swapchain_texture = xg->get_swapchain_texture ( swapchain );

    // Allocate temp texture
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    xg_texture_info_t swapchain_texture_info;
    xg->get_texture_info ( &swapchain_texture_info, swapchain_texture );

#if 1
    xg_texture_params_t params = xg_default_texture_params_m;
    params.memory_type = xg_memory_type_gpu_only_m;
    params.device = device;
    params.width = swapchain_texture_info.width;
    params.height = swapchain_texture_info.height;
    params.format = swapchain_texture_info.format;
    params.allowed_usage = xg_texture_usage_bit_copy_source_m | xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_storage_m;
    params.initial_layout = xg_texture_layout_undefined_m;
    std_str_copy_static_m ( params.debug_name, "temp_texture" );
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
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = temp_texture;
        texture_barrier.layout.old = xg_texture_layout_undefined_m;
        texture_barrier.layout.new = xg_texture_layout_shader_write_m;
        texture_barrier.memory.flushes = xg_memory_access_bit_none_m;
        texture_barrier.memory.invalidations = xg_memory_access_bit_none_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_bit_transfer_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_bit_compute_shader_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

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
        xg->cmd_set_compute_pipeline_state ( cmd_buffer, compute_pipeline_state, 0 );

        xg_texture_resource_binding_t texture;
        texture.shader_register = 0;
        texture.texture = temp_texture;
        texture.view = xg_default_texture_view_m;
        texture.layout = xg_texture_layout_shader_write_m;

        xg_pipeline_resource_bindings_t bindings = xg_default_pipeline_resource_bindings_m;
        bindings.set = xg_resource_binding_set_per_frame_m;
        bindings.texture_count = 1;
        bindings.textures = &texture;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, 0 );

        xg->cmd_dispatch_compute ( cmd_buffer, swapchain_texture_info.width, swapchain_texture_info.height, 1, 0 );
#endif
    }

    // Copy temp texture on swapchain texture
    {
        // flush and make visible the clear
        // transition to copy_source
        xg_texture_memory_barrier_t texture_barrier[2];
#if !COMPUTE_CLEAR
        texture_barrier[0] = xg_default_texture_memory_barrier_m;
        texture_barrier[0].texture = temp_texture;
        texture_barrier[0].layout.old = xg_texture_layout_copy_dest_m;
        texture_barrier[0].layout.new = xg_texture_layout_copy_source_m;
        texture_barrier[0].memory.flushes = xg_memory_access_bit_transfer_write_m;
        texture_barrier[0].memory.invalidations = xg_memory_access_bit_transfer_write_m;
        texture_barrier[0].execution.blocker = xg_pipeline_stage_bit_transfer_m;
        texture_barrier[0].execution.blocked = xg_pipeline_stage_bit_transfer_m;
#else
        texture_barrier[0] = xg_default_texture_memory_barrier_m;
        texture_barrier[0].texture = temp_texture;
        texture_barrier[0].layout.old = xg_texture_layout_shader_write_m;
        texture_barrier[0].layout.new = xg_texture_layout_copy_source_m;
        texture_barrier[0].memory.flushes = xg_memory_access_bit_shader_write_m;
        texture_barrier[0].memory.invalidations = xg_memory_access_bit_transfer_write_m; // xg_memory_access_bit_transfer_read_m?
        texture_barrier[0].execution.blocker = xg_pipeline_stage_bit_compute_shader_m;
        texture_barrier[0].execution.blocked = xg_pipeline_stage_bit_transfer_m;
#endif
        // transition to to copy_dest
        texture_barrier[1] = xg_default_texture_memory_barrier_m;
        texture_barrier[1].texture = swapchain_texture;
        texture_barrier[1].layout.old = xg_texture_layout_undefined_m; //xg_texture_layout_present_m;
        texture_barrier[1].layout.new = xg_texture_layout_copy_dest_m;
        texture_barrier[1].memory.flushes = xg_memory_access_bit_none_m;
        texture_barrier[1].memory.invalidations = xg_memory_access_bit_none_m;
        texture_barrier[1].execution.blocker = xg_pipeline_stage_bit_transfer_m;
        texture_barrier[1].execution.blocked = xg_pipeline_stage_bit_transfer_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 2;
        barrier_set.texture_memory_barriers = texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    xg_texture_copy_params_t copy_params = xg_default_texture_copy_params_m;
    copy_params.source.texture = temp_texture;
    copy_params.destination.texture = swapchain_texture;

    xg->cmd_copy_texture ( cmd_buffer, &copy_params, 0 );
#endif

    // Bind swapchain texture as render target
    {
        xg_color_clear_t color_clear;
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        xg_render_textures_binding_t render_textures = xg_render_texture_bindings_m();
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0].texture = swapchain_texture;
        render_textures.render_targets[0].view = xg_default_texture_view_m;
        render_textures.depth_stencil.texture = xg_null_handle_m;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, 0 );
    }

    // Transition swpachain texture to render target
    {
        // make copy on swapchain available and visible to
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = swapchain_texture;
        texture_barrier.layout.old = xg_texture_layout_copy_dest_m;
        texture_barrier.layout.new = xg_texture_layout_render_target_m;
        texture_barrier.memory.flushes = xg_memory_access_bit_transfer_write_m;
        texture_barrier.memory.invalidations = xg_memory_access_bit_color_write_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_bit_transfer_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_bit_color_output_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    // Set pipeline
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, graphics_pipeline_state, 0 );

    xg_viewport_state_t viewport = xg_default_viewport_state_m;
    viewport.width = 600;
    viewport.height = 400;
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, 0 );

    xg_buffer_range_t cbuffer_range;
    {
        frame_cbuffer_t cbuffer_data;
        cbuffer_data.color[0] = 0.0;
        cbuffer_data.color[1] = 0.0;
        cbuffer_data.color[2] = 1.0;
        //cbuffer_range = xg->write_workload_uniform ( workload, &cbuffer_data, sizeof ( cbuffer_data ) );
        xg_buffer_h buffer_handle = xg->create_buffer ( & xg_buffer_params_m (
            .memory_type = xg_memory_type_gpu_mappable_m,
            .device = device,
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
    {
        xg_buffer_resource_binding_t buffer;
        buffer.shader_register = 0;
        buffer.type = xg_buffer_binding_type_uniform_m;
        buffer.range = cbuffer_range;

        xg_pipeline_resource_bindings_t bindings = xg_default_pipeline_resource_bindings_m;
        bindings.set = xg_resource_binding_set_per_draw_m;
        bindings.buffer_count = 1;
        bindings.buffers = &buffer;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, 0 );
    }

    // Draw
    xg->cmd_draw ( cmd_buffer, 3, 0, 0 );

    // Transition swapchain texture to present mode
    {
        xg_texture_memory_barrier_t texture_barrier = xg_default_texture_memory_barrier_m;
        texture_barrier.texture = swapchain_texture;
        texture_barrier.layout.old = xg_texture_layout_render_target_m;
        texture_barrier.layout.new = xg_texture_layout_present_m;
        texture_barrier.memory.flushes = xg_memory_access_bit_none_m;
        texture_barrier.memory.invalidations = xg_memory_access_bit_none_m;
        texture_barrier.execution.blocker = xg_pipeline_stage_bit_color_output_m;
        texture_barrier.execution.blocked = xg_pipeline_stage_bit_bottom_of_pipe_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &texture_barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, 0 );
    }

    //xg->close_resource_cmd_buffers ( &resource_cmd_buffer, 1 );

    //xg->close_cmd_buffers ( &cmd_buffer, 1 );
    xg->submit_workload ( workload );
    xg->present_swapchain ( swapchain, workload );
}

static void xs_test2 ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    std_assert_m ( wm );

    wm_window_params_t window_params = { .name = "xs_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
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

    xs_pipeline_state_h graphics_pipeline_state;
    xs_pipeline_state_h compute_pipeline_state;
    xs_i* xs = std_module_load_m ( xs_module_name_m );
    {
        xs->add_database_folder ( "shader/" );
        xs->set_output_folder ( "output/shader/" );

        xs_database_build_params_t build_params;
        build_params.viewport_width = 600;
        build_params.viewport_height = 400;

        xs->build_database_shaders ( device, &build_params );

        {
            const char* pipeline_name = "test_state";
            xs_string_hash_t pipeline_hash = xs_hash_string_m ( pipeline_name, std_str_len ( pipeline_name ) );
            graphics_pipeline_state = xs->lookup_pipeline_state_hash ( pipeline_hash );
            std_assert_m ( graphics_pipeline_state != xs_null_handle_m );
        }

        {
            const char* pipeline_name = "compute_state";
            xs_string_hash_t pipeline_hash = xs_hash_string_m ( pipeline_name, std_str_len ( pipeline_name ) );
            compute_pipeline_state = xs->lookup_pipeline_state_hash ( pipeline_hash );
            std_assert_m ( compute_pipeline_state != xs_null_handle_m );
        }
    }

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
            xs_database_build_params_t build_params;
            build_params.viewport_width = window_info.width;
            build_params.viewport_height = window_info.height;
            xs->build_database_shaders ( device, &build_params );
        }

        wm_window_info_t new_window_info;
        wm->get_window_info ( window, &new_window_info );

        if ( window_info.width != new_window_info.width || window_info.height != new_window_info.height ) {
            xg->resize_swapchain ( swapchain, new_window_info.width, new_window_info.height );

            xs_database_build_params_t build_params;
            build_params.viewport_width = new_window_info.width;
            build_params.viewport_height = new_window_info.height;
            xs->build_database_shaders ( device, &build_params );
        }

        window_info = new_window_info;
        input_state = new_input_state;

        xg_workload_h workload = xg->create_workload ( device );

        xg_graphics_pipeline_state_h graphics_pipeline = xs->get_pipeline_state ( graphics_pipeline_state );
        xg_compute_pipeline_state_h compute_pipeline = xs->get_pipeline_state ( compute_pipeline_state );
        xs_test2_frame ( device, workload, swapchain, graphics_pipeline, compute_pipeline );

        xs->update_pipeline_states ( workload );
    }

    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
}

static void xs_test1 ( void ) {
    xg_device_h device;
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
    }

    xs_i* xs = std_module_load_m ( xs_module_name_m );
    {
        xs->add_database_folder ( "shader/" );
        xs->set_output_folder ( "output/shader/" );

        xs_database_build_params_t build_params;
        build_params.viewport_width = 600;
        build_params.viewport_height = 400;
        xs_database_build_result_t result = xs->build_database_shaders ( device, &build_params );
        std_log_info_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders", result.successful_pipeline_states, result.successful_shaders );

        if ( result.failed_shaders || result.failed_pipeline_states ) {
            std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed" );
        }
    }

    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
}

void std_main ( void ) {
    xs_test2();
    std_log_info_m ( "xs_test_m COMPLETE!" );
}
