#pragma once

#include <wm.h>

#include <std_platform.h>
#include <std_compiler.h>
#include <std_mutex.h>
#include <std_hash.h>

typedef struct wm_window_t {
    struct wm_window_t* next;
    struct wm_window_t* prev;

#if wm_enable_input_state_m
    uint8_t pending_wheel_up;
    uint8_t pending_wheel_down;
    wm_input_state_t input_state;
    uint32_t cursor_x;
    uint32_t cursor_y;
#endif

#if wm_enable_input_events_m
    wm_input_event_handler_t input_handlers[wm_max_input_event_handlers_m];
    size_t handlers_count;
#endif

    wm_window_params_t params;
    wm_window_info_t info;

#if defined(std_platform_linux_m)
    uint64_t exit_message_id;
#endif

    uint32_t gen;

    wm_input_buffer_t input_buffer;
    uint32_t input_flags;
} wm_window_t;

typedef struct wm_window_state_t {
    std_mutex_t mutex;

    wm_window_t* windows_array;
    wm_window_t* windows_freelist;
    wm_window_t* windows_list;
    uint64_t* windows_bitset;

#if defined(std_platform_win32_m)
    std_hash_map_t map;   // window os handle -> wm_window_h handle
#endif
} wm_window_state_t;

void wm_window_load ( wm_window_state_t* state );
void wm_window_reload ( wm_window_state_t* state );
void wm_window_unload ( void );

wm_window_h wm_window_create ( const wm_window_params_t* params );
bool mw_window_is_alive ( wm_window_h window );
bool wm_window_destroy ( wm_window_h window );
bool wm_window_get_info ( wm_window_h window, wm_window_info_t* info );
void wm_window_update ( wm_window_h window );

#if wm_enable_input_events_m
    bool wm_window_add_event_handler ( wm_window_h window, const wm_input_event_handler_t* handler );
    bool wm_window_remove_event_handler ( wm_window_h window, const wm_input_event_handler_t* handler );
#endif

#if wm_enable_input_state_m
    bool wm_window_get_input_state ( wm_window_h window, wm_input_state_t* state );
    void wm_window_debug_print_input_state ( wm_window_h window, bool overwrite_console );
#endif

void wm_window_input_buffer_get ( wm_window_h window, wm_input_buffer_t* buffer );