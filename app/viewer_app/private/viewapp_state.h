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

#include <xg_geo_util.h>

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
#if 0
typedef struct {
    char name[32];
    xg_geo_util_geometry_data_t data;
    xg_geo_util_geometry_gpu_data_t gpu_data;
} viewapp_scene_mesh_t;

typedef struct {
    viewapp_scene_mesh_t* mesh;
    float local_transform[16];
    float global_transform[16];
} viewapp_scene_mesh_instance_t;
#endif
typedef struct {
    uint32_t active_scene;
    char custom_scene_path[128];
    //uint32_t entity_count;
    //se_entity_h entities[128];
} viewapp_scene_state_t;

#define viewapp_scene_state_m( ... ) ( viewapp_scene_state_t ) { \
    .active_scene = 0, \
    .custom_scene_path[0] = '\0' \
    ##__VA_ARGS__ \
}

// Render
typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    uint32_t frame_id;
    bool capture_frame;
    float time_ms;
    float delta_time_ms;
    float update_time_ms;
    std_tick_t frame_tick;

    uint32_t next_object_id;

    wm_window_h window;
    wm_window_info_t window_info;
    wm_input_state_t input_state;

    xg_device_h device;
    xg_swapchain_h swapchain;

    xs_database_h sdb;

    xf_graph_h raster_graph;
    xf_graph_h raytrace_graph;
    xf_graph_h active_graph;

    xf_graph_h mouse_pick_graph;
    xg_texture_h object_id_readback_texture;

    xf_node_h taa_node;
    xg_raytrace_world_h raytrace_world;
    xg_resource_bindings_layout_h workload_bindings_layout;

    bool graph_reload;
    bool allow_graph_aliasing;

    xf_texture_h export_dest;
} viewapp_render_state_t;

#define viewapp_render_state_m( ... ) ( viewapp_render_state_t ) { \
    .resolution_x = 0, \
    .resolution_y = 0, \
    .frame_id = 0, \
    .capture_frame = false, \
    .time_ms = 0, \
    .delta_time_ms = 0, \
    .update_time_ms = 0, \
    .next_object_id = 1, \
    .window = wm_null_handle_m, \
    .device = xg_null_handle_m, \
    .swapchain = xg_null_handle_m, \
    .sdb = xs_null_handle_m, \
    .raster_graph = xf_null_handle_m, \
    .raytrace_graph = xf_null_handle_m, \
    .active_graph = xf_null_handle_m, \
    .taa_node = xf_null_handle_m, \
    .raytrace_world = xg_null_handle_m, \
    .window_info = {}, \
    .input_state = {}, \
    .frame_tick = 0, \
    .allow_graph_aliasing = true, \
    .export_dest = xf_null_handle_m, \
    ##__VA_ARGS__ \
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
    xi_section_state_t xf_textures_state;

    uint64_t expanded_nodes_bitset[1];
    uint64_t expanded_entities_bitset[8];

    xg_texture_h export_texture;
    xf_texture_h export_source;
    uint64_t export_id;
    uint64_t export_node_id;
    uint64_t export_tex_id;
    xf_node_h export_node;
    xf_export_channel_e export_channels[4];

    se_entity_h mouse_pick_entity;
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
    .export_channels = { xf_export_channel_r, xf_export_channel_g, xf_export_channel_b, xf_export_channel_a }, \
    .export_node_id = -1, \
    .export_tex_id = -1, \
    .mouse_pick_entity = se_null_handle_m, \
    ##__VA_ARGS__ \
}

// Components
#define viewapp_mesh_component_id_m 0
#define viewapp_camera_component_id_m 1
#define viewapp_light_component_id_m 2
#define viewapp_raytrace_mesh_component_id 3
#define viewapp_transform_component_id_m 4

typedef struct {
    float base_color[3];
    bool ssr;
    float roughness;
    float metalness;
    float emissive;
    xf_texture_h color_texture;
    xf_texture_h normal_texture;
    xf_texture_h metalness_roughness_texture;
} viewapp_material_data_t;

#define viewapp_material_data_m( ... ) ( viewapp_material_data_t ) { \
    .base_color = { 1, 1, 1 }, \
    .emissive = 0, \
    .ssr = false, \
    .roughness = 0.5, \
    .metalness = 0, \
    .color_texture = xg_null_handle_m, \
    .normal_texture = xg_null_handle_m, \
    .metalness_roughness_texture = xg_null_handle_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    float position[3];
    float scale;
    float orientation[4];
} viewapp_transform_component_t;

#define viewapp_transform_component_m( ... ) { \
    .position = { 0, 0, 0 }, \
    .scale = 1, \
    .orientation = { 0, 0, 0, 1 }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_geo_util_geometry_data_t geo_data;
    xg_geo_util_geometry_gpu_data_t geo_gpu_data;
    xs_database_pipeline_h object_id_pipeline;
    xs_database_pipeline_h geometry_pipeline;
    xs_database_pipeline_h shadow_pipeline;
    viewapp_transform_component_t prev_transform;
    uint32_t object_id;
    viewapp_material_data_t material;
    xg_raytrace_geometry_h rt_geo;
    uint32_t rt_instance_id;
} viewapp_mesh_component_t;

#define viewapp_mesh_component_m( ... ) ( viewapp_mesh_component_t ) { \
    .geo_data = { 0 }, \
    .geo_gpu_data = { 0 }, \
    .object_id_pipeline = xs_null_handle_m, \
    .geometry_pipeline = xs_null_handle_m, \
    .shadow_pipeline = xs_null_handle_m, \
    .prev_transform = viewapp_transform_component_m(), \
    .object_id = 0, \
    .material = viewapp_material_data_m(), \
    .rt_geo = xg_null_handle_m, \
    .rt_instance_id = 0 ,\
    ##__VA_ARGS__ \
}

typedef enum {
    viewapp_camera_type_arcball_m,
    viewapp_camera_type_flycam_m,
} viewapp_camera_type_e;

typedef struct {
    rv_view_h view;
    bool enabled;
    float move_speed;
    viewapp_camera_type_e type;
} viewapp_camera_component_t;

#define viewapp_camera_component_m( ... ) { \
    .view = rv_null_handle_m, \
    .enabled = false, \
    .move_speed = 0.00002, \
    .type = viewapp_camera_type_flycam_m, \
    ##__VA_ARGS__ \
}

#define viewapp_light_max_views_m 6

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t size;
} viewapp_light_view_shadow_tile_t;

typedef struct {
    bool shadow_casting;
    rv_view_h views[viewapp_light_max_views_m];
    uint32_t view_count;
    float position[3];
    float radius;
    float color[3];
    float intensity;
    viewapp_light_view_shadow_tile_t shadow_tiles[viewapp_light_max_views_m];
} viewapp_light_component_t;

#define viewapp_light_component_m( ... ) ( viewapp_light_component_t ) { \
    .shadow_casting = false, \
    .views = { [0 ... viewapp_light_max_views_m-1] = rv_null_handle_m }, \
    .view_count = 0, \
    .position = { 0, 0, 0 }, \
    .radius = 100, \
    .color = { 0, 0, 0 }, \
    .intensity = 0, \
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
