#pragma once

#include <xg.h>

/*
-------------------------------------------------------------------------------

    IDEA: this is the memory abstraction layer. Memory allocation is not
    very application dependant, only requires a few hints (permanent/temp
    memory, ?).
    Works by basically managing memory pools used to place requested
    allocations of different kinds (perm/temp, buffer/image, ...). Also
    keeps a mapping of resource -> memory location for every resource
    created.
    ISSUES: is keeping an internal mapping a good idea? what about returning
    two handles representing allocation & data, like for CPU memory
    allocations?).
    Is having memory pools at this level a good idea? what about a frame
    based allocator that works by tagging memory pools and allocating
    and releasing by tag? Having pools here basically means there's no way
    to write a custom gpu memory allocator. On the other side, keeping the
    memory allocation explicit on the main xg api is troublesome because
    there's no direct mapping of that concept in the older graphics APIs.
    Could make the allocator a plug-in, but then again it forces the one
    allocator to be global and omni-present.
    RESOLUTION: A tag-like allocator could be made by deferring actual
    allocation to the backend and simply managing the resource -> tag
    mapping.
    Would allocation grouping matter in this case? (e.g.
    allocate together stuff that will be free'd together. maybe? in such
    case tags basically need to propagate to the backend allocator).
    Actually, they already do, just generalize tags! e.g.:
        frame n: n|000
        frame n, per-frame:  n|000
        frame n, per-view:   n|010
        frame n, per-object: n|001
    how to group per-tag now, though? make it explicit by setting the mask
    manually.
    This is way too complex for a first implementation, though. Something
    much more simple, like https://ourmachinery.com/post/device-memory-management/
    is a good idea (alloc > 256MB -> dedicated call to vkAllocMemory;
    alloc <= 256MB -> allocate inside a 256MB block).

    examples
        stage | frame | view | object
        set_grouping_mask ( frame )

    Alternative: make the allocator an interface (similarly to the cpu
    allocator one) and pass it in every time you allocate (or once per
    cmd buffer to be less verbose?)
    The allocator allocates resources and hides memory from the user.
    All create/delete calls go through the allocator.
        or allocators only manage memory?
            how is non resident memory even handled if that is the case?
                just have an allocator dedicated to that?

    the following assumes that an allocator only manages memory, the above that
    it manages both memory and resources.
    is the allocator interface the std one? what info does the allocator need?
        size - ok
        alignment - ok
        memory type - missing from std_allocator, but might want to have one
        allocator per memory type since the types are finite and small in
        number, might be more practical too to have them completely separate
        in different timplementations.

    so just need to write the implementaitons
        buddy allocator that allocates 256MB blocks for small size device allocations
        1:1 allocator for big size device allocations
        linear allocator that allocates min(size, 256MB) for staging cached/uncached


-------------------------------------------------------------------------------
*/

/*
    - internal state
    - std module api
    - module public api
    - module private api
    - static private code
*/

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
