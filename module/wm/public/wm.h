#pragma once

#include <std_module.h>
#include <std_allocator.h>
#include <std_compiler.h>

#define wm_module_name_m wm
std_module_export_m void* wm_load ( void* );
std_module_export_m void wm_unload ( void );
std_module_export_m void wm_reload ( void*, void* );

typedef uint64_t wm_window_h;
typedef uint64_t wm_manager_h;
//typedef uint64_t wm_window_os_h;
typedef uint64_t wm_display_h;

#define wm_null_handle_m UINT64_MAX

typedef struct {
    union {
        struct {
            uint64_t root;
            uint64_t window;
        };
        struct {
            uint64_t hinstance;
            uint64_t hwnd;
        } win32;
        struct {
            void* display;
            uint64_t window;
        } x11;
    };
} wm_window_os_h;

#define wm_window_title_max_len_m 128
typedef struct {
    wm_window_os_h os_handle;
    int64_t x;  // Windows OS: x y coordinates are relative to primary monitor top left, so they can be negative
    int64_t y;
    size_t width;
    size_t height;
    size_t border_top;
    size_t border_bottom;
    size_t border_left;
    size_t border_right;
    size_t borders_width; // TODO move borders width/height out of here?
    size_t borders_height;
    bool is_focus;
    bool is_minimized;
    char title[wm_window_title_max_len_m];
} wm_window_info_t;

#if wm_enable_input_events_m || wm_enable_input_state_m

typedef enum {
    wm_keyboard_state_a_m,
    wm_keyboard_state_b_m,
    wm_keyboard_state_c_m,
    wm_keyboard_state_d_m,
    wm_keyboard_state_e_m,
    wm_keyboard_state_f_m,
    wm_keyboard_state_g_m,
    wm_keyboard_state_h_m,
    wm_keyboard_state_i_m,
    wm_keyboard_state_j_m,
    wm_keyboard_state_k_m,
    wm_keyboard_state_l_m,
    wm_keyboard_state_m_m,
    wm_keyboard_state_n_m,
    wm_keyboard_state_o_m,
    wm_keyboard_state_p_m,
    wm_keyboard_state_q_m,
    wm_keyboard_state_r_m,
    wm_keyboard_state_s_m,
    wm_keyboard_state_t_m,
    wm_keyboard_state_u_m,
    wm_keyboard_state_v_m,
    wm_keyboard_state_w_m,
    wm_keyboard_state_x_m,
    wm_keyboard_state_y_m,
    wm_keyboard_state_z_m,
    wm_keyboard_state_0_m,
    wm_keyboard_state_1_m,
    wm_keyboard_state_2_m,
    wm_keyboard_state_3_m,
    wm_keyboard_state_4_m,
    wm_keyboard_state_5_m,
    wm_keyboard_state_6_m,
    wm_keyboard_state_7_m,
    wm_keyboard_state_8_m,
    wm_keyboard_state_9_m,
    wm_keyboard_state_period_m, // TODO state
    wm_keyboard_state_plus_m, // TODO state
    wm_keyboard_state_minus_m, // TODO state
    wm_keyboard_state_space_m,
    wm_keyboard_state_tab_m,
    wm_keyboard_state_enter_m,
    wm_keyboard_state_backspace_m,
    wm_keyboard_state_f1_m,
    wm_keyboard_state_f2_m,
    wm_keyboard_state_f3_m,
    wm_keyboard_state_f4_m,
    wm_keyboard_state_f5_m,
    wm_keyboard_state_f6_m,
    wm_keyboard_state_f7_m,
    wm_keyboard_state_f8_m,
    wm_keyboard_state_f9_m,
    wm_keyboard_state_f10_m,
    wm_keyboard_state_f11_m,
    wm_keyboard_state_f12_m,
    wm_keyboard_state_esc_m,
    wm_keyboard_state_shift_left_m,
    wm_keyboard_state_shift_right_m,
    wm_keyboard_state_ctrl_left_m,
    wm_keyboard_state_ctrl_right_m,
    wm_keyboard_state_alt_left_m,
    wm_keyboard_state_alt_right_m,
    wm_keyboard_state_up_m,
    wm_keyboard_state_left_m,
    wm_keyboard_state_down_m,
    wm_keyboard_state_right_m,
    wm_keyboard_state_capslock_m,

    wm_keyboard_state_count_m
} wm_keyboard_state_e;

typedef enum {
    wm_mouse_state_left_m,
    wm_mouse_state_wheel_m,
    wm_mouse_state_right_m,
    wm_mouse_state_wheel_down_m,
    wm_mouse_state_wheel_up_m,

    wm_mouse_state_count_m
} wm_mouse_state_e;
#endif

#if wm_enable_input_events_m

typedef enum {
    // args: key | flags | null | null
    wm_event_key_down_m       = 1 << 0,
    wm_event_key_up_m         = 1 << 1,

    // args: button | cursor x | cursor y | flags
    wm_event_mouse_down_m     = 1 << 2,
    wm_event_mouse_up_m       = 1 << 3,
    wm_event_mouse_wheel_m    = 1 << 4,

    // args: new width | new height | old width | old height
    wm_event_resize_m         = 1 << 5,

    // args: new x | new y | new width | new height
    wm_event_maximize_m       = 1 << 6,
    wm_event_restore_m        = 1 << 7,

    // args: null | null | null | null
    wm_event_minimize_m       = 1 << 8,

    // args: new x | new y | old x | old y
    wm_event_move_m           = 1 << 9,

    // args: null | null | null | null
    wm_event_close_m          = 1 << 10,
    wm_event_destroy_m        = 1 << 11,
    wm_event_create_m         = 1 << 12,
    wm_event_gain_focus_m     = 1 << 13,
    wm_event_lose_focus_m     = 1 << 14,

    wm_event_all_m            = 0xffff,
} wm_input_event_e;

typedef enum {
    wm_input_flag_bit_shift_left_m = 1 << 0,
    wm_input_flag_bit_shift_right_m = 1 << 1,
    wm_input_flag_bit_alt_left_m = 1 << 2,
    wm_input_flag_bit_alt_right_m = 1 << 3,
    wm_input_flag_bit_ctrl_left_m = 1 << 4,
    wm_input_flag_bit_ctrl_right_m = 1 << 5,
    wm_input_flag_bit_capslock_m = 1 << 6,

    wm_input_flag_bits_shift_m = ( wm_input_flag_bit_shift_left_m | wm_input_flag_bit_shift_right_m ),
    wm_input_flag_bits_alt_m = ( wm_input_flag_bit_shift_left_m | wm_input_flag_bit_shift_right_m ),
    wm_input_flag_bits_ctrl_m = ( wm_input_flag_bit_shift_left_m | wm_input_flag_bit_shift_right_m ),
    wm_input_flag_bits_uppercase_m = ( wm_input_flag_bit_shift_left_m | wm_input_flag_bit_shift_right_m | wm_input_flag_bit_capslock_m ),
} wm_input_flags_bit_e;

typedef struct {
    uint32_t keycode;
    uint32_t flags; // wm_input_flags_bit_e
    uint32_t character;
} wm_keyboard_event_args_t;

typedef struct {
    uint32_t button;
    int32_t x;
    int32_t y;
    uint32_t flags; // wm_input_flags_bit_e
} wm_mouse_event_args_t;

typedef struct {
    uint32_t new_width;
    uint32_t new_height;
    uint32_t prev_width;
    uint32_t prev_height;
} wm_window_resize_event_args_t;

typedef struct {
    int32_t new_x;
    int32_t new_y;
    int32_t prev_x;
    int32_t prev_y;
} wm_window_move_event_args_t;

typedef struct {
    union {
        wm_keyboard_event_args_t keyboard;
        wm_mouse_event_args_t mouse;
        wm_window_move_event_args_t window_move;
        wm_window_resize_event_args_t window_resize;
    };
} wm_input_event_args_t;

typedef struct {
    wm_input_event_e type;
    wm_input_event_args_t args;
} wm_input_event_t;

typedef struct {
    wm_input_event_t events[wm_input_buffer_max_events_m];
    uint32_t count;
} wm_input_buffer_t;

// Return true to trap the message, false to feed it to the system default handler
typedef bool ( wm_input_event_handler_f ) ( wm_window_h window, wm_input_event_e event_id,
    uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4 );
typedef struct {
    wm_input_event_e event_mask;
    wm_input_event_handler_f* callback;
} wm_input_event_handler_t;

#endif // wm_enable_input_events_m

#if wm_enable_input_state_m
#define wm_keyboard_state_size_m wm_keyboard_state_count_m
#define wm_mouse_state_size_m wm_mouse_state_count_m
typedef struct {
    uint32_t global_cursor_x;
    uint32_t global_cursor_y;
    uint32_t cursor_x;
    uint32_t cursor_y;
    char mouse[wm_mouse_state_size_m]; // access with [] by wm_mouse_state_e values
    char keyboard[wm_keyboard_state_size_m]; // access with [] by wm_keyboard_state_e values
} wm_input_state_t;
#endif // wm_enable_input_state_m

typedef struct {
    size_t width;
    size_t height;
    size_t refresh_rate;
    size_t bits_per_pixel;
    //size_t id; win32 API recommends passing the same DEVMODE struct that gets returned when querying EnumDisplaySettings, so storing its idx would be more correct, but for current usage it's not required.
} wm_display_mode_t;

typedef struct {
    char adapter_id[32];
    char adapter_name[256];
    char display_id[32];
    char display_name[256];
    bool is_primary;
    wm_display_mode_t mode;
} wm_display_info_t;

typedef enum {
    wm_window_creation_bit_gain_focus_m = 1 << 0,
    wm_window_creation_bit_borderless_m = 1 << 1,
} wm_window_creation_b;

typedef struct {
    char name[wm_window_name_size];
    int64_t x;
    int64_t y;
    size_t width;
    size_t height;
    bool gain_focus;
    bool borderless;
    //uint64_t os_module;
} wm_window_params_t;

#define wm_window_params_m( ... ) { \
    .name = "", \
    .x = 0, \
    .y = 0, \
    .width = 400, \
    .height = 300, \
    .gain_focus = true, \
    .borderless = false, \
    ##__VA_ARGS__ \
}

typedef struct {
    void ( *print ) ( void );

    wm_window_h ( *create_window ) ( const wm_window_params_t* params );
    bool ( *destroy_window ) ( wm_window_h window );

    bool ( *is_window_alive ) ( wm_window_h window );
    bool ( *get_window_info ) ( wm_window_h window, wm_window_info_t* info );
    void ( *update_window ) ( const wm_window_h window );

#if wm_enable_input_events_m
    bool ( *add_window_event_handler ) ( wm_window_h window, const wm_input_event_handler_t* handler );
    bool ( *remove_window_event_handler ) ( wm_window_h window, const wm_input_event_handler_t* handler );
#endif

#if wm_enable_input_state_m
    bool ( *get_window_input_state ) ( wm_window_h window, wm_input_state_t* state );
    void ( *debug_print_window_input_state ) ( wm_window_h window, bool overwrite_console );
#endif

    void ( *get_window_input_buffer ) ( wm_window_h window, wm_input_buffer_t* buffer );

    size_t ( *get_displays_count ) ( void );
    size_t ( *get_displays ) ( wm_display_h* displays, size_t cap );
    bool   ( *get_display_info ) ( wm_display_info_t* info, wm_display_h display );
    size_t ( *get_display_modes_count ) ( wm_display_h display );
    bool   ( *get_display_modes ) ( wm_display_mode_t* modes, size_t cap, wm_display_h display );
} wm_i;
