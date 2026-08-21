#pragma once

#include <xg.h>

/*
    TODO
        handle properly separating linear and tiled resources, see https://asawicki.info/articles/memory_management_vulkan_direct3d_12.php5 "Proximity requirements"
*/

#include "xg_vk.h"

#include <std_mutex.h>
#include <std_file.h>

typedef struct {
    xg_device_h device;
    VkDeviceMemory vk_handle;
    xg_memory_flag_bit_e memory_flags;
    uint64_t size;
    void* mapped_address;
    char debug_name[xg_debug_name_size_m];
} xg_vk_alloc_t;

#define xg_vk_alloc_m( ... ) ( xg_vk_alloc_t ) { \
    .device = xg_null_handle_m, \
    .vk_handle = VK_NULL_HANDLE, \
    .memory_flags = 0, \
    .mapped_address = NULL, \
    __VA_ARGS__ \
}

#if std_build_debug_m
typedef struct {
    std_alloc_scope_t scope;
    xg_memory_h user;
} xg_vk_allocator_debug_record_t;
#endif

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
#if std_build_debug_m
    xg_vk_allocator_debug_record_t* debug_record;
#endif
} xg_vk_allocator_tlsf_segment_t;

#define xg_vk_allocator_tlsf_min_x_level_m 14
#define xg_vk_allocator_tlsf_max_x_level_m 36
#define xg_vk_allocator_tlsf_x_size_m (xg_vk_allocator_tlsf_max_x_level_m - xg_vk_allocator_tlsf_min_x_level_m)
#define xg_vk_allocator_tlsf_y_size_m 16
#define xg_vk_allocator_tlsf_log2_y_size_m 4

// Very similar to the heap allocator used by std, with the main difference that it cannot use intrusive pointers for its freelist,
// so it has to store an additional array of pointers to gpu memory.
typedef struct {
    std_mutex_t mutex;

    xg_memory_type_e memory_type;
    uint64_t device_idx;

    xg_vk_alloc_t gpu_alloc;
    uint64_t allocated_size;

    xg_vk_allocator_tlsf_segment_t* segments;
    xg_vk_allocator_tlsf_segment_t* unused_segments_freelist;
    uint64_t unused_segments_count;
    void* freelists[xg_vk_allocator_tlsf_x_size_m][xg_vk_allocator_tlsf_y_size_m];
    uint16_t available_freelists[xg_vk_allocator_tlsf_x_size_m];
    uint64_t available_rows;

#if std_build_debug_m
    xg_vk_allocator_debug_record_t* debug_records_array;
    xg_vk_allocator_debug_record_t* debug_records_freelist;
    uint64_t* debug_records_bitset;
#endif
} xg_vk_allocator_tlsf_heap_t;

#define xg_vk_allocator_tlsf_heap_m( ... ) ( xg_vk_allocator_tlsf_heap_t ) { \
    .gpu_alloc = xg_vk_alloc_m(), \
    __VA_ARGS__ \
}

typedef struct {
    xg_memory_type_e memory_type;
    xg_device_h device;

    uint64_t grow_size;

    xg_vk_allocator_tlsf_heap_t* heaps_array;
    xg_vk_allocator_tlsf_heap_t* heaps_freelist;
    uint64_t* heaps_bitset;
    std_mutex_t heaps_mutex;

#if std_allocator_log_allocations_to_file_m
    std_file_h log_file;
    uint64_t log_count;
    uint64_t file_offset;
#endif
} xg_vk_allocator_tlsf_pool_t;

typedef struct {
    xg_memory_type_e memory_type;
    xg_device_h device;
    uint64_t device_idx;

    xg_vk_alloc_t* allocations_array;
    xg_vk_alloc_t* allocations_freelist;
    uint64_t* allocations_bitset;
    std_mutex_t allocations_mutex;

#if std_allocator_log_allocations_to_file_m
    std_file_h log_file;
    uint64_t log_count;
    uint64_t file_offset;
#endif
} xg_vk_allocator_reserved_pool_t; // TODO rename to dedicated_pool?

// TODO rename? typed_allocator? memory_type_allocator
typedef struct {
    bool enabled;
    xg_vk_allocator_tlsf_pool_t tlsf_pool;
    xg_vk_allocator_reserved_pool_t reserved_pool;
} xg_vk_allocator_global_t;

typedef struct {
    xg_vk_allocator_global_t global_allocators[xg_memory_type_count_m];
} xg_vk_allocator_device_context_t;

typedef struct {
    xg_vk_allocator_device_context_t device_contexts[xg_max_active_devices_m];
} xg_vk_allocator_state_t;

void xg_vk_allocator_load ( xg_vk_allocator_state_t* state );
void xg_vk_allocator_reload ( xg_vk_allocator_state_t* state );
void xg_vk_allocator_unload ( void );

void xg_vk_allocator_activate_device ( xg_device_h device );
void xg_vk_allocator_deactivate_device ( xg_device_h device );

void xg_vk_allocator_get_info ( xg_allocator_info_t* info, xg_device_h device, xg_memory_type_e type );

#if std_build_debug_m
xg_alloc_t xg_alloc ( const xg_alloc_params_t* params, std_alloc_scope_t scope );
#define xg_alloc_m( params ) ( xg_alloc ( params, std_alloc_scope_m() ) )
#else
xg_alloc_t xg_alloc ( const xg_alloc_params_t* params );
#define xg_alloc_m( params ) ( xg_alloc ( params ) )
#endif

void xg_free ( xg_memory_h handle );
