#include <xi.h>

#include "xi_state.h"

#include "xi_update.h"

#include "std_string.h"

static void xi_load_shaders ( xg_device_h device ) {
    xs_i* xs = std_module_get_m ( xs_module_name_m );

    char path[std_process_path_max_len_m];
    std_stack_t stack = std_static_stack_m ( path );
    std_stack_string_append ( &stack, std_module_path_m );
    std_stack_string_append ( &stack, "shader/");
    
    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "xi_sdb" ) );
    xs->add_database_folder ( sdb, path );
    xs->set_output_folder ( sdb, "output/shader/" );
    xi_workload_load_shaders ( xs, sdb );
    xi_font_load_shaders ( xs, sdb );
    xs->build_database ( sdb );
    xi_state_set_sdb ( sdb );
}

static void xi_api_init ( xi_i* xi ) {
    xi->create_font = xi_font_create_ttf;

    xi->load_shaders = xi_load_shaders;

    xi->create_workload = xi_workload_create;
    xi->flush_workload = xi_workload_flush;
    xi->set_workload_view_info = xi_workload_set_view_info;

    xi->begin_update = xi_update_begin;
    xi->end_update = xi_update_end;
    
    xi->begin_window = xi_ui_window_begin;
    xi->end_window = xi_ui_window_end;
    xi->begin_section = xi_ui_section_begin;
    xi->end_section = xi_ui_section_end;
    xi->add_label = xi_ui_label;
    xi->add_switch = xi_ui_switch;
    xi->add_slider = xi_ui_slider;
    xi->add_button = xi_ui_button;
    xi->add_select = xi_ui_select;
    xi->add_textfield  = xi_ui_textfield;
    xi->add_property_editor = xi_ui_property_editor;
    xi->add_texture = xi_ui_texture;
    xi->add_arrow = xi_ui_arrow;

    xi->newline = xi_ui_newline;

    xi->init_geos = xi_ui_geo_init;
    xi->draw_transform = xi_ui_draw_transform;

    xi->get_active_element_id = xi_ui_get_active_id;
    xi->get_hovered_element_id = xi_ui_get_hovered_id;

    xi->file_pick = xi_ui_file_pick;

    xi->test_layer_row_hover = xi_ui_layer_row_hover_test;

    xi->draw_overlay_texture = xi_ui_overlay_texture;
}

void* xi_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xi_state_t* state = xi_state_alloc();
    xi_state_set_sdb ( xs_null_handle_m );

    xi_workload_load ( &state->workload );
    xi_ui_load ( &state->ui );
    xi_font_load ( &state->font );

    xi_api_init ( &state->api );

    return &state->api;
}

void xi_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    std_auto_m state = ( xi_state_t* ) api;

    xi_workload_reload ( &state->workload );
    xi_ui_reload ( &state->ui );
    xi_font_reload ( &state->font );

    xi_api_init ( &state->api );
}

void xi_unload ( void ) {
    xi_workload_unload();
    xi_ui_unload();
    xi_font_unload();

    xs_database_h sdb = xi_state_get_sdb();
    if ( sdb != xs_null_handle_m ) {
        xs_i* xs = std_module_get_m ( xs_module_name_m );
        xs->destroy_database ( sdb );
        xi_state_set_sdb ( xs_null_handle_m );
    }

    xi_state_free();
}
