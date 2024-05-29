#include <std_main.h>
#include <std_log.h>

#include <wm.h>

#include <stdio.h>

std_warnings_ignore_m ( "-Wmissing-noreturn" )
std_warnings_ignore_m ( "-Wunused-function" )

static void wm_test_run_2 ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );

    while ( true ) {
        wm->print();
        std_thread_this_sleep ( 1 );
    }

    std_module_unload_m ( wm_module_name_m );
}

static void wm_test_run ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    std_assert_m ( wm );

#if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
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
#endif

    wm_window_params_t params = { .name = "wm_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
    wm_window_h window = wm->create_window ( &params );

#if std_enabled_m(wm_input_state_m)
    bool first_print = true;
#endif

    std_unused_m ( window );

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

#if std_enabled_m(wm_input_state_m)
        wm->debug_print_window_input_state ( window, !first_print );
        first_print = false;
#endif

        wm_input_state_t input_state;
        wm->get_window_input_state ( window, &input_state );

        if ( input_state.keyboard[wm_keyboard_state_esc_m] ) {
            break;
        }

        if ( input_state.keyboard[wm_keyboard_state_f1_m] ) {
            std_module_reload_m();
            first_print = true;
        }

        std_thread_this_sleep ( 1 );
    }

    std_module_unload_m ( wm_module_name_m );
}

void std_main ( void ) {
    wm_test_run();
    std_log_info_m ( "WM_TEST COMPLETE!" );
}
