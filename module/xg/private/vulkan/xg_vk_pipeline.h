#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_mutex.h>
#include <std_hash.h>
#include <std_allocator.h>

typedef struct {
    VkDescriptorSetLayout vk_handle;
    uint64_t hash;
    uint32_t ref_count;
    uint32_t shader_register_to_descriptor_idx[xg_pipeline_resource_max_bindings_per_set_m];
    xg_resource_bindings_layout_params_t params;
} xg_vk_resource_bindings_layout_t;

typedef struct {
    VkFramebuffer vk_handle;
} xg_vk_framebuffer_t;

typedef struct {
    VkRenderPass vk_handle;
    VkFramebuffer vk_framebuffer_handle;
    xg_renderpass_params_t params;
} xg_vk_renderpass_t;

typedef struct {
    xg_device_h device_handle;
    VkPipeline vk_handle;
    VkPipelineLayout vk_layout_handle;
    VkShaderModule vk_shader_handles[xg_shading_stage_count_m];
    xg_resource_bindings_layout_h resource_layouts[xg_shader_binding_set_count_m];

    uint64_t hash;
    uint64_t push_constants_hash;
    uint32_t reference_count;

    xg_pipeline_e type;
    char debug_name[xg_debug_name_size_m];
} xg_vk_pipeline_common_t;

typedef struct {
    xg_vk_pipeline_common_t common;
    VkRenderPass vk_renderpass;
    xg_graphics_pipeline_state_t state;
} xg_vk_graphics_pipeline_t;

typedef struct {
    xg_vk_pipeline_common_t common;
    xg_compute_pipeline_state_t state;
} xg_vk_compute_pipeline_t;

typedef struct {
    xg_vk_pipeline_common_t common;
    void* sbt_handle_buffer;
    // binding -> handle buffer offset
    uint32_t gen_offsets[xg_raytrace_shader_state_max_gen_shaders_m];
    uint32_t miss_offsets[xg_raytrace_shader_state_max_miss_shaders_m];
    uint32_t hit_offsets[xg_raytrace_shader_state_max_hit_groups_m];
    xg_buffer_h sbt_buffer;
#if xg_vk_enable_nv_raytracing_ext_m
    VkDeviceSize sbt_gen_offset;
    VkDeviceSize sbt_miss_offset;
    VkDeviceSize sbt_miss_stride;
    VkDeviceSize sbt_hit_offset;
    VkDeviceSize sbt_hit_stride;
#else
    VkStridedDeviceAddressRegionKHR sbt_gen_region;
    VkStridedDeviceAddressRegionKHR sbt_miss_region;
    VkStridedDeviceAddressRegionKHR sbt_hit_region;
#endif
    xg_raytrace_pipeline_state_t state;
} xg_vk_raytrace_pipeline_t;

typedef struct {
    VkDescriptorSet vk_handle;
    xg_resource_bindings_layout_h layout;
} xg_vk_resource_bindings_t;

typedef struct {
    // TODO use a pipeline cache? is the manual PSO caching even needed anymore at that point?
    //      current load times observed:
    //          vk caching (no prebuilt PSO load):  t
    //          vk caching + manual caching:        t - 0.1s
    //          manual caching only:                t - 0.2s
    //VkPipelineCache vk_pipeline_cache;
    VkDescriptorPool vk_desc_pool;
    xg_vk_resource_bindings_t groups_array[xg_vk_max_pipeline_resource_groups_m];
    xg_vk_resource_bindings_t* groups_freelist;
    std_mutex_t groups_mutex;
} xg_vk_pipeline_device_context_t;

typedef struct {
    // Graphics pipelines
    xg_vk_graphics_pipeline_t* graphics_pipelines_array;
    xg_vk_graphics_pipeline_t* graphics_pipelines_freelist;
    std_hash_map_t graphics_pipelines_map;       // hash -> handle

    // Compute pipelines
    xg_vk_compute_pipeline_t* compute_pipelines_array;
    xg_vk_compute_pipeline_t* compute_pipelines_freelist;
    std_hash_map_t compute_pipelines_map;

    // Raytrace pipelines
    xg_vk_raytrace_pipeline_t* raytrace_pipelines_array;
    xg_vk_raytrace_pipeline_t* raytrace_pipelines_freelist;
    std_hash_map_t raytrace_pipelines_map;

    // Renderpasses
    xg_vk_renderpass_t* renderpasses_array;
    xg_vk_renderpass_t* renderpasses_freelist;
    uint64_t* renderpasses_bitset;

    // Resource set layouts
    xg_vk_resource_bindings_layout_t* resource_bindings_layouts_array;
    xg_vk_resource_bindings_layout_t* resource_bindings_layouts_freelist;
    std_hash_map_t resource_bindings_layouts_map;     // uint64_t hash -> xg_resource_bindings_layout_h
    uint64_t* resource_bindings_layouts_bitset;

    // Device contexts
    xg_vk_pipeline_device_context_t device_contexts[xg_max_active_devices_m];
} xg_vk_pipeline_state_t;

void xg_vk_pipeline_load ( xg_vk_pipeline_state_t* state );
void xg_vk_pipeline_reload ( xg_vk_pipeline_state_t* state );
void xg_vk_pipeline_unload ( void );

const xg_vk_graphics_pipeline_t* xg_vk_graphics_pipeline_get ( xg_graphics_pipeline_state_h pipeline );
const xg_vk_compute_pipeline_t* xg_vk_compute_pipeline_get ( xg_compute_pipeline_state_h pipeline );
const xg_vk_raytrace_pipeline_t* xg_vk_raytrace_pipeline_get ( xg_raytrace_pipeline_state_h pipeline );
const xg_vk_renderpass_t* xg_vk_renderpass_get ( xg_renderpass_h renderpass_handle );

xg_renderpass_h xg_vk_renderpass_create ( const xg_renderpass_params_t* params );
void xg_vk_renderpass_destroy ( xg_renderpass_h renderpass );

xg_graphics_pipeline_state_h xg_vk_graphics_pipeline_create ( xg_device_h device, const xg_graphics_pipeline_params_t* params );
void xg_vk_graphics_pipeline_destroy ( xg_graphics_pipeline_state_h pipeline );

xg_compute_pipeline_state_h xg_vk_compute_pipeline_create ( xg_device_h device, const xg_compute_pipeline_params_t* params );
void xg_vk_compute_pipeline_destroy ( xg_compute_pipeline_state_h pipeline );

xg_raytrace_pipeline_state_h xg_vk_raytrace_pipeline_create ( xg_device_h device, const xg_raytrace_pipeline_params_t* params );
void xg_vk_raytrace_pipeline_destroy ( xg_raytrace_pipeline_state_h pipeline );

xg_resource_bindings_layout_h xg_vk_pipeline_resource_bindings_layout_create ( const xg_resource_bindings_layout_params_t* params );
void xg_vk_pipeline_resource_binding_set_layouts_get ( xg_resource_bindings_layout_h* layouts, xg_pipeline_state_h pipeline );
xg_resource_bindings_layout_h xg_vk_pipeline_resource_binding_set_layout_get ( xg_pipeline_state_h pipeline, xg_shader_binding_set_e set );
void xg_vk_pipeline_resource_bindings_layout_destroy ( xg_resource_bindings_layout_h layout );
xg_vk_resource_bindings_layout_t* xg_vk_pipeline_resource_bindings_layout_get ( xg_resource_bindings_layout_h layout );

void xg_vk_pipeline_activate_device ( xg_device_h device );
void xg_vk_pipeline_deactivate_device ( xg_device_h device );

xg_vk_pipeline_common_t* xg_vk_common_pipeline_get ( xg_pipeline_state_h );

// For creating a reusable binding set
// TODO rename resource_group to something else? feels too generic. bindset? preset?
xg_resource_bindings_h xg_vk_pipeline_create_resource_bindings ( xg_resource_bindings_layout_h layout );
void xg_vk_pipeline_update_resource_bindings ( xg_device_h device, xg_resource_bindings_h group, const xg_pipeline_resource_bindings_t* bindings );
void xg_vk_pipeline_destroy_resource_bindings ( xg_device_h device, xg_resource_bindings_h group );
const xg_vk_resource_bindings_t* xg_vk_pipeline_resource_group_get ( xg_device_h device, xg_resource_bindings_h group );

void xg_vk_pipeline_get_info ( xg_pipeline_info_t* info, xg_pipeline_state_h pipeline_handle );
