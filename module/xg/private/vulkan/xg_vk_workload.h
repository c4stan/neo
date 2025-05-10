#pragma once

#include <xg.h>

#include "xg_vk.h"

#include "xg_vk_device.h"
#include "xg_cmd_buffer.h"
#include "xg_vk_pipeline.h"

typedef uint64_t xg_queue_event_h;
typedef uint64_t xg_cpu_queue_event_h;

typedef struct {
    void* next; // used by the freelist
    xg_buffer_h handle;
    xg_alloc_t alloc;
    uint64_t used_size;
    uint64_t total_size;
} xg_vk_workload_buffer_t;

#if 0
typedef struct {
    xg_timestamp_query_pool_h handle;
    std_buffer_t buffer;
} xg_vk_workload_query_pool_t;
#endif

// To be popped from the global freelist as needed. Needs to be replaced once full.
// The workload stores the used ones.
typedef struct {
    void* next; // used by the freelist
    VkDescriptorPool vk_desc_pool;
    uint32_t set_count;
    int32_t descriptor_counts[xg_resource_binding_count_m];
} xg_vk_desc_allocator_t;

// Technically would be enough to have one per cmd recording thread per workload.
// In practice, ideally the cmd buffers are pre-allocated and reused. Which means there's a limit on
// the number of cmd buffers linked to a cmd pool, even though VK doesn't actually have a hard limit on it.
// So one alternative is to have a global freelist here too, and have threads pop cmd allocators
// from there, and replace it in case they run out of pre-allocated cmd buffers.
typedef struct {
    void* next;
    VkCommandPool vk_cmd_pool;
    VkCommandBuffer vk_cmd_buffers[xg_vk_workload_cmd_buffers_per_allocator_m];
    size_t cmd_buffers_count; // number of used cmd buffers
} xg_vk_cmd_allocator_t;


typedef struct {
    xg_cmd_buffer_h cmd_buffers[xg_cmd_buffer_max_cmd_buffers_per_workload_m];
    xg_resource_cmd_buffer_h resource_cmd_buffers[xg_cmd_buffer_max_resource_cmd_buffers_per_workload_m];
    size_t cmd_buffers_count;
    size_t resource_cmd_buffers_count;
    
    xg_device_h device;
    uint64_t id;
    uint64_t gen;

    // wait on before starting the workload. part of VkSubmitInfo
    xg_queue_event_h swapchain_texture_acquired_event;
    // signal at the very end of the workload. part of VkSubmitInfo
    xg_queue_event_h execution_complete_gpu_event;
    // for cpu to check workload completion. TODO currently never used
    xg_cpu_queue_event_h execution_complete_cpu_event;

    bool stop_debug_capture_on_present;

    xg_vk_workload_buffer_t* uniform_buffer;
    xg_vk_workload_buffer_t* uniform_buffers_array[xg_vk_workload_max_uniform_buffers_per_workload_m];
    uint32_t uniform_buffers_count;

    xg_vk_workload_buffer_t* staging_buffer;
    xg_vk_workload_buffer_t* staging_buffers_array[xg_vk_workload_max_staging_buffers_per_workload_m];
    uint32_t staging_buffers_count;

    xg_vk_desc_allocator_t* desc_allocator;
    xg_vk_desc_allocator_t* desc_allocators_array[xg_vk_workload_max_desc_allocators_per_workload_m];
    uint32_t desc_allocators_count;

#if 0
    xg_vk_cmd_allocator_t* graphics_cmd_allocators_array[xg_vk_workload_max_graphics_cmd_allocators_m];
    xg_vk_cmd_allocator_t* compute_cmd_allocators_array[xg_vk_workload_max_graphics_cmd_allocators_m];
    xg_vk_cmd_allocator_t* copy_cmd_allocators_array[xg_vk_workload_max_graphics_cmd_allocators_m];
    xg_vk_cmd_allocator_t* cmd_allocators_array[xg_cmd_queue_count_m];
    uint32_t cmd_allocators_count[xg_cmd_queue_count_m];
#endif

    xg_resource_bindings_layout_h desc_layouts_array[xg_vk_workload_max_resource_bindings_m];
    uint32_t desc_layouts_count;
    VkDescriptorSet desc_sets_array[xg_vk_workload_max_resource_bindings_m];

    //xg_vk_workload_query_pool_t timestamp_query_pools[xg_workload_max_timestamp_query_pools_per_workload_m];
    //uint32_t timestamp_query_pools_count;

    xg_resource_bindings_h global_bindings;

    bool debug_capture;
} xg_vk_workload_t;

typedef struct {
    xg_cmd_header_t* result;
    xg_cmd_header_t* temp;
    uint64_t capacity;
} xg_vk_workload_sort_context_t;

typedef struct {
    uint32_t begin;
    uint32_t end;
    xg_cmd_queue_e queue;
} xg_vk_workload_cmd_chunk_t;

#define xg_vk_workload_cmd_chunk_m( ... ) ( xg_vk_workload_cmd_chunk_t ) { \
    .begin = -1, \
    .end = -1, \
    .queue = xg_cmd_queue_graphics_m, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t begin;
    uint32_t end;
    xg_cmd_queue_e queue;
    xg_queue_event_h signal_events[xg_cmd_bind_queue_max_signal_events_m];
    xg_queue_event_h wait_events[xg_cmd_bind_queue_max_wait_events_m];
    xg_pipeline_stage_bit_e wait_stages[xg_cmd_bind_queue_max_wait_events_m];
    uint32_t wait_count;
    uint32_t signal_count;
} xg_vk_workload_queue_chunk_t;

#define xg_vk_workload_queue_chunk_m( ... ) ( xg_vk_workload_queue_chunk_t ) { \
    .begin = -1, \
    .end = -1, \
    .queue = xg_cmd_queue_graphics_m, \
    .signal_events = { [0 ... xg_cmd_bind_queue_max_signal_events_m-1] = xg_null_handle_m }, \
    .wait_events = { [0 ... xg_cmd_bind_queue_max_wait_events_m-1] = xg_null_handle_m }, \
    .wait_stages = { [0 ... xg_cmd_bind_queue_max_wait_events_m-1] = xg_pipeline_stage_bit_none_m }, \
    ##__VA_ARGS__ \
}

typedef struct {
    xg_vk_workload_cmd_chunk_t* cmd_chunks_array;
    xg_vk_workload_queue_chunk_t* queue_chunks_array;
    uint32_t cmd_chunks_capacity;
    uint32_t queue_chunks_capacity;
} xg_vk_workload_chunk_context_t;

typedef struct {
    xg_vk_cmd_allocator_t cmd_allocators[xg_cmd_queue_count_m];
    VkCommandBuffer* translated_chunks_array;
    uint32_t translated_chunks_capacity;
} xg_vk_workload_translate_context_t;

typedef struct {
    xg_queue_event_h events_array[xg_vk_workload_max_queue_chunks_m]; // TODO grow dynamically instead?
    uint32_t events_count;
} xg_vk_workload_submit_context_t;

typedef struct {
    xg_workload_h workload;
    bool is_submitted; // TODO remove?

    xg_vk_workload_sort_context_t sort;
    xg_vk_workload_chunk_context_t chunk;
    //xg_vk_workload_translate_context_t* translate_array[xg_vk_workload_max_translate_contexts_per_submit_m];
    xg_vk_workload_translate_context_t translate;
    xg_vk_workload_submit_context_t submit;

    // Translation context
    //xg_cmd_header_t* cmd_headers;
    //size_t cmd_headers_count;
    //xg_vk_cmd_allocator_t cmd_allocator;
    //xg_vk_desc_allocator_t* desc_allocator;
} xg_vk_workload_context_t;

typedef struct {
    bool is_active;
    xg_device_h device_handle;
    xg_vk_workload_context_t* workload_contexts_array;
    std_ring_t workload_contexts_ring;

    // TODO mutex guard (or atomic pop from an array of free indices?)
    xg_vk_workload_buffer_t* staging_buffers_array;
    xg_vk_workload_buffer_t* staging_buffers_freelist;
    xg_vk_workload_buffer_t* uniform_buffers_array;
    xg_vk_workload_buffer_t* uniform_buffers_freelist;

    xg_vk_desc_allocator_t* desc_allocators_array;
    xg_vk_desc_allocator_t* desc_allocators_freelist;
    xg_vk_cmd_allocator_t* cmd_allocators_array[xg_cmd_queue_count_m];
    xg_vk_cmd_allocator_t* cmd_allocators_freelist[xg_cmd_queue_count_m];
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
xg_buffer_range_t xg_workload_write_staging ( xg_workload_h workload, void* data, size_t size );
xg_resource_bindings_h xg_workload_create_resource_group ( xg_workload_h workload, xg_resource_bindings_layout_h layout_handle );
void xg_workload_set_global_resource_group ( xg_workload_h workload, xg_resource_bindings_h group );

bool xg_workload_is_complete ( xg_workload_h workload );

const xg_vk_workload_t* xg_vk_workload_get ( xg_workload_h workload );
void xg_vk_workload_queue_debug_capture_stop_on_present ( xg_workload_h workload );
#if 0
    void xg_vk_workload_add_timestamp_query_pool ( xg_workload_h workload, xg_timestamp_query_pool_h pool, std_buffer_t buffer );
#endif
// TODO prefix these with xg_vk ?
//void xg_workload_set_swapchain_texture_acquired_event ( xg_workload_h workload, xg_queue_event_h event );
void xg_workload_set_execution_complete_gpu_event ( xg_workload_h workload, xg_queue_event_h event );
void xg_workload_init_swapchain_texture_acquired_gpu_event ( xg_workload_h workload );
void xg_vk_workload_activate_device ( xg_device_h device );
void xg_vk_workload_deactivate_device ( xg_device_h device );

void xg_workload_wait_all_workload_complete ( void );

typedef struct {
    xg_workload_h workload;
    VkCommandBuffer cmd_buffer;
    void* impl;
} xg_vk_workload_context_inline_t;

xg_vk_workload_context_inline_t xg_vk_workload_context_create_inline ( xg_device_h device_handle );
void xg_vk_workload_context_submit_inline ( xg_vk_workload_context_inline_t* context );

void xg_vk_workload_enable_debug_capture ( xg_workload_h workload );
