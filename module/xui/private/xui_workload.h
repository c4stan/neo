#pragma once

#include <xui.h>

#include <xg.h>
#include <xs.h>

typedef struct {
    float x;
    float y;
    float width;
    float height;
    xui_color_t color;
    xg_texture_h texture;
    bool linear_sampler_filter;
    float uv0[2]; // top left
    float uv1[2]; // bottom right
    uint64_t sort_order;
} xui_draw_rect_t;

#define xui_default_draw_rect_m ( xui_draw_rect_t ) { \
    .x = 0, \
    .y = 0, \
    .width = 0, \
    .height = 0, \
    .color = xui_color_black_m, \
    .texture = xg_null_handle_m, \
    .linear_sampler_filter = false, \
    .uv0 = { 0, 0 }, \
    .uv1 = { 0, 0 }, \
    .sort_order = 0, \
}

typedef struct {
    float xy0[2];
    float xy1[2];
    float xy2[2];
    xui_color_t color;
    xg_texture_h texture;
    bool linear_sampler_filter;
    float uv0[2];
    float uv1[2];
    float uv2[2];
    uint64_t sort_order;
} xui_draw_tri_t;

#define xui_default_draw_tri_m ( xui_draw_tri_t ) { \
    .xy0 = { 0, 0 }, \
    .xy1 = { 0, 0 }, \
    .xy2 = { 0, 0 }, \
    .color = xui_color_black_m, \
    .texture = xg_null_handle_m, \
    .linear_sampler_filter = false, \
    .uv0 = { 0, 0 }, \
    .uv1 = { 0, 0 }, \
    .uv2 = { 0, 0 }, \
    .sort_order = 0, \
}

typedef struct {
    float pos[2];
    float uv[2];
    float color[4];
} xui_workload_vertex_t;

typedef struct {
    xui_draw_rect_t rect_array[xui_workload_max_rects_m];
    uint64_t rect_count;
    xui_draw_tri_t tri_array[xui_workload_max_tris_m];
    uint64_t tri_count;
    xui_workload_vertex_t vertex_buffer_data[xui_workload_max_rects_m * 6 + xui_workload_max_tris_m * 3];
    xg_buffer_h vertex_buffer;
    // TODO index buffer
    xg_workload_h xg_workload;
} xui_workload_t;

typedef struct {
    xg_buffer_h vertex_buffer;
} xui_workload_device_context_t;

typedef struct {
    xui_workload_t* workloads_array;
    xui_workload_t* workloads_freelist;
    uint64_t workloads_count;

    //xui_workload_submit_context_t* submit_contexts;
    //std_ring_t submit_ring;
    xui_workload_device_context_t device_contexts[xg_vk_max_active_devices_m];

    xs_pipeline_state_h render_pipeline_bgra8;
    xs_pipeline_state_h render_pipeline_a2bgr10;

    xg_texture_h null_texture;
    xg_sampler_h point_sampler;
    xg_sampler_h linear_sampler;
} xui_workload_state_t;

void xui_workload_load ( xui_workload_state_t* state );
void xui_workload_reload ( xui_workload_state_t* state );
void xui_workload_unload ( void );

void xui_workload_load_shaders ( xs_i* xs );

xui_workload_h xui_workload_create ( void );
void xui_workload_cmd_draw ( xui_workload_h workload, const xui_draw_rect_t* rects, uint64_t rect_count );
void xui_workload_cmd_draw_tri ( xui_workload_h workload, const xui_draw_tri_t* tris, uint64_t tri_count );
void xui_workload_flush ( xui_workload_h workload, const xui_flush_params_t* params );

void xui_workload_activate_device ( xg_i* xg, xg_device_h device );
void xui_workload_deactivate_device ( xg_i* xg, xg_device_h device );
