#pragma once

#include <std_module.h>

#include <xg.h>
#include <xs.h>
#include <rv.h>

#define xi_module_name_m xi
std_module_export_m void* xi_load ( void* );
std_module_export_m void xi_reload ( void*, void* );
std_module_export_m void xi_unload ( void );

#define xi_null_handle_m UINT64_MAX

typedef uint64_t xi_id_t;
typedef uint64_t xi_font_h;
typedef uint64_t xi_workload_h;

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
} xi_color_t;

//#define xi_color_rgba_u32_m( r, g, b, a ) ( xi_color_t ) { .u32 = ( r | ( g << 8 ) | ( b << 16 ) | ( a << 24 ) ) }
#define xi_color_rgba_u32_m( r, g, b, a ) ( xi_color_t ) { .u32 = ( (uint32_t) r | ( (uint32_t) g << 8 ) | ( (uint32_t) b << 16 ) | ( (uint32_t) a << 24 ) ) }

#define xi_color_rgb_mul_m( color, scalar )  ( xi_color_t ) xi_color_rgba_u32_m ( (color.r * scalar), (color.g * scalar), (color.b * scalar), color.a )
#define xi_color_rgba_mul_m( color, scalar ) ( xi_color_t ) xi_color_rgba_u32_m ( (color.r * scalar), (color.g * scalar), (color.b * scalar), (color.a * scalar) )

#define xi_color_red_m         xi_color_rgba_u32_m ( 176,  40,  48, 255 )
#define xi_color_orange_m      xi_color_rgba_u32_m ( 222, 122,  34, 255 )
#define xi_color_yellow_m      xi_color_rgba_u32_m ( 240, 201,   0, 255 )
#define xi_color_cyan_m        xi_color_rgba_u32_m (   0, 136, 166, 255 )
#define xi_color_blue_m        xi_color_rgba_u32_m (  28,  56, 146, 255 )
#define xi_color_green_m       xi_color_rgba_u32_m (  67, 149,  66, 255 )
#define xi_color_black_m       xi_color_rgba_u32_m (  40,  39,  40, 255 )
#define xi_color_white_m       xi_color_rgba_u32_m ( 248, 248, 242, 255 )
#define xi_color_gray_m        xi_color_rgba_u32_m ( 118, 120, 119, 255 )
#define xi_color_dark_gray_m   xi_color_rgba_u32_m (  79,  80,  81, 255 )
#define xi_color_invalid_m     xi_color_rgba_u32_m (   0,   0,   0,   0 )

// Style

typedef enum {
    xi_horizontal_alignment_left_to_right_m,
    xi_horizontal_alignment_right_to_left_m,
    xi_horizontal_alignment_centered_m, // when using this only one element can fit per line
    xi_horizontal_alignment_unaligned_m,
    xi_horizontal_alignment_invalid_m,
} xi_horizontal_alignment_e;

typedef enum {
    xi_vertical_alignment_top_m,
    xi_vertical_alignment_centered_m,
    xi_vertical_alignment_bottom_m,
    xi_vertical_alignment_unaligned_m,
    xi_vertical_alignment_invalid_m,
} xi_vertical_alignment_e;

typedef struct {
    xi_font_h font;
    uint32_t font_height;
    xi_color_t color;
    xi_color_t font_color;
    xi_horizontal_alignment_e horizontal_alignment;
    xi_vertical_alignment_e vertical_alignment;
    // TODO add padding/margin here
} xi_style_t;

#define xi_style_m( ... ) ( xi_style_t ) { \
    .font = xi_null_handle_m, \
    .font_height = 14, \
    .color = xi_color_black_m, \
    .font_color = xi_color_white_m, \
    .horizontal_alignment = xi_horizontal_alignment_left_to_right_m, \
    .vertical_alignment = xi_vertical_alignment_bottom_m, \
    ##__VA_ARGS__ \
}

#define xi_null_style_m( ... ) ( xi_style_t ) { \
    .font = xi_null_handle_m, \
    .font_height = 0, \
    .color = xi_color_invalid_m, \
    .font_color = xi_color_invalid_m, \
    .horizontal_alignment = xi_horizontal_alignment_invalid_m, \
    .vertical_alignment = xi_vertical_alignment_invalid_m, \
    ##__VA_ARGS__ \
}

#define xi_default_style_m xi_style_m()

// Elements

// TODO XOR instead of + ?
#define xi_line_id_m() ( xi_id_t ) ( std_hash_64_m ( std_file_name_hash_m + std_line_num_m ) )

typedef struct {
    char title[xi_window_title_size];
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t padding_x;
    uint32_t padding_y;
    uint32_t header_height;
    bool minimized;
    bool resizable;
    bool movable;
    bool scrollable;
    float scroll; 
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_window_state_t;

#define xi_window_state_m( ... ) ( xi_window_state_t ) { \
    .title = "", \
    .x = 0, \
    .y = 0, \
    .width = 0, \
    .height = 0, \
    .header_height = 20, \
    .padding_x = 10, \
    .padding_y = 2, \
    .minimized = false, \
    .resizable = true, \
    .movable = true, \
    .scrollable = true, \
    .scroll = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_style_m(), \
    ##__VA_ARGS__ \
}

#define xi_default_window_state_m xi_window_state_m()

typedef struct {
    bool minimized;
    char title[xi_section_title_size];
    xi_font_h font;
    uint32_t height;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_section_state_t;

#define xi_section_state_m( ... ) ( xi_section_state_t ) { \
    .minimized = false, \
    .title = "", \
    .font = xi_null_handle_m, \
    .height = 20, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t width;
    uint32_t height;
    char text[xi_button_text_size];
    bool down;
    bool pressed;
    bool disabled;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_button_state_t;

#define xi_button_state_m( ... ) ( xi_button_state_t ) { \
    .width = 0, \
    .height = 0, \
    .text = "", \
    .down = false, \
    .pressed = false, \
    .disabled = false, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t thickness;
    uint32_t length;
    uint32_t width;
    uint32_t height;
    float value;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_slider_state_t;

#define xi_slider_state_m( ... ) ( xi_slider_state_t ) { \
    .thickness = 0, \
    .length = 0, \
    .width = 0, \
    .height = 0, \
    .value = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
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
    xi_id_t id;
    uint64_t sort_order;
} xi_text_state_t;

#define xi_default_text_state_m ( xi_text_state_t ) { \
    .text = NULL, \
    .text_capacity = 0, \
    .width = 0, \
    .height = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
}
#endif

typedef struct {
    char text[xi_textfield_text_size_m];
    xi_font_h font;
    uint32_t width;
    uint32_t height;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_textfield_state_t;

#define xi_textfield_state_m( ... ) ( xi_textfield_state_t ) { \
    .text = "", \
    .font = xi_null_handle_m, \
    .height = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    char text[xi_label_text_size];
    xi_font_h font;
    uint32_t height;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_label_state_t;

#define xi_label_state_m( ... ) ( xi_label_state_t ) { \
    .text = "", \
    .font = xi_null_handle_m, \
    .height = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    const char** items;
    uint32_t item_count;
    uint32_t item_idx;
    xi_font_h font;
    uint32_t width;
    uint32_t height;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_select_state_t;

#define xi_select_state_m( ... ) ( xi_select_state_t ) { \
    .items = NULL, \
    .item_count = 0, \
    .item_idx = 0, \
    .font = xi_null_handle_m, \
    .width = 0, \
    .height = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t width;
    uint32_t height;
    bool value;
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_switch_state_t;

#define xi_switch_state_m( ... ) ( xi_switch_state_t ) { \
    .width = 0, \
    .height = 0, \
    .value = false, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef enum {
    xi_property_f32_m,
    xi_property_u32_m,
    xi_property_i32_m,
    xi_property_u64_m,
    xi_property_i64_m,
    xi_property_2f32_m,
    xi_property_3f32_m,
    xi_property_4f32_m,
    xi_property_string_m,
    xi_property_bool_m,
    xi_property_invalid_m,
} xi_property_e;

typedef struct {
    xi_property_e type;
    void* data;
    uint32_t property_idx;
    xi_font_h font;
    uint32_t property_width;
    uint32_t property_height;
    uint64_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_property_editor_state_t;

#define xi_property_editor_state_m( ... ) ( xi_property_editor_state_t ) { \
    .type = xi_property_invalid_m, \
    .data = NULL, \
    .font = xi_null_handle_m, \
    .property_width = 0, \
    .property_height = 0, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    float position[3];
    float rotation[4];
    xi_id_t id;
    uint64_t sort_order;
    xi_style_t style;
} xi_transform_state_t;

#define xi_transform_state_m(...) ( xi_transform_state_t ) { \
    .position = { 0, 0, 0 }, \
    .rotation = { 0, 0, 0, 1 }, \
    .id = xi_line_id_m(), \
    .sort_order = 0, \
    .style = xi_null_style_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    float start[3];
    float end[3];
    xi_color_t color;
    xi_id_t id;
    uint64_t sort_order;
} xi_line_state_t;

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
    char debug_name[xi_debug_name_size_m];
} xi_font_params_t;

#define xi_font_char_ascii_base_m 32
#define xi_font_char_ascii_count_m 94

#define xi_font_params_m( ... ) ( xi_font_params_t ) { \
    .xg_device = xg_null_handle_m, \
    .pixel_height = 0, \
    .first_char_code = xi_font_char_ascii_base_m, \
    .char_count = xi_font_char_ascii_count_m, \
    .outline = false, \
    .debug_name = "", \
    ##__VA_ARGS__ \
}

// Update

typedef struct {
    const wm_window_info_t* window_info;
    const wm_input_state_t* input_state;
    const wm_input_buffer_t* input_buffer;
    const rv_view_info_t* view_info;
} xi_update_params_t;

#define xi_update_params_m( ... ) ( xi_update_params_t ) { \
    .window_info = NULL, \
    .input_state = NULL, \
    .input_buffer = NULL, \
    .view_info = NULL, \
    ##__VA_ARGS__ \
}

// Rendering

typedef struct {
    uint64_t width;
    uint64_t height;
} xi_viewport_t;

typedef struct {
    xg_device_h device;
    xg_workload_h workload;
    xg_cmd_buffer_h cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer;
    uint64_t key;

    xg_format_e render_target_format;
    xi_viewport_t viewport;
    xg_renderpass_h renderpass;
    xg_render_target_binding_t render_target_binding;
} xi_flush_params_t;

// Api

typedef struct {
    void ( *load_shaders ) ( xg_device_h device );
    void ( *reload_shaders ) ( xg_device_h device );
    //void ( *activate_device ) ( xg_device_h device );
    //void ( *deactivate_device ) ( xg_device_h device );

    void ( *begin_update ) ( const xi_update_params_t* params ); // TODO remove, pass update_params to workload

    uint64_t ( *get_active_element_id ) ( void );
    uint64_t ( *get_hovered_element_id ) ( void );

    xi_font_h ( *create_font ) ( std_buffer_t ttf_data, const xi_font_params_t* params );
    void ( *destroy_font ) ( xi_font_h font );

    void ( *set_workload_view_info ) ( xg_workload_h workload, const rv_view_info_t* view_info ); // TODO remove!

    xi_workload_h ( *create_workload ) ( void );
    uint64_t ( *flush_workload ) ( xi_workload_h workload, const xi_flush_params_t* params );
    // Add api to get oldest non-flushed workload? can avoid having to keep track of created workloads on client side
    // Auto flush all pending workloads on end_frame/end_update?

    void ( *end_update ) ( void );

    void ( *newline ) ( void );

    void ( *set_style )     ( xi_style_t* style );

    void ( *begin_window )  ( xi_workload_h workload, xi_window_state_t* state );
    void ( *end_window )    ( xi_workload_h workload );

    void ( *begin_section ) ( xi_workload_h workload, xi_section_state_t* state );
    void ( *end_section )   ( xi_workload_h workload );

    // TODO rename these, remove add_ and leave just element name? replace with draW?
    // TODO return bool true if internal value changed
    bool ( *add_button )    ( xi_workload_h workload, xi_button_state_t* state );
    void ( *add_slider )    ( xi_workload_h workload, xi_slider_state_t* state );
    //void ( *add_text )     ( xi_workload_h workload, xi_text_state_t*   state );
    void ( *add_label )     ( xi_workload_h workload, xi_label_state_t*  state );
    void ( *add_select )    ( xi_workload_h workload, xi_select_state_t* state );
    bool ( *add_switch )    ( xi_workload_h workload, xi_switch_state_t* state );
    bool ( *add_textfield ) ( xi_workload_h workload, xi_textfield_state_t* state );
    bool ( *add_property_editor ) ( xi_workload_h workload, xi_property_editor_state_t* state );

    void ( *init_geos )     ( xg_device_h device );
    void ( *draw_line )     ( xi_workload_h workload, xi_line_state_t* state );
    bool ( *draw_transform )( xi_workload_h workload, xi_transform_state_t* state );    
} xi_i;
