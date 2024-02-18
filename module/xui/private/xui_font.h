#pragma once

#include <xui.h>
#include <xg.h>
#include <xs.h>

typedef struct {
    xui_font_params_t params;
    xg_texture_h atlas_texture;
    std_memory_h char_info_handle;
    void* char_info; // stbtt_packedchar*
    //std_memory_h font_info_handle;
    //stbtt_fontinfo font_info;
    bool outline;
} xui_font_t;

typedef struct {
    float color[3];
    uint32_t _pad0;
    float outline_color[3];
    bool outline;
    float resolution_f32[2];
    uint32_t _pad1[2];
} xui_font_atlas_uniform_data_t;

typedef struct {
    std_memory_h fonts_memory_handle;
    xui_font_t* fonts_array;
    xui_font_t* fonts_freelist;

    xs_pipeline_state_h font_atlas_pipeline;
    //xg_buffer_h uniform_buffer; // TODO have a per-device cache instead of creating a new one on font creation
    xui_font_atlas_uniform_data_t* uniform_data;
} xui_font_state_t;

void xui_font_load ( xui_font_state_t* state );
void xui_font_reload ( xui_font_state_t* state );
void xui_font_unload ( void );

void xui_font_load_shaders ( xs_i* xs );

xui_font_h xui_font_create_ttf ( std_buffer_t ttf_data, const xui_font_params_t* params );
void xui_font_destroy ( xui_font_h font );

xg_texture_h xui_font_atlas_get ( xui_font_h font );

typedef struct {
    float uv0[2];
    float uv1[2];
} xui_font_char_info_t;

typedef struct {
    uint32_t pixel_height;
} xui_font_info_t;

void xui_font_get_info ( xui_font_info_t* info, xui_font_h font );

typedef struct {
    float uv0[2]; // top left
    float uv1[2]; // bottom right
    float xy0[2];
    float xy1[2];
    float width;
    float height;
} xui_font_char_box_t;

// TODO avoid copying from stbtt, convert at creation and return a const pointer to the relevant data here
xui_font_char_box_t xui_font_char_box_get ( float* x, float* y, xui_font_h font, uint32_t character );

void xui_font_get_string_size ( float* w, float* h, xui_font_h font, const char* string );
