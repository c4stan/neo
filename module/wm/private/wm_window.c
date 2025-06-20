#include "wm_window.h"

#include "wm_state.h"

#include <std_mutex.h>
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

#define wm_window_bitset_u64_count_m std_div_ceil_m ( wm_max_windows_m, 64 )

void wm_window_load ( wm_window_state_t* state ) {
    wm_window_state = state;

    wm_window_state->windows_array = std_virtual_heap_alloc_array_m ( wm_window_t, wm_max_windows_m );
    wm_window_state->windows_freelist = std_freelist_m ( wm_window_state->windows_array, wm_max_windows_m );
    wm_window_state->windows_list = NULL;
    wm_window_state->windows_bitset = std_virtual_heap_alloc_array_m ( uint64_t, wm_window_bitset_u64_count_m );
    std_mem_zero_array_m ( wm_window_state->windows_array, wm_max_windows_m );
    std_mem_zero_array_m ( wm_window_state->windows_bitset, wm_window_bitset_u64_count_m );

    std_mutex_init ( &wm_window_state->mutex );

#if defined(std_platform_win32_m)
    void* map_keys = std_virtual_heap_alloc_array_m ( wm_window_h, wm_max_windows_m * 2 );
    void* map_values = std_virtual_heap_alloc_array_m ( uint64_t, wm_max_windows_m * 2 );
    wm_window_state->map = std_hash_map ( map_keys, map_values, wm_max_windows_m * 2 );
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
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, wm_window_state->windows_bitset, idx, wm_window_bitset_u64_count_m ) ) {
        wm_window_h handle = idx;
        wm_window_destroy ( handle );
        ++idx;
    }

    std_virtual_heap_free ( wm_window_state->windows_array );
    std_virtual_heap_free ( wm_window_state->windows_bitset );
#if defined(std_platform_win32_m)
    std_virtual_heap_free ( wm_window_state->map.hashes );
    std_virtual_heap_free ( wm_window_state->map.payloads );
#endif
}

static uint32_t wm_window_keycode ( uint64_t os_keycode ) {
#if defined ( std_platform_win32_m )
    switch ( os_keycode ) {
    case VK_BACK:
        return wm_keyboard_state_backspace_m;
    case VK_RETURN:
        return wm_keyboard_state_enter_m;
    case VK_OEM_PERIOD:
        return wm_keyboard_state_period_m;
    case VK_OEM_PLUS:
        return wm_keyboard_state_plus_m;
    case VK_OEM_MINUS:
        return wm_keyboard_state_minus_m;
    case VK_TAB:
        return wm_keyboard_state_tab_m;
    case VK_LSHIFT:
        return wm_keyboard_state_shift_left_m;
    case VK_RSHIFT:
        return wm_keyboard_state_shift_right_m;
    case VK_LCONTROL:
        return wm_keyboard_state_ctrl_left_m;
    case VK_RCONTROL:
        return wm_keyboard_state_ctrl_right_m;
    case VK_LMENU:
        return wm_keyboard_state_alt_left_m;
    case VK_RMENU:
        return wm_keyboard_state_alt_right_m;
    case VK_SPACE:
        return wm_keyboard_state_space_m;
    case VK_LEFT:
        return wm_keyboard_state_left_m;
    case VK_UP:
        return wm_keyboard_state_up_m;
    case VK_RIGHT:
        return wm_keyboard_state_right_m;
    case VK_DOWN:
        return wm_keyboard_state_down_m;
    case 0x30:
        return wm_keyboard_state_0_m;
    case 0x31:
        return wm_keyboard_state_1_m;
    case 0x32:
        return wm_keyboard_state_2_m;
    case 0x33:
        return wm_keyboard_state_3_m;
    case 0x34:
        return wm_keyboard_state_4_m;
    case 0x35:
        return wm_keyboard_state_5_m;
    case 0x36:
        return wm_keyboard_state_6_m;
    case 0x37:
        return wm_keyboard_state_7_m;
    case 0x38:
        return wm_keyboard_state_8_m;
    case 0x39:
        return wm_keyboard_state_9_m;
    case 0x41:
        return wm_keyboard_state_a_m;
    case 0x42:
        return wm_keyboard_state_b_m;
    case 0x43:
        return wm_keyboard_state_c_m;
    case 0x44:
        return wm_keyboard_state_d_m;
    case 0x45:
        return wm_keyboard_state_e_m;
    case 0x46:
        return wm_keyboard_state_f_m;
    case 0x47:
        return wm_keyboard_state_g_m;
    case 0x48:
        return wm_keyboard_state_h_m;
    case 0x49:
        return wm_keyboard_state_i_m;
    case 0x4a:
        return wm_keyboard_state_j_m;
    case 0x4b:
        return wm_keyboard_state_k_m;
    case 0x4c:
        return wm_keyboard_state_l_m;
    case 0x4d:
        return wm_keyboard_state_m_m;
    case 0x4e:
        return wm_keyboard_state_n_m;
    case 0x4f:
        return wm_keyboard_state_o_m;
    case 0x50:
        return wm_keyboard_state_p_m;
    case 0x51:
        return wm_keyboard_state_q_m;
    case 0x52:
        return wm_keyboard_state_r_m;
    case 0x53:
        return wm_keyboard_state_s_m;
    case 0x54:
        return wm_keyboard_state_t_m;
    case 0x55:
        return wm_keyboard_state_u_m;
    case 0x56:
        return wm_keyboard_state_v_m;
    case 0x57:
        return wm_keyboard_state_w_m;
    case 0x58:
        return wm_keyboard_state_x_m;
    case 0x59:
        return wm_keyboard_state_y_m;
    case 0x5a:
        return wm_keyboard_state_z_m;
    case 0x70:
        return wm_keyboard_state_f1_m;
    case 0x71:
        return wm_keyboard_state_f2_m;
    case 0x72:
        return wm_keyboard_state_f3_m;
    case 0x73:
        return wm_keyboard_state_f4_m;
    case 0x74:
        return wm_keyboard_state_f5_m;
    case 0x75:
        return wm_keyboard_state_f6_m;
    case 0x76:
        return wm_keyboard_state_f7_m;
    case 0x77:
        return wm_keyboard_state_f8_m;
    case 0x78:
        return wm_keyboard_state_f9_m;
    case 0x79:
        return wm_keyboard_state_f10_m;
    case 0x7a:
        return wm_keyboard_state_f11_m;
    case 0x7b:
        return wm_keyboard_state_f12_m;
    case VK_CAPITAL:
        return wm_keyboard_state_capslock_m;
    case VK_DELETE:
        return wm_keyboard_state_del_m;
    default:
        return wm_keyboard_state_count_m;
    }
#elif defined ( std_platform_linux_m )
    switch ( os_keycode ) {
    case XK_BackSpace:
        return wm_keyboard_state_backspace_m;
    case XK_Return:
        return wm_keyboard_state_enter_m;
    case XK_period:
        return wm_keyboard_state_period_m;
    case XK_plus:
        return wm_keyboard_state_plus_m;
    case XK_minus:
        return wm_keyboard_state_minus_m;
    case XK_Tab:
        return wm_keyboard_state_tab_m;
    case XK_Shift_L:
        return wm_keyboard_state_shift_left_m;
    case XK_Shift_R:
        return wm_keyboard_state_shift_right_m;
    case XK_Control_L:
        return wm_keyboard_state_ctrl_left_m;
    case XK_Control_R:
        return wm_keyboard_state_ctrl_right_m;
    case XK_Alt_L:
        return wm_keyboard_state_alt_left_m;
    case XK_Alt_R:
        return wm_keyboard_state_alt_right_m;
    case XK_space:
        return wm_keyboard_state_space_m;
    case XK_Left:
        return wm_keyboard_state_left_m;
    case XK_Up:
        return wm_keyboard_state_up_m;
    case XK_Right:
        return wm_keyboard_state_right_m;
    case XK_Down:
        return wm_keyboard_state_down_m;
    case XK_0:
        return wm_keyboard_state_0_m;
    case XK_1:
        return wm_keyboard_state_1_m;
    case XK_2:
        return wm_keyboard_state_2_m;
    case XK_3:
        return wm_keyboard_state_3_m;
    case XK_4:
        return wm_keyboard_state_4_m;
    case XK_5:
        return wm_keyboard_state_5_m;
    case XK_6:
        return wm_keyboard_state_6_m;
    case XK_7:
        return wm_keyboard_state_7_m;
    case XK_8:
        return wm_keyboard_state_8_m;
    case XK_9:
        return wm_keyboard_state_9_m;
    case XK_A:
    case XK_a:
        return wm_keyboard_state_a_m;
    case XK_B:
    case XK_b:
        return wm_keyboard_state_b_m;
    case XK_C:
    case XK_c:
        return wm_keyboard_state_c_m;
    case XK_D:
    case XK_d:
        return wm_keyboard_state_d_m;
    case XK_E:
    case XK_e:
        return wm_keyboard_state_e_m;
    case XK_F:
    case XK_f:
        return wm_keyboard_state_f_m;
    case XK_G:
    case XK_g:
        return wm_keyboard_state_g_m;
    case XK_H:
    case XK_h:
        return wm_keyboard_state_h_m;
    case XK_I:
    case XK_i:
        return wm_keyboard_state_i_m;
    case XK_J:
    case XK_j:
        return wm_keyboard_state_j_m;
    case XK_K:
    case XK_k:
        return wm_keyboard_state_k_m;
    case XK_L:
    case XK_l:
        return wm_keyboard_state_l_m;
    case XK_M:
    case XK_m:
        return wm_keyboard_state_m_m;
    case XK_N:
    case XK_n:
        return wm_keyboard_state_n_m;
    case XK_O:
    case XK_o:
        return wm_keyboard_state_o_m;
    case XK_P:
    case XK_p:
        return wm_keyboard_state_p_m;
    case XK_Q:
    case XK_q:
        return wm_keyboard_state_q_m;
    case XK_R:
    case XK_r:
        return wm_keyboard_state_r_m;
    case XK_S:
    case XK_s:
        return wm_keyboard_state_s_m;
    case XK_T:
    case XK_t:
        return wm_keyboard_state_t_m;
    case XK_U:
    case XK_u:
        return wm_keyboard_state_u_m;
    case XK_V:
    case XK_v:
        return wm_keyboard_state_v_m;
    case XK_W:
    case XK_w:
        return wm_keyboard_state_w_m;
    case XK_X:
    case XK_x:
        return wm_keyboard_state_x_m;
    case XK_Y:
    case XK_y:
        return wm_keyboard_state_y_m;
    case XK_Z:
    case XK_z:
        return wm_keyboard_state_z_m;
    case XK_F1:
        return wm_keyboard_state_f1_m;
    case XK_F2:
        return wm_keyboard_state_f2_m;
    case XK_F3:
        return wm_keyboard_state_f3_m;
    case XK_F4:
        return wm_keyboard_state_f4_m;
    case XK_F5:
        return wm_keyboard_state_f5_m;
    case XK_F6:
        return wm_keyboard_state_f6_m;
    case XK_F7:
        return wm_keyboard_state_f7_m;
    case XK_F8:
        return wm_keyboard_state_f8_m;
    case XK_F9:
        return wm_keyboard_state_f9_m;
    case XK_F10:
        return wm_keyboard_state_f10_m;
    case XK_F11:
        return wm_keyboard_state_f11_m;
    case XK_F12:
        return wm_keyboard_state_f12_m;
    case XK_Caps_Lock:
        return wm_keyboard_state_capslock_m;
    case XK_Delete:
        return wm_keyboard_state_del_m;
    default:
        return wm_keyboard_state_count_m;
    }
#endif
}

// TODO factor in flags (shift, capslock, ...)
static uint32_t wm_input_keycode_to_character ( uint32_t keycode ) {
    switch ( keycode ) {
    case wm_keyboard_state_a_m:
        return 'a';
    case wm_keyboard_state_b_m:
        return 'b';
    case wm_keyboard_state_c_m:
        return 'c';
    case wm_keyboard_state_d_m:
        return 'd';
    case wm_keyboard_state_e_m:
        return 'e';
    case wm_keyboard_state_f_m:
        return 'f';
    case wm_keyboard_state_g_m:
        return 'g';
    case wm_keyboard_state_h_m:
        return 'h';
    case wm_keyboard_state_i_m:
        return 'i';
    case wm_keyboard_state_j_m:
        return 'j';
    case wm_keyboard_state_k_m:
        return 'k';
    case wm_keyboard_state_l_m:
        return 'l';
    case wm_keyboard_state_m_m:
        return 'm';
    case wm_keyboard_state_n_m:
        return 'n';
    case wm_keyboard_state_o_m:
        return 'o';
    case wm_keyboard_state_p_m:
        return 'p';
    case wm_keyboard_state_q_m:
        return 'q';
    case wm_keyboard_state_r_m:
        return 'r';
    case wm_keyboard_state_s_m:
        return 's';
    case wm_keyboard_state_t_m:
        return 't';
    case wm_keyboard_state_u_m:
        return 'u';
    case wm_keyboard_state_v_m:
        return 'v';
    case wm_keyboard_state_w_m:
        return 'w';
    case wm_keyboard_state_x_m:
        return 'x';
    case wm_keyboard_state_y_m:
        return 'y';
    case wm_keyboard_state_z_m:
        return 'z';
    case wm_keyboard_state_0_m:
        return '0';
    case wm_keyboard_state_1_m:
        return '1';
    case wm_keyboard_state_2_m:
        return '2';
    case wm_keyboard_state_3_m:
        return '3';
    case wm_keyboard_state_4_m:
        return '4';
    case wm_keyboard_state_5_m:
        return '5';
    case wm_keyboard_state_6_m:
        return '6';
    case wm_keyboard_state_7_m:
        return '7';
    case wm_keyboard_state_8_m:
        return '8';
    case wm_keyboard_state_9_m:
        return '9';
    case wm_keyboard_state_space_m:
        return ' ';
    case wm_keyboard_state_tab_m:
        return '\t';
    case wm_keyboard_state_period_m:
        return '.';
    case wm_keyboard_state_plus_m:
        return '+';
    case wm_keyboard_state_minus_m:
        return '-';
    default:
        return '\0';
    }
}

std_unused_static_m()
static void wm_window_update_modifier_keydown ( wm_window_t* window, uint32_t keycode ) {
    switch ( keycode ) {
    case wm_keyboard_state_shift_left_m:
        window->input_flags |= wm_input_flag_bit_shift_left_m;
        return;
    case wm_keyboard_state_shift_right_m:
        window->input_flags |= wm_input_flag_bit_shift_right_m;
        return;
    case wm_keyboard_state_alt_left_m:
        window->input_flags |= wm_input_flag_bit_alt_left_m;
        return;
    case wm_keyboard_state_alt_right_m:
        window->input_flags |= wm_input_flag_bit_alt_right_m;
        return;
    case wm_keyboard_state_ctrl_left_m:
        window->input_flags |= wm_input_flag_bit_ctrl_left_m;
        return;
    case wm_keyboard_state_ctrl_right_m:
        window->input_flags |= wm_input_flag_bit_ctrl_right_m;
        return;
    case wm_keyboard_state_capslock_m:
        window->input_flags |= wm_input_flag_bit_capslock_m;
        return;
    }
}

std_unused_static_m()
static void wm_window_update_modifier_keyup ( wm_window_t* window, uint32_t keycode ) {
    switch ( keycode ) {
    case wm_keyboard_state_shift_left_m:
        window->input_flags &= ~wm_input_flag_bit_shift_left_m;
        return;
    case wm_keyboard_state_shift_right_m:
        window->input_flags &= ~wm_input_flag_bit_shift_right_m;
        return;
    case wm_keyboard_state_alt_left_m:
        window->input_flags &= ~wm_input_flag_bit_alt_left_m;
        return;
    case wm_keyboard_state_alt_right_m:
        window->input_flags &= ~wm_input_flag_bit_alt_right_m;
        return;
    case wm_keyboard_state_ctrl_left_m:
        window->input_flags &= ~wm_input_flag_bit_ctrl_left_m;
        return;
    case wm_keyboard_state_ctrl_right_m:
        window->input_flags &= ~wm_input_flag_bit_ctrl_right_m;
        return;
    case wm_keyboard_state_capslock_m:
        window->input_flags &= ~wm_input_flag_bit_capslock_m;
        return;
    }
}

#if defined ( std_platform_win32_m )
// https://stackoverflow.com/questions/15966642/how-do-you-tell-lshift-apart-from-rshift-in-wm-keydown-events
static WPARAM wm_window_map_left_right_key ( WPARAM vk, LPARAM lParam )
{
    WPARAM new_vk = vk;
    UINT scancode = (lParam & 0x00ff0000) >> 16;
    int extended  = (lParam & 0x01000000) != 0;

    switch (vk) {
    case VK_SHIFT:
        new_vk = MapVirtualKey(scancode, MAPVK_VSC_TO_VK_EX);
        break;
    case VK_CONTROL:
        new_vk = extended ? VK_RCONTROL : VK_LCONTROL;
        break;
    case VK_MENU:
        new_vk = extended ? VK_RMENU : VK_LMENU;
        break;
    default:
        // not a key we map from generic to left/right specialized
        //  just return it.
        new_vk = vk;
        break;    
    }

    return new_vk;
}
#endif

#if wm_enable_input_events_m
#if defined(std_platform_win32_m)
static bool wm_process_input_event ( wm_window_h handle, wm_window_t* window, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, int64_t* retcode ) {
    // Process msg
    wm_input_event_e event;
    uint64_t args[4] = { 0, 0, 0, 0 };

    // TODO
    //      test for std_enabled_m(wm_input_buffer_m)
    //      if enabled store all event input received during this frame in a buffer
    //      allow user to read from it (makes implementing a text box possible)

    // TODO cleanup this, replace old even scheme with new one, allow for event callback update, ...

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
        wm_input_event_t* buffer_event;
        uint64_t keycode;

        switch ( msg ) {
            case WM_KEYDOWN:
                // TODO standardize keydown/up flags, don't just pass lparam
                event = wm_event_key_down_m;
                wparam = wm_window_map_left_right_key ( wparam, lparam );
                args[0] = ( uint64_t ) ( wparam );
                args[1] = ( uint64_t ) lparam; // flags - todo use wm_input_key_modifier_f
                on_handle = 0;
                on_pass = 1;

                // TODO check bounds
                keycode = wm_window_keycode ( wparam );
                wm_window_update_modifier_keydown ( window, keycode );
                if ( keycode != wm_keyboard_state_count_m ) {
                    buffer_event = &window->input_buffer.events[window->input_buffer.count++];
                    buffer_event->type = event;
                    buffer_event->args.keyboard.keycode = keycode;
                    buffer_event->args.keyboard.character = wm_input_keycode_to_character ( keycode );
                    buffer_event->args.keyboard.flags = window->input_flags;
                }
                break;

            case WM_KEYUP:
                event = wm_event_key_up_m;
                wparam = wm_window_map_left_right_key ( wparam, lparam );
                args[0] = ( uint64_t ) wparam; // key
                args[1] = ( uint64_t ) lparam;  // flags
                on_handle = 0;
                on_pass = 1;

                keycode = wm_window_keycode ( wparam );
                wm_window_update_modifier_keyup ( window, keycode );
                break;

            case WM_LBUTTONDOWN:
                event = wm_event_mouse_down_m;
                args[0] = wm_mouse_state_left_m;
                args[1] = ( uint64_t ) GET_X_LPARAM ( lparam );
                args[2] = ( uint64_t ) GET_Y_LPARAM ( lparam );
                args[3] = wparam;   // TODO
                on_handle = 0;
                on_pass = 1;

                buffer_event = &window->input_buffer.events[window->input_buffer.count++];
                buffer_event->type = event;
                buffer_event->args.mouse.button = wm_mouse_state_left_m;
                buffer_event->args.mouse.x = GET_X_LPARAM ( lparam );
                buffer_event->args.mouse.y = GET_Y_LPARAM ( lparam );
                buffer_event->args.mouse.flags = wparam;
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

                buffer_event = &window->input_buffer.events[window->input_buffer.count++];
                buffer_event->type = event;
                buffer_event->args.mouse.button = wm_mouse_state_right_m;
                buffer_event->args.mouse.x = GET_X_LPARAM ( lparam );
                buffer_event->args.mouse.y = GET_Y_LPARAM ( lparam );
                buffer_event->args.mouse.flags = wparam;
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

                buffer_event = &window->input_buffer.events[window->input_buffer.count++];
                buffer_event->type = event;
                buffer_event->args.mouse.button = GET_WHEEL_DELTA_WPARAM ( wparam ) > 0 ? wm_mouse_state_wheel_up_m : wm_mouse_state_wheel_down_m;
                buffer_event->args.mouse.x = GET_X_LPARAM ( lparam );
                buffer_event->args.mouse.y = GET_Y_LPARAM ( lparam );
                buffer_event->args.mouse.flags = wparam;

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
    wm_input_event_e event;
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
        int64_t on_handle;
        int64_t on_pass;
        wm_input_event_t* buffer_event;
        uint64_t keycode;
        uint64_t button = wm_mouse_state_count_m;
        if ( x_event->type == ButtonPress || x_event->type == ButtonRelease ) {
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

        // TODO flags and callback args
#if 0
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
#endif

        switch ( x_event->type ) {
            case MotionNotify:
                window->cursor_x = x_event->xmotion.x;
                window->cursor_y = x_event->xmotion.y;
                break;

            case Expose:
                if ( x_event->xexpose.count == 0 ) {
                    XClearWindow ( ( Display* ) window->info.os_handle.root, ( Window ) window->info.os_handle.window );
                }

                break;

            case KeyPress:
                event = wm_event_key_down_m;
                args[0] = ( uint64_t ) x_event->xkey.keycode;
                args[1] = 0;

                keycode = wm_window_keycode ( XLookupKeysym ( &x_event->xkey, 0 ) );
                wm_window_update_modifier_keydown ( window, keycode );
                if ( keycode!= wm_keyboard_state_count_m ) {
                    buffer_event = &window->input_buffer.events[window->input_buffer.count++];
                    buffer_event->type = event;
                    buffer_event->args.keyboard.keycode = keycode;
                    buffer_event->args.keyboard.character = wm_input_keycode_to_character ( keycode );
                    buffer_event->args.keyboard.flags = window->input_flags;
                }
                on_handle = 0;
                on_pass = 1;
                break;

            case KeyRelease:
                event = wm_event_key_up_m;
                args[0] = ( uint64_t ) x_event->xkey.keycode;
                args[1] = 0;

                keycode = wm_window_keycode ( XLookupKeysym ( &x_event->xkey, 0 ) );
                wm_window_update_modifier_keyup ( window, keycode );
                break;

            case ButtonPress:
                event = wm_event_mouse_down_m;
                args[0] = button;
                args[1] = x_event->xbutton.x;
                args[2] = x_event->xbutton.y;
                args[3] = 0;
                on_handle = 0;
                on_pass = 1;

                buffer_event = &window->input_buffer.events[window->input_buffer.count++];
                buffer_event->type = event;
                buffer_event->args.mouse.button = button;
                buffer_event->args.mouse.x = x_event->xbutton.x;
                buffer_event->args.mouse.y = x_event->xbutton.y;
                buffer_event->args.mouse.flags = 0;
                break;

            case ButtonRelease:
                event = wm_event_mouse_up_m;
                args[0] = button;
                args[1] = x_event->xbutton.x;
                args[2] = x_event->xbutton.y;
                args[3] = 0;
                on_handle = 0;
                on_pass = 1;
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
#endif // wm_enable_input_events_m

#if wm_enable_input_state_m
// TODO move this to wm_test?
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
        "TAB:"std_fmt_u8_m " LSHIFT:"std_fmt_u8_m" LCTRL:"std_fmt_u8_m" LALT:"std_fmt_u8_m" SPACE:"std_fmt_u8_m" RALT:"std_fmt_u8_m" RCTRL:"std_fmt_u8_m" RSHIFT:"std_fmt_u8_m" ENTER:"std_fmt_u8_m" BACKSPACE:"std_fmt_u8_m" DEL:"std_fmt_u8_m,
        kb[wm_keyboard_state_tab_m], kb[wm_keyboard_state_shift_left_m], kb[wm_keyboard_state_ctrl_left_m], kb[wm_keyboard_state_alt_left_m], kb[wm_keyboard_state_space_m],
        kb[wm_keyboard_state_alt_right_m], kb[wm_keyboard_state_ctrl_right_m], kb[wm_keyboard_state_shift_right_m], kb[wm_keyboard_state_enter_m], kb[wm_keyboard_state_backspace_m], kb[wm_keyboard_state_del_m] );
    char line7[1024];
    std_str_format ( line7, sizeof ( line7 ),
        "UP:" std_fmt_u8_m " LEFT:" std_fmt_u8_m " DOWN:" std_fmt_u8_m " RIGHT:" std_fmt_u8_m " X:" std_fmt_u32_pad_m ( 4 ) " Y:" std_fmt_u32_pad_m ( 4 ) " LB:" std_fmt_u8_m " RB:" std_fmt_u8_m " WU:" std_fmt_u8_m " WD:" std_fmt_u8_m,
        kb[wm_keyboard_state_up_m], kb[wm_keyboard_state_left_m], kb[wm_keyboard_state_down_m], kb[wm_keyboard_state_right_m], window->input_state.cursor_x, window->input_state.cursor_y, mouse[wm_mouse_state_left_m], mouse[wm_mouse_state_right_m], mouse[wm_mouse_state_wheel_up_m], mouse[wm_mouse_state_wheel_down_m] );

    const char* prefix = "";

    if ( overwrite_console ) {
        prefix = std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m std_fmt_prevline_m;
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

    // TODO linux
    // TODO remove event parsing entirely?
    window->info.is_focus = GetForegroundWindow() == ( HWND ) window->info.os_handle.window;

    // Fetch system info before locking
    BOOL result;
    POINT cursor;

    GetCursorPos ( &cursor );

    uint8_t keyboard[256];
    GetKeyState ( 0 );
    // TODO this seems to be laggy, replace with local persistent cache updated when reading the event queue?
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
            window->input_state.keyboard[wm_keyboard_state_del_m] = keyboard[0x2E] >> 7;

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

            code = XKeysymToKeycode ( x_display, XK_Delete );
            byte_idx = code / 8;
            shift = code % 8;
            window->input_state.keyboard[wm_keyboard_state_del_m] = ( keyboard[byte_idx] & ( 1 << shift ) ) != 0;

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
#endif // wm_enable_input_state_m

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
#if wm_enable_input_events_m
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
#if wm_enable_input_events_m

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

#if wm_enable_input_state_m
    window->pending_wheel_up = 0;
    window->pending_wheel_down = 0;
    wm_clear_input_state ( handle );
#endif

    window->input_buffer.count = 0;

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
#if wm_enable_input_state_m
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
    bool is_focus;
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
            std_log_os_error_m();
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
        is_focus = params->gain_focus;
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
        is_focus = params->gain_focus;
    }
#endif // defined(std_platform_linux_m)

    // Lock
    std_mutex_lock ( &wm_window_state->mutex );
    // Pop from freelist and write all data
    wm_window_t* window = std_list_pop_m ( &wm_window_state->windows_freelist );
    uint64_t window_idx = window - wm_window_state->windows_array;
    std_bitset_set ( wm_window_state->windows_bitset, window_idx );

#if wm_enable_input_events_m
    window->handlers_count = 0;
#endif

#if wm_enable_input_state_m
    std_mem_zero ( &window->input_state, sizeof ( window->input_state ) );
#endif

    window->params = *params;
    std_str_copy ( window->info.title, wm_window_title_max_len_m, params->name );
    window->info.x = params->x;
    window->info.y = params->y;
    window->info.width = params->width;
    window->info.height = params->height;
    window->info.os_handle = os_handle;
    window->info.is_focus = is_focus;
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
    if ( !DestroyWindow ( ( HWND ) window->info.os_handle.window ) ) {
        std_log_os_error_m();
    }
    HINSTANCE instance = GetModuleHandle ( NULL );
    if ( !UnregisterClassA ( window->params.name, instance ) ) {
        std_log_os_error_m();
    }
    uint64_t hash = std_hash_64_m ( ( uint64_t ) window->info.os_handle.window );
    bool remove_result = std_hash_map_remove_hash ( &wm_window_state->map, hash );
    std_verify_m ( remove_result );
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
    uint64_t window_idx = window - wm_window_state->windows_array;
    std_bitset_clear ( wm_window_state->windows_bitset, window_idx );

    // Unlock and return true
    std_mutex_unlock ( &wm_window_state->mutex );
    return true;
}

#if wm_enable_input_events_m
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

void wm_window_input_buffer_get ( wm_window_h handle, wm_input_buffer_t* buffer ) {
    wm_window_t* window = &wm_window_state->windows_array[wm_handle_idx_m ( handle )];
    *buffer = window->input_buffer;
}
