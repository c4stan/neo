#pragma once

#include <std_time.h>
#include <std_app.h>

#include <fs.h>
#include <xf.h>
#include <xs.h>
#include <se.h>
#include <rv.h>
#include <wm.h>
#include <xg.h>
#include <xi.h>

// Modules
typedef struct {
    fs_i* fs;
    wm_i* wm;
    xg_i* xg;
    xs_i* xs;
    xf_i* xf;
    se_i* se;
    rv_i* rv;
    xi_i* xi;
} viewapp_modules_state_t;

// Render
typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    uint32_t frame_id;
    bool capture_frame;
    float time_ms;

    wm_window_h window;

    xg_device_h device;
    xg_swapchain_h swapchain;
    xg_texture_h depth_stencil_texture;

    xf_graph_h graph;

    xf_node_h taa_node;

    wm_window_info_t window_info;
    wm_input_state_t input_state;

    std_tick_t frame_tick;

    xf_texture_h swapchain_multi_texture;
    xf_texture_h ssgi_raymarch_texture;
    xf_texture_h ssgi_accumulation_texture;
    xf_texture_h ssr_accumulation_texture;
    xf_texture_h ssgi_2_raymarch_texture;
    xf_texture_h ssgi_2_accumulation_texture;
    xf_texture_h taa_accumulation_texture;
    xf_texture_h object_id_texture;
} viewapp_render_state_t;

// UI

typedef struct {
    xi_font_h font;

    xi_window_state_t window_state;
    xi_style_t window_style;

    xi_section_state_t xf_graph_section_state;
    xi_section_state_t xf_alloc_section_state;
    xi_section_state_t entities_section_state;

    xi_style_t graph_label_style;
    xi_style_t graph_switch_style;
} viewapp_ui_state_t;

// Components
#define MESH_COMPONENT_ID 0
#define CAMERA_COMPONENT_ID 1
#define LIGHT_COMPONENT_ID 2
#define MODEL_COMPONENT_ID 3

typedef struct {
    float base_color[3];
    bool ssr;
    float roughness;
    float metalness;
} viewapp_material_data_t;

#define viewapp_material_data_m( ... ) ( viewapp_material_data_t ) { \
    .base_color = { 1, 1, 1 }, \
    .ssr = false, \
    .roughness = 0.5, \
    .metalness = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    xs_pipeline_state_h depth_pipeline;
    xs_pipeline_state_h geometry_pipeline;
    xs_pipeline_state_h shadow_pipeline;
    xg_buffer_h pos_buffer;
    xg_buffer_h nor_buffer;
    //xg_buffer_h tan_buffer;
    xg_buffer_h idx_buffer;
    uint32_t vertex_count;
    uint32_t index_count;
    float position[3];
    float orientation[3];
    float up[3];
    viewapp_material_data_t material;
} viewapp_mesh_component_t;

#define viewapp_mesh_component_m( ... ) ( viewapp_mesh_component_t ) { \
    .depth_pipeline = xs_null_handle_m, \
    .geometry_pipeline = xs_null_handle_m, \
    .shadow_pipeline = xs_null_handle_m, \
    .pos_buffer = xg_null_handle_m, \
    .nor_buffer = xg_null_handle_m, \
    .idx_buffer = xg_null_handle_m, \
    .vertex_count = 0, \
    .index_count = 0, \
    .position = { 0, 0, 0 }, \
    .orientation = { 0, 0, 1 }, \
    .up = { 0, 1, 0 }, \
    .material = viewapp_material_data_m(), \
    ##__VA_ARGS__ \
}

typedef struct {
    rv_view_h view;
} viewapp_camera_component_t;

typedef struct {
    bool shadow_casting;
    rv_view_h view;
    float position[3];
    float intensity;
    float color[3];
} viewapp_light_component_t; // spotlight

#define viewapp_light_component_m( ... ) ( viewapp_light_component_t ) { \
    .shadow_casting = false, \
    .view = rv_null_handle_m, \
    .position = { 0, 0, 0 }, \
    .intensity = 0, \
    .color = { 0, 0, 0 }, \
    ##__VA_ARGS__ \
}

#define viewapp_render_component_name_size_m 32
#define viewapp_render_component_meshes_max_count_m 32

typedef struct {
    char name[viewapp_render_component_name_size_m];
    se_entity_h meshes[viewapp_render_component_meshes_max_count_m];
} viewapp_render_component_t;

#define viewapp_render_component_m( ... ) ( viewapp_render_component_t ) { \
    .name = {0}, \
    .meshes = {0}, \
    ##__VA_ARGS__ \
}

#if 0
typedef struct {
    se_entity_h camera;
    //se_entity_h sphere;
    se_entity_h planes[5];
    se_entity_h light;

    viewapp_render_component_t render_components[32];
    viewapp_render_component_t* render_components_freelist;

    viewapp_mesh_component_t mesh_components[32];
    viewapp_mesh_component_t* mesh_components_freelist;

    viewapp_light_component_t light_components[32];
    viewapp_light_component_t* light_components_freelist;

    viewapp_camera_component_t camera_component;
} viewapp_components_state_t;
#else
typedef struct {
    se_entity_h camera;
    se_entity_h sphere;
    se_entity_h planes[5];
    se_entity_h light;
} viewapp_entity_state_t;
#endif

// Viewapp
typedef struct {
    std_app_i api;

    viewapp_modules_state_t modules;
    viewapp_render_state_t render;
    //viewapp_components_state_t components;
    //viewapp_entity_state_t entities;
    viewapp_ui_state_t ui;
} viewapp_state_t;

std_module_declare_state_m ( viewapp )

viewapp_state_t* viewapp_state_get ( void );
