#include <memview.h>

#include <std_app.h>
#include <std_file.h>
#include <std_allocator.h>
#include <std_sort.h>

#include <wm.h>
#include <xg.h>
#include <xs.h>
#include <xi.h>
#include <xf.h>

typedef struct {
    wm_i* wm;
    xg_i* xg;
    xs_i* xs;
    xi_i* xi;
    xf_i* xf;
} memview_modules_t;

typedef struct {
    wm_window_h window;
    wm_input_state_t input_state;
    xi_font_h font;
    xi_window_state_t window_state;

    float time_slider_value;
    uint64_t time;
    uint64_t time_records_begin;
    uint64_t time_records_end;
    uint32_t min_allocation;
    uint64_t allocator_idx;
} memview_ui_state_t;

typedef struct {
    xg_device_h device;
    xg_workload_h xg_workload;
    xi_workload_h xi_workload;
    uint32_t resolution_x;
    uint32_t resolution_y;
} memview_ui_pass_args_t;

typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    xg_device_h device;
    xg_swapchain_h swapchain;
    xf_graph_h render_graph;
    // TODO need better way to pass per-frame data to node execute...
    memview_ui_pass_args_t ui_pass_args;
    std_tick_t frame_tick;
} memview_render_state_t;

typedef struct {
    char name[32];
} memview_allocator_t;

typedef struct {
    char path[std_path_size_m];
    bool is_loaded;
    std_stack_t allocator;
    memview_allocator_t* allocators;
    uint64_t allocator_count;
    std_allocator_log_record_t* records;
    uint64_t record_count;
    void* min_address;
    void* max_address;
    std_allocator_log_record_t* filtered_records;
    uint64_t filtered_record_count;
} memview_log_state_t;

typedef struct {
    std_app_i api;
    memview_modules_t modules;
    uint64_t tick_id;
    memview_ui_state_t ui;
    memview_render_state_t render;
    memview_log_state_t log;
} memview_state_t;

std_module_implement_state_m ( memview );

static int memview_log_record_cmp_timestamp ( const void* a, const void* b, const void* user_args ) {
    std_unused_m ( user_args );
    std_auto_m l1 = ( std_allocator_log_record_t* ) a;
    std_auto_m l2 = ( std_allocator_log_record_t* ) b;
    if ( l1->timestamp.count < l2->timestamp.count ) {
        return -1;
    } else if ( l1->timestamp.count > l2->timestamp.count ) {
        return 1;
    } else {
        return 0;
    }
}

static int memview_log_record_cmp_address ( const void* a, const void* b, const void* user_args ) {
    std_unused_m ( user_args );
    std_auto_m l1 = ( std_allocator_log_record_t* ) a;
    std_auto_m l2 = ( std_allocator_log_record_t* ) b;
    if ( l1->address < l2->address ) {
        return -1;
    } else if ( l1->address > l2->address ) {
        return 1;
    } else {
        return 0;
    }
}

#define memview_max_allocators_m 64
static void memview_register_log_allocator ( const std_allocator_log_record_t* record ) { 
    for ( uint64_t i = 0; i < memview_state->log.allocator_count; ++i ) {
        if ( std_str_cmp ( memview_state->log.allocators[i].name, record->allocator ) == 0 ) {
            return;
        }
    }

    std_assert_m ( memview_state->log.allocator_count < memview_max_allocators_m );
    memview_allocator_t* allocator = &memview_state->log.allocators[memview_state->log.allocator_count++];
    std_str_copy_static_m ( allocator->name, record->allocator );
}

static void memview_log_load_data ( void ) {
    memview_state_t* state = memview_state;
    state->log.is_loaded = true;

    std_buffer_t data = std_file_read_to_virtual_heap ( state->log.path );

    std_stack_clear ( &state->log.allocator );
    uint64_t* record_count_ptr = ( uint64_t* ) data.base;
    uint64_t record_count = *record_count_ptr;
    state->log.record_count = record_count;
    state->log.records = std_stack_alloc_array_m ( &state->log.allocator, std_allocator_log_record_t, record_count );

    state->log.allocator_count = 0;
    state->log.allocators = std_stack_alloc_array_m ( &state->log.allocator, memview_allocator_t, memview_max_allocators_m );

    std_allocator_log_record_t* data_records = ( std_allocator_log_record_t* ) ( record_count_ptr + 1 );
    std_sort_insertion_copy ( state->log.records, data_records, sizeof ( std_allocator_log_record_t ), record_count, memview_log_record_cmp_timestamp, NULL );

    void* min_address = ( void* ) -1;
    void* max_address = 0;
    for ( uint64_t i = 0; i < record_count; ++i ) {
        std_allocator_log_record_t* record = &state->log.records[i];
        memview_register_log_allocator ( record );
        min_address = ( void* ) ( std_min_u64 ( ( uint64_t ) min_address, ( uint64_t ) record->address ) );
        max_address = ( void* ) ( std_max_u64 ( ( uint64_t ) max_address, ( uint64_t ) record->address ) );
    }
    state->log.min_address = min_address;
    state->log.max_address = max_address;

    state->log.filtered_records = NULL;
    state->log.filtered_record_count = 0;

    std_virtual_heap_free ( data.base );

    state->ui.time_slider_value = 0;
    state->ui.time_records_begin = 0;
    state->ui.time_records_end = 0;
    state->ui.min_allocation = 128;
    state->ui.allocator_idx = 0;
}

static void memview_ui_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;
    std_auto_m args = ( memview_ui_pass_args_t* ) user_args;

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

    xi_i* xi = std_module_get_m ( xi_module_name_m );
    key = xi->flush_workload ( args->xi_workload, &params );
}

static void memview_boot ( void ) {
    memview_state_t* state = memview_state;
    wm_i* wm = state->modules.wm;
    xg_i* xg = state->modules.xg;
    xi_i* xi = state->modules.xi;
    xf_i* xf = state->modules.xf;

    state->render.resolution_x = 1024;
    state->render.resolution_y = 768;

    wm_window_h window = wm->create_window ( &wm_window_params_m (
        .name = "memview",
        .width = state->render.resolution_x,
        .height = state->render.resolution_y,
    ) );
    memview_state->ui.window = window;

    xg_device_h device;
    xg->get_devices ( &device, 1 );
    xg->activate_device ( device );
    state->render.device = device;

    xg_swapchain_h swapchain = xg->create_window_swapchain ( &xg_swapchain_window_params_m (
        .texture_count = 3,
        .format = xg_format_b8g8r8a8_unorm_m,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_render_target_m,
        .device = device,
        .window = window,
        .debug_name = "swapchain",
    ) );
    state->render.swapchain = swapchain;

    xi->load_shaders ( device );

    char font_path_buffer[128];
    std_string_t font_path = std_static_string_m ( font_path_buffer );
    std_path_append_dir ( &font_path, std_source_data_path_m );
    std_path_append_file ( &font_path, "ProggyVector-Regular.ttf" );
    std_buffer_t font_data = std_file_read_to_virtual_heap ( font_path.str );
    xi_font_h font = xi->create_font ( font_data, &xi_font_params_m (
        .xg_device = device,
        .pixel_height = 16,
        .first_char_code = xi_font_char_ascii_base_m,
        .char_count = xi_font_char_ascii_count_m,
        .debug_name = "proggy_clean"
    ) );
    std_virtual_heap_free ( font_data.base );
    state->ui.font = font;

    xf_graph_h graph = xf->create_graph ( &xf_graph_params_m (
        .device = device,
        .debug_name = "render_graph",
    ) );
    state->render.render_graph = graph;

    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( graph, swapchain );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { xf_texture_clear_m () }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
        )
    ) );

    xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "ui_pass",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = memview_ui_pass,
            .user_args = &state->render.ui_pass_args,
            .copy_args = false,
            .key_space_size = 16
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = swapchain_multi_texture ) },
            .presentable_texture = swapchain_multi_texture,
        )
    ) );

    state->ui.window_state = xi_window_state_m (
        .title = "memview",
        .width = state->render.resolution_x,
        .height = state->render.resolution_y,
        .scrollable = true,
        .movable = false,
        .resizable = false,
        .minimizable = false,
        .style = xi_window_style_m (
            .font = state->ui.font,
        ),
    );

    state->log.allocator = std_stack_create ( 1024 * 1024 * 1024 * 1 ); // 1GB 
}

static void memview_update_ui ( const wm_window_info_t* new_window_info, const wm_input_state_t* input_state, const wm_input_state_t* new_input_state, xg_workload_h workload ) {
    memview_state_t* state = memview_state;
    wm_i* wm = state->modules.wm;
    xi_i* xi = state->modules.xi;

    xi_workload_h xi_workload = xi->create_workload();

    wm_input_buffer_t input_buffer;
    wm->get_window_input_buffer ( state->ui.window, &input_buffer );

    xi->begin_update ( &xi_update_params_m (
        .window_info = new_window_info,
        .input_state = new_input_state,
        .input_buffer = &input_buffer,
    ) );

    state->ui.window_state.width = new_window_info->width;
    state->ui.window_state.height = new_window_info->height;
    if ( state->log.is_loaded ) {
        std_string_t title = std_static_string_m ( state->ui.window_state.title );
        std_string_append ( &title, "memview - " );
        std_string_append ( &title, state->log.path );
    }

    bool ignore_scroll = new_input_state->keyboard[wm_keyboard_state_ctrl_left_m];
    state->ui.window_state.ignore_scroll_input = ignore_scroll;
    state->ui.window_state.steady_scroll_on_content_resize = !ignore_scroll;

    xi->begin_window ( xi_workload, &state->ui.window_state );

    bool load = false;
    if ( xi->add_button ( xi_workload, &xi_button_state_m (
        .text = "Load",
        .style = xi_style_m (
            .horizontal_padding = 10
        ),
    ) ) ) {
        char path_buffer[std_path_size_m] = {};
        if ( xi->file_pick ( std_static_buffer_m ( path_buffer ), NULL ) ) {
            std_str_copy_static_m ( state->log.path, path_buffer );
            memview_log_load_data();
            load = true;
        }
    }

    if ( state->log.is_loaded ) {
        // allocator
        const char* allocator_select_items[64];
        for ( uint64_t i = 0; i < memview_state->log.allocator_count; ++i ) {
            allocator_select_items[i] = memview_state->log.allocators[i].name;
        }
        xi_select_state_t allocator_select = xi_select_state_m (
            .items = allocator_select_items,
            .item_count = memview_state->log.allocator_count,
            .item_idx = memview_state->ui.allocator_idx,
            .width = 100,
            .sort_order = 1,
        );
        if ( xi->add_select ( xi_workload, &allocator_select ) ) {
            memview_state->ui.allocator_idx = allocator_select.item_idx;
        }

        // slider
        xi_slider_state_t time_slider = xi_slider_state_m (
            .width = new_window_info->width - 400,
            .value = state->ui.time_slider_value,
            .delayed = true,
            .style = xi_style_m (
                .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
            ),
        );
        bool needs_update = xi->add_slider ( xi_workload, &time_slider ) || load;
        if ( needs_update )
        {
            // convert slider value to time value
            uint64_t record_count = state->log.record_count;
            uint64_t begin_time = state->log.records[0].timestamp.count;
            uint64_t end_time = state->log.records[record_count - 1].timestamp.count;
            uint64_t time = begin_time + ( uint64_t ) ( ( end_time - begin_time ) * ( double ) time_slider.value );

            // find first record with matching (more or equal) timestamp
            uint64_t time_records_begin = 0;
            for ( ; time_records_begin < record_count; ++time_records_begin ) {
                if ( state->log.records[time_records_begin].timestamp.count >= time ) {
                    break;
                }
            }

            // find last record with matching timestamp
            uint64_t time_records_end = time_records_begin;
            for ( ; time_records_end < state->log.record_count; ++time_records_end ) {
                std_allocator_log_record_t* record = &state->log.records[time_records_begin];
                std_allocator_log_record_t* r = &state->log.records[time_records_end];
                if ( record->timestamp.count != r->timestamp.count ) {
                    break;
                }
            }

            // round time to existing timestamp record
            time = state->log.records[time_records_begin].timestamp.count;

            // update slider state to snap to record
            float slider_value = ( double ) ( time - begin_time ) / ( end_time - begin_time );
            state->ui.time_slider_value = slider_value;

            // update ui state
            state->ui.time_records_begin = time_records_begin;
            state->ui.time_records_end = time_records_end;
            state->ui.time = time;
        }

        // slider label
        xi_label_state_t slider_label = xi_label_state_m (
            .font = state->ui.font,
            .style = xi_style_m (
                .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                .horizontal_margin = 10,
            )
        );
        std_timestamp_to_string ( ( std_timestamp_t ) { state->ui.time }, slider_label.text, sizeof ( slider_label.text ) );
        //std_u64_to_str ( slider_label.text, sizeof ( slider_label.text ), state->ui.time );
        xi->add_label ( xi_workload, &slider_label );

        // filter
        if ( needs_update ) {
            if ( state->log.filtered_records ) {
                std_stack_free_from ( &state->log.allocator, state->log.filtered_records );
            }

            //uint64_t time_records_begin = state->ui.time_records_begin;
            uint64_t time_records_end = state->ui.time_records_end;
            void* filtered_base = state->log.allocator.top;
            for ( uint64_t record_it = 0; record_it < time_records_end; ++record_it ) {
                std_allocator_log_record_t* record = &state->log.records[record_it];

                if ( record->type == std_allocator_log_record_free_m ) {
                    continue;
                }
                
                bool freed = false;
                for ( uint64_t i = record_it + 1; i < time_records_end; ++i ) {
                    std_allocator_log_record_t* r = &state->log.records[i];
                    if ( record->address == r->address ) {
                        //std_assert_m ( r->type == std_allocator_log_record_free_m );
                        if ( r->type != std_allocator_log_record_free_m ) {
                            std_log_error_m ( "Allocations "
                                std_fmt_ptr_m ":" std_fmt_u64_m " " std_fmt_str_m ":" std_fmt_size_m " and " 
                                std_fmt_ptr_m ":" std_fmt_u64_m " " std_fmt_str_m ":" std_fmt_size_m " have overlapping addresses",
                                record->address, record->size, record->scope.file, record->scope.line,
                                r->address, r->size, r->scope.file, r->scope.line
                            );
                        }
                        freed = true;
                        break;
                    }
                }

                if ( !freed ) {
                    std_stack_write ( &state->log.allocator, record, sizeof ( *record ) );
                }
            }

            state->log.filtered_record_count = ( state->log.allocator.top - filtered_base ) / sizeof ( std_allocator_log_record_t );
            std_allocator_log_record_t tmp;
            std_sort_insertion ( filtered_base, sizeof ( std_allocator_log_record_t ), state->log.filtered_record_count, memview_log_record_cmp_address, NULL, &tmp );
            state->log.filtered_records = ( std_allocator_log_record_t* ) filtered_base;
        }

        std_allocator_log_record_t* filtered_records = state->log.filtered_records;
        uint64_t filtered_record_count = state->log.filtered_record_count;

        // draw
        xi->newline();
        uint32_t line_width = xi->line_remaining_size() - 150 * 2;
        uint32_t min_allocation = state->ui.min_allocation;
        uint64_t address_step = line_width * min_allocation;
        uint64_t base_address = 0;
        uint64_t record_it = 0;
        bool line_first;
        uint64_t record_remaining_size = 0;
        uint64_t record_offset = 0;
        uint64_t pixel_to_size = address_step / line_width;
        bool tooltip_visible = false;

        if ( new_input_state->keyboard[wm_keyboard_state_ctrl_left_m] ) {
            if ( new_input_state->mouse[wm_mouse_state_wheel_up_m] || new_input_state->mouse[wm_mouse_state_wheel_down_m] ) {
                if ( new_input_state->mouse[wm_mouse_state_wheel_up_m] ) {
                    min_allocation = std_max ( min_allocation / 2, 8 );
                } else if ( new_input_state->mouse[wm_mouse_state_wheel_down_m] ) {
                    min_allocation *= 2;
                }
                address_step = line_width * min_allocation;
                pixel_to_size = address_step / line_width;
                state->ui.min_allocation = min_allocation;
            }
        } 

        if ( filtered_record_count > 0 ) {
            uint64_t align_step = std_pow2_round_up_u64 ( address_step );
            base_address = std_align_u64 ( filtered_records[0].address, align_step ) - align_step;
        }

        while ( record_it < filtered_record_count ) {
            xi->newline();
            line_first = true;
            std_allocator_log_record_t* record = &filtered_records[record_it];

            uint64_t range_max = base_address + address_step;
            while ( record->address + record_offset >= range_max ) {
                base_address += address_step;
                range_max = base_address + address_step;
            }

            while ( record_it < filtered_record_count && record->address + record_offset < range_max ) {
                if ( line_first ) {
                    line_first = false;
                    xi_label_state_t label = xi_label_state_m ( .width = 150 );
                    std_str_format_static_m ( label.text, std_fmt_ptr_m, base_address );
                    xi->add_label ( xi_workload, &label );
                }
                
                float record_begin_t = std_lerp_inverse_u64 ( ( uint64_t ) base_address, ( uint64_t ) range_max, ( uint64_t ) record->address + record_offset );
                float line_t = std_lerp_inverse_u32 ( 0, line_width, xi->line_offset() - 150 );
                float delta_t = record_begin_t - line_t;
                if ( delta_t > 0 ) {
                    uint32_t offset = delta_t * line_width;
                    xi->add_line_offset ( offset );
                }

                uint32_t line_remaining_width = xi->line_remaining_size() - 150;
                if ( record_remaining_size == 0 ) {
                    record_remaining_size = record->size;
                }
                uint32_t record_width = record_remaining_size / pixel_to_size;
                uint32_t width = std_min_u64 ( record_width, line_remaining_width );                
                xi_id_t id = xi_mix_id_m ( record_it );
                uint64_t hash = std_hash_64_m ( ( uint64_t ) record->address );
                xi_label_state_t label = xi_label_state_m ( 
                    .width = width,
                    .id = id,
                    .style = xi_style_m ( .color = xi_color_rgba_u32_m ( hash & 0xff, ( hash >> 8 ) & 0xff, ( hash >> 16 ) & 0xff, 255 ) ),
                );
                xi->add_label ( xi_workload, &label );
                if ( !tooltip_visible && xi->get_hovered_element_id() == id ) {
                    xi_label_state_t tooltip = xi_label_state_m (
                        .sort_order = 1,
                    );
                    std_str_format_static_m ( tooltip.text, std_fmt_ptr_m ":" std_fmt_u64_m " " std_fmt_str_m ":" std_fmt_size_m, 
                        record->address, record->size, record->scope.file, record->scope.line );
                    xi->draw_tooltip ( xi_workload, &tooltip );
                    tooltip_visible = true;
                }

                record_remaining_size -= width * pixel_to_size;
                if ( record_remaining_size < pixel_to_size ) {
                    record_remaining_size = 0;
                }
                if ( record_remaining_size == 0 ) {
                    ++record_it;
                }
                record = &filtered_records[record_it];
                if ( record_it < filtered_record_count ) {
                    if ( record_remaining_size == 0 ) {
                        record_offset = 0;
                    } else {
                        record_offset = record->size - record_remaining_size;
                        break;
                    }
                }
            }

            if ( !line_first ) {
                xi_label_state_t label = xi_label_state_m ( .width = 150, .style = xi_style_m ( .horizontal_alignment = xi_horizontal_alignment_right_to_left_m ) );
                std_str_format_static_m ( label.text, std_fmt_ptr_m, range_max );
                xi->add_label ( xi_workload, &label );
            }
        }
    }

    xi->newline();

    xi->end_window ( xi_workload );
    xi->end_update();

    state->render.ui_pass_args.device = state->render.device;
    state->render.ui_pass_args.xg_workload = workload;
    state->render.ui_pass_args.resolution_x = new_window_info->width;
    state->render.ui_pass_args.resolution_y = new_window_info->height;
    state->render.ui_pass_args.xi_workload = xi_workload;
}

static std_app_state_e memview_update ( void ) {
    memview_state_t* state = memview_state;
    wm_i* wm = state->modules.wm;
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;

    wm_window_h window = state->ui.window;
    if ( !wm->is_window_alive ( window ) ) {
        return std_app_state_exit_m;
    }

    float target_fps = 24.f;
    float target_frame_period = target_fps > 0.f ? 1.f / target_fps * 1000.f : 0.f;
    std_tick_t frame_tick = state->render.frame_tick;
    std_tick_t new_tick = std_tick_now();
    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

    if ( delta_ms < target_frame_period ) {
        std_thread_this_sleep ( 0 );
        return std_app_state_tick_m;
    }

    state->render.frame_tick = new_tick;
    state->tick_id += 1;

    wm->update_window ( window );

    wm_input_state_t* input_state = &state->ui.input_state;
    wm_input_state_t new_input_state;
    wm->get_window_input_state ( window, &new_input_state );

    if ( new_input_state.keyboard[wm_keyboard_state_esc_m] ) {
        return std_app_state_exit_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
        return std_app_state_reload_m;
    }

    xg_workload_h workload = xg->create_workload ( state->render.device );

    wm_window_info_t new_window_info;
    wm->get_window_info ( &new_window_info, window );
    memview_update_ui ( &new_window_info, input_state, &new_input_state, workload );
    
    xf->execute_graph ( state->render.render_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->present_swapchain ( state->render.swapchain, workload );

    return std_app_state_tick_m;
}

std_app_state_e memview_tick ( void ) {
    if ( memview_state->tick_id == 0 ) {
        memview_boot();
        memview_state->render.frame_tick = std_tick_now();
        memview_state->tick_id = 1;
    }
    return memview_update();
}

std_module_export_m void* memview_load ( void* runtime ) {
    std_runtime_bind ( runtime );

    memview_state_t* state = memview_state_alloc();
    state->api.tick = memview_tick;
    state->tick_id = 0;
    state->modules = ( memview_modules_t ) {
        .wm = std_module_load_m ( wm_module_name_m ),
        .xg = std_module_load_m ( xg_module_name_m ),
        .xs = std_module_load_m ( xs_module_name_m ),
        .xi = std_module_load_m ( xi_module_name_m ),
        .xf = std_module_load_m ( xf_module_name_m ),
    };
    state->log = ( memview_log_state_t ) {
    };

    return state;
}

std_module_export_m void memview_unload ( void ) {
    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xi_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );

    memview_state_free();
}

std_module_export_m void memview_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );

    std_auto_m state = ( memview_state_t* ) api;
    state->api.tick = memview_tick;

    memview_state_bind ( state );

    xf_i* xf = state->modules.xf;
    xf_node_h ui_node = xf->get_node_by_name ( state->render.render_graph, "ui_pass" );
    xf->bind_custom_node_routine ( state->render.render_graph, ui_node, memview_ui_pass );
}
