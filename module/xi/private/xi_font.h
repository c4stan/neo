#pragma once

#include <xi.h>
#include <xg.h>
#include <xs.h>

#include "stb_truetype.h"

typedef struct {
    xi_font_params_t params;
    xg_texture_h atlas_texture;
    stbtt_fontinfo font_info;
    void* char_info; // stbtt_packedchar*
    bool outline;
    int32_t ascent;
    int32_t descent;
    float scale;
} xi_font_t;

typedef struct {
    float color[3];
    uint32_t _pad0;
    float outline_color[3];
    bool outline;
    float resolution_f32[2];
    uint32_t _pad1[2];
} xi_font_atlas_uniform_data_t;

typedef struct {
    xi_font_t* fonts_array;
    xi_font_t* fonts_freelist;

    xs_pipeline_state_h font_atlas_pipeline;
    //xg_buffer_h uniform_buffer; // TODO have a per-device cache instead of creating a new one on font creation
    xi_font_atlas_uniform_data_t* uniform_data;
} xi_font_state_t;

void xi_font_load ( xi_font_state_t* state );
void xi_font_reload ( xi_font_state_t* state );
void xi_font_unload ( void );

void xi_font_load_shaders ( xs_i* xs );

xi_font_h xi_font_create_ttf ( std_buffer_t ttf_data, const xi_font_params_t* params );
void xi_font_destroy ( xi_font_h font );

xg_texture_h xi_font_atlas_get ( xi_font_h font );

typedef struct {
    float uv0[2];
    float uv1[2];
} xi_font_char_info_t;

typedef struct {
    uint32_t pixel_height;
    int32_t ascent;
    int32_t descent;
} xi_font_info_t;

void xi_font_get_info ( xi_font_info_t* info, xi_font_h font );

typedef struct {
    float uv0[2]; // top left
    float uv1[2]; // bottom right
    float xy0[2];
    float xy1[2];
    float width;
    float height;
} xi_font_char_box_t;

// TODO avoid copying from stbtt, convert at creation and return a const pointer to the relevant data here
xi_font_char_box_t xi_font_char_box_get ( float* x, float* y, xi_font_h font, uint32_t character );

void xi_font_get_string_size ( float* w, float* h, xi_font_h font, const char* string );
