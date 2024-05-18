#include <xi.h>

#include "xi_state.h"

/*
    API is not like that of a typical immediate UI, instead it's split in 2 parts, elements need to be created and deleted explicitly
    which means that some state is stored per element on xi. On the other hand draw has to be called every frame for the elements
    to draw and update and each draw call returns the state of the element.
*/

static void xi_register_shaders ( xs_i* xs ) {
    char path[std_process_path_max_len_m];
    std_stack_t stack = std_static_stack_m ( path );
    std_stack_string_append ( &stack, std_module_path_m );
    std_stack_string_append ( &stack, "shader/");
    xs->add_database_folder ( path );
    xi_workload_load_shaders ( xs );
    xi_font_load_shaders ( xs );
}

static void xi_api_init ( xi_i* xi ) {
    xi->create_font = xi_font_create_ttf;

    xi->register_shaders = xi_register_shaders;
    xi->create_workload = xi_workload_create;
    xi->flush_workload = xi_workload_flush;

    xi->begin_update = xi_element_update;
    xi->end_update = xi_element_update_end;

    xi->begin_window = xi_element_window_begin;
    xi->end_window = xi_element_window_end;

    xi->begin_section = xi_element_section_begin;
    xi->end_section = xi_element_section_end;

    xi->add_label = xi_element_label;
    xi->add_switch = xi_element_switch;
    xi->add_slider = xi_element_slider;
    xi->add_button = xi_element_button;
    xi->add_select = xi_element_select;
    //xi->add_text  = xi_element_text;

    xi->newline = xi_element_newline;

    xi->get_active_element_id = xi_element_get_active_id;
}

void* xi_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xi_state_t* state = xi_state_alloc();

    xi_workload_load ( &state->workload );
    xi_element_load ( &state->element );
    xi_font_load ( &state->font );

    xi_api_init ( &state->api );

    return &state->api;
}

void xi_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    std_auto_m state = ( xi_state_t* ) api;

    xi_workload_reload ( &state->workload );
    xi_element_reload ( &state->element );
    xi_font_reload ( &state->font );

    xi_api_init ( &state->api );
}

void xi_unload ( void ) {
    xi_workload_unload();
    xi_element_unload();
    xi_font_unload();

    xi_state_free();
}
