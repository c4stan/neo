#pragma once

#include <xi.h>

#include <xg.h>
#include <xs.h>

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} xi_scissor_t;

typedef struct {
    float x;
    float y;
    float width;
    float height;
    xi_color_t color;
    xg_texture_h texture;
    bool linear_sampler_filter;
    float uv0[2]; // top left
    float uv1[2]; // bottom right
    uint64_t sort_order;
    xi_scissor_h scissor;
} xi_draw_rect_t;

#define xi_draw_rect_m( ... ) ( xi_draw_rect_t ) { \
    .x = 0, \
    .y = 0, \
    .width = 0, \
    .height = 0, \
    .color = xi_color_black_m, \
    .texture = xg_null_handle_m, \
    .linear_sampler_filter = false, \
    .uv0 = { 0, 0 }, \
    .uv1 = { 0, 0 }, \
    .sort_order = 0, \
    .scissor = xi_null_scissor_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    float xy0[2];
    float xy1[2];
    float xy2[2];
    xi_color_t color;
    xg_texture_h texture;
    bool linear_sampler_filter;
    float uv0[2];
    float uv1[2];
    float uv2[2];
    uint64_t sort_order;
    xi_scissor_h scissor;
} xi_draw_tri_t;

#define xi_default_draw_tri_m ( xi_draw_tri_t ) { \
    .xy0 = { 0, 0 }, \
    .xy1 = { 0, 0 }, \
    .xy2 = { 0, 0 }, \
    .color = xi_color_black_m, \
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
} xi_workload_vertex_t;

typedef struct {
    xg_buffer_h pos_buffer;
    xg_buffer_h nor_buffer;
    xg_buffer_h idx_buffer;
    xg_buffer_h uv_buffer;
    xg_texture_h texture;
    bool linear_sampler_filter;
    uint32_t idx_count;
    uint64_t sort_order;
    float traslation[3]; // x y z
    float rotation[4];    // x y z w
    float scale;
    float color[4];
} xi_draw_mesh_t;

#define xi_draw_mesh_m( ... ) ( xi_draw_mesh_t ) { \
    .pos_buffer = xg_null_handle_m, \
    .nor_buffer = xg_null_handle_m, \
    .idx_buffer = xg_null_handle_m, \
    .uv_buffer = xg_null_handle_m, \
    .texture = xg_null_handle_m, \
    .linear_sampler_filter = false, \
    .idx_count = 0, \
    .sort_order = 0, \
    .traslation = { 0, 0, 0 }, \
    .rotation = { 0, 0, 0, 1 }, \
    .scale = 1, \
    .color = { 1, 1, 1, 1 }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xi_draw_rect_t rect_array[xi_workload_max_rects_m];
    uint64_t rect_count;
    xi_draw_tri_t tri_array[xi_workload_max_tris_m];
    uint64_t tri_count;

    xi_workload_vertex_t vertex_buffer_data[xi_workload_max_rects_m * 6 + xi_workload_max_tris_m * 3];
    xg_buffer_h vertex_buffer;
    // TODO index buffer

    xi_draw_mesh_t mesh_array[xi_workload_max_meshes_m];
    uint32_t mesh_count;

    float proj_from_world[16];

    xi_scissor_t scissor_array[xi_workload_max_scissors_m];
    uint32_t scissor_count;

    xg_workload_h xg_workload;
} xi_workload_t;

typedef struct {
    xg_buffer_h vertex_buffer;
} xi_workload_device_context_t;

typedef struct {
    xg_renderpass_h xg_handle;
    uint32_t resolution_x;
    uint32_t resolution_y;
} xi_workload_renderpass_t;

// TODO embed all ui state in here? at the moment building multiple workloads at the same time is not supported...
typedef struct {
    xi_workload_t* workloads_array;
    xi_workload_t* workloads_freelist;
    uint64_t workloads_count;

    //xi_workload_submit_context_t* submit_contexts;
    //std_ring_t submit_ring;
    xi_workload_device_context_t device_contexts[xg_max_active_devices_m];

    xs_database_pipeline_h ui_pipeline_rgba8;
    xs_database_pipeline_h ui_pipeline_bgra8;
    xs_database_pipeline_h ui_pipeline_a2bgr10;

    xs_database_pipeline_h geo_pipeline_rgba8;
    xs_database_pipeline_h geo_pipeline_bgra8;
    xs_database_pipeline_h geo_pipeline_a2bgr10;

    xi_workload_renderpass_t ui_renderpass_rgba8;
    xi_workload_renderpass_t ui_renderpass_bgra8;
    xi_workload_renderpass_t ui_renderpass_a2bgr10;
} xi_workload_state_t;

void xi_workload_load ( xi_workload_state_t* state );
void xi_workload_reload ( xi_workload_state_t* state );
void xi_workload_unload ( void );

void xi_workload_load_shaders ( xs_i* xs, xs_database_h sdb );

xi_workload_h xi_workload_create ( void );
void xi_workload_cmd_draw ( xi_workload_h workload, const xi_draw_rect_t* rects, uint64_t rect_count );
void xi_workload_cmd_draw_tri ( xi_workload_h workload, const xi_draw_tri_t* tris, uint64_t tri_count );
void xi_workload_cmd_draw_mesh ( xi_workload_h workload, const xi_draw_mesh_t* mesh );
uint64_t xi_workload_flush ( xi_workload_h workload, const xi_flush_params_t* params );

xi_scissor_h xi_workload_scissor ( xi_workload_h workload, uint32_t x, uint32_t y, uint32_t width, uint32_t height );

void xi_workload_activate_device ( xg_i* xg, xg_device_h device );
void xi_workload_deactivate_device ( xg_i* xg, xg_device_h device );

void xi_workload_set_view_info ( xi_workload_h workload_handle, const rv_view_info_t* view_info );
