#include <std_app_test.h>

#include <std_app.h>
#include <std_log.h>

#include <wm.h>

// TODO heap alloc to support reloads

typedef struct {
    std_app_i api;
    bool boot;
#if std_enabled_m(wm_input_state_m)
    bool first_print;
#endif
    wm_window_h window;
} std_app_test_state_t;

static std_app_test_state_t* m_state;

static std_app_state_e std_app_test_tick ( void ) {
    if ( m_state->boot ) {
        void* p = std_virtual_heap_alloc ( 16, 16 );
        std_unused_m ( p );

        wm_i* wm = std_module_get_m ( wm_module_name_m );
        std_assert_m ( wm );

        wm_display_h displays[8];
        size_t display_count = wm->get_displays ( displays, 8 );

        for ( size_t i = 0; i < display_count; ++i ) {
            wm_display_info_t info;
            wm->get_display_info ( &info, displays[i] );
            std_log_info_m ( "Display " std_fmt_size_m ":", i );
            std_log_info_m ( std_fmt_tab_m "Adapter ID: " std_fmt_str_m, info.adapter_id );
            std_log_info_m ( std_fmt_tab_m "Adapter name: " std_fmt_str_m, info.adapter_name );
            std_log_info_m ( std_fmt_tab_m "Display ID: " std_fmt_str_m, info.display_id );
            std_log_info_m ( std_fmt_tab_m "Display name: " std_fmt_str_m, info.display_name );
            const char* is_primary = info.is_primary ? "y" : "n";
            std_log_info_m ( std_fmt_tab_m "Primary display: " std_fmt_str_m, is_primary );

            std_log_info_m ( std_fmt_tab_m "Width: " std_fmt_size_m, info.mode.width );
            std_log_info_m ( std_fmt_tab_m "Height: " std_fmt_size_m, info.mode.height );
            std_log_info_m ( std_fmt_tab_m "Refresh rate: " std_fmt_size_m, info.mode.refresh_rate );
            std_log_info_m ( std_fmt_tab_m "Bits per pixel: " std_fmt_size_m, info.mode.bits_per_pixel );

            size_t modes_count = wm->get_display_modes_count ( displays[i] );
            std_log_info_m ( std_fmt_tab_m "Display modes: " std_fmt_size_m, modes_count );
        }

        wm_window_params_t params = { .name = "wm_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
        m_state->window = wm->create_window ( &params );
    }

    {
        wm_i* wm = std_module_get_m ( wm_module_name_m );

        wm->update_window ( m_state->window );

        if ( !wm->is_window_alive ( m_state->window ) ) {
            return std_app_state_exit_m;
        }

#if std_enabled_m(wm_input_state_m)
        //wm->debug_print_window_input_state ( m_state->window, !m_state->first_print );
        m_state->first_print = false;
#endif

        wm_input_state_t input_state;
        wm->get_window_input_state ( m_state->window, &input_state );

        if ( input_state.keyboard[wm_keyboard_state_esc_m] ) {
            return std_app_state_exit_m;
        }

        if ( input_state.keyboard[wm_keyboard_state_f1_m] ) {
            return std_app_state_reload_m;
            m_state->first_print = true;
        }

        std_thread_this_sleep ( 1 );
    }

    m_state->boot = false;

    return std_app_state_tick_m;
}

static void std_app_test_api_init ( std_app_i* app ) {
    app->tick = std_app_test_tick;
}

void* std_app_test_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    std_auto_m state = std_virtual_heap_alloc_m ( std_app_test_state_t );
    state->boot = true;
    state->first_print = true;
    state->window = wm_null_handle_m;

    m_state = state;

    std_app_i* api = &state->api;
    std_app_test_api_init ( api );
    return api;
}

void std_app_test_unload ( void ) {
    wm_i* wm = std_module_get_m ( wm_module_name_m );
    wm->destroy_window ( m_state->window );
    std_module_release ( wm );
    std_virtual_heap_free ( m_state );
}

void std_app_test_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );
    std_auto_m state = ( std_app_test_state_t* ) api;
    m_state = state;
    std_app_test_api_init ( &state->api );
}
