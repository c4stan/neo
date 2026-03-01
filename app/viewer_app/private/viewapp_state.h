#pragma once

#include <std_time.h>
#include <std_app.h>

#include <xf.h>
#include <xs.h>
#include <se.h>
#include <rv.h>
#include <wm.h>
#include <xg.h>
#include <xi.h>

#include "viewapp_scene.h"
#include "viewapp_render.h"

// Modules
typedef struct {
    wm_i* wm;
    xg_i* xg;
    xs_i* xs;
    xf_i* xf;
    se_i* se;
    rv_i* rv;
    xi_i* xi;
} viewapp_modules_state_t;

// Scene
typedef struct {
    viewapp_scene_e active_scene;
    char custom_scene_path[128];
    viewapp_envmap_e active_envmap;
    char envmap_path[128];
} viewapp_scene_state_t;

#define viewapp_scene_state_m( ... ) ( viewapp_scene_state_t ) { \
    .active_scene = viewapp_scene_cornell_box_m, \
    __VA_ARGS__ \
}

// Render
#define viewapp_max_lights_m 1024
// to change reverse_z setting: viewapp_state, common.glsl, reverse_z param at view creation time, depth_write_dxx instead of depth_write_reverse_dxx in .xsg files 
//      TODO automate this to only one place/setting
#define viewapp_main_view_reverse_z_m false
#define viewapp_main_view_depth_clear_m ( viewapp_main_view_reverse_z_m ? xg_depth_clear_reverse_m : xg_depth_clear_regular_m )

typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    uint32_t frame_id;
    bool capture_frame;
    float time_ms;
    float delta_time_ms;
    std_tick_t frame_tick;
    float target_fps;

    uint32_t next_object_id;

    wm_window_h window;
    wm_window_info_t window_info;
    wm_input_state_t input_state;

    xg_device_h device;
    xg_swapchain_h swapchain;

    xs_database_h sdb;
    bool supports_raytrace;

    xf_graph_h render_graph;
    viewapp_render_graph_e active_render_graph;
    viewapp_render_graph_e new_render_graph;
    
    xf_graph_h mouse_pick_graph;
    xg_texture_h object_id_readback_texture;
    
    xf_graph_h cubemap_gen_graph;
    xf_texture_h cubemap_gen_texture;

    xg_raytrace_world_h raytrace_world;
    xs_database_pipeline_h raytrace_pipeline;
    xg_resource_bindings_layout_h workload_bindings_layout;

    bool clear_history;
    bool allow_graph_aliasing;
    bool raytrace_world_update;

    xf_texture_h export_dest;
    xf_texture_h sky_radiance_texture;
    xg_texture_h sky_cubemap;

    xg_buffer_h tessellation_instance_vertex_buffer;
} viewapp_render_state_t;

#define viewapp_render_state_m( ... ) ( viewapp_render_state_t ) { \
    .target_fps = 120, \
    .next_object_id = 1, \
    .window = wm_null_handle_m, \
    .device = xg_null_handle_m, \
    .swapchain = xg_null_handle_m, \
    .sdb = xs_null_handle_m, \
    .render_graph = xf_null_handle_m, \
    .active_render_graph = viewapp_render_graph_invalid_m, \
    .new_render_graph = viewapp_render_graph_invalid_m, \
    .mouse_pick_graph = xf_null_handle_m, \
    .object_id_readback_texture = xg_null_handle_m, \
    .raytrace_world = xg_null_handle_m, \
    .workload_bindings_layout = xg_null_handle_m, \
    .allow_graph_aliasing = true, \
    .export_dest = xf_null_handle_m, \
    .tessellation_instance_vertex_buffer = xg_null_handle_m, \
    .sky_radiance_texture = xf_null_handle_m, \
    __VA_ARGS__ \
}

// UI
typedef struct {
    xi_font_h font;

    xi_window_state_t window_state;
    xi_style_t window_style;

    xi_section_state_t frame_section_state;
    xi_section_state_t xg_alloc_section_state;
    xi_section_state_t xf_graph_section_state;
    xi_section_state_t scene_section_state;
    xi_section_state_t entities_section_state;

    uint64_t expanded_nodes_bitset[1];
    uint64_t expanded_entities_bitset[8];

    xg_texture_h export_texture;
    xf_texture_h export_source;
    uint64_t export_id;
    xf_node_h export_node;
    xf_export_channel_e export_channels[4];

    se_entity_h mouse_pick_entity;

    uint32_t target_fps_values[6];
    uint32_t target_fps_idx;
} viewapp_ui_state_t;

#define viewapp_ui_state_m( ... ) ( viewapp_ui_state_t ) { \
    .font = xi_null_handle_m, \
    .window_state = xi_window_state_m(), \
    .window_style = xi_style_m(), \
    .frame_section_state = xi_section_state_m(), \
    .xg_alloc_section_state = xi_section_state_m(), \
    .xf_graph_section_state = xi_section_state_m(), \
    .entities_section_state = xi_section_state_m(), \
    .export_texture = xg_null_handle_m, \
    .export_source = xf_null_handle_m, \
    .export_id = 0, \
    .export_node = xf_null_handle_m, \
    .export_channels = { xf_export_channel_r, xf_export_channel_g, xf_export_channel_b, xf_export_channel_a }, \
    .mouse_pick_entity = se_null_handle_m, \
    .target_fps_values = { 120, 90, 60, 30, 24, 8 }, \
    .target_fps_idx = 0, \
    __VA_ARGS__ \
}

// Viewapp
typedef struct {
    std_app_i api;
    viewapp_modules_state_t modules;
    viewapp_render_state_t render;
    viewapp_ui_state_t ui;
    viewapp_scene_state_t scene;
    bool reload;
} viewapp_state_t;

std_module_declare_state_m ( viewapp )

viewapp_state_t* viewapp_state_get ( void );
