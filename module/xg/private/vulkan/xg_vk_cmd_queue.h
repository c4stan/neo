#pragma once

#include "xg_vk.h"

typedef struct {
    VkCommandPool vk_cmd_pool;
    VkCommandBuffer vk_cmd_buffers[xg_vk_cmd_buffer_cmd_buffers_per_allocator_m];
    size_t cmd_buffers_count;
} xg_vk_cmd_allocator_t;

typedef struct {
    VkDescriptorPool vk_desc_pool;
    uint32_t set_count;
    int32_t descriptor_counts[xg_resource_binding_count_m];
} xg_vk_desc_allocator_t;

typedef struct {
    xg_vk_cmd_allocator_t cmd_allocator;
    xg_vk_desc_allocator_t desc_allocator;
} xg_vk_cmd_recorder_t;

typedef struct {
    xg_workload_h workload;
    xg_vk_cmd_recorder_t* recorder;
} xg_vk_cmd_queue_submit_context_t;

typedef struct {
    std_ring_t ring;
    xg_vk_cmd_queue_submit_context_t* submit_contexts[xg_vk_cmd_queue_size_m];
    xg_vk_cmd_recorder_t* cmd_recorder_array;
    xg_vk_cmd_recorder_t* cmd_recorder_freelist;
} xg_vk_cmd_queue_device_context_t;

typedef struct {
    xg_vk_cmd_queue_device_context_t device_contexts[xg_max_active_devices_m];
} xg_vk_cmd_queue_state_t;
