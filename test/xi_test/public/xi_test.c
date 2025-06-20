#include <std_main.h>
#include <std_time.h>
#include <std_log.h>
#include <std_file.h>

#include <xs.h>
#include <xf.h>
#include <xi.h>
#include <se.h>
#include <rv.h>

#include <math.h>

#define UI_WINDOW_COMPONENT_ID 1
#define UI_LABEL_COMPONENT_ID 2
#define UI_SWITCH_COMPONENT_ID 3

typedef struct {
    xi_window_state_t state;
} ui_window_component_t;

typedef struct {
    xi_label_state_t state;
} ui_label_component_t;

typedef struct {
    xi_switch_state_t state;
} ui_switch_component_t;

std_warnings_ignore_m ( "-Wunused-function" )

typedef struct {
    xg_device_h device;
    xg_workload_h xg_workload;
    uint64_t resolution_x;
    uint64_t resolution_y;
    xi_workload_h xi_workload;
} xf_ui_pass_args_t;

static void ui_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    xf_ui_pass_args_t* args = ( xf_ui_pass_args_t* ) user_args;

    //xg_i* xg = std_module_get_m ( xg_module_name_m );

#if 0
    // Bind swapchain texture as render target
    {
        xg_render_textures_binding_t render_textures;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        render_textures.depth_stencil.texture = xg_null_handle_m;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }
#endif

    xi_i* xi = std_module_get_m ( xi_module_name_m );
    {
        xi_flush_params_t params;
        params.device = args->device;
        params.workload = args->xg_workload;
        params.cmd_buffer = cmd_buffer;
        params.resource_cmd_buffer = resource_cmd_buffer;
        params.key = key;
        params.render_target_format = xg_format_b8g8r8a8_unorm_m;
        params.viewport.width = args->resolution_x;
        params.viewport.height = args->resolution_y;
        params.render_target_binding = xf_render_target_binding_m ( node_args->io->render_targets[0] );

        key = xi->flush_workload ( args->xi_workload, &params );
    }
}

static void xi_test ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );

    uint32_t resolution_x = 600;
    uint32_t resolution_y = 400;

    wm_window_params_t window_params = { .name = "xi_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
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
            .allowed_usage = xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_render_target_m,
            .debug_name = "swapchain",
        );
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    // let xi register its shaders before building the xs database
    // TODO support dynamic add and rebuild (do we already do that?)
    xs_i* xs = std_module_load_m ( xs_module_name_m );
    xi_i* xi = std_module_load_m ( xi_module_name_m );
    xi->load_shaders ( device );
    {
        //xs_database_build_params_t build_params;
        //build_params.viewport_width = 600;
        //build_params.viewport_height = 400;
        //xs_database_build_result_t result = xs->build_database_shaders ( device, &build_params );
        //std_log_info_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders built", result.successful_pipeline_states, result.successful_shaders );

        //if ( result.failed_shaders || result.failed_pipeline_states ) {
        //    std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed" );
        //}
    }

    // create test font
    xi_font_h font;
    {
        std_file_h font_file = std_file_open ( "assets/ProggyVector-Regular.ttf", std_file_read_m );
        std_file_info_t font_file_info;
        std_file_info ( &font_file_info, font_file );
        void* font_data_alloc = std_virtual_heap_alloc_m ( font_file_info.size, 16 );
        std_file_read ( font_data_alloc, font_file_info.size, font_file );

        xi_font_params_t font_params = xi_font_params_m (
            .xg_device = device,
            .pixel_height = 16,
            .first_char_code = xi_font_char_ascii_base_m,
            .char_count = xi_font_char_ascii_count_m,
            .outline = false,
            .debug_name = "proggy_clean"
        );
        font = xi->create_font ( std_buffer_m ( .base = font_data_alloc, .size = font_file_info.size ), &font_params );
        std_virtual_heap_free ( font_data_alloc );
    }

    // create ui entities
    // TODO: handles inside components, xi has create_x api that returns handle and layouts in immediate mode,
    // and creates internal state for the element rect, then in the render pass callback flush_x on the handle
    // causes xi to consume and draw the previously set up rect to an xg cmd buffer
    // OR: simply build the xi_workload in here, and provide the workload only to the render pass, and the render
    // pass only calls flush on it.

    xf_i* xf = std_module_load_m ( xf_module_name_m );
    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m ( .device = device, .debug_name = "xi_graph" ) );

    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "clear",
        .debug_color = xg_debug_region_color_red_m,
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { xf_texture_clear_m () }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
        )
    ) );

    xf_ui_pass_args_t ui_node_args;
    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "ui",
        .debug_color = xg_debug_region_color_green_m,
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = ui_pass,
            .user_args = std_buffer_struct_m ( &ui_node_args ),
            .copy_args = false,
            .key_space_size = 128,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = swapchain_multi_texture ) },
            .presentable_texture = swapchain_multi_texture,
        )
    ) );

    wm_window_info_t window_info;
    wm->get_window_info ( window, &window_info );

    // ui
    xi_window_state_t ui_window = xi_window_state_m (
        .title = "window",
        .x = 20,
        .y = 20,
        .width = 250,
        .height = 250,
        .style = xi_default_style_m (
            .font = font,
        ),
    );

    xi_label_state_t ui_label = xi_label_state_m (
        .text = "label",
        .height = 20,
    );

    xi_switch_state_t ui_switch = xi_switch_state_m (
        .width = 20,
        .height = 20,
        .style = xi_style_m ( 
            .color = xi_color_blue_m,
            .horizontal_alignment = xi_horizontal_alignment_right_to_left_m 
        ),
    );
    xi_label_state_t ui_switch_label = xi_label_state_m (
        .text = "off",
        .height = 14,
    );

    xi_slider_state_t ui_slider = xi_slider_state_m (
        .width = 100,
        .height = 20,
        .style = xi_style_m ( 
            .color = xi_color_blue_m,
            .horizontal_alignment = xi_horizontal_alignment_right_to_left_m 
        ),
    );

    xi_label_state_t ui_slider_label = xi_label_state_m (
        .text = "0",
        .height = 14,
    );

    xi_button_state_t ui_button = xi_button_state_m (
        .width = 70,
        .height = 20,
        .text = "button",
        .style = xi_style_m ( 
            .color = xi_color_blue_m,
            .horizontal_alignment = xi_horizontal_alignment_right_to_left_m 
        ),
    );
    xi_label_state_t ui_button_label = xi_label_state_m (
        .height = 14,
    );

    void* select_alloc = std_virtual_heap_alloc_m ( 128, 8 );
    {
        std_stack_t stack = std_stack ( select_alloc, 128 );
        char** p1 = std_stack_alloc_m ( &stack, char* );
        char** p2 = std_stack_alloc_m ( &stack, char* );
        char** p3 = std_stack_alloc_m ( &stack, char* );
        char* s1 = std_stack_alloc_array_m ( &stack, char, 8 );
        char* s2 = std_stack_alloc_array_m ( &stack, char, 8 );
        char* s3 = std_stack_alloc_array_m ( &stack, char, 8 );
        std_str_copy_static_m ( s1, "qwe" );
        std_str_copy_static_m ( s2, "asd" );
        std_str_copy_static_m ( s3, "zxc" );
        *p1 = s1;
        *p2 = s2;
        *p3 = s3;
    }
    xi_select_state_t ui_select = xi_select_state_m (
        .width = 100,
        .height = 20,
        .items = ( const char** ) select_alloc,
        .item_count = 3,
        .item_idx = 0,
        .font = font,
        .sort_order = 1,
        .style = xi_style_m ( 
            .color = xi_color_blue_m,
            .horizontal_alignment = xi_horizontal_alignment_right_to_left_m 
        ),
    );
    xi_label_state_t ui_select_label = xi_label_state_m (
        .height = 14,
    );

    xi_textfield_state_t ui_textfield = xi_textfield_state_m (
        .width = 128,
        .text = "text",
        .text_alignment = xi_horizontal_alignment_right_to_left_m,
        .style = xi_style_m (
            .horizontal_alignment = xi_horizontal_alignment_right_to_left_m
        ),
    );

    xi_section_state_t ui_section = xi_section_state_m (
        .title = "section1",
        .font = font,
        .height = 20,
    );

    xi_section_state_t ui_section2 = xi_section_state_m (
        .title = "section2",
        .font = font,
        .height = 20,
    );

    float p[3] = {1, 2, 3};
    xi_property_editor_state_t ui_property_editor_3f32 = xi_property_editor_state_m ( 
        .type = xi_property_3f32_m,
        .data = p,
        .property_width = 64,
        .id = xi_line_id_m(),
        .style = xi_style_m ( .horizontal_alignment = xi_horizontal_alignment_right_to_left_m ),
    );

    float q[3] = {4, 5, 6};
    xi_property_editor_state_t ui_property_editor_3f32_2 = xi_property_editor_state_m ( 
        .type = xi_property_3f32_m,
        .data = q,
        .property_width = 64,
        .id = xi_line_id_m(),
        .style = xi_style_m ( .horizontal_alignment = xi_horizontal_alignment_right_to_left_m ),
    );

    rv_i* rv = std_module_load_m ( rv_module_name_m );
    rv_view_params_t view_params = rv_view_params_m (
        .transform = rv_view_transform_m (
            .position = { 0, 0, -8 },
        ),
        .proj_params.perspective = rv_perspective_projection_params_m (
            .aspect_ratio = ( float ) resolution_x / ( float ) resolution_y,
            .near_z = 0.1,
            .far_z = 1000,
            .fov_y = 50.f * rv_deg_to_rad_m,
            .jitter = { 1.f / resolution_x, 1.f / resolution_y },
            .reverse_z = false,
        ),
    );
    rv_view_h view = rv->create_view ( &view_params );

    xg_workload_h workload = xg->create_workload ( device );
    xi->init_geos ( device, workload );
    xg->submit_workload ( workload );

    xi_transform_state_t xform_state = xi_transform_state_m();

    float target_fps = 24.f;
    float target_frame_period = target_fps > 0.f ? 1.f / target_fps * 1000.f : 0.f;
    std_tick_t frame_tick = std_tick_now();

    while ( true ) {
        std_tick_t new_tick = std_tick_now();
        float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

        if ( delta_ms < target_frame_period ) {
            continue;
        }

        frame_tick = new_tick;

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
            std_module_reload_m();
        }

        if ( input_state.keyboard[wm_keyboard_state_f2_m] ) {
            xs->rebuild_databases();
        }

        wm_window_info_t new_window_info;
        wm->get_window_info ( window, &new_window_info );

        window_info = new_window_info;

        xg_workload_h workload = xg->create_workload ( device );

        // build ui
        {
            std_f32_to_str ( ui_slider.value, ui_slider_label.text, 32 );
            std_str_copy_static_m ( ui_switch_label.text, ui_switch.value ? "on" : "off" );
            std_str_copy_static_m ( ui_button_label.text, ui_button.pressed ? "click" : ui_button.down ? "down" : "up" );
            std_str_copy_static_m ( ui_select_label.text, ui_select.items[ui_select.item_idx] );

            xi_workload_h xi_workload = xi->create_workload();

            wm_input_buffer_t input_buffer;
            wm->get_window_input_buffer ( window, &input_buffer );

            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, view );

            // TODO remove
            xi->set_workload_view_info ( xi_workload, &view_info );

            xi->begin_update ( &xi_update_params_m (  
                .window_info = &new_window_info, 
                .input_state = &input_state, 
                .input_buffer = &input_buffer,
                .view_info = &view_info
            ) );

            xi->begin_window ( xi_workload, &ui_window );
            xi->begin_section ( xi_workload, &ui_section );
            xi->add_label ( xi_workload, &ui_label );
            xi->newline();
            xi->add_label ( xi_workload, &ui_switch_label );
            xi->add_switch ( xi_workload, &ui_switch );
            xi->newline();
            xi->add_label ( xi_workload, &ui_slider_label );
            xi->add_slider ( xi_workload, &ui_slider );
            xi->newline();
            xi->end_section ( xi_workload );
            xi->begin_section ( xi_workload, &ui_section2 );
            xi->add_label ( xi_workload, &ui_select_label );
            xi->add_select ( xi_workload, &ui_select );
            xi->newline();
            xi->add_label ( xi_workload, &ui_button_label );
            xi->add_button ( xi_workload, &ui_button );
            xi->newline();
            xi->add_textfield ( xi_workload, &ui_textfield );
            xi->newline();
            xi->add_property_editor ( xi_workload, &ui_property_editor_3f32 );
            xi->newline();
            xi->add_property_editor ( xi_workload, &ui_property_editor_3f32_2 );
            xi->end_section ( xi_workload );
            xi->end_window ( xi_workload );

            xi->draw_transform ( xi_workload, &xform_state );

            xi->end_update();

            // TODO updating node params like this is only ok when sim and render are serial
            ui_node_args.device = device;
            ui_node_args.xg_workload = workload;
            ui_node_args.resolution_x = new_window_info.width;
            ui_node_args.resolution_y = new_window_info.height;
            ui_node_args.xi_workload = xi_workload;
        }

        xf->execute_graph ( graph, workload, 0 );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );

        xs->update_pipeline_states ( workload );
    }

    std_virtual_heap_free ( select_alloc );

    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xi_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
    std_module_unload_m ( rv_module_name_m );
}

void std_main ( void ) {
    xi_test();
    std_log_info_m ( "xi_test COMPLETE!" );
}
