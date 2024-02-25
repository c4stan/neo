#include "wm_window.h"

#include "wm_state.h"

#include <std_mutex.h>
#include <std_buffer.h>
#include <std_list.h>
#include <std_platform.h>
#include <std_log.h>

#if defined(std_platform_win32_m)
    #include <std_hash.h>
#endif

#if defined(std_platform_linux_m)
    #include <X11/X.h>
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
    #include <X11/keysym.h>

    // https://tronche.com/gui/x/xlib/
    // https://arcturus.su/~alvin/docs/xlpm/
    // https://www.khronos.org/opengl/wiki/Programming_OpenGL_in_Linux:_GLX_and_Xlib
#endif

typedef uint64_t wm_window_h;
#define wm_make_handle_m( idx, gen )          ( ( uint64_t ) (idx) + ( ( uint64_t ) (gen) << 32 ) )
#define wm_handle_idx_m( handle )             ( (handle) & 0xffffffff )
#define wm_handle_gen_m( handle )             ( ( (handle) >> 32 ) & 0xffffffff )

#if 0
typedef struct {
    std_mutex_t mutex;
    wm_window_t* windows;
    wm_window_t* windows_freelist;
#if defined(std_platform_win32_m)
    std_map_t map;   // window os handle -> wm_window_h handle
#endif
} wm_window_state_t;

static wm_window_state_t wm_window_state;
#endif

static wm_window_state_t* wm_window_state;

#if defined(std_platform_win32_m)
    LRESULT CALLBACK wm_window_os_callback ( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam );
#endif

/*
    Win32 event handling:
        user calls process_window
        event loop begins
            for each event queued, call wm_window_os_callback
                if wm_input_events_m is on, call wm_process_input_event
                    return value tells if event is trapped or not, if not called should call default win32 handler on it
                    retcode parameter sends back value that the os callback should return to the os for the event
                        see https://docs.microsoft.com/en-us/windows/win32/winmsg/window-notifications
                        generally it tells the os whether to trap the event or propagate it further
                        need to investigate further what this means in practice
                if wm_input_events_m is off, call default win32 handler
            callback calls wm_process_input_event (and update_input_state ???)
        when all events are dequeued, event loop terminates and returns
*/

void wm_window_load ( wm_window_state_t* state ) {
    wm_window_state = state;

    std_alloc_t windows_alloc = std_virtual_heap_alloc_array_m ( wm_window_t, wm_max_windows_m );
    wm_window_state->windows_memory_handle = windows_alloc.handle;
    wm_window_state->windows_array = ( wm_window_t* ) windows_alloc.buffer.base;
    wm_window_state->windows_freelist = std_freelist ( windows_alloc.buffer, sizeof ( wm_window_t ) );
    wm_window_state->windows_list = NULL;

    std_mutex_init ( &wm_window_state->mutex );

#if defined(std_platform_win32_m)
    std_alloc_t map_keys_alloc = std_virtual_heap_alloc_array_m ( wm_window_h, wm_max_windows_m * 2 );
    std_alloc_t map_values_alloc = std_virtual_heap_alloc_array_m ( uint64_t, wm_max_windows_m * 2 );
    wm_window_state->map_keys_memory_handle = map_keys_alloc.handle;
    wm_window_state->map_values_memory_handle = map_values_alloc.handle;
    wm_window_state->map = std_hash_map ( map_keys_alloc.buffer, map_values_alloc.buffer );
#endif

#if defined(std_platform_linux_m)
    XInitThreads();
#endif
}

void wm_window_reload ( wm_window_state_t* state ) {
    wm_window_state = state;

#if defined(std_platform_win32_m)
    wm_window_t* window = wm_window_state->windows_list;

    while ( window ) {
        SetWindowLongPtr ( ( HWND ) window->info.os_handle.window, GWLP_WNDPROC, ( LONG_PTR ) &wm_window_os_callback );
        window = window->next;
    }

#endif
}

void wm_window_unload ( void ) {
    // TODO check for resource leaks?

    std_virtual_heap_free ( wm_window_state->windows_memory_handle );
#if defined(std_platform_win32_m)
    std_virtual_heap_free ( wm_window_state->map_keys_memory_handle );
    std_virtual_heap_free ( wm_window_state->map_values_memory_handle );
#endif
}

#if std_enabled_m(wm_input_events_m)
#if defined(std_platform_win32_m)
static bool wm_process_input_event ( wm_window_h handle, wm_window_t* window, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, int64_t* retcode ) {
    // Process msg
    wm_input_event_t event;
    uint64_t args[4] = { 0, 0, 0, 0 };

    // TODO
    //      test for std_enabled_m(wm_input_buffer_m)
    //      if enabled store all event input received during this frame in a buffer
    //      allow user to read from it (makes implementing a text box possible)

    if ( msg == WM_CLOSE ) {
        event = wm_event_close_m;
        bool trap = false;

        for ( size_t i = window->handlers_count - 1; i != ( size_t ) - 1; --i ) {
            if ( window->input_handlers[i].event_mask & event ) {
                if ( window->input_handlers[i].callback ( handle, event, args[0], args[1], args[2], args[3] ) ) {
                    trap = true;
                    break;
                }
            }
        }

        if ( !trap ) {
            wm_window_destroy ( handle );
        }

        return trap;
    } else {
        // TODO
        int64_t on_handle;
        int64_t on_pass;

        switch ( msg ) {
            case WM_KEYDOWN:
                // TODO standardize keydown/up flags, don't just pass lparam
                event = wm_event_key_down_m;
                args[0] = ( uint64_t ) wparam; // key
                args[1] = ( uint64_t ) lparam; // flags - todo use wm_input_key_modifier_f
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_KEYUP:
                event = wm_event_key_up_m;
                args[0] = ( uint64_t ) wparam; // key
                args[1] = ( uint64_t ) lparam;  // flags
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_LBUTTONDOWN:
                event = wm_event_mouse_down_m;
                args[0] = wm_mouse_state_left_m;
                args[1] = ( uint64_t ) GET_X_LPARAM ( lparam );
                args[2] = ( uint64_t ) GET_Y_LPARAM ( lparam );
                args[3] = wparam;   // TODO
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_LBUTTONUP:
                event = wm_event_mouse_up_m;
                args[0] = wm_mouse_state_left_m;
                args[1] = ( uint64_t ) GET_X_LPARAM ( lparam );
                args[2] = ( uint64_t ) GET_Y_LPARAM ( lparam );
                args[3] = wparam;   // TODO
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_RBUTTONDOWN:
                event = wm_event_mouse_down_m;
                args[0] = wm_mouse_state_right_m;
                args[1] = ( uint64_t ) GET_X_LPARAM ( lparam );
                args[2] = ( uint64_t ) GET_Y_LPARAM ( lparam );
                args[3] = wparam;   // TODO
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_RBUTTONUP:
                event = wm_event_mouse_up_m;
                args[0] = wm_mouse_state_right_m;
                args[1] = ( uint64_t ) GET_X_LPARAM ( lparam );
                args[2] = ( uint64_t ) GET_Y_LPARAM ( lparam );
                args[3] = wparam;   // TODO
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_MOUSEWHEEL:
                event = wm_event_mouse_wheel_m;
                args[0] = GET_WHEEL_DELTA_WPARAM ( wparam ) > 0 ? wm_mouse_state_wheel_up_m : wm_mouse_state_wheel_down_m;
                args[1] = ( uint64_t ) GET_X_LPARAM ( lparam );
                args[2] = ( uint64_t ) GET_Y_LPARAM ( lparam );
                args[3] = GET_KEYSTATE_WPARAM ( wparam ); // TODO
                on_handle = 0;
                on_pass = 1;

                if ( args[0] == wm_mouse_state_wheel_up_m ) {
                    window->pending_wheel_up += 1;
                } else {
                    window->pending_wheel_down += 1;
                }

                break;

            case WM_SIZING: {
                event = wm_event_resize_m;
                RECT* rect = ( RECT* ) lparam;
                std_mutex_t* mutex = &wm_window_state->mutex;
                std_mutex_lock ( mutex );
                wm_window_info_t* info = &window->info;
                args[0] = ( uint64_t ) ( rect->right - rect->left ) - info->borders_width;
                args[1] = ( uint64_t ) ( rect->bottom - rect->top ) - info->borders_height;
                args[2] = info->width;
                args[3] = info->height;
                info->width = args[0];
                info->height = args[1];
                std_mutex_unlock ( mutex );
                on_handle = TRUE;
                on_pass = FALSE;
                break;
            }

            case WM_MOVING: {
                event = wm_event_resize_m;
                RECT* rect = ( RECT* ) lparam;
                args[0] = ( uint64_t ) rect->left;
                args[1] = ( uint64_t ) rect->top;
                std_mutex_t* mutex = &wm_window_state->mutex;
                std_mutex_lock ( mutex );
                wm_window_info_t* info = &window->info;
                args[2] = ( uint64_t ) info->x;
                args[3] = ( uint64_t ) info->y;
                info->x = ( int64_t ) args[0];
                info->y = ( int64_t ) args[1];
                std_mutex_unlock ( mutex );
                on_handle = TRUE;
                on_pass = FALSE;
                break;
            }

            case WM_SIZE: {
                switch ( wparam ) {
                    case SIZE_MAXIMIZED: {
                        event = wm_event_maximize_m;
                        RECT rect;
                        GetWindowRect ( hwnd, &rect );
                        args[0] = ( uint64_t ) rect.left;
                        args[1] = ( uint64_t ) rect.top;
                        args[2] = ( uint64_t ) rect.right;
                        args[3] = ( uint64_t ) rect.bottom;
                        std_mutex_t* mutex = &wm_window_state->mutex;
                        std_mutex_lock ( mutex );
                        wm_window_info_t* info = &window->info;
                        info->is_minimized = false;
                        info->x = ( int64_t ) args[0];
                        info->y = ( int64_t ) args[1];
                        info->width = ( uint64_t ) ( args[2] - args[0] ) - info->borders_width;
                        info->height = ( uint64_t ) ( args[3] - args[1] ) - info->borders_height;
                        std_mutex_unlock ( mutex );
                        break;
                    }

                    case SIZE_RESTORED: {
                        event = wm_event_restore_m;
                        RECT rect;
                        GetWindowRect ( hwnd, &rect );
                        args[0] = ( uint64_t ) rect.left;
                        args[1] = ( uint64_t ) rect.top;
                        args[2] = ( uint64_t ) rect.right;
                        args[3] = ( uint64_t ) rect.bottom;
                        std_mutex_t* mutex = &wm_window_state->mutex;
                        std_mutex_lock ( mutex );
                        wm_window_info_t* info = &window->info;
                        info->is_minimized = false;
                        info->x = ( int64_t ) args[0];
                        info->y = ( int64_t ) args[1];
                        info->width = ( uint64_t ) ( args[2] - args[0] ) - info->borders_width;
                        info->height = ( uint64_t ) ( args[3] - args[1] ) - info->borders_height;
                        std_mutex_unlock ( mutex );
                        break;
                    }

                    case SIZE_MINIMIZED: {
                        event = wm_event_minimize_m;
                        RECT rect;
                        GetWindowRect ( hwnd, &rect );
                        std_mutex_t* mutex = &wm_window_state->mutex;
                        std_mutex_lock ( mutex );
                        wm_window_info_t* info = &window->info;
                        info->is_minimized = true;
                        std_mutex_unlock ( mutex );
                        break;
                    }

                    default:
                        return false;
                }

                on_handle = 0;
                on_pass = 1;
                break;
            }

            case WM_DESTROY:
                event = wm_event_destroy_m;
                on_handle = 0;
                on_pass = 1;
                break;

            case WM_CREATE:
                event = wm_event_create_m;
                on_handle = 0;
                on_pass = 1;    // TODO is this ok? what happens? docs don't mention it https://docs.microsoft.com/en-us/windows/desktop/winmsg/wm-create
                break;

            case WM_SETFOCUS: {
                event = wm_event_gain_focus_m;
                std_mutex_t* mutex = &wm_window_state->mutex;
                std_mutex_lock ( mutex );
                wm_window_info_t* info = &window->info;
                info->is_focus = true;
                std_mutex_unlock ( mutex );
                on_handle = 0;
                on_pass = 1;
                break;
            }

            case WM_KILLFOCUS: {
                event = wm_event_lose_focus_m;
                std_mutex_t* mutex = &wm_window_state->mutex;
                std_mutex_lock ( mutex );
                wm_window_info_t* info = &window->info;
                info->is_focus = false;
                std_mutex_unlock ( mutex );
                on_handle = 0;
                on_pass = 1;
                break;
            }

            default:
                return false;
        }

        // TODO skip all/some of this if there's no handler registered for the current event?
        // User callback
        for ( size_t i = window->handlers_count - 1; i != ( size_t ) - 1; --i ) {
            if ( window->input_handlers[i].event_mask & event ) {
                if ( window->input_handlers[i].callback ( handle, event, args[0], args[1], args[2], args[3] ) ) {
                    // TODO is it correct to trap in case the user provided an event handler?
                    *retcode = on_handle;
                    return true;
                }
            }
        }

        *retcode = on_pass;
        return false;
    }
}
#elif defined(std_platform_linux_m)
static bool wm_process_input_event ( wm_window_h handle, wm_window_t* window, XEvent* x_event ) {
    wm_input_event_t event;
    uint64_t args[4] = { 0, 0, 0, 0 };

    if ( x_event->type == ClientMessage && x_event->xclient.data.l[0] == window->exit_message_id ) {
        event = wm_event_close_m;
        bool trap = false;

        for ( size_t i = window->handlers_count - 1; i != ( size_t ) - 1; --i ) {
            if ( window->input_handlers[i].event_mask & event ) {
                if ( window->input_handlers[i].callback ( handle, event, args[0], args[1], args[2], args[3] ) ) {
                    trap = true;
                    break;
                }
            }
        }

        if ( !trap ) {
            wm_window_destroy ( handle );
        }

        return trap;
    } else {
        uint64_t flags = 0;
        uint64_t button;

        if ( x_event->type == KeyPress || x_event->type == KeyRelease ) {
            if ( x_event->xkey.state & Mod1Mask ) {
                flags |= wm_event_key_modifier_alt_m;
            }

            if ( x_event->xkey.state & ControlMask ) {
                flags |= wm_event_key_modifier_ctrl_m;
            }

            if ( x_event->xkey.state & ShiftMask ) {
                flags |= wm_event_key_modifier_shift_m;
            }

            if ( x_event->xkey.state & LockMask ) {
                flags |= wm_event_key_modifier_capslock_m;
            }
        }

        if ( x_event->type == ButtonPress || x_event->type == ButtonRelease ) {
            if ( x_event->xbutton.state & Mod1Mask ) {
                flags |= wm_event_key_modifier_alt_m;
            }

            if ( x_event->xbutton.state & ControlMask ) {
                flags |= wm_event_key_modifier_ctrl_m;
            }

            if ( x_event->xbutton.state & ShiftMask ) {
                flags |= wm_event_key_modifier_shift_m;
            }

            if ( x_event->xbutton.state & LockMask ) {
                flags |= wm_event_key_modifier_capslock_m;
            }

            if ( x_event->xbutton.button == 1 )  {
                button = wm_mouse_state_left_m;
            } else if ( x_event->xbutton.button == 2 ) {
                button = wm_mouse_state_wheel_m;
            } else if ( x_event->xbutton.button == 3 ) {
                button = wm_mouse_state_right_m;
            } else if ( x_event->xbutton.button == 4 ) {
                button = wm_mouse_state_wheel_down_m;
            } else if ( x_event->xbutton.button == 5 ) {
                button = wm_mouse_state_wheel_up_m;
            }
        }

        switch ( x_event->type ) {
            case MotionNotify:
                window->cursor_x = x_event->xmotion.x;
                window->cursor_y = x_event->xmotion.y;
                break;

            case Expose:

                //std_log_info_m ( "Expose: " std_fmt_int_m, x_event->xexpose.count );

                if ( x_event->xexpose.count == 0 ) {
                    XClearWindow ( ( Display* ) window->info.os_handle.root, ( Window ) window->info.os_handle.window );
                }

                break;

            case KeyPress:
                // TODO pass key modifier flags
                //std_log_info_m ( "KeyPress: " std_fmt_uint_m " " std_fmt_uint_m " " std_fmt_int_m " " std_fmt_int_m,
                //               x_event->xkey.keycode, x_event->xkey.state, x_event->xkey.x, x_event->xkey.y );
                event = wm_event_key_down_m;
                args[0] = ( uint64_t ) x_event->xkey.keycode;
                args[1] = flags;
                break;

            case KeyRelease:
                //std_log_info_m ( "KeyRelease" );
                event = wm_event_key_up_m;
                args[0] = ( uint64_t ) x_event->xkey.keycode;
                args[1] = flags;
                break;

            case ButtonPress:

                //std_log_info_m ( "ButtonPress: " std_fmt_uint_m " " std_fmt_uint_m " " std_fmt_int_m " " std_fmt_int_m,
                //               x_event->xbutton.button, x_event->xbutton.state, x_event->xbutton.x, x_event->xbutton.y );

                if ( button == wm_mouse_state_left_m || button == wm_mouse_state_right_m || button == wm_mouse_state_wheel_m ) {
                    event = wm_event_mouse_down_m;
                } else {
                    event = wm_event_mouse_wheel_m;
                }

                args[0] = button;
                args[1] = x_event->xbutton.x;
                args[2] = x_event->xbutton.y;
                args[3] = flags;
                break;

            case ButtonRelease:

                //std_log_info_m ( "ButtonRelease" );

                if ( button == wm_mouse_state_left_m || button == wm_mouse_state_right_m || button == wm_mouse_state_wheel_m ) {
                    event = wm_event_mouse_up_m;
                } else {
                    // Wheel scroll causes both a ButtonPress and a ButtonRelease, so propagate the first and skip the second
                    return false;
                }

                args[0] = button;
                args[1] = x_event->xbutton.x;
                args[2] = x_event->xbutton.y;
                args[3] = flags;
                break;

            case FocusIn: {
                event = wm_event_gain_focus_m;
                std_mutex_t* mutex = &wm_window_state->mutex;
                std_mutex_lock ( mutex );
                window->info.is_focus = true;
                std_mutex_unlock ( mutex );
            }
            break;

            case FocusOut: {
                event = wm_event_lose_focus_m;
                std_mutex_t* mutex = &wm_window_state->mutex;
                std_mutex_lock ( mutex );
                window->info.is_focus = false;
                std_mutex_unlock ( mutex );
            }
            break;

            case ConfigureNotify:
                if ( x_event->xconfigure.width != window->info.width || x_event->xconfigure.height != window->info.height ) {
                    event = wm_event_resize_m;
                    std_mutex_t* mutex = &wm_window_state->mutex;
                    std_mutex_lock ( mutex );
                    args[0] = ( uint64_t ) x_event->xconfigure.width;
                    args[1] = ( uint64_t ) x_event->xconfigure.height;
                    args[2] = window->info.width;
                    args[3] = window->info.height;
                    window->info.width = args[0];
                    window->info.height = args[1];
                    std_mutex_unlock ( mutex );
                }

                break;
        }

        // TODO handle minimize, restore, maximize, open, close, destroy, move
    }

    return false;
}
#endif
#endif // std_enabled_m(wm_input_events_m)

#if std_enabled_m(wm_input_state_m)
void wm_window_debug_print_input_state ( wm_window_h handle, bool overwrite_console ) {
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];
    char* kb = window->input_state.keyboard;
    char* mouse = window->input_state.mouse;
    char line1[1024];
    std_str_format ( line1, sizeof ( line1 ),
        "ESC:" std_fmt_u8_m " F1:" std_fmt_u8_m " F2:" std_fmt_u8_m " F3:" std_fmt_u8_m " F4:" std_fmt_u8_m " F5:" std_fmt_u8_m " F6:" std_fmt_u8_m
        " F7:" std_fmt_u8_m " F8:" std_fmt_u8_m " F9:" std_fmt_u8_m " F10:" std_fmt_u8_m " F11:" std_fmt_u8_m " F12:" std_fmt_u8_m,
        kb[wm_keyboard_state_esc_m], kb[wm_keyboard_state_f1_m], kb[wm_keyboard_state_f2_m], kb[wm_keyboard_state_f3_m], kb[wm_keyboard_state_f4_m],
        kb[wm_keyboard_state_f5_m], kb[wm_keyboard_state_f6_m], kb[wm_keyboard_state_f7_m], kb[wm_keyboard_state_f8_m], kb[wm_keyboard_state_f9_m],
        kb[wm_keyboard_state_f10_m], kb[wm_keyboard_state_f11_m], kb[wm_keyboard_state_f12_m] );
    char line2[1024];
    std_str_format ( line2, sizeof ( line2 ),
        "1:" std_fmt_u8_m " 2:" std_fmt_u8_m " 3:" std_fmt_u8_m " 4:" std_fmt_u8_m " 5:" std_fmt_u8_m
        " 6:" std_fmt_u8_m " 7:" std_fmt_u8_m " 8:" std_fmt_u8_m " 9:" std_fmt_u8_m " 0:" std_fmt_u8_m,
        kb[wm_keyboard_state_1_m], kb[wm_keyboard_state_2_m], kb[wm_keyboard_state_3_m], kb[wm_keyboard_state_4_m], kb[wm_keyboard_state_5_m],
        kb[wm_keyboard_state_6_m], kb[wm_keyboard_state_7_m], kb[wm_keyboard_state_8_m], kb[wm_keyboard_state_9_m], kb[wm_keyboard_state_0_m] );
    char line3[1024];
    std_str_format ( line3, sizeof ( line3 ),
        "Q:" std_fmt_u8_m " W:" std_fmt_u8_m " E:" std_fmt_u8_m " R:" std_fmt_u8_m " T:" std_fmt_u8_m " Y:" std_fmt_u8_m
        " U:" std_fmt_u8_m " I:" std_fmt_u8_m " O:" std_fmt_u8_m " P:" std_fmt_u8_m,
        kb[wm_keyboard_state_q_m], kb[wm_keyboard_state_w_m], kb[wm_keyboard_state_e_m], kb[wm_keyboard_state_r_m], kb[wm_keyboard_state_t_m], kb[wm_keyboard_state_y_m],
        kb[wm_keyboard_state_u_m], kb[wm_keyboard_state_i_m], kb[wm_keyboard_state_o_m], kb[wm_keyboard_state_p_m] );
    char line4[1024];
    std_str_format ( line4, sizeof ( line4 ),
        "A:" std_fmt_u8_m " S:" std_fmt_u8_m " D:" std_fmt_u8_m " F:" std_fmt_u8_m " G:" std_fmt_u8_m " H:" std_fmt_u8_m
        " J:" std_fmt_u8_m" K:" std_fmt_u8_m" L:" std_fmt_u8_m,
        kb[wm_keyboard_state_a_m], kb[wm_keyboard_state_s_m], kb[wm_keyboard_state_d_m], kb[wm_keyboard_state_f_m], kb[wm_keyboard_state_g_m], kb[wm_keyboard_state_h_m],
        kb[wm_keyboard_state_j_m], kb[wm_keyboard_state_k_m], kb[wm_keyboard_state_l_m] );
    char line5[1024];
    std_str_format ( line5, sizeof ( line5 ),
        "Z:" std_fmt_u8_m " X:" std_fmt_u8_m " C:" std_fmt_u8_m " V:" std_fmt_u8_m " B:" std_fmt_u8_m " N:" std_fmt_u8_m " M:" std_fmt_u8_m,
        kb[wm_keyboard_state_z_m], kb[wm_keyboard_state_x_m], kb[wm_keyboard_state_c_m], kb[wm_keyboard_state_v_m], kb[wm_keyboard_state_b_m], kb[wm_keyboard_state_n_m],
        kb[wm_keyboard_state_m_m] );
    char line6[1024];
    std_str_format ( line6, sizeof ( line6 ),
        "TAB:"std_fmt_u8_m " LSHIFT:"std_fmt_u8_m" LCTRL:"std_fmt_u8_m" LALT:"std_fmt_u8_m" SPACE:"std_fmt_u8_m" RALT:"std_fmt_u8_m" RCTRL:"std_fmt_u8_m" RSHIFT:"std_fmt_u8_m" ENTER:"std_fmt_u8_m" BACKSPACE:"std_fmt_u8_m,
        kb[wm_keyboard_state_tab_m], kb[wm_keyboard_state_shift_left_m], kb[wm_keyboard_state_ctrl_left_m], kb[wm_keyboard_state_alt_left_m], kb[wm_keyboard_state_space_m],
        kb[wm_keyboard_state_alt_right_m], kb[wm_keyboard_state_ctrl_right_m], kb[wm_keyboard_state_shift_right_m], kb[wm_keyboard_state_enter_m], kb[wm_keyboard_state_backspace_m] );
    char line7[1024];
    std_str_format ( line7, sizeof ( line7 ),
        "UP:" std_fmt_u8_m " LEFT:" std_fmt_u8_m " DOWN:" std_fmt_u8_m " RIGHT:" std_fmt_u8_m " X:" std_fmt_u32_pad_m ( 4 ) " Y:" std_fmt_u32_pad_m ( 4 ) " LB:" std_fmt_u8_m " RB:" std_fmt_u8_m " WU:" std_fmt_u8_m " WD:" std_fmt_u8_m,
        kb[wm_keyboard_state_up_m], kb[wm_keyboard_state_left_m], kb[wm_keyboard_state_down_m], kb[wm_keyboard_state_right_m], window->input_state.cursor_x, window->input_state.cursor_y, mouse[wm_mouse_state_left_m], mouse[wm_mouse_state_right_m], mouse[wm_mouse_state_wheel_up_m], mouse[wm_mouse_state_wheel_down_m] );

    const char* prefix = "";

    if ( overwrite_console ) {
        prefix = std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m;
    }

    std_log_m ( 0, std_fmt_str_m"\n"std_fmt_str_m"\n"std_fmt_str_m"\n"std_fmt_str_m"\n"std_fmt_str_m"\n"std_fmt_str_m"\n"std_fmt_str_m"\n"std_fmt_str_m"\n",
        prefix, line1, line2, line3, line4, line5, line6, line7 );
}

static void wm_clear_input_state ( wm_window_h window_handle ) {
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( window_handle )];

    std_assert_m ( window->gen == wm_handle_gen_m ( window_handle ) );

    std_mem_zero ( window->input_state.keyboard, wm_keyboard_state_size_m );
    std_mem_zero ( window->input_state.mouse, wm_mouse_state_size_m );
}

#if defined(std_platform_win32_m)
static void wm_update_input_state ( wm_window_h handle ) {
    // Make sure window is still alive, return immediately if not
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        return;
    }

    // Fetch system info before locking
    BOOL result;
    POINT cursor;

    GetCursorPos ( &cursor );

    uint8_t keyboard[256];
    GetKeyState ( 0 );
    result = GetKeyboardState ( keyboard );
    std_assert_m ( result );

    // TODO why
    SHORT left_button = GetAsyncKeyState ( VK_LBUTTON );
    SHORT right_button = GetAsyncKeyState ( VK_RBUTTON );

    std_mutex_lock ( &wm_window_state->mutex );

    int64_t x = window->info.x + window->info.border_right;
    int64_t y = window->info.y + window->info.border_top;
    int64_t w = ( int64_t ) window->info.width;  // Casted to signed because of following comparison. TODO change type to int32_t to avoid possible overflow when casting?
    int64_t h = ( int64_t ) window->info.height;
    window->input_state.global_cursor_x = cursor.x;
    window->input_state.global_cursor_y = cursor.y;
    window->input_state.cursor_x = cursor.x < x ? 0 : cursor.x > x + w ? w : cursor.x - x;
    window->input_state.cursor_y = cursor.y < y ? 0 : cursor.y > y + h ? h : cursor.y - y;
    //std_mem_zero ( window->input_state.keyboard, wm_keyboard_state_size_m );
    //std_mem_zero ( window->input_state.mouse, wm_mouse_state_size_m );

    if ( window->info.is_focus ) {
        // Letters A-Z
        {
            size_t os_base = 0x41;
            size_t wm_base = wm_keyboard_state_a_m;

            for ( size_t i = 0; i < 26; ++i ) {
                window->input_state.keyboard[wm_base + i] = keyboard[os_base + i] >> 7;
            }
        }
        // Numbers 0-9
        {
            size_t os_base = 0x30;
            size_t wm_base = wm_keyboard_state_0_m;

            for ( size_t i = 0; i < 10; ++i ) {
                window->input_state.keyboard[wm_base + i] = keyboard[os_base + i] >> 7;

                if ( window->input_state.keyboard[wm_base + i] ) {
                    int a = 1;
                    ( void ) a;
                }
            }
        }
        // F keys F1-F12
        {
            size_t os_base = 0x70;
            size_t wm_base = wm_keyboard_state_f1_m;

            for ( size_t i = 0; i < 12; ++i ) {
                window->input_state.keyboard[wm_base + i] = keyboard[os_base + i] >> 7;
            }
        }
        // Special keys
        {
            window->input_state.keyboard[wm_keyboard_state_esc_m] = keyboard[0x1B] >> 7;

            window->input_state.keyboard[wm_keyboard_state_shift_left_m] = keyboard[0xA0] >> 7;
            window->input_state.keyboard[wm_keyboard_state_shift_right_m] = keyboard[0xA1] >> 7;

            window->input_state.keyboard[wm_keyboard_state_ctrl_left_m] = keyboard[0XA2] >> 7;
            window->input_state.keyboard[wm_keyboard_state_ctrl_right_m] = keyboard[0xA3] >> 7;

            window->input_state.keyboard[wm_keyboard_state_alt_left_m] = keyboard[0XA4] >> 7;
            window->input_state.keyboard[wm_keyboard_state_alt_right_m] = keyboard[0xA5] >> 7;

            window->input_state.keyboard[wm_keyboard_state_space_m] = keyboard[0X20] >> 7;

            window->input_state.keyboard[wm_keyboard_state_enter_m] = keyboard[0x0D] >> 7;

            window->input_state.keyboard[wm_keyboard_state_backspace_m] = keyboard[0x08] >> 7;

            window->input_state.keyboard[wm_keyboard_state_up_m] = keyboard[0x26] >> 7;
            window->input_state.keyboard[wm_keyboard_state_left_m] = keyboard[0x25] >> 7;
            window->input_state.keyboard[wm_keyboard_state_down_m] = keyboard[0x28] >> 7;
            window->input_state.keyboard[wm_keyboard_state_right_m] = keyboard[0x27] >> 7;

            window->input_state.keyboard[wm_keyboard_state_tab_m] = keyboard[0x09] >> 7;
        }

        window->input_state.mouse[wm_mouse_state_left_m] = ( left_button & 0x8000 ) ? 1 : 0;
        window->input_state.mouse[wm_mouse_state_right_m] = ( right_button & 0x8000 ) ? 1 : 0;
        window->input_state.mouse[wm_mouse_state_wheel_up_m] = window->pending_wheel_up;
        window->input_state.mouse[wm_mouse_state_wheel_down_m] = window->pending_wheel_down;
    }

    std_mutex_unlock ( &wm_window_state->mutex );
}
#elif defined(std_platform_linux_m)
static void wm_update_input_state ( wm_window_h handle ) {
    // Make sure window is still alive, return immediately if not
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        return;
    }

    Display* x_display = ( Display* ) window->info.os_handle.root;
    Window x_window = ( Window ) window->info.os_handle.window;

    char keyboard[32];
    XQueryKeymap ( x_display, keyboard );

    Window pointer_window;
    Window pointer_root_window;
    int window_x;
    int window_y;
    int root_x;
    int root_y;
    unsigned int pointer_mask;
    XQueryPointer ( x_display, x_window, &pointer_window, &pointer_root_window, &root_x, &root_y, &window_x, &window_y, &pointer_mask );

    // TODO make this behave the same as win32
    window->input_state.global_cursor_x = root_x;
    window->input_state.global_cursor_y = root_y;
    window->input_state.cursor_x = window_x < 0 ? 0 : window_x;
    window->input_state.cursor_y = window_y < 0 ? 0 : window_y;
    //window->input_state.cursor_x = window->cursor_x;
    //window->input_state.cursor_y = window->cursor_y;

    std_mutex_lock ( &wm_window_state->mutex );

    // TODO set input_state.cursor
    std_mem_zero ( window->input_state.keyboard, wm_keyboard_state_size_m );

    if ( window->info.is_focus ) {
        // TODO is there a better way to index the key bit other than doing a div per key?
        // Letters A-Z
        {
            size_t os_base = XK_a;
            size_t wm_base = wm_keyboard_state_a_m;

            for ( size_t i = 0; i < 26; ++i ) {
                size_t code = XKeysymToKeycode ( x_display, os_base + i );
                size_t byte_idx = code / 8;
                size_t shift = code % 8;
                window->input_state.keyboard[wm_base + i] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;
            }
        }
        // Numbers 0-9
        {
            size_t os_base = XK_0;
            size_t wm_base = wm_keyboard_state_0_m;

            for ( size_t i = 0; i < 10; ++i ) {
                size_t code = XKeysymToKeycode ( x_display, os_base + i );
                size_t byte_idx = code / 8;
                size_t shift = code % 8;
                window->input_state.keyboard[wm_base + i] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;
            }
        }
        // F keys F1-F12
        {
            size_t os_base = XK_F1;
            size_t wm_base = wm_keyboard_state_f1_m;

            for ( size_t i = 0; i < 12; ++i ) {
                size_t code = XKeysymToKeycode ( x_display, os_base + i );
                size_t byte_idx = code / 8;
                size_t shift = code % 8;
                window->input_state.keyboard[wm_base + i] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;
            }
        }
        // Special keys
        {
            size_t code, byte_idx, shift;

            code = XKeysymToKeycode ( x_display, XK_Escape );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_esc_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Shift_L );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_shift_left_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Shift_R );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_shift_right_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Control_L );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_ctrl_left_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Control_R );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_ctrl_right_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Alt_L );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_alt_left_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Alt_R );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_alt_right_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_space );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_space_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Return );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_enter_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_BackSpace );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_backspace_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Up );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_up_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Left );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_left_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Down );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_down_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

            code = XKeysymToKeycode ( x_display, XK_Right );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_right_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;
        }

        window->input_state.mouse[wm_mouse_state_left_m] = ( pointer_mask & 0x100 ) != 0;
        window->input_state.mouse[wm_mouse_state_right_m] = ( pointer_mask & 0x400 ) != 0;
    } else {
        window->input_state.mouse[wm_mouse_state_left_m] = 0;
        window->input_state.mouse[wm_mouse_state_right_m] = 0;
    }

    std_mutex_unlock ( &wm_window_state->mutex );
}
#endif // defined(std_platform_linux_m)

bool wm_window_get_input_state ( wm_window_h handle, wm_input_state_t* state ) {
    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    // Validate IID
    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        // On fail unlock and return false
        std_mutex_unlock ( &wm_window_state->mutex );
        return false;
    }

    // Copy state data, unlock and return true
    *state = window->input_state;
    std_mutex_unlock ( &wm_window_state->mutex );
    return true;
}
#endif // std_enabled_m(wm_input_state_m)

#if defined(std_platform_win32_m)
LRESULT CALLBACK wm_window_os_callback ( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam ) {
    // TODO why is this checking that the window is valid on every message? to protect from messages coming after a window destroy? can that happen?

    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    // Translate os handle to user handle
    uint64_t hash = std_hash_64_m ( ( uint64_t ) hwnd );
    void* handle_lookup = std_hash_map_lookup ( &wm_window_state->map, hash );

    // Return on lookup fail
    // TODO assert false?
    if ( handle_lookup == NULL ) {
        std_mutex_unlock ( &wm_window_state->mutex );
        return DefWindowProc ( hwnd, msg, wparam, lparam );
    }

    // Get access to the window data structure
    wm_window_h handle = * ( wm_window_h* ) handle_lookup;
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    // Validate the data structure and return on fail
    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        std_mutex_unlock ( &wm_window_state->mutex );
        return DefWindowProc ( hwnd, msg, wparam, lparam );
    }

    // Release
    std_mutex_unlock ( &wm_window_state->mutex );

    // Process msg
#if std_enabled_m ( wm_input_events_m )
    int64_t retcode = 0;

    if ( wm_process_input_event ( handle, window, hwnd, msg, wparam, lparam, &retcode ) ) {
        return ( LRESULT ) retcode;
    } else {
        // Send event to default handler if the wm handler didn't trap it
        return DefWindowProc ( hwnd, msg, wparam, lparam );
    }

#else
    // Use default event handler if wm_input_events_m is off
    return DefWindowProc ( hwnd, msg, wparam, lparam );
#endif
}

#elif defined(std_platform_linux_m)

static void wm_window_default_event_handler ( wm_window_h handle, wm_window_t* window, XEvent* event ) {
    if ( event->type == ClientMessage && event->xclient.data.l[0] == window->exit_message_id ) {
        wm_window_destroy ( handle );
        return;
    }
}

static void wm_window_event_dispatcher ( wm_window_h handle, wm_window_t* window, XEvent* event ) {
#if std_enabled_m(wm_input_events_m)

    if ( wm_process_input_event ( handle, window, event ) ) {
        //    return ( LRESULT ) retcode;
    } else {
        // Send event to default handler if the wm handler didn't trap it
        wm_window_default_event_handler ( handle, window, event );
    }

#else
    // Use default event handler if wm_input_events_m is off
    wm_window_default_event_handler ( handle, window, event );
#endif

}
#endif // defined(std_platform_linux_m)

void wm_window_update ( wm_window_h handle ) {
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

#if std_enabled_m ( wm_input_state_m )
    window->pending_wheel_up = 0;
    window->pending_wheel_down = 0;
    wm_clear_input_state ( handle );
#endif

#if defined(std_platform_win32_m)
    MSG msg;

    while ( PeekMessage ( &msg, ( HANDLE ) window->info.os_handle.window, 0, 0, PM_REMOVE ) ) {
        TranslateMessage ( &msg );
        DispatchMessage ( &msg );
    }

#elif defined(std_platform_linux_m)
    XEvent event;

    while ( XPending ( ( Display* ) window->info.os_handle.root ) > 0 ) {
        XNextEvent ( ( Display* ) window->info.os_handle.root, &event );
        wm_window_event_dispatcher ( handle, window, &event );
    }

#endif // defined(std_platform_linux_m)

    // Update state after processing all events.
#if std_enabled_m ( wm_input_state_m )
    wm_update_input_state ( handle );
#endif
}

wm_window_h wm_window_create ( const wm_window_params_t* params ) {
    // Create os window
    wm_window_os_h os_handle;
    size_t border_top;
    size_t border_bottom;
    size_t border_left;
    size_t border_right;
    size_t border_width;
    size_t border_height;
#if defined(std_platform_win32_m)
    {
        // TODO convert class_name from u8 to wchar and call RegisterClassExW
        const char* class_name = params->name;
        DWORD wstyle = params->borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
        std_assert_m ( params->width <= LONG_MAX );
        std_assert_m ( params->height <= LONG_MAX );
        RECT rect = { 0, 0, ( LONG ) params->width, ( LONG ) params->height };
        AdjustWindowRect ( &rect, wstyle, false );
        int outer_width = rect.right - rect.left;
        int outer_height = rect.bottom - rect.top;
        WNDCLASSEX wc = {0};
        HINSTANCE instance = GetModuleHandle ( NULL );
        //instance = ( HINSTANCE ) params->os_module;
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
        wc.lpfnWndProc = wm_window_os_callback;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = instance;
        wc.hIcon = LoadIcon ( NULL, IDI_WINLOGO );
        wc.hIconSm = wc.hIcon;
        wc.hCursor = LoadCursor ( NULL, IDC_ARROW );
        wc.hbrBackground = ( HBRUSH ) BLACK_BRUSH;
        wc.lpszMenuName = NULL;
        wc.lpszClassName = class_name;
        wc.cbSize = sizeof ( WNDCLASSEXA );

        if ( !RegisterClassExA ( &wc ) ) {
            DWORD error = GetLastError();
            std_unused_m(error);
            return wm_null_handle_m;
        }

        HWND hwnd = CreateWindow ( class_name, params->name, wstyle, params->x, params->y, outer_width, outer_height, NULL, NULL, instance, NULL );

        if ( hwnd == NULL ) {
            return wm_null_handle_m;
        }

        int show_flags = 0;
        show_flags |= params->gain_focus ? SW_SHOW : SW_SHOWNOACTIVATE;
        ShowWindow ( hwnd, show_flags );
        os_handle.root = ( uint64_t ) instance;
        os_handle.window = ( uint64_t ) hwnd;
        border_top = -rect.top;
        border_left = -rect.left;
        border_bottom = rect.bottom - params->height;
        border_right = rect.right - params->width;
        border_width = ( size_t ) outer_width - params->width;
        border_height = ( size_t ) outer_height - params->height;
    }
#elif defined(std_platform_linux_m)
    //std_unused_m ( gain_focus ); // TODO

    uint64_t exit_message_id;
    {
        // TODO is it good practice to create one display per window? should it be shared across all windows?
        Display* x_display = XOpenDisplay ( NULL );
        std_assert_m ( x_display );

        int screen = DefaultScreen ( x_display );

        int event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | FocusChangeMask | StructureNotifyMask | PointerMotionMask;
        //ResizeRedirectMask;

        XSetWindowAttributes wa;
        wa.event_mask = 0;
        //Window x_window = XCreateSimpleWindow ( x_display, DefaultRootWindow ( x_display ), x, y, width, height, 0, 0, 0 );
        Window x_window = XCreateWindow ( x_display, RootWindow ( x_display, screen ), params->x, params->y, params->width, params->height,
            0, DefaultDepth ( x_display, screen ), InputOutput, DefaultVisual ( x_display, screen ), 0, &wa );
        XSelectInput ( x_display, x_window, event_mask );

        // register interest in the delete window message
        Atom delete_atom = XInternAtom ( x_display, "wm_delete_window_m", False );
        XSetWMProtocols ( x_display, x_window, &delete_atom, 1 );

        exit_message_id = ( uint64_t ) delete_atom;

        if ( params->borderless ) {
            Atom window_type = XInternAtom ( x_display, "_NET_wm_window_type_m", False );
            long value = XInternAtom ( x_display, "_NET_wm_window_type_dock_m", False );
            XChangeProperty ( x_display, x_window, window_type, XA_ATOM, 32, PropModeReplace, ( unsigned char* ) &value, 1 );
        }

        XMapWindow ( x_display, x_window );
        XStoreName ( x_display, x_window, params->name );
        XFlush ( x_display );

        os_handle.root = ( uint64_t ) x_display;
        os_handle.window = ( uint64_t ) x_window;
        border_width = 0;
        border_height = 0;

        border_top = 0;
        border_right = 0;
        border_bottom = 0;
        border_left = 0;
    }
#endif // defined(std_platform_linux_m)

    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    // Pop from freelist and write all data
    wm_window_t* window = std_list_pop_m ( &wm_window_state->windows_freelist );

#if std_enabled_m ( wm_input_events_m )
    window->handlers_count = 0;
#endif

#if std_enabled_m ( wm_input_state_m )
    std_mem_zero ( &window->input_state, sizeof ( window->input_state ) );
#endif

    std_str_copy ( window->info.title, wm_window_title_max_len_m, params->name );
    window->info.x = params->x;
    window->info.y = params->y;
    window->info.width = params->width;
    window->info.height = params->height;
    window->info.os_handle = os_handle;
    window->info.is_focus = params->gain_focus; //flags & SW_SHOW; // TODO
    window->info.is_minimized = false;
    window->info.border_top = border_top;
    window->info.border_right = border_right;
    window->info.border_bottom = border_bottom;
    window->info.border_left = border_left;
    window->info.borders_width = border_width;
    window->info.borders_height = border_height;
#if defined(std_platform_linux_m)
    //window->exit_message_id = ( uint64_t ) exit_message_id;
    window->exit_message_id = exit_message_id;
#endif

    wm_window_h h = wm_make_handle_m ( window - wm_window_state->windows_array, window->gen );
#if defined(std_platform_win32_m)
    uint64_t hash = std_hash_64_m ( ( uint64_t ) os_handle.window );
    std_hash_map_insert ( &wm_window_state->map, hash, ( uint64_t ) h );
#endif

    // Add window to non-free windows list
    std_dlist_push ( &wm_window_state->windows_list, window );

    // Unlock
    std_mutex_unlock ( &wm_window_state->mutex );
    return h;
}

bool mw_window_is_alive ( wm_window_h handle ) {
    // Lock
    std_mutex_lock ( &wm_window_state->mutex );

    // Check IID
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        // If the check fails the window is dead, unlock and return false
        std_mutex_unlock ( &wm_window_state->mutex );
        return false;
    }

    // Unlock and return true
    std_mutex_unlock ( &wm_window_state->mutex );
    return true;
}

bool wm_window_destroy ( wm_window_h handle ) {
    // Lock
    std_mutex_lock ( &wm_window_state->mutex );

    // Lookup handle on map
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    // Validate IID
    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        // On fail unlock and return false
        std_mutex_unlock ( &wm_window_state->mutex );
        return false;
    }

    // Invalidate IID
    window->gen++;

    // TODO fence here?

    // Destroy os window
#if defined(std_platform_win32_m)
    DestroyWindow ( ( HWND ) window->info.os_handle.window );
    uint64_t hash = std_hash_64_m ( ( uint64_t ) window->info.os_handle.window );
    bool remove_result = std_hash_map_remove ( &wm_window_state->map, hash );
    std_assert_m ( remove_result );
#elif defined(std_platform_linux_m)
    XDestroyWindow ( ( Display* ) window->info.os_handle.root, ( Window ) window->info.os_handle.window );
#endif

    // Remove window to non-free windows list
    if ( window->next ) {
        window->next->prev = window->prev;
    }

    if ( window->prev ) {
        window->prev->next = window->next;
    }

    // Put window back into freelist
    std_list_push ( &wm_window_state->windows_freelist, window );

    // Unlock and return true
    std_mutex_unlock ( &wm_window_state->mutex );
    return true;
}

#if std_enabled_m(wm_input_events_m)
bool wm_window_add_event_handler ( wm_window_h handle, const wm_input_event_handler_t* handler ) {
    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    // Validate IID
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        // On fail unlock and return false
        std_mutex_unlock ( &wm_window_state->mutex );
        return false;
    }

    // Unlock
    std_mutex_unlock ( &wm_window_state->mutex );
    // Lock again, this time for a write
    std_mutex_lock ( &wm_window_state->mutex );
    // Add the handler
    window->input_handlers[window->handlers_count++] = *handler;
    // Unlock and return true
    std_mutex_unlock ( &wm_window_state->mutex );
    return true;
}

bool wm_window_remove_event_handler ( wm_window_h handle, const wm_input_event_handler_t* handler ) {
    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    // Validate IID
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        // On fail unlock and return false
        std_mutex_unlock ( &wm_window_state->mutex );
        return false;
    }

    // Unlock
    std_mutex_unlock ( &wm_window_state->mutex );
    // Lock again, this time for a write
    std_mutex_lock ( &wm_window_state->mutex );
    // Lookup and remove the handler
    size_t handlers_count = window->handlers_count;
    bool found = false;

    for ( size_t i = 0; i < handlers_count; ++i ) {
        if ( window->input_handlers[i].callback == handler->callback && window->input_handlers[i].event_mask == handler->event_mask ) {
            // Remove and shift back, std::vector style
            --handlers_count;

            for ( ; i < handlers_count; ++i ) {
                window->input_handlers[i] = window->input_handlers[i + 1];
            }

            window->handlers_count = handlers_count;
            break;
        }
    }

    // Unlock and return true
    std_mutex_unlock ( &wm_window_state->mutex );
    return found;
}
#endif // std_enabled_m(wm_input_events_m)

bool wm_window_get_info ( wm_window_h handle, wm_window_info_t* info ) {
    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];

    // Validate IID
    if ( window->gen != wm_handle_gen_m ( handle ) ) {
        // On fail unlock and return false
        std_mutex_unlock ( &wm_window_state->mutex );
        return false;
    }

    // Copy info data, unlock and return true
    *info = window->info;
    std_mutex_unlock ( &wm_window_state->mutex );
    return true;
}
