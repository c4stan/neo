#pragma once

#include <xg.h>

#include "xg_vk.h"
#include "xg_vk_device.h"
#include "xg_cmd_buffer.h"

typedef struct {
    VkCommandPool vk_cmd_pool;
    VkCommandBuffer vk_cmd_buffers[xg_vk_cmd_buffer_cmd_buffers_per_allocator_m];
    size_t cmd_buffers_count;
} xg_vk_cmd_allocator_t;

#define xg_vk_max_total_descriptors_per_pool_m ( \
      xg_vk_max_samplers_per_descriptor_pool_m \
    + xg_vk_max_sampled_texture_per_descriptor_pool_m \
    + xg_vk_max_storage_texture_per_descriptor_pool_m \
    + xg_vk_max_uniform_buffer_per_descriptor_pool_m \
    + xg_vk_max_storage_buffer_per_descriptor_pool_m \
    + xg_vk_max_uniform_texel_buffer_per_descriptor_pool_m \
    + xg_vk_max_storage_texel_buffer_per_descriptor_pool_m \
)

typedef struct {
    VkDescriptorPool vk_desc_pool;
    uint32_t set_count;
    int32_t descriptor_counts[7]; // indexed by xg_resource_binding_e
    //VkDescriptorSetLayoutBinding bindings[xg_vk_max_total_descriptors_per_pool_m];
    //VkWriteDescriptorSet writes[xg_vk_max_total_descriptors_per_pool_m];
} xg_vk_desc_allocator_t;

typedef struct {
    xg_workload_h workload;
    bool is_submitted;

    // Translation context
    xg_cmd_header_t* cmd_headers;
    size_t cmd_headers_count;
    xg_vk_cmd_allocator_t cmd_allocator;
    xg_vk_desc_allocator_t desc_allocator;
} xg_vk_cmd_buffer_submit_context_t;

typedef struct xg_vk_cmd_buffer_device_context_t {
    bool is_active;

    xg_device_h device_handle;

    xg_vk_cmd_buffer_submit_context_t* submit_contexts;
    std_ring_t submit_ring;
    std_mutex_t submit_contexts_mutex;
} xg_vk_cmd_buffer_device_context_t;

typedef struct {
    xg_vk_cmd_buffer_device_context_t device_contexts[xg_vk_max_active_devices_m];
    std_mutex_t device_contexts_mutex;
} xg_vk_cmd_buffer_state_t;

void xg_vk_cmd_buffer_load ( xg_vk_cmd_buffer_state_t* state );
void xg_vk_cmd_buffer_reload ( xg_vk_cmd_buffer_state_t* state );
void xg_vk_cmd_buffer_unload ( void );

void xg_vk_cmd_buffer_activate_device ( xg_device_h device_handle );
void xg_vk_cmd_buffer_deactivate_device ( xg_device_h device_handle );

void xg_vk_cmd_buffer_submit ( xg_workload_h workload );
