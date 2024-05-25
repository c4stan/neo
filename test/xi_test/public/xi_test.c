#include <std_main.h>
#include <std_time.h>
#include <std_log.h>

#include <xs.h>
#include <xf.h>
#include <xi.h>
#include <fs.h>
#include <se.h>

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

static void clear_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
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
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        xg->cmd_clear_texture ( cmd_buffer, texture, color_clear, key++ );
    }
}

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

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // Bind swapchain texture as render target
    {
        xg_render_textures_binding_t render_textures;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        render_textures.depth_stencil.texture = xg_null_handle_m;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

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

        xi->flush_workload ( args->xi_workload, &params );
    }
}

static void xi_test ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    fs_i* fs = std_module_load_m ( fs_module_name_m );

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
            .present_mode = xg_present_mode_mailbox_m,
            .debug_name = "swapchain",
        );
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    // let xi register its shaders before building the xs database
    // TODO support dynamic add and rebuild (do we already do that?)
    xs_i* xs = std_module_load_m ( xs_module_name_m );
    xi_i* xi = std_module_load_m ( xi_module_name_m );
    xi->register_shaders ( xs );
    {
        xs->set_output_folder ( "output/shader/" );

        xs_database_build_params_t build_params;
        build_params.viewport_width = 600;
        build_params.viewport_height = 400;
        xs_database_build_result_t result = xs->build_database_shaders ( device, &build_params );
        std_log_info_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders built", result.successful_pipeline_states, result.successful_shaders );

        if ( result.failed_shaders || result.failed_pipeline_states ) {
            std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed" );
        }
    }

    // create test font
    xi_font_h font;
    {
        fs_file_h font_file = fs->open_file ( "assets/ProggyVector-Regular.ttf", fs_file_read_m );
        fs_file_info_t font_file_info;
        fs->get_file_info ( &font_file_info, font_file );
        void* font_data_alloc = std_virtual_heap_alloc ( font_file_info.size, 16 );
        fs->read_file ( font_data_alloc, font_file_info.size, font_file );

        xi_font_params_t font_params;
        font_params.xg_device = device;
        font_params.pixel_height = 16;
        font_params.first_char_code = xi_font_char_ascii_base_m;
        font_params.char_count = xi_font_char_ascii_count_m;
        font_params.outline = false;
        font = xi->create_font ( std_buffer ( font_data_alloc, font_file_info.size ), &font_params );
    }

    // create ui entities
    // TODO: handles inside components, xi has create_x api that returns handle and layouts in immediate mode,
    // and creates internal state for the element rect, then in the render pass callback flush_x on the handle
    // causes xi to consume and draw the previously set up rect to an xg cmd buffer
    // OR: simply build the xi_workload in here, and provide the workload only to the render pass, and the render
    // pass only calls flush on it.

    xf_i* xf = std_module_load_m ( xf_module_name_m );
    xf_graph_h graph = xf->create_graph ( device, swapchain );

    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );

    xf_node_h clear_node;
    {
        xf_node_params_t node_params = xf_default_node_params_m;
        node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m );
        node_params.execute_routine = clear_pass;
        std_str_copy_m ( node_params.debug_name, "clear" );
        node_params.debug_color = xg_debug_region_color_red_m;

        clear_node = xf->create_node ( graph, &node_params );
    }

    xf_ui_pass_args_t ui_node_args;
    xf_node_h ui_node;
    {
        xf_node_params_t node_params = xf_default_node_params_m;
        node_params.render_targets[node_params.render_targets_count++] = xf_render_target_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m );
        node_params.presentable_texture = swapchain_multi_texture;
        node_params.execute_routine = ui_pass;
        node_params.user_args = std_buffer_m ( &ui_node_args );
        node_params.copy_args = false;
        std_str_copy_m ( node_params.debug_name, "ui" );
        node_params.debug_color = xg_debug_region_color_green_m;
        node_params.key_space_size = 128;

        ui_node = xf->create_node ( graph, &node_params );
    }

    xf->debug_print_graph ( graph );

    wm_window_info_t window_info;
    wm->get_window_info ( window, &window_info );

    // ui
    #if 0
    xi_style_t window_style = xi_default_style_m;
    {
        window_style.font = font;
        window_style.font_height = 14;
        window_style.color = xi_color_black_m;//xi_color_rgba_u32_m ( 100, 100, 100, 100 );
    }
    xi_style_t label_style = xi_default_style_m;
    {
        label_style.font = font;
    }
    xi_style_t switch_style = xi_default_style_m;
    {
        switch_style.font = font;
        switch_style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m;
        switch_style.color = xi_color_blue_m;
    }
    xi_style_t slider_style = xi_default_style_m;
    {
        slider_style.color = xi_color_blue_m;
        slider_style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m;
    }
    xi_style_t button_style = xi_default_style_m;
    {
        button_style.color = xi_color_blue_m;
        button_style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m;
        button_style.font = font;
        button_style.font_height = 14;
    }
    xi_style_t text_style = xi_default_style_m;
    {
        text_style.color = xi_color_blue_m;
        text_style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m;
        text_style.font = font;
        text_style.font_height = 14;
    }
    xi_style_t select_style = xi_default_style_m;
    {
        select_style.color = xi_color_blue_m;
        select_style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m;
        select_style.font = font;
        select_style.font_height = 14;
    }
    #endif

    xi_window_state_t ui_window = xi_window_state_m (
        .title = "window",
        .x = 50,
        .y = 100,
        .width = 200,
        .height = 250,
        .padding_x = 10,
        .padding_y = 2,
        .style = xi_style_m (
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
    //char ui_slider_label_buffer[32];
    //{
    //    std_f32_to_str ( 0, ui_slider_label_buffer, 32 );
    //}
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

#if 0
    char ui_text_buffer[32] = {0};
    xi_text_state_t ui_text = xi_text_state_m (
        ui_text.width = 100;
        ui_text.height = 20;
        ui_text.text = ui_text_buffer;
        ui_text.text_capacity = 32;
    )
    xi_label_state_t ui_text_label = xi_default_label_state_m;
    {
        ui_text_label.height = 14;
        ui_text_label.text = ui_text.text;
    }
#endif

    //const char* ui_select_items[] = {"qwe", "asd", "zxc"};
    void* select_alloc = std_virtual_heap_alloc ( 128, 8 );
    {
        std_stack_t stack = std_stack ( select_alloc, 128 );
        char** p1 = std_stack_alloc_m ( &stack, char* );
        char** p2 = std_stack_alloc_m ( &stack, char* );
        char** p3 = std_stack_alloc_m ( &stack, char* );
        char* s1 = std_stack_alloc_array_m ( &stack, char, 8 );
        char* s2 = std_stack_alloc_array_m ( &stack, char, 8 );
        char* s3 = std_stack_alloc_array_m ( &stack, char, 8 );
        std_str_copy_m ( s1, "qwe" );
        std_str_copy_m ( s2, "asd" );
        std_str_copy_m ( s3, "zxc" );
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

    float target_fps = 30.f;
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
            xs_database_build_params_t build_params;
            build_params.viewport_width = window_info.width;
            build_params.viewport_height = window_info.height;
            xs->build_database_shaders ( device, &build_params );
        }

        wm_window_info_t new_window_info;
        wm->get_window_info ( window, &new_window_info );

        if ( window_info.width != new_window_info.width || window_info.height != new_window_info.height ) {
            xg->resize_swapchain ( swapchain, new_window_info.width, new_window_info.height );
            xf->refresh_external_texture ( swapchain_multi_texture );

            xs_database_build_params_t build_params;
            build_params.viewport_width = new_window_info.width;
            build_params.viewport_height = new_window_info.height;
            xs->build_database_shaders ( device, &build_params );
        }

        window_info = new_window_info;

        xg_workload_h workload = xg->create_workload ( device );

        xg->acquire_next_swapchain_texture ( swapchain, workload );
        //xg_texture_h xg_swapchain_texture = xg->get_swapchain_texture ( swapchain );
        //xf->bind_texture ( swapchain_texture, xg_swapchain_texture );

        // build ui
        {
            std_f32_to_str ( ui_slider.value, ui_slider_label.text, 32 );
            std_str_copy_m ( ui_switch_label.text, ui_switch.value ? "on" : "off" );
            std_str_copy_m ( ui_button_label.text, ui_button.pressed ? "click" : ui_button.down ? "down" : "up" );
            std_str_copy_m ( ui_select_label.text, ui_select.items[ui_select.item_idx] );

            xi_workload_h xi_workload = xi->create_workload();

            xi->begin_update ( &new_window_info, &input_state );

            xi->begin_window ( xi_workload, &ui_window );
            xi->begin_section ( xi_workload, &ui_section );
            xi->add_label ( xi_workload, &ui_label );
            xi->add_label ( xi_workload, &ui_label );
            xi->newline();
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
            xi->add_label ( xi_workload, &ui_button_label );
            xi->add_button ( xi_workload, &ui_button );
            xi->newline();
            xi->add_label ( xi_workload, &ui_select_label );
            xi->add_select ( xi_workload, &ui_select );
            xi->end_section ( xi_workload );
            //xi->newline();
            //xi->add_label ( xi_workload, &ui_button_label, &label_style );
            //xi->add_button ( xi_workload, &ui_button, &button_style );
            xi->end_window ( xi_workload );

            xi->end_update();

            // TODO updating node params like this is only ok when sim and render are serial
            ui_node_args.device = device;
            ui_node_args.xg_workload = workload;
            ui_node_args.resolution_x = 600;
            ui_node_args.resolution_y = 400;
            ui_node_args.xi_workload = xi_workload;
        }

        xf->execute_graph ( graph, workload );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );

        xs->update_pipeline_states ( workload );
    }

    std_module_unload_m ( xi_module_name_m );
    std_module_unload_m ( fs_module_name_m );
    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
}

void std_main ( void ) {
    xi_test();
    std_log_info_m ( "xi_test COMPLETE!" );
}
