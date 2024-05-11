#include <xui.h>

#include "xui_state.h"

/*
    API is not like that of a typical immediate UI, instead it's split in 2 parts, elements need to be created and deleted explicitly
    which means that some state is stored per element on xui. On the other hand draw has to be called every frame for the elements
    to draw and update and each draw call returns the state of the element.
*/

static void xui_register_shaders ( xs_i* xs ) {
    char path[std_process_path_max_len_m];
    std_stack_t stack = std_static_stack_m ( path );
    std_stack_string_append ( &stack, std_module_path_m );
    std_stack_string_append ( &stack, "shader/");
    xs->add_database_folder ( path );
    xui_workload_load_shaders ( xs );
    xui_font_load_shaders ( xs );
}

static void xui_api_init ( xui_i* xui ) {
    xui->create_font = xui_font_create_ttf;

    xui->register_shaders = xui_register_shaders;
    xui->create_workload = xui_workload_create;
    xui->flush_workload = xui_workload_flush;

    xui->begin_update = xui_element_update;
    xui->end_update = xui_element_update_end;

    xui->begin_window = xui_element_window_begin;
    xui->end_window = xui_element_window_end;

    xui->begin_section = xui_element_section_begin;
    xui->end_section = xui_element_section_end;

    xui->add_label = xui_element_label;
    xui->add_switch = xui_element_switch;
    xui->add_slider = xui_element_slider;
    xui->add_button = xui_element_button;
    xui->add_select = xui_element_select;
    //xui->add_text  = xui_element_text;

    xui->newline = xui_element_newline;

    xui->get_active_element_id = xui_element_get_active_id;
}

void* xui_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xui_state_t* state = xui_state_alloc();

    xui_workload_load ( &state->workload );
    xui_element_load ( &state->element );
    xui_font_load ( &state->font );

    xui_api_init ( &state->api );

    return &state->api;
}

void xui_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    std_auto_m state = ( xui_state_t* ) api;

    xui_workload_reload ( &state->workload );
    xui_element_reload ( &state->element );
    xui_font_reload ( &state->font );

    xui_api_init ( &state->api );
}

void xui_unload ( void ) {
    xui_workload_unload();
    xui_element_unload();
    xui_font_unload();

    xui_state_free();
}
