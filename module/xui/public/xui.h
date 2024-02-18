#pragma once

#include <std_module.h>

#include <xg.h>
#include <xs.h>

#define xui_module_name_m xui
std_module_export_m void* xui_load ( void* );
std_module_export_m void xui_reload ( void*, void* );
std_module_export_m void xui_unload ( void );

#define xui_null_handle_m UINT64_MAX

typedef uint64_t xui_id_t;
typedef uint64_t xui_font_h;
typedef uint64_t xui_workload_h;

// Color

typedef struct {
    union {
        struct {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            uint8_t a;
        };
        uint32_t u32;
    };
} xui_color_t;

//#define xui_color_rgba_u32_m( r, g, b, a ) ( xui_color_t ) { .u32 = ( r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ) ) }
#define xui_color_rgba_u32_m( r, g, b, a ) ( xui_color_t ) { .u32 = ( (uint32_t) r | ( (uint32_t) g << 8 ) | ( (uint32_t) b << 16 ) | ( (uint32_t) a << 24 ) ) }

#define xui_color_rgb_mul_m( color, scalar )  ( xui_color_t ) xui_color_rgba_u32_m ( (color.r * scalar), (color.g * scalar), (color.b * scalar), color.a )
#define xui_color_rgba_mul_m( color, scalar ) ( xui_color_t ) xui_color_rgba_u32_m ( (color.r * scalar), (color.g * scalar), (color.b * scalar), (color.a * scalar) )

#define xui_color_red_m         xui_color_rgba_u32_m ( 176,  40,  48, 255 )
#define xui_color_orange_m      xui_color_rgba_u32_m ( 222, 122,  34, 255 )
#define xui_color_yellow_m      xui_color_rgba_u32_m ( 240, 201,   0, 255 )
#define xui_color_cyan_m        xui_color_rgba_u32_m (   0, 136, 166, 255 )
#define xui_color_blue_m        xui_color_rgba_u32_m (  28,  56, 146, 255 )
#define xui_color_green_m       xui_color_rgba_u32_m (  67, 149,  66, 255 )
#define xui_color_black_m       xui_color_rgba_u32_m (  40,  39,  40, 255 )
#define xui_color_white_m       xui_color_rgba_u32_m ( 248, 248, 242, 255 )
#define xui_color_gray_m        xui_color_rgba_u32_m ( 118, 120, 119, 255 )
#define xui_color_dark_gray_m   xui_color_rgba_u32_m (  79,  80,  81, 255 )
#define xui_color_invalid_m     xui_color_rgba_u32_m (   0,   0,   0,   0 )

// Style

typedef enum {
    xui_horizontal_alignment_left_to_right_m,
    xui_horizontal_alignment_right_to_left_m,
    xui_horizontal_alignment_centered_m, // when using this only one element can fit per line
    xui_horizontal_alignment_invalid_m,
} xui_horizontal_alignment_e;

typedef struct {
    xui_font_h font;
    uint32_t font_height;
    xui_color_t color;
    xui_color_t font_color;
    xui_horizontal_alignment_e horizontal_alignment;
} xui_style_t;

#define xui_style_m( ... ) ( xui_style_t ) { \
    .font = xui_null_handle_m, \
    .font_height = 14, \
    .color = xui_color_black_m, \
    .font_color = xui_color_white_m, \
    .horizontal_alignment = xui_horizontal_alignment_left_to_right_m, \
    ##__VA_ARGS__ \
}

#define xui_null_style_m ( xui_style_t ) { \
    .font = xui_null_handle_m, \
    .font_height = 0, \
    .color = xui_color_invalid_m, \
    .font_color = xui_color_invalid_m, \
    .horizontal_alignment = xui_horizontal_alignment_invalid_m, \
}

#define xui_default_style_m xui_style_m()

// Elements

typedef struct {
    char title[xui_window_title_size];
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t padding_x;
    uint32_t padding_y;
    bool minimized;
    bool resizable;
    bool movable;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_window_state_t;

#define xui_window_state_m( ... ) ( xui_window_state_t ) { \
    .title = "", \
    .x = 0, \
    .y = 0, \
    .width = 0, \
    .height = 0, \
    .padding_x = 10, \
    .padding_y = 5, \
    .minimized = false, \
    .resizable = true, \
    .movable = true, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_style_m(), \
    ##__VA_ARGS__ \
}

#define xui_default_window_state_m xui_window_state_m()

typedef struct {
    bool minimized;
    char title[xui_section_title_size];
    xui_font_h font;
    uint32_t height;
    uint32_t padding_x;
    uint32_t padding_y;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_section_state_t;

#define xui_section_state_m( ... ) ( xui_section_state_t ) { \
    .minimized = false, \
    .title = "", \
    .font = xui_null_handle_m, \
    .height = 0, \
    .padding_x = 0, \
    .padding_y = 0, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_null_style_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t width;
    uint32_t height;
    char text[xui_button_text_size];
    bool down;
    bool pressed;
    bool disabled;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_button_state_t;

#define xui_button_state_m( ... ) ( xui_button_state_t ) { \
    .width = 0, \
    .height = 0, \
    .text = "", \
    .down = false, \
    .pressed = false, \
    .disabled = false, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_null_style_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t thickness;
    uint32_t length;
    uint32_t width;
    uint32_t height;
    float value;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_slider_state_t;

#define xui_slider_state_m( ... ) ( xui_slider_state_t ) { \
    .thickness = 0, \
    .length = 0, \
    .width = 0, \
    .height = 0, \
    .value = 0, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_null_style_m, \
    ##__VA_ARGS__ \
}

#if 0
typedef struct {
    char* text; // must be heap allocated for reload to work
    uint32_t text_capacity;
    uint32_t width;
    uint32_t height;
    // TODO remove these?
    //uint32_t text_height;
    //uint32_t max_len;
    xui_id_t id;
    uint64_t sort_order;
} xui_text_state_t;

#define xui_default_text_state_m ( xui_text_state_t ) { \
    .text = NULL, \
    .text_capacity = 0, \
    .width = 0, \
    .height = 0, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
}
#endif

typedef struct {
    char text[xui_label_text_size];
    xui_font_h font;
    uint32_t height;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_label_state_t;

#define xui_label_state_m( ... ) ( xui_label_state_t ) { \
    .text = "", \
    .font = xui_null_handle_m, \
    .height = 0, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_null_style_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    const char** items; // must be heap allocated for reload to work
    uint32_t item_count;
    uint32_t item_idx;
    xui_font_h font;
    uint32_t width;
    uint32_t height;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_select_state_t;

#define xui_select_state_m( ... ) ( xui_select_state_t ) { \
    .items = NULL, \
    .item_count = 0, \
    .item_idx = 0, \
    .font = xui_null_handle_m, \
    .width = 0, \
    .height = 0, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_null_style_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t width;
    uint32_t height;
    bool value;
    xui_id_t id;
    uint64_t sort_order;
    xui_style_t style;
} xui_switch_state_t;

#define xui_switch_state_m( ... ) ( xui_switch_state_t ) { \
    .width = 0, \
    .height = 0, \
    .value = false, \
    .id = xui_line_id_m(), \
    .sort_order = 0, \
    .style = xui_null_style_m, \
    ##__VA_ARGS__ \
}

#define xui_line_id_m() ( xui_id_t ) ( std_hash_64_m ( std_file_name_hash_m + std_line_num_m ) )

// Font

typedef struct {
    xg_device_h xg_device;
    // TODO support dynamic viewport in xg, then use these
    //uint64_t atlas_width;
    //uint64_t atlas_height;
    uint64_t pixel_height;
    uint32_t first_char_code;
    uint32_t char_count;
    bool outline;
} xui_font_params_t;

#define xui_font_char_ascii_base_m 32
#define xui_font_char_ascii_count_m 94

#define xui_font_params_m( ... ) ( xui_font_params_t ) { \
    .xg_device = xg_null_handle_m, \
    .pixel_height = 0, \
    .first_char_code = xui_font_char_ascii_base_m, \
    .char_count = xui_font_char_ascii_count_m, \
    .outline = false, \
    ##__VA_ARGS__ \
}

// Rendering

typedef struct {
    uint64_t width;
    uint64_t height;
} xui_viewport_t;

typedef struct {
    xg_device_h device;
    xg_workload_h workload;
    xg_cmd_buffer_h cmd_buffer;
    uint64_t key;

    xg_format_e render_target_format;
    xui_viewport_t viewport;
} xui_flush_params_t;

// Api

typedef struct {
    void ( *register_shaders ) ( xs_i* xs );
    void ( *activate_device ) ( xg_device_h device );
    void ( *deactivate_device ) ( xg_device_h device );

    void ( *begin_update ) ( const wm_window_info_t* window_info, const wm_input_state_t* input_state );

    uint64_t ( *get_active_element_id ) ( void );

    xui_font_h ( *create_font ) ( std_buffer_t ttf_data, const xui_font_params_t* params );
    void ( *destroy_font ) ( xui_font_h font );

    xui_workload_h ( *create_workload ) ( void );
    void ( *flush_workload ) ( xui_workload_h workload, const xui_flush_params_t* params );
    // Add api to get oldest non-flushed workload? can avoid having to keep track of created workloads on client side
    // Auto flush all pending workloads on end_frame/end_update?

    void ( *end_update ) ( void );

    void ( *newline ) ( void );

    void ( *set_style )     ( xui_style_t* style );

    void ( *begin_window )  ( xui_workload_h workload, xui_window_state_t* state );
    void ( *end_window )    ( xui_workload_h workload );

    void ( *begin_section ) ( xui_workload_h workload, xui_section_state_t* state );
    void ( *end_section )   ( xui_workload_h workload );

    void ( *add_button )    ( xui_workload_h workload, xui_button_state_t* state );
    void ( *add_slider )    ( xui_workload_h workload, xui_slider_state_t* state );
    //void ( *add_text )     ( xui_workload_h workload, xui_text_state_t*   state );
    void ( *add_label )     ( xui_workload_h workload, xui_label_state_t*  state );
    void ( *add_select )    ( xui_workload_h workload, xui_select_state_t* state );
    void ( *add_switch )    ( xui_workload_h workload, xui_switch_state_t* state );
} xui_i;
