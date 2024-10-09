#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_mutex.h>
#include <std_hash.h>
#include <std_allocator.h>

typedef uint64_t xg_vk_renderpass_h;
typedef uint64_t xg_vk_framebuffer_h;
typedef uint64_t xg_vk_descriptor_set_layout_h;

typedef struct {
    uint32_t shader_register;
    xg_shading_stage_bit_e stages;
    xg_resource_binding_e type;
} xg_vk_descriptor_slot_t;

typedef struct {
    xg_device_h device;
    VkDescriptorSetLayout vk_handle;
    uint64_t hash;
    size_t descriptor_count;
    xg_vk_descriptor_slot_t descriptors[xg_pipeline_resource_max_bindings_per_set_m];
    uint16_t shader_register_to_descriptor_idx[xg_pipeline_resource_max_bindings_per_set_m];
    uint32_t ref_count;
} xg_vk_descriptor_set_layout_t;

typedef struct {
    xg_device_h device;
    VkRenderPass vk_renderpass;
    uint64_t hash;
    xg_render_textures_layout_t render_textures_layout;
    uint32_t ref_count;
} xg_vk_renderpass_t;

typedef struct {
    xg_device_h device;
    VkFramebuffer vk_handle;
    uint64_t hash;
    uint64_t ref_count;
} xg_vk_framebuffer_t;

typedef struct {
    xg_vk_renderpass_h renderpass_handle;
    xg_vk_framebuffer_h framebuffer_handle;
    xg_graphics_renderpass_params_t params;
} xg_vk_graphics_renderpass_t;

/*
    When creating a pipeline the bindings provided each have a 'set' field. The pipeline instead stores the bindings data per-set.

    TODO
        hash descriptor sets layouts similarly to renderpasses, storing them in a separate pool and sharing them between pipes
        at cmd parse check layout to be bound vs current bound by handle/hash, avoid binding again if same ?
*/

/*
    bindless framebuffer -> framebuffer can be bundled with renderpass to create a "render target config"
    user only has render target and pipeline state
    when binding pipeline state check render target for compatibility
    translate render targets to primary cmd buffer and pipelines to secondary (?)
*/

typedef struct {
    VkPipeline vk_handle;
    VkPipelineLayout vk_layout_handle;
    VkShaderModule vk_shader_handles[xg_shading_stage_count_m];

    xg_vk_descriptor_set_layout_h descriptor_set_layouts[xg_resource_binding_set_count_m];
    uint64_t push_constants_hash;

    uint64_t hash;
    xg_device_h device_handle;

    uint32_t reference_count;

    char debug_name[xg_debug_name_size_m];
} xg_vk_pipeline_common_t;

typedef struct {
    xg_vk_pipeline_common_t common;
    xg_vk_renderpass_h renderpass;
    xg_graphics_pipeline_state_t state;
} xg_vk_graphics_pipeline_t;

typedef struct {
    xg_vk_pipeline_common_t common;
    xg_compute_pipeline_state_t state;
} xg_vk_compute_pipeline_t;

typedef struct {
    xg_vk_pipeline_common_t common;
    //xg_buffer_h sbt_buffer;
    //VkStridedDeviceAddressRegionKHR sbt_raygen_region;
    //VkStridedDeviceAddressRegionKHR sbt_miss_region;
    //VkStridedDeviceAddressRegionKHR sbt_hit_region;
    void* sbt_handle_buffer;
    // binding -> handle buffer offset
    uint32_t gen_offsets[xg_raytrace_shader_state_max_gen_shaders_m];
    uint32_t miss_offsets[xg_raytrace_shader_state_max_miss_shaders_m];
    uint32_t hit_offsets[xg_raytrace_shader_state_max_hit_groups_m];
    VkStridedDeviceAddressRegionKHR sbt_gen_region;
    VkStridedDeviceAddressRegionKHR sbt_miss_region;
    VkStridedDeviceAddressRegionKHR sbt_hit_region;
    xg_raytrace_pipeline_state_t state;
} xg_vk_raytrace_pipeline_t;

// TODO rename to pipeline_resource_preset
typedef struct {
    VkDescriptorSet vk_set;
    xg_vk_descriptor_set_layout_h layout;
} xg_vk_pipeline_resource_group_t;

typedef struct {
    // TODO use a pipeline cache? is the manual PSO caching even needed anymore at that point?
    //      current load times observed:
    //          vk caching (no prebuilt PSO load):  t
    //          vk caching + manual caching:        t - 0.1s
    //          manual caching only:                t - 0.2s
    //VkPipelineCache vk_pipeline_cache;
    VkDescriptorPool vk_desc_pool;
    xg_vk_pipeline_resource_group_t groups_array[xg_vk_max_pipeline_resource_groups_m];
    xg_vk_pipeline_resource_group_t* groups_freelist;
    std_mutex_t groups_mutex;
} xg_vk_pipeline_device_context_t;

typedef struct {
    // Graphics pipelines
    xg_vk_graphics_pipeline_t* graphics_pipelines_array;
    xg_vk_graphics_pipeline_t* graphics_pipelines_freelist;
    std_mutex_t graphics_pipelines_freelist_mutex;
    std_hash_map_t graphics_pipelines_map;       // hash -> handle
    std_mutex_t graphics_pipelines_map_mutex;

    // Compute pipelines
    xg_vk_compute_pipeline_t* compute_pipelines_array;
    xg_vk_compute_pipeline_t* compute_pipelines_freelist;
    std_mutex_t compute_pipelines_freelist_mutex;
    std_hash_map_t compute_pipelines_map;
    std_mutex_t compute_pipelines_map_mutex;

    // Raytrace pipelines
    xg_vk_raytrace_pipeline_t* raytrace_pipelines_array;
    xg_vk_raytrace_pipeline_t* raytrace_pipelines_freelist;
    std_mutex_t raytrace_pipelines_freelist_mutex;
    std_hash_map_t raytrace_pipelines_map;
    std_mutex_t raytrace_pipelines_map_mutex;

    // Renderpasses
    xg_vk_renderpass_t* renderpasses_array;
    xg_vk_renderpass_t* renderpasses_freelist;
    std_mutex_t renderpasses_freelist_mutex;
    std_hash_map_t renderpasses_map;    // hash -> handle
    std_mutex_t renderpasses_map_mutex;

    // Resource set layouts
    xg_vk_descriptor_set_layout_t* set_layouts_array;
    xg_vk_descriptor_set_layout_t* set_layouts_freelist;
    std_hash_map_t set_layouts_map;     // uint64_t hash -> xg_vk_descriptor_set_layout_h
    std_mutex_t set_layouts_mutex;

    // Framebuffers
    xg_vk_framebuffer_t* framebuffers_array;
    xg_vk_framebuffer_t* framebuffers_freelist;
    std_mutex_t framebuffers_freelist_mutex;
    std_hash_map_t framebuffers_map;    // hash -> handle
    std_mutex_t framebuffers_map_mutex;

    // Graphics renderpasses
    xg_vk_graphics_renderpass_t* graphics_renderpass_array;
    xg_vk_graphics_renderpass_t* graphics_renderpass_freelist;
    uint64_t* graphics_renderpass_bitset;

    // Device contexts
    xg_vk_pipeline_device_context_t device_contexts[xg_max_active_devices_m];
} xg_vk_pipeline_state_t;

void xg_vk_pipeline_load ( xg_vk_pipeline_state_t* state );
void xg_vk_pipeline_reload ( xg_vk_pipeline_state_t* state );
void xg_vk_pipeline_unload ( void );

const xg_vk_graphics_pipeline_t* xg_vk_graphics_pipeline_get ( xg_graphics_pipeline_state_h pipeline );
const xg_vk_compute_pipeline_t* xg_vk_compute_pipeline_get ( xg_compute_pipeline_state_h pipeline );
const xg_vk_raytrace_pipeline_t* xg_vk_raytrace_pipeline_get ( xg_raytrace_pipeline_state_h pipeline );
const xg_vk_renderpass_t* xg_vk_renderpass_get ( xg_vk_renderpass_h renderpass_handle );
const xg_vk_framebuffer_t* xg_vk_framebuffer_get ( xg_vk_framebuffer_h framebuffer_handle );

xg_graphics_pipeline_state_h xg_vk_graphics_pipeline_create ( xg_device_h device, const xg_graphics_pipeline_params_t* params );
void xg_vk_graphics_pipeline_destroy ( xg_graphics_pipeline_state_h pipeline );

xg_renderpass_h xg_vk_graphics_renderpass_create ( const xg_graphics_renderpass_params_t* params );
void xg_vk_graphics_renderpass_destroy ( xg_renderpass_h renderpass );
const xg_vk_graphics_renderpass_t* xg_vk_graphics_renderpass_get ( xg_renderpass_h renderpass );

xg_compute_pipeline_state_h xg_vk_compute_pipeline_create ( xg_device_h device, const xg_compute_pipeline_params_t* params );
void xg_vk_compute_pipeline_destroy ( xg_compute_pipeline_state_h pipeline );

xg_raytrace_pipeline_state_h xg_vk_raytrace_pipeline_create ( xg_device_h device, const xg_raytrace_pipeline_params_t* params );
void xg_vk_raytrace_pipeline_destroy ( xg_raytrace_pipeline_state_h pipeline );

const xg_vk_descriptor_set_layout_t* xg_vk_descriptor_set_layout_get ( xg_vk_descriptor_set_layout_h handle );

xg_vk_framebuffer_h xg_vk_framebuffer_acquire ( xg_device_h device_handle, xg_vk_renderpass_h renderpass, uint32_t width, uint32_t height );
void xg_vk_framebuffer_release ( xg_vk_framebuffer_h framebuffer );

void xg_vk_pipeline_activate_device ( xg_device_h device );
void xg_vk_pipeline_deactivate_device ( xg_device_h device );

// For creating a reusable binding set
// TODO rename resource_group to something else? feels too generic. bindset? preset?
xg_pipeline_resource_group_h xg_vk_pipeline_create_resource_group ( xg_device_h device, xg_pipeline_state_h pipeline, xg_resource_binding_set_e set );
void xg_vk_pipeline_update_resource_group ( xg_device_h device, xg_pipeline_resource_group_h group, const xg_pipeline_resource_bindings_t* bindings );
void xg_vk_pipeline_destroy_resource_group ( xg_device_h device, xg_pipeline_resource_group_h group );
const xg_vk_pipeline_resource_group_t* xg_vk_pipeline_resource_group_get ( xg_device_h device, xg_pipeline_resource_group_h group );
