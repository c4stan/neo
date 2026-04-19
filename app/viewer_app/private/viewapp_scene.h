#pragma once

#include <xg.h>
#include <xg_geo_util.h>
#include <xf.h>
#include <se.h>
#include <rv.h>

// Components
#define viewapp_mesh_component_id_m 0
#define viewapp_camera_component_id_m 1
#define viewapp_light_component_id_m 2
#define viewapp_raytrace_mesh_component_id 3
#define viewapp_transform_component_id_m 4
#define viewapp_parent_component_id_m 5
#define viewapp_tessellation_mesh_component_id_m 6
#define viewapp_sky_component_id_m 7

typedef struct {
    float base_color[3];
    bool ssr;
    float roughness;
    float metalness;
    float emissive[3];
    xf_texture_h color_texture;
    xf_texture_h normal_texture;
    xf_texture_h metalness_roughness_texture;
} viewapp_material_data_t;

#define viewapp_material_data_m( ... ) ( viewapp_material_data_t ) { \
    .base_color = { 1, 1, 1 }, \
    .emissive = { 0, 0, 0 }, \
    .ssr = false, \
    .roughness = 0.5, \
    .metalness = 0, \
    .color_texture = xg_null_handle_m, \
    .normal_texture = xg_null_handle_m, \
    .metalness_roughness_texture = xg_null_handle_m, \
    __VA_ARGS__ \
}

typedef struct {
    float position[3];
    float scale;
    float orientation[4];
} viewapp_transform_t;

#define viewapp_transform_m( ... ) ( viewapp_transform_t ) { \
    .position = { 0, 0, 0 }, \
    .scale = 1, \
    .orientation = { 0, 0, 0, 1 }, \
    __VA_ARGS__ \
}

typedef struct {
    viewapp_transform_t local;
    viewapp_transform_t global;
} viewapp_transform_component_t;

#define viewapp_transform_component_m( ... ) ( viewapp_transform_component_t ) { \
    .local = viewapp_transform_m(), \
    .global = viewapp_transform_m(), \
    __VA_ARGS__ \
}

typedef struct {
    se_entity_h parent;
} viewapp_parent_component_t;

#define viewapp_parent_component_m( ... ) ( viewapp_parent_component_t ) { \
    .parent = se_null_handle_m, \
    __VA_ARGS__ \
}

typedef struct {
    // TODO
    xg_geo_util_geometry_data_t geo_data;
    xg_geo_util_geometry_gpu_data_t geo_gpu_data;
    uint32_t object_id;
    viewapp_material_data_t material;
} viewapp_tessellation_mesh_component_t;

#define viewapp_tessellation_mesh_component_m( ... ) ( viewapp_tessellation_mesh_component_t ) { \
    .geo_gpu_data = xg_geo_util_geometry_gpu_data_m(), \
    __VA_ARGS__ \
}

typedef struct {
    xg_geo_util_geometry_data_t geo_data;
    xg_geo_util_geometry_gpu_data_t geo_gpu_data;
    xg_texture_h radiance_texture;
    float irradiance_sh[3*9];
    xg_texture_h cubemap_texture;
} viewapp_sky_component_t;

#define viewapp_sky_component_m( ... ) ( viewapp_sky_component_t ) { \
    .radiance_texture = xg_null_handle_m, \
    __VA_ARGS__ \
}

typedef struct {
    xg_geo_util_geometry_data_t geo_data;
    xg_geo_util_geometry_gpu_data_t geo_gpu_data;
    xs_database_pipeline_h object_id_pipeline;
    xs_database_pipeline_h geometry_pipeline;
    xs_database_pipeline_h shadow_pipeline;
    viewapp_transform_t prev_transform;
    uint32_t object_id;
    viewapp_material_data_t material;
    xg_raytrace_geometry_h rt_geo;
} viewapp_mesh_component_t;

#define viewapp_mesh_component_m( ... ) ( viewapp_mesh_component_t ) { \
    .geo_gpu_data = xg_geo_util_geometry_gpu_data_m(), \
    .object_id_pipeline = xs_null_handle_m, \
    .geometry_pipeline = xs_null_handle_m, \
    .shadow_pipeline = xs_null_handle_m, \
    .prev_transform = viewapp_transform_m(), \
    .object_id = 0, \
    .material = viewapp_material_data_m(), \
    .rt_geo = xg_null_handle_m, \
    __VA_ARGS__ \
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
    __VA_ARGS__ \
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
    float radius;
    float color[3];
    float intensity;
    viewapp_light_view_shadow_tile_t shadow_tiles[viewapp_light_max_views_m];
} viewapp_light_component_t;

#define viewapp_light_component_m( ... ) ( viewapp_light_component_t ) { \
    .shadow_casting = false, \
    .views = { [0 ... viewapp_light_max_views_m-1] = rv_null_handle_m }, \
    .view_count = 0, \
    .radius = 100, \
    .color = { 0, 0, 0 }, \
    .intensity = 0, \
    __VA_ARGS__ \
}

// API
void viewapp_boot_scene ( void );

typedef enum {
    viewapp_scene_cornell_box_m,
    viewapp_scene_field_m,
    viewapp_scene_external_m,
} viewapp_scene_e;

typedef enum {
    viewapp_envmap_none_m,
    viewapp_envmap_external_m,
} viewapp_envmap_e;

void viewapp_load_scene ( viewapp_scene_e scene );

uint64_t viewapp_load_envmap ( xg_workload_h workload, uint64_t key, viewapp_envmap_e envmap );
viewapp_camera_component_t* viewapp_get_active_camera ( void );

se_entity_h spawn_plane ( xg_workload_h workload );
se_entity_h spawn_sphere ( xg_workload_h workload );
se_entity_h spawn_light ( xg_workload_h workload );

void viewapp_destroy_entity_resources ( se_entity_h entity, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, xg_resource_cmd_buffer_time_e time );

void viewapp_build_raytrace_world ( void );
void viewapp_update_raytrace_world ( void );
