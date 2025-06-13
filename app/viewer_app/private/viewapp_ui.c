#include "viewapp_ui.h"

#include "viewapp_state.h"
#include "viewapp_render.h"
#include "viewapp_scene.h"

#include "ui_pass.h"

#include <std_file.h>

void viewapp_boot_ui ( xg_device_h device ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xi_i* xi = state->modules.xi;

    xg_workload_h workload = xg->create_workload ( device );

    xi->load_shaders ( device );
    xi->init_geos ( device, workload );

    std_file_h font_file = std_file_open ( "assets/ProggyVector-Regular.ttf", std_file_read_m );
    std_file_info_t font_file_info;
    std_file_info ( &font_file_info, font_file );
    void* font_data_alloc = std_virtual_heap_alloc_m ( font_file_info.size, 16 );
    std_file_read ( font_data_alloc, font_file_info.size, font_file );

    state->ui.font = xi->create_font ( 
        std_buffer_m ( .base = font_data_alloc, .size = font_file_info.size ),
        &xi_font_params_m (
            .xg_device = device,
            .pixel_height = 16,
            .debug_name = "proggy_clean"
        )
    );

    std_virtual_heap_free ( font_data_alloc );

    state->ui.window_state = xi_window_state_m (
        .title = "control",
        .minimized = false,
        .x = 50,
        .y = 100,
        .width = 350,
        .height = 500,
        .padding_x = 10,
        .padding_y = 2,
        .style = xi_default_style_m (
            .font = state->ui.font,
            .color = xi_color_gray_m,
        )
    );

    state->ui.frame_section_state = xi_section_state_m ( .title = "frame" );
    state->ui.xg_alloc_section_state = xi_section_state_m ( .title = "memory" );
    state->ui.scene_section_state = xi_section_state_m ( .title = "scene" );
    state->ui.xf_graph_section_state = xi_section_state_m ( .title = "xf graph" );
    state->ui.entities_section_state = xi_section_state_m ( .title = "entities", .style = xi_default_style_m() );
    state->ui.xf_textures_state = xi_section_state_m ( .title = "xf textures" );

    state->ui.export_texture = xg->create_texture ( &xg_texture_params_m (
        .device = device,
        .width = state->render.resolution_x,
        .height = state->render.resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .allowed_usage = xg_texture_usage_bit_storage_m | xg_texture_usage_bit_sampled_m,
        .debug_name = "export",
    ) );

    xg->submit_workload ( workload );
}

static void mouse_pick ( uint32_t x, uint32_t y ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xi_i* xi = state->modules.xi;
    xf_i* xf = state->modules.xf;

    if ( xi->get_active_element_id () != 0 ) {
        return;
    }

    xg_workload_h workload = xg->create_workload ( state->render.device );
    viewapp_update_workload_uniforms ( workload );
    xf->execute_graph ( state->render.mouse_pick_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->wait_all_workload_complete();

    uint32_t resolution_x = state->render.resolution_x;

    xg_texture_info_t info;
    xg->get_texture_info ( &info, state->render.object_id_readback_texture );
    std_auto_m data = ( uint8_t* ) info.allocation.mapped_address;

    uint8_t pixel = data[y * resolution_x + x];
    uint8_t id = pixel;

    se_i* se = state->modules.se;
    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &query_result.entities );
    uint32_t mesh_count = query_result.entity_count;

    for ( uint32_t i = 0; i < mesh_count; ++i ) {
        se_entity_h* entity = se_stream_iterator_next ( &entity_iterator );
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );

        if ( mesh_component->object_id == id ) {
            state->ui.mouse_pick_entity = *entity;
            se_entity_properties_t props;
            se->get_entity_properties ( &props, *entity );
            std_log_info_m ( "Mouse pick entity handle " std_fmt_u64_m " " std_fmt_str_m, state->ui.mouse_pick_entity, props.name );
            return;
        }
    }
 
    state->ui.mouse_pick_entity = se_null_handle_m;
}

static bool viewapp_get_camera_info ( rv_view_info_t* view_info ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    rv_i* rv = state->modules.rv;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );

    se_stream_iterator_t camera_iterator = se_component_iterator_m ( camera_query_result.components, 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );

        if ( !camera_component->enabled ) {
            continue;
        }

        rv->get_view_info ( view_info, camera_component->view );
        return true;
    }

    return false;
}

void viewapp_update_ui ( wm_window_info_t* window_info, wm_input_state_t* old_input_state, wm_input_state_t* input_state, xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    wm_i* wm = state->modules.wm;
    xg_i* xg = state->modules.xg;
    xf_i* xf = state->modules.xf;
    xi_i* xi = state->modules.xi;
    se_i* se = state->modules.se;
    wm_window_h window = state->render.window;

    xi_workload_h xi_workload = xi->create_workload();
    set_ui_pass_xi_workload ( xi_workload );

    wm_input_buffer_t input_buffer;
    wm->get_window_input_buffer ( window, &input_buffer );

    rv_view_info_t view_info;
    viewapp_get_camera_info ( &view_info );

    // TODO remove
    xi->set_workload_view_info ( xi_workload, &view_info );

    xi->begin_update ( &xi_update_params_m (
        .window_info = window_info,
        .input_state = input_state,
        .input_buffer = &input_buffer,
        .view_info = &view_info
    ) );

    // export
    if ( state->ui.export_source != xf_null_handle_m ) {
        xi->draw_overlay_texture ( xi_workload, &xi_overlay_texture_state_m ( 
            .handle = state->ui.export_texture,
            .width = state->render.resolution_x,
            .height = state->render.resolution_y,
        ) );
    }

    // ui
    xi->begin_window ( xi_workload, &state->ui.window_state );
    
    // frame
    xi->begin_section ( xi_workload, &state->ui.frame_section_state );
    {
        xi_label_state_t device_name_label = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        xg_device_info_t device_info;
        xg->get_device_info ( &device_info, state->render.device );
        std_str_copy_static_m ( device_name_label.text, device_info.name );
        xi->add_label ( xi_workload, &xi_label_state_m ( .text = "device" ) );
        xi->add_label ( xi_workload, &device_name_label );
        xi->newline();

        xi_label_state_t frame_id_label = xi_label_state_m ( .text = "frame id" );
        xi_label_state_t frame_id_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        std_u32_to_str ( frame_id_value.text, xi_label_text_size, state->render.frame_id, 0 );
        xi->add_label ( xi_workload, &frame_id_label );
        xi->add_label ( xi_workload, &frame_id_value );
        xi->newline();

        xi->add_label ( xi_workload, &xi_label_state_m ( .text = "target fps" ) );
        char target_fps_select_strings[std_static_array_capacity_m ( state->ui.target_fps_values )][6];
        const char* target_fps_select_items[std_static_array_capacity_m ( state->ui.target_fps_values )];
        for ( uint32_t i = 0; i < std_static_array_capacity_m ( state->ui.target_fps_values ); ++i ) {
            std_u32_to_str ( target_fps_select_strings[i], 6, state->ui.target_fps_values[i], 0 );
            target_fps_select_items[i] = target_fps_select_strings[i];
        }
        xi_select_state_t target_fps_select = xi_select_state_m (
            .items = target_fps_select_items,
            .item_count = std_static_array_capacity_m ( target_fps_select_items ),
            .item_idx = state->ui.target_fps_idx,
            .width = 30,
            .sort_order = 2,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );
        if ( xi->add_select ( xi_workload, &target_fps_select ) ) {
            state->ui.target_fps_idx = target_fps_select.item_idx;
            state->render.target_fps = state->ui.target_fps_values[target_fps_select.item_idx];
        }
        xi->newline();

        xi_label_state_t frame_time_label = xi_label_state_m ( .text = "frame time" );
        xi_label_state_t frame_time_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        std_f32_to_str ( state->render.delta_time_ms, frame_time_value.text, xi_label_text_size );
        xi->add_label ( xi_workload, &frame_time_label );
        xi->add_label ( xi_workload, &frame_time_value );
        xi->newline();

        xi_label_state_t fps_label = xi_label_state_m ( .text = "fps" );
        xi_label_state_t fps_value = xi_label_state_m ( .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m );
        float fps = 1000.f / state->render.delta_time_ms;
        std_f32_to_str ( fps, fps_value.text, xi_label_text_size );
        xi->add_label ( xi_workload, &fps_label );
        xi->add_label ( xi_workload, &fps_value );
        xi->newline();
    }
    xi->end_section ( xi_workload );

    // xg allocator
    xi->begin_section ( xi_workload, &state->ui.xg_alloc_section_state );
    {
        std_allocator_info_t cpu_info;
        std_allocator_info ( &cpu_info );
        std_platform_memory_info_t memory_info = std_platform_memory_info();
        xi->add_label ( xi_workload, &xi_label_state_m ( .text = "cpu" ) );
        xi_label_state_t size_label = xi_label_state_m ( 
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );
        std_stack_t stack = std_static_stack_m ( size_label.text );
        char buffer[32];
        std_size_to_str_approx ( buffer, 32, cpu_info.used_heap_size );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, "/" );
        std_size_to_str_approx ( buffer, 32, cpu_info.total_heap_size );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, "/" );
        std_size_to_str_approx ( buffer, 32, memory_info.total_ram_size );
        std_stack_string_append ( &stack, buffer );
        xi->add_label ( xi_workload, &size_label );
    }
    xi->newline();
    for ( uint32_t i = 0; i < xg_memory_type_count_m; ++i ) {
        xg_allocator_info_t info;
        xg->get_allocator_info ( &info, state->render.device, i );
        {
            xi_label_state_t type_label = xi_label_state_m();
            std_stack_t stack = std_static_stack_m ( type_label.text );
            const char* memory_type_name;
            switch ( i ) {
                case xg_memory_type_gpu_only_m:     memory_type_name = "gpu"; break;
                case xg_memory_type_gpu_mapped_m:   memory_type_name = "mapped"; break;
                case xg_memory_type_upload_m:       memory_type_name = "upload"; break;
                case xg_memory_type_readback_m:     memory_type_name = "readback"; break;
            }
            std_stack_string_append ( &stack, memory_type_name );
            xi->add_label ( xi_workload, &type_label );
        }
        {
            xi_label_state_t size_label = xi_label_state_m ( 
                .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
            );
            std_stack_t stack = std_static_stack_m ( size_label.text );
            char buffer[32];
            std_size_to_str_approx ( buffer, 32, info.allocated_size );
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, "/" );
            std_size_to_str_approx ( buffer, 32, info.reserved_size );
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, "/" );
            std_size_to_str_approx ( buffer, 32, info.system_size );
            std_stack_string_append ( &stack, buffer );
            xi->add_label ( xi_workload, &size_label );
        }
        xi->newline();
    }
    xi->end_section ( xi_workload );

    // scene
    xi->begin_section ( xi_workload, &state->ui.scene_section_state );
    {
        xi_label_state_t scene_select_label = xi_label_state_m ( .text = "scene" );
        xi->add_label ( xi_workload, &scene_select_label );

        const char* scene_select_items[] = { "cornell box", "field", "..." };
        xi_select_state_t scene_select = xi_select_state_m (
            .items = scene_select_items,
            .item_count = std_static_array_capacity_m ( scene_select_items ),
            .item_idx = state->scene.active_scene,
            .width = 100,
            .sort_order = 2,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );
        if ( xi->add_select ( xi_workload, &scene_select ) ) {
            if ( scene_select.item_idx == 2 ) {
                xi->file_pick ( std_buffer_static_array_m ( state->scene.custom_scene_path ), NULL );
            }
            if ( scene_select.item_idx != 2 || state->scene.custom_scene_path[0] != '\0' ) {
                viewapp_load_scene ( scene_select.item_idx );
            }
        }
    }
    xi->end_section ( xi_workload );
    
    // xf graph
    xi->begin_section ( xi_workload, &state->ui.xf_graph_section_state );
    {
        xi_label_state_t graph_select_label = xi_label_state_m ( .text = "graph" );
        xi->add_label ( xi_workload, &graph_select_label );

#if xg_enable_raytracing_m
        const char* graph_select_items[] = { "raster", "raytrace" };
#else
        const char* graph_select_items[] = { "raster" };
#endif
        xi_select_state_t graph_select = xi_select_state_m (
            .items = graph_select_items,
            .item_count = std_static_array_capacity_m ( graph_select_items ),
            .item_idx = state->render.active_graph == state->render.raster_graph ? 0 : 1,
            .width = 100,
            .sort_order = 1,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
        );

        xi->add_select ( xi_workload, &graph_select );

        xf_graph_h active_graph = graph_select.item_idx == 0 ? state->render.raster_graph : state->render.raytrace_graph;
        if ( state->render.active_graph != active_graph ) {
            state->render.active_graph = active_graph;
        }

        xi->newline();

        xf_graph_info_t graph_info;
        xf->get_graph_info ( &graph_info, state->render.active_graph );

        const uint64_t* timings = xf->get_graph_timings ( state->render.active_graph );

        std_assert_m ( graph_info.node_count < 64 );
        uint64_t timestamp_sum = 0;
        for ( uint32_t i = 0, node_id = -1; i < graph_info.node_count; ++i ) {
            xf_node_info_t node_info;
            xf->get_node_info ( &node_info, state->render.active_graph, graph_info.nodes[i] );

            if ( std_str_cmp ( node_info.debug_name, "export" ) == 0 ) {
                continue;
            }

            ++node_id;

            // skip mip generation passes...
            if ( std_str_find ( node_info.debug_name, "_mip_" ) != std_str_find_null_m ) {
                continue;
            }

            xi_label_state_t node_label = xi_label_state_m (
                .id = xi_mix_id_m ( i ),
            );
            std_str_copy_static_m ( node_label.text, node_info.debug_name );
            xi->add_label ( xi_workload, &node_label );

            bool any_resource = node_info.resources.render_targets_count + node_info.resources.storage_texture_writes_count + node_info.resources.copy_texture_writes_count > 0;
            bool hover = xi->test_layer_row_hover ( 14 );
            bool expanded = std_bitset_test ( state->ui.expanded_nodes_bitset, node_id );
            if ( any_resource && ( hover || expanded ) ) {
                xi_arrow_state_t node_arrow = xi_arrow_state_m (
                    .width = 14,
                    .height = 14,
                    .style = xi_style_m (
                        .horizontal_margin = 8
                    ),
                    .expanded = expanded,
                );
                bool changed = xi->add_arrow ( xi_workload, &node_arrow );
                if ( changed ) {
                    if ( node_arrow.expanded ) {
                        std_bitset_set ( state->ui.expanded_nodes_bitset, node_id );
                    } else {
                        std_bitset_clear ( state->ui.expanded_nodes_bitset, node_id );
                    }
                }
            }

            if ( node_info.passthrough ) {
                bool node_enabled = node_info.enabled;
                
                xi_switch_state_t node_switch = xi_switch_state_m (
                    .width = 14,
                    .height = 14,
                    .value = node_enabled,
                    .style = xi_style_m (
                        .horizontal_alignment = xi_horizontal_alignment_right_to_left_m
                    ),
                    .id = xi_mix_id_m ( node_id ),
                );
                xi->add_switch ( xi_workload, &node_switch );

                if ( node_switch.value != node_enabled ) {
                    xf->node_set_enabled ( state->render.active_graph, graph_info.nodes[i], node_switch.value );
                    xf->invalidate_graph ( state->render.active_graph, workload );
                    state->render.graph_reload = true;
                }
            }

            uint64_t timestamp_diff = timings[i * 2 + 1] - timings[i * 2];
            //timestamp_sum += timestamp_diff;
            float ms = xg->timestamp_to_ns ( state->render.device ) * timestamp_diff / 1000000.f;
            char buffer[32];
            std_f32_to_str ( ms, buffer, 32 );
            xi_label_state_t time_label = xi_label_state_m (
                .style = xi_style_m (
                    .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                    .horizontal_border_margin = 25,
                )
            );
            std_str_copy_static_m ( time_label.text, buffer );
            xi->add_label ( xi_workload, &time_label );

            xi->newline();

            if ( expanded ) {
                for ( uint32_t j = 0; j < node_info.texture_count; ++j ) {
                    xf_node_texture_info_t* node_tex_info = &node_info.texture_info[j];
                    if ( node_tex_info->access == xf_resource_access_render_target_m || node_tex_info->access == xf_resource_access_storage_write_m ) {
                        xf_texture_h texture_handle = node_tex_info->handle;
                        xf_texture_info_t texture_info;
                        xf->get_texture_info ( &texture_info, texture_handle );
                        xi_label_state_t texture_label = xi_label_state_m (
                            .style = xi_style_m (
                                .horizontal_margin = 16,
                            )
                        );
                        std_str_copy_static_m ( texture_label.text, texture_info.debug_name );
                        xi->add_label ( xi_workload, &texture_label );
                        uint64_t id = xi_mix_id_m ( j );
                        xi_switch_state_t export_switch = xi_switch_state_m (
                            .width = 14,
                            .height = 14,
                            .value = state->ui.export_id == id,
                            .id = id,
                            .style = xi_style_m (
                                .horizontal_margin = 8,
                            )
                        );
                        xi->add_switch ( xi_workload, &export_switch );

                        bool switch_change = export_switch.value != ( state->ui.export_id == id );

                        const char* channel_select_items[] = { "r", "g", "b", "a", "1", "0" };
                        xi_select_state_t channel_select_0 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = state->ui.export_channels[0],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi_select_state_t channel_select_1 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = state->ui.export_channels[1],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi_select_state_t channel_select_2 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = state->ui.export_channels[2],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi_select_state_t channel_select_3 = xi_select_state_m (
                            .items = channel_select_items,
                            .item_count = std_static_array_capacity_m ( channel_select_items ),
                            .item_idx = state->ui.export_channels[3],
                            .width = 20,
                            .sort_order = node_info.texture_count - j,
                            .id = xi_mix_id_m ( j ),
                        );
                        xi->add_select ( xi_workload, &channel_select_0 );
                        xi->add_select ( xi_workload, &channel_select_1 );
                        xi->add_select ( xi_workload, &channel_select_2 );
                        xi->add_select ( xi_workload, &channel_select_3 );
                        bool channel_change = false;
                        channel_change |= state->ui.export_channels[0] != channel_select_0.item_idx;
                        channel_change |= state->ui.export_channels[1] != channel_select_1.item_idx;
                        channel_change |= state->ui.export_channels[2] != channel_select_2.item_idx;
                        channel_change |= state->ui.export_channels[3] != channel_select_3.item_idx;
                        bool reload = state->render.graph_reload && state->ui.export_node_id == node_id && state->ui.export_tex_id == j;

                        if ( switch_change || channel_change || reload ) {
                            state->ui.export_channels[0] = channel_select_0.item_idx;
                            state->ui.export_channels[1] = channel_select_1.item_idx;
                            state->ui.export_channels[2] = channel_select_2.item_idx;
                            state->ui.export_channels[3] = channel_select_3.item_idx;

                            if ( switch_change || reload ) {
                                if ( export_switch.value ) {
                                    state->ui.export_source = texture_handle;
                                    state->ui.export_node_id = node_id;
                                    state->ui.export_tex_id = j;
                                    state->ui.export_id = id;
                                    state->ui.export_node = graph_info.nodes[i];
                                } else {
                                    state->ui.export_source = xf_null_handle_m;
                                    state->ui.export_node_id = -1;
                                    state->ui.export_tex_id = -1;
                                    state->ui.export_id = 0;
                                    state->ui.export_node = xf_null_handle_m;
                                }
                            }

                            xf->invalidate_graph ( state->render.active_graph, workload );
                            xf->set_graph_texture_export ( state->render.active_graph, state->ui.export_node, state->ui.export_source, state->render.export_dest, state->ui.export_channels );
                        }

                        xi->newline();
                    }
                }
            }
        }

        for ( uint32_t i = 0; i < graph_info.node_count; ++i ) {
            uint64_t timestamp_diff = timings[i * 2 + 1] - timings[i * 2];
            timestamp_sum += timestamp_diff;
        }

        xi->newline();

        {
            xi_label_state_t time_label = xi_label_state_m (
                .style = xi_style_m (
                    .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                    .horizontal_border_margin = 25,
                )
            );
            std_stack_t stack = std_static_stack_m ( time_label.text );
            char buffer[32];

            float sum_ms = xg->timestamp_to_ns ( state->render.device ) * timestamp_sum / 1000000.f;
            std_f32_to_str ( sum_ms, buffer, 32 );
            std_stack_string_append ( &stack, buffer );

            //std_stack_string_append ( &stack, "/" );
            //uint64_t timestamp_diff = timings[graph_info.node_count * 2 - 1] - timings[0];
            //float diff_ms = xg->timestamp_to_ns ( state->render.device ) * timestamp_diff / 1000000.f;
            //std_f32_to_str ( diff_ms, buffer, 32 );
            //std_stack_string_append ( &stack, buffer );

            xi->add_label ( xi_workload, &time_label );
        }

        xi->newline();

        if ( xi->add_button ( xi_workload, &xi_button_state_m ( 
            .text = "Disable all",
            .width = 100,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m  
        ) ) ) {
            xf_graph_info_t info;
            xf->get_graph_info ( &info, state->render.active_graph );

            for ( uint32_t i = 0; i < info.node_count; ++i ) {
                xf->disable_node ( state->render.active_graph, info.nodes[i] );
            }
            xf->invalidate_graph ( state->render.active_graph, workload );
        }
        if ( xi->add_button ( xi_workload, &xi_button_state_m ( 
            .text = "Enable all", 
            .width = 100,
            .style.horizontal_alignment = xi_horizontal_alignment_right_to_left_m  
        ) ) ) {
            xf_graph_info_t info;
            xf->get_graph_info ( &info, state->render.active_graph );

            for ( uint32_t i = 0; i < info.node_count; ++i ) {
                xf->enable_node ( state->render.active_graph, info.nodes[i] );
            }
            xf->invalidate_graph ( state->render.active_graph, workload );
        }
    }
    xi->end_section ( xi_workload );

    // se entities
    bool entity_edit = false;
    xi->begin_section ( xi_workload, &state->ui.entities_section_state );
    {
        se_i* se = state->modules.se;

        se_entity_h entity_list[se_max_entities_m];
        uint32_t destroy_list[se_max_entities_m];
        uint32_t destroy_count = 0;
        size_t entity_count = se->get_entity_list ( entity_list, se_max_entities_m );
        std_assert_m ( entity_count < 64 * std_static_array_capacity_m ( state->ui.expanded_entities_bitset ) );

        bool delete_selected = false;
        if ( !old_input_state->keyboard[wm_keyboard_state_del_m] && input_state->keyboard[wm_keyboard_state_del_m] ) {
            delete_selected = true;
        }

        for ( uint32_t i = 0; i < entity_count; ++i ) {
            se_entity_h entity = entity_list[i];

            xi_color_t font_color = xi_color_invalid_m;
            if ( entity == state->ui.mouse_pick_entity ) {
                if ( delete_selected ) {
                    destroy_list[destroy_count++] = i;
                    state->ui.mouse_pick_entity = se_null_handle_m;
                    continue;
                }
                font_color = xi_color_yellow_m;
            }

            se_entity_properties_t props;
            se->get_entity_properties ( &props, entity );

            // entity label
            xi_label_state_t name_label = xi_label_state_m(
                .style = xi_style_m (
                    .font_color = font_color,
                )
            );
            std_str_copy_static_m ( name_label.text, props.name );
            xi->add_label ( xi_workload, &name_label );

            // arrow
            bool hovered = xi->test_layer_row_hover ( 14 );
            bool expanded = std_bitset_test ( state->ui.expanded_entities_bitset, i );
            bool interacted = false;
            uint64_t arrow_id, button_id;
            if ( hovered || expanded ) {
                xi_arrow_state_t node_arrow = xi_arrow_state_m (
                    .width = 14,
                    .height = 14,
                    .style = xi_style_m (
                        .horizontal_margin = 8
                    ),
                    .expanded = expanded,
                    .id = xi_mix_id_m ( i ),
                );
                arrow_id = node_arrow.id;
                bool changed = xi->add_arrow ( xi_workload, &node_arrow );
                if ( changed ) {
                    if ( node_arrow.expanded ) {
                        std_bitset_set ( state->ui.expanded_entities_bitset, i );
                    } else {
                        std_bitset_clear ( state->ui.expanded_entities_bitset, i );
                    }
                }
                interacted |= changed;
            }

            if ( hovered ) {
                xi_button_state_t delete_button = xi_button_state_m (
                    .style = xi_style_m (
                        .horizontal_alignment = xi_horizontal_alignment_right_to_left_m,
                    ),
                    .text = "X",
                    .width = 15,
                    .id = xi_mix_id_m ( i ),
                );
                button_id = delete_button.id;
                if ( xi->add_button ( xi_workload, &delete_button ) ) {
                    destroy_list[destroy_count++] = i;
                    interacted = true;
                    continue;
                }
            }

            uint64_t hovered_element = xi->get_hovered_element_id();
            if ( hovered_element != arrow_id && hovered_element != button_id && hovered && input_state->mouse[wm_mouse_state_left_m] ) {
                state->ui.mouse_pick_entity = entity;
            }

            xi->newline();

            if ( expanded ) {
                char buffer[1024];
                std_stack_t stack = std_static_stack_m ( buffer );

                for ( uint32_t j = 0; j < props.component_count; ++j ) {
                    se_component_properties_t* component = &props.components[j];

                    // component label
                    xi_label_state_t label = xi_label_state_m();
                    std_stack_string_append ( &stack, "  " );
                    std_stack_string_append ( &stack, component->name );
                    std_str_copy_static_m ( label.text, buffer );
                    std_stack_clear ( &stack );
                    xi->add_label ( xi_workload, &label );
                    xi->newline();

                    for ( uint32_t k = 0; k < component->property_count; ++k ) {
                        se_property_t* property = &component->properties[k];

                        // property label
                        xi_label_state_t label = xi_label_state_m();
                        std_stack_string_append ( &stack, "  " );
                        std_stack_string_append ( &stack, "  " );
                        std_stack_string_append ( &stack, property->name );
                        std_str_copy_static_m ( label.text, buffer );
                        std_stack_clear ( &stack );
                        xi->add_label ( xi_workload, &label );

                        // property editor
                        void* component_data = se->get_entity_component ( entity, component->id, property->stream );
                        xi_property_e type = ( xi_property_e ) property->type; // This assumes the se and xi enums are laid out identical...
                        xi_property_editor_state_t property_editor_state = xi_property_editor_state_m (
                            .type = type,
                            .data = component_data + property->offset,
                            .property_width = type == xi_property_bool_m ? 14 : 64,
                            .property_height = type == xi_property_bool_m ? 14 : 0,
                            .id = ( ( ( uint64_t ) i ) << 32 ) + ( ( ( uint64_t ) j ) << 16 ) + ( ( uint64_t ) k + 1 ),
                            .style = xi_style_m ( .horizontal_alignment = xi_horizontal_alignment_right_to_left_m ),
                        );
                        entity_edit |= xi->add_property_editor ( xi_workload, &property_editor_state );
                        xi->newline();
                    }
                }
            }
        }

        // dispatch deletes and keep expanded entities bitset in sync
        uint32_t remaining_entities = entity_count;
        xg_resource_cmd_buffer_h resource_cmd_buffer = xg_null_handle_m;
        if ( destroy_count > 0 ) {
            resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
            update_raytrace_world();
        }
        for ( uint32_t i = 0; i < destroy_count; ++i ) {
            uint32_t entity_idx = destroy_list[i];
            viewapp_destroy_entity_resources ( entity_list[entity_idx], workload, resource_cmd_buffer, xg_resource_cmd_buffer_time_workload_complete_m );
            se->destroy_entity ( entity_list[entity_idx] );
            --remaining_entities;
            std_bitset_shift_left ( state->ui.expanded_entities_bitset, entity_idx, 1, std_static_array_capacity_m ( state->ui.expanded_entities_bitset ) );
        }

        if ( xi->add_button ( xi_workload, &xi_button_state_m (
            .text = "Add light",
            .style = xi_default_style_m (
                .horizontal_padding = 8,
            ),
        ) ) ) {
            spawn_light ( workload );
        }

        if ( xi->add_button ( xi_workload, &xi_button_state_m (
            .text = "Add sphere",
            .style = xi_default_style_m (
                .horizontal_padding = 8,
            ),
        ) ) ) {
            spawn_sphere ( workload );
        }

        if ( xi->add_button ( xi_workload, &xi_button_state_m (
            .text = "Add plane",
            .style = xi_default_style_m (
                .horizontal_padding = 8,
            ),
        ) ) ) {
            spawn_plane ( workload );
        }
    }
    xi->end_section ( xi_workload );

    xi->end_window ( xi_workload );

    // raytrace update
    if ( entity_edit ) {
        update_raytrace_world();
    }

    // geos
    bool transform_drag = false;
    if ( state->ui.mouse_pick_entity != se_null_handle_m ) {
        viewapp_transform_component_t* transform = se->get_entity_component ( state->ui.mouse_pick_entity, viewapp_transform_component_id_m, 0 );
        if ( transform ) {
            xi_transform_state_t xform = xi_transform_state_m (
                .position = { transform->position[0], transform->position[1], transform->position[2] },
                .rotation = { transform->orientation[0], transform->orientation[1], transform->orientation[2], transform->orientation[3] },
                .sort_order = 1,
                .mode = input_state->keyboard[wm_keyboard_state_alt_left_m] ? xi_transform_mode_rotation_m : xi_transform_mode_translation_m,
            );
            transform_drag = xi->draw_transform ( xi_workload, &xform );

            bool rtworld_needs_update = false;
            if ( ( transform->position[0] != xform.position[0]
                || transform->position[1] != xform.position[1]
                || transform->position[2] != xform.position[2]
                || transform->orientation[0] != xform.rotation[0]
                || transform->orientation[1] != xform.rotation[1]
                || transform->orientation[2] != xform.rotation[2]
                || transform->orientation[3] != xform.rotation[3]
                )
                && state->render.active_graph == state->render.raytrace_graph )
            {
                rtworld_needs_update = true;
            }

            transform->position[0] = xform.position[0];
            transform->position[1] = xform.position[1];
            transform->position[2] = xform.position[2];
            transform->orientation[0] = xform.rotation[0];
            transform->orientation[1] = xform.rotation[1];
            transform->orientation[2] = xform.rotation[2];
            transform->orientation[3] = xform.rotation[3];

            if ( rtworld_needs_update ) {
                update_raytrace_world();
            }
        }
    }

    // mouse pick
    if ( !transform_drag && !old_input_state->mouse[wm_mouse_state_left_m] && input_state->mouse[wm_mouse_state_left_m] ) {
        mouse_pick ( input_state->cursor_x, input_state->cursor_y );
    }

    xi->end_update();
}
