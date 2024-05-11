#include <wm.h>

#include "wm_state.h"
#include "wm_window.h"
#include "wm_display.h"

static void wm_api_init ( wm_i* wm ) {
    wm->create_window = wm_window_create;
    wm->destroy_window = wm_window_destroy;

    wm->get_window_info = wm_window_get_info;
    wm->is_window_alive = mw_window_is_alive;
    wm->update_window = wm_window_update;

#if std_enabled_m ( wm_input_events_m )
    wm->add_window_event_handler = wm_window_add_event_handler;
    wm->remove_window_event_handler = wm_window_remove_event_handler;
#endif

#if std_enabled_m ( wm_input_state_m )
    wm->get_window_input_state = wm_window_get_input_state;
    wm->debug_print_window_input_state = wm_window_debug_print_input_state;
#endif

    wm->get_displays_count = wm_display_get_count;
    wm->get_displays = wm_display_get;
    wm->get_display_info = wm_display_get_info;
    wm->get_display_modes_count = wm_display_get_modes_count;
    wm->get_display_modes = wm_display_get_modes;
}

void* wm_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );
    wm_state_t* state = wm_state_alloc();
    wm_window_load ( &state->window );
    wm_api_init ( &state->api );
    return &state->api;
}

void wm_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );
    wm_state_t* state = ( wm_state_t* ) api;
    wm_window_reload ( &state->window );
    wm_api_init ( &state->api );
    wm_state_bind ( state );
}

void wm_unload ( void ) {
    wm_window_unload();
    wm_state_free();
}
