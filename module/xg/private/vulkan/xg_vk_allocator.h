#pragma once

#include <xg.h>

/*
    TODO
        handle properly separating linear and tiled resources, see https://asawicki.info/articles/memory_management_vulkan_direct3d_12.php5 "Proximity requirements"
*/

#include "xg_vk.h"

#include <std_mutex.h>
#include <std_queue.h>

typedef struct {
    uint64_t offset;
    uint64_t size;
    bool free;
    // tlsf freelist next and prev segments
    void* next;
    void* prev;
    // memory adjacent segments
    void* right;
    void* left;
    bool retired;
} xg_vk_allocator_tlsf_segment_t;

#define xg_vk_allocator_tlsf_min_x_level_m 10
#define xg_vk_allocator_tlsf_max_x_level_m 36
#define xg_vk_allocator_tlsf_x_size_m (xg_vk_allocator_tlsf_max_x_level_m - xg_vk_allocator_tlsf_min_x_level_m)
#define xg_vk_allocator_tlsf_y_size_m 16
#define xg_vk_allocator_tlsf_log2_y_size_m 4

// Very similar to the heap allocator used by std, with the main difference that it cannot use intrusive pointers for its freelist,
// so it has to store an additional array of pointers to gpu memory.
typedef struct {
    std_mutex_t mutex;
    xg_alloc_t gpu_alloc;
    uint64_t allocated_size;
    xg_vk_allocator_tlsf_segment_t* segments;
    xg_vk_allocator_tlsf_segment_t* unused_segments_freelist;
    uint64_t unused_segments_count;
    void* freelists[xg_vk_allocator_tlsf_x_size_m][xg_vk_allocator_tlsf_y_size_m];
    uint16_t available_freelists[xg_vk_allocator_tlsf_x_size_m];
    uint64_t available_rows;

    xg_memory_type_e memory_type;
    uint64_t device_idx;
} xg_vk_allocator_tlsf_heap_t;

typedef struct {
    xg_device_h device;
    VkDeviceMemory vk_handle;
    char debug_name[xg_debug_name_size_m];
} xg_vk_alloc_t;

typedef struct {
    xg_vk_allocator_tlsf_heap_t heaps[xg_memory_type_count_m];
} xg_vk_allocator_device_context_t;

typedef struct {
    xg_vk_alloc_t* allocations_array;
    xg_vk_alloc_t* allocations_freelist;
    std_mutex_t allocations_mutex;

    xg_vk_allocator_device_context_t device_contexts[xg_max_active_devices_m];
} xg_vk_allocator_state_t;

void xg_vk_allocator_load ( xg_vk_allocator_state_t* state );
void xg_vk_allocator_reload ( xg_vk_allocator_state_t* state );
void xg_vk_allocator_unload ( void );

void xg_vk_allocator_activate_device ( xg_device_h device );
void xg_vk_allocator_deactivate_device ( xg_device_h device );

void xg_vk_allocator_get_info ( xg_allocator_info_t* info, xg_device_h device, xg_memory_type_e type );

xg_alloc_t xg_alloc ( const xg_alloc_params_t* params );
void xg_free ( xg_memory_h handle );
