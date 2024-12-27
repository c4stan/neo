#pragma once

#include <xg.h>

#include "xg_vk.h"

#include "xg_vk_device.h"
#include "xg_cmd_buffer.h"
#include "xg_vk_pipeline.h"

// A workload is the smallest unit of GPU work that can be submitted for execution and checked for completion.

typedef uint64_t xg_gpu_queue_event_h;
typedef uint64_t xg_cpu_queue_event_h;

typedef struct {
    xg_buffer_h handle;
    xg_alloc_t alloc;
    //char* base;
    uint64_t used_size;
    uint64_t total_size;
} xg_vk_workload_uniform_buffer_t;

#if 0
typedef struct {
    xg_timestamp_query_pool_h handle;
    std_buffer_t buffer;
} xg_vk_workload_query_pool_t;
#endif

typedef struct {
    void* next; // used by the freelist
    VkDescriptorPool vk_desc_pool;
    uint32_t set_count;
    int32_t descriptor_counts[xg_resource_binding_count_m];
} xg_vk_desc_allocator_t;

typedef struct {
    xg_cmd_buffer_h cmd_buffers[xg_cmd_buffer_max_cmd_buffers_per_workload_m];
    xg_resource_cmd_buffer_h resource_cmd_buffers[xg_cmd_buffer_max_resource_cmd_buffers_per_workload_m];
    size_t cmd_buffers_count;
    size_t resource_cmd_buffers_count;
    
    xg_device_h device;
    uint64_t id;
    uint64_t gen;

    xg_gpu_queue_event_h swapchain_texture_acquired_event; // wait on before starting the workload. part of VkSubmitInfo
    xg_gpu_queue_event_h execution_complete_gpu_event; // signal at the very end of the workload. part of VkSubmitInfo
    xg_cpu_queue_event_h execution_complete_cpu_event; // for cpu to check workload completion. TODO currently never used

    bool stop_debug_capture_on_present;

    //xg_vk_desc_allocator_t desc_allocator;
    xg_vk_workload_uniform_buffer_t uniform_buffer;
    xg_vk_framebuffer_h framebuffer;
    xg_vk_desc_allocator_t* desc_allocator;

    xg_vk_desc_allocator_t* desc_allocators_array[xg_vk_workload_max_desc_allocators_per_workload_m];
    uint32_t desc_allocators_count;

    xg_vk_descriptor_set_layout_h desc_layouts_array[xg_vk_workload_max_resource_groups_m];
    uint32_t desc_layouts_count;
    VkDescriptorSet desc_sets_array[xg_vk_workload_max_resource_groups_m];

    //xg_vk_workload_query_pool_t timestamp_query_pools[xg_workload_max_timestamp_query_pools_per_workload_m];
    //uint32_t timestamp_query_pools_count;
} xg_vk_workload_t;

typedef struct {
    VkCommandPool vk_cmd_pool;
    VkCommandBuffer vk_cmd_buffers[xg_vk_workload_cmd_buffers_per_allocator_m];
    size_t cmd_buffers_count;
} xg_vk_cmd_allocator_t;

typedef struct {
    xg_cmd_header_t* result;
    xg_cmd_header_t* temp;
    uint64_t capacity;
} xg_vk_workload_sort_context_t;

typedef struct {
    xg_workload_h workload;
    bool is_submitted;

    xg_vk_workload_sort_context_t sort;

    // Translation context
    xg_cmd_header_t* cmd_headers;
    size_t cmd_headers_count;
    xg_vk_cmd_allocator_t cmd_allocator;
    xg_vk_desc_allocator_t* desc_allocator;
} xg_vk_workload_submit_context_t;

typedef struct {
    bool is_active;
    xg_device_h device_handle;
    xg_vk_workload_submit_context_t* submit_contexts_array;
    std_ring_t submit_contexts_ring;
    xg_vk_desc_allocator_t* desc_allocators_array;
    xg_vk_desc_allocator_t* desc_allocators_freelist; // TODO mutex guard
} xg_vk_workload_device_context_t;

typedef struct {
    xg_vk_workload_t* workload_array;
    xg_vk_workload_t* workload_freelist;
    uint64_t workloads_uid;

    xg_vk_workload_device_context_t device_contexts[xg_max_active_devices_m];
} xg_vk_workload_state_t;

void xg_vk_workload_load ( xg_vk_workload_state_t* state );
void xg_vk_workload_reload ( xg_vk_workload_state_t* state );
void xg_vk_workload_unload ( void );

xg_workload_h xg_workload_create ( xg_device_h device );
void xg_workload_destroy ( xg_workload_h workload );
void xg_workload_submit ( xg_workload_h workload_handle );

xg_cmd_buffer_h xg_workload_add_cmd_buffer ( xg_workload_h workload );
void xg_workload_remove_cmd_buffer ( xg_workload_h workload, xg_cmd_buffer_h cmd_buffer );
xg_resource_cmd_buffer_h xg_workload_add_resource_cmd_buffer ( xg_workload_h workload );
void xg_workload_remove_resource_cmd_buffer ( xg_workload_h workload, xg_resource_cmd_buffer_h cmd_buffer );

xg_buffer_range_t xg_workload_write_uniform ( xg_workload_h workload, void* data, size_t size );
xg_pipeline_resource_group_h xg_workload_create_resource_group ( xg_workload_h workload, xg_pipeline_state_h pipeline, xg_shader_binding_set_e set );

bool xg_workload_is_complete ( xg_workload_h workload );

const xg_vk_workload_t* xg_vk_workload_get ( xg_workload_h workload );
void xg_vk_workload_queue_debug_capture_stop_on_present ( xg_workload_h workload );
#if 0
    void xg_vk_workload_add_timestamp_query_pool ( xg_workload_h workload, xg_timestamp_query_pool_h pool, std_buffer_t buffer );
#endif
// TODO prefix these with xg_vk ?
//void xg_workload_set_swapchain_texture_acquired_event ( xg_workload_h workload, xg_gpu_queue_event_h event );
void xg_workload_set_execution_complete_gpu_event ( xg_workload_h workload, xg_gpu_queue_event_h event );
void xg_workload_init_swapchain_texture_acquired_gpu_event ( xg_workload_h workload );
void xg_vk_workload_activate_device ( xg_device_h device );
void xg_vk_workload_deactivate_device ( xg_device_h device );

void xg_workload_wait_all_workload_complete ( void );

// TODO clean up this ugly stuff..
xg_vk_workload_submit_context_t* xg_vk_workload_submit_context_create ( xg_workload_h workload );
void xg_vk_workload_submit_context_submit ( xg_vk_workload_submit_context_t* context );
