#include "xg_vk_allocator.h"

#include <std_allocator.h>
#include <std_mutex.h>
#include <std_byte.h>

#include "xg_vk.h"
#include "xg_vk_device.h"
#include "xg_vk_instance.h"

// --------------------------

static xg_vk_allocator_state_t* xg_vk_allocator_state;

void xg_vk_allocator_load ( xg_vk_allocator_state_t* state ) {
    xg_vk_allocator_state = state;

    state->allocations_array = std_virtual_heap_alloc_array_m ( xg_vk_alloc_t, xg_vk_max_allocations_m );
    state->allocations_freelist = std_freelist_m ( state->allocations_array, xg_vk_max_allocations_m );
    
    for ( uint32_t i = 0; i < xg_max_active_devices_m; ++i ) {
        for ( uint32_t j = 0; j < xg_memory_type_count_m; ++j ) {
            state->device_contexts[i].heaps[j].gpu_alloc = xg_null_alloc_m;
            // TODO rest?
        }
    }

    std_mutex_init ( &state->allocations_mutex );
}

void xg_vk_allocator_reload ( xg_vk_allocator_state_t* state ) {
    xg_vk_allocator_state = state;
}

void xg_vk_allocator_unload ( void ) {
    std_virtual_heap_free ( xg_vk_allocator_state->allocations_array );
    
    std_mutex_deinit ( &xg_vk_allocator_state->allocations_mutex );
}

// --------------------------

static xg_alloc_t xg_vk_allocator_simple_alloc ( xg_device_h device_handle, size_t size, xg_memory_type_e type ) {
    // Get the device
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // Get memory type index
    uint32_t memory_type_index = device->memory_heaps[type].vk_memory_type_idx;
    xg_memory_flag_bit_e memory_flags = device->memory_heaps[type].memory_flags;

    // Allocate Vulkan memory
    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
#if xg_enable_raytracing_m
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
#endif
    };

    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &flags_info,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index,
    };
    VkDeviceMemory memory;
    VkResult vk_result = vkAllocateMemory ( device->vk_handle, &info, NULL, &memory );
    std_assert_m ( vk_result == VK_SUCCESS );

    VkDebugUtilsObjectNameInfoEXT debug_name = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext = NULL,
        .objectType = VK_OBJECT_TYPE_DEVICE_MEMORY,
        .objectHandle = ( uint64_t ) memory,
        .pObjectName = "simple_alloc",
    };
    xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );

    // Store the allocation
    std_mutex_lock ( &xg_vk_allocator_state->allocations_mutex );
    xg_vk_alloc_t* xg_vk_alloc = std_list_pop_m ( &xg_vk_allocator_state->allocations_freelist );
    std_mutex_unlock ( &xg_vk_allocator_state->allocations_mutex );
    std_assert_m ( xg_vk_alloc );
    xg_vk_alloc->device = device_handle;
    xg_vk_alloc->vk_handle = memory;

    // map if needed
    void* mapped_address = NULL;
    if ( memory_flags & xg_memory_type_bit_mapped_m ) {
        vk_result = vkMapMemory ( device->vk_handle, memory, 0, size, 0, &mapped_address );
        std_assert_m ( vk_result == VK_SUCCESS );
    }

    // Return the allocation
    xg_alloc_t alloc = {
        .device = device_handle,
        .base = ( uint64_t ) memory,
        .offset = 0,
        .size = size,
        .flags = memory_flags,
        .handle = {
            .id = ( uint64_t ) ( xg_vk_alloc - xg_vk_allocator_state->allocations_array ),
            .size = size,
            .type = type,
            .device = xg_vk_device_get_idx ( device_handle ),
        },
        .mapped_address = mapped_address,
    };
    return alloc;
}

static void xg_vk_allocator_simple_free ( xg_memory_h memory_handle ) {
    std_mutex_lock ( &xg_vk_allocator_state->allocations_mutex );
    xg_vk_alloc_t* alloc = &xg_vk_allocator_state->allocations_array[memory_handle.id];

    xg_device_h device_handle = alloc->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    std_assert_m ( device );

    vkFreeMemory ( device->vk_handle, alloc->vk_handle, NULL );
    std_list_push ( &xg_vk_allocator_state->allocations_freelist, alloc );
    std_mutex_unlock ( &xg_vk_allocator_state->allocations_mutex );
}

// --------------------------

#define xg_vk_allocator_tlsf_min_segment_size_m ( 1 << xg_vk_allocator_tlsf_min_x_level_m )
#define xg_vk_allocator_tlsf_max_segment_size_m ( ( 1ull << xg_vk_allocator_tlsf_max_x_level_m ) - 1 )

typedef struct {
    uint64_t x;
    uint64_t y;
} xg_vk_allocator_tlsf_freelist_idx_t;

xg_vk_allocator_tlsf_freelist_idx_t xg_vk_allocator_tlsf_freelist_idx ( uint64_t size ) {
    xg_vk_allocator_tlsf_freelist_idx_t idx;
    idx.x = 63 - std_bit_scan_rev_64 ( size );
    idx.y = ( size >> ( idx.x - xg_vk_allocator_tlsf_log2_y_size_m ) ) - xg_vk_allocator_tlsf_y_size_m;
    // offset the x level so that the min level indexes the tables at 0
    idx.x = std_max_u64 ( idx.x, xg_vk_allocator_tlsf_min_x_level_m ) - xg_vk_allocator_tlsf_min_x_level_m;
    return idx;
}

uint64_t xg_vk_allocator_tlsf_heap_size_roundup ( uint64_t size ) {
    size += ( 1ull << ( 63 - std_bit_scan_rev_64 ( size ) - xg_vk_allocator_tlsf_log2_y_size_m ) ) - 1;
    return size;
}

xg_vk_allocator_tlsf_freelist_idx_t xg_vk_allocator_tlsf_freelist_idx_first_available ( xg_vk_allocator_tlsf_heap_t* heap, xg_vk_allocator_tlsf_freelist_idx_t base ) {
    xg_vk_allocator_tlsf_freelist_idx_t idx;

    uint32_t mask = ( 1 << xg_vk_allocator_tlsf_y_size_m ) - 1;
    uint32_t t = heap->available_freelists[base.x] & ( mask << base.y );

    if ( t != 0 ) {
        idx.x = base.x;
        idx.y = std_bit_scan_32 ( t );
    } else {
        uint32_t mask = ( 1 << xg_vk_allocator_tlsf_x_size_m ) - 1;
        t = heap->available_rows & ( mask << ( base.x + 1 ) );
        std_assert_m ( t );
        idx.x = std_bit_scan_32 ( t );
        std_assert_m ( heap->available_freelists[idx.x] );
        idx.y = std_bit_scan_32 ( heap->available_freelists[idx.x] );
    }

    return idx;
}

void xg_vk_allocator_tlsf_add_to_freelist ( xg_vk_allocator_tlsf_heap_t* heap, xg_vk_allocator_tlsf_segment_t* segment ) {
    xg_vk_allocator_tlsf_freelist_idx_t idx = xg_vk_allocator_tlsf_freelist_idx ( segment->size );
    std_dlist_push ( &heap->freelists[idx.x][idx.y], &segment->next );
    heap->available_freelists[idx.x] |= 1 << idx.y;
    heap->available_rows |= 1ull << idx.x;
}

void xg_vk_allocator_tlsf_remove_from_freelist ( xg_vk_allocator_tlsf_heap_t* heap, xg_vk_allocator_tlsf_segment_t* segment ) {
    xg_vk_allocator_tlsf_freelist_idx_t idx = xg_vk_allocator_tlsf_freelist_idx ( segment->size );
    std_dlist_remove ( &segment->next );

    if ( heap->freelists[idx.x][idx.y] == NULL ) {
        heap->available_freelists[idx.x] &= ~ ( 1 << idx.y );

        if ( heap->available_freelists[idx.x] == 0 ) {
            heap->available_rows &= ~ ( 1ull << idx.x );
        }
    }    
}

#define xg_vk_allocator_tlsf_get_segment_m( _ptr, _field ) ( xg_vk_allocator_tlsf_segment_t* ) ( ( char* ) (_ptr) - std_field_offset_m ( xg_vk_allocator_tlsf_segment_t, _field ) )

xg_vk_allocator_tlsf_segment_t* xg_vk_allocator_tlsf_pop_from_freelist ( xg_vk_allocator_tlsf_heap_t* heap, uint64_t size ) {
    xg_vk_allocator_tlsf_freelist_idx_t start_idx = xg_vk_allocator_tlsf_freelist_idx ( size );
    xg_vk_allocator_tlsf_freelist_idx_t idx = xg_vk_allocator_tlsf_freelist_idx_first_available ( heap, start_idx );
    void* list_ptr = std_dlist_pop ( &heap->freelists[idx.x][idx.y] );
    xg_vk_allocator_tlsf_segment_t* segment = xg_vk_allocator_tlsf_get_segment_m ( list_ptr, next );

    if ( heap->freelists[idx.x][idx.y] == NULL ) {
        heap->available_freelists[idx.x] &= ~ ( 1 << idx.y );

        if ( heap->available_freelists[idx.x] == 0 ) {
            heap->available_rows &= ~ ( 1ull << idx.x );
        }
    }

    return segment;
}

xg_vk_allocator_tlsf_segment_t* xg_vk_allocator_tlsf_acquire_new_segment ( xg_vk_allocator_tlsf_heap_t* heap ) {
    xg_vk_allocator_tlsf_segment_t* segment = std_list_pop ( &heap->unused_segments_freelist );
    std_assert_m ( segment );
    std_assert_m ( segment->retired );
    segment->retired = false;
    --heap->unused_segments_count;
    return segment;
}

void xg_vk_allocator_tlsf_retire_segment ( xg_vk_allocator_tlsf_heap_t* heap, xg_vk_allocator_tlsf_segment_t* segment ) {
    std_assert_m ( !segment->retired );
    std_dlist_remove ( &segment->right );
    std_list_push ( &heap->unused_segments_freelist, segment );
    segment->retired = true;
    ++heap->unused_segments_count;
}

void xg_vk_allocator_tlsf_heap_init ( xg_vk_allocator_tlsf_heap_t* heap, xg_device_h device, xg_memory_type_e type, uint64_t size ) {
    std_mem_zero_m ( heap );

    std_mutex_init ( &heap->mutex );

    uint64_t segment_count = std_div_ceil ( size, xg_vk_allocator_tlsf_min_segment_size_m );
    heap->segments = std_virtual_heap_alloc_array_m ( xg_vk_allocator_tlsf_segment_t, segment_count );

    for (uint32_t i = 0; i < segment_count; ++i ) {
        std_mem_zero_m ( &heap->segments[i] );
        heap->segments[i].retired = true;
    }

    heap->unused_segments_freelist = std_freelist_m ( heap->segments, segment_count );
    heap->unused_segments_count = segment_count;

    heap->gpu_alloc = xg_vk_allocator_simple_alloc ( device, size, type );

    heap->memory_type = type;
    heap->device_idx = xg_vk_device_get_idx ( device );

#if 0
    uint64_t s = xg_vk_allocator_tlsf_min_segment_size_m;
    while ( s < xg_vk_allocator_tlsf_max_segment_size_m ) {
        xg_vk_allocator_tlsf_freelist_idx_t idx = xg_vk_allocator_tlsf_freelist_idx ( s );
        std_log_info_m ( std_fmt_u64_m " " std_fmt_u64_m " " std_fmt_u64_m, s, idx.x, idx.y );
        s = xg_vk_allocator_tlsf_heap_size_roundup ( s + 1 );
    }
#endif


    xg_vk_allocator_tlsf_segment_t* segment = xg_vk_allocator_tlsf_acquire_new_segment ( heap );
    segment->offset = 0;
    segment->size = size;
    segment->free = true;
    xg_vk_allocator_tlsf_add_to_freelist ( heap, segment );
}

void xg_vk_allocator_tlsf_heap_deinit ( xg_vk_allocator_tlsf_heap_t* heap, xg_device_h device ) {
    // TODO
    std_unused_m ( device );
    xg_vk_allocator_simple_free ( heap->gpu_alloc.handle );
    std_mutex_deinit ( &heap->mutex );
    heap->gpu_alloc = xg_null_alloc_m;
}

#define xg_vk_allocator_tlsf_debug_print 0

xg_alloc_t xg_vk_tlsf_heap_alloc ( xg_vk_allocator_tlsf_heap_t* heap, uint64_t size, uint64_t align ) {
    // check size
    //size = std_align ( size, 8 );
    size += align;
    size = std_max_u64 ( size, xg_vk_allocator_tlsf_min_segment_size_m );
    std_assert_m ( size <= xg_vk_allocator_tlsf_max_segment_size_m );
    uint64_t size_roundup = xg_vk_allocator_tlsf_heap_size_roundup ( size );

    std_mutex_lock ( &heap->mutex );

    // grab from freelist
    xg_vk_allocator_tlsf_segment_t* segment = xg_vk_allocator_tlsf_pop_from_freelist ( heap, size_roundup );

#if xg_vk_allocator_tlsf_debug_print
    std_log_info_m("pop     " std_fmt_ptr_m, segment);
#endif

    // load segment
    uint64_t segment_size = segment->size;
    uint64_t segment_offset = segment->offset;
    std_assert_m ( segment->free );
    std_assert_m ( segment_size >= size );

    // split
    bool needs_split = segment_size - size >= xg_vk_allocator_tlsf_min_segment_size_m;

    if ( needs_split ) {
        xg_vk_allocator_tlsf_segment_t* extra_segment = xg_vk_allocator_tlsf_acquire_new_segment ( heap );
        //xg_vk_allocator_tlsf_segment_t* extra_segment = std_list_pop_m ( &heap->unused_segments_freelist );
        //std_assert_m ( extra_segment );
        //--heap->unused_segments_count;
        //std_assert_m ( heap->unused_segments_freelist != extra_segment );
#if xg_vk_allocator_tlsf_debug_print
        std_log_info_m ( "acquire " std_fmt_ptr_m, extra_segment );
#endif

        uint64_t extra_size = segment_size - size;
        segment->size = size;
        segment_size = size;

        extra_segment->offset = segment_offset + segment_size;
        extra_segment->size = extra_size;
        extra_segment->free = true;
        extra_segment->left = &segment->right;
        extra_segment->right = segment->right;
        xg_vk_allocator_tlsf_add_to_freelist ( heap, extra_segment );
#if xg_vk_allocator_tlsf_debug_print
        std_log_info_m ( "pool    " std_fmt_ptr_m " " std_fmt_u64_m, extra_segment, extra_size );
#endif

        if ( segment->right ) {
            xg_vk_allocator_tlsf_segment_t* next_segment = xg_vk_allocator_tlsf_get_segment_m ( segment->right, right );
            next_segment->left = &extra_segment->right;
        }

        segment->right = &extra_segment->right;
    }

    // update this
    segment->free = false;

    std_assert_m ( segment->left == NULL || segment->left != segment->right );

    heap->allocated_size += segment_size;

    std_mutex_unlock ( &heap->mutex );

#if xg_vk_allocator_tlsf_debug_print
    std_log_info_m ( "allocd  " std_fmt_ptr_m, segment );
#endif
    xg_alloc_t alloc;
    alloc.handle.id = ( uint64_t ) segment;
    alloc.handle.size = segment_size;
    alloc.handle.device = heap->device_idx;
    alloc.handle.type = heap->memory_type;
    alloc.base = heap->gpu_alloc.base;
    alloc.offset = std_align_u64 ( segment_offset, align );
    alloc.size = segment_size;
    alloc.flags = heap->gpu_alloc.flags;
    alloc.device = heap->gpu_alloc.device;
    alloc.mapped_address = heap->gpu_alloc.mapped_address + alloc.offset;
    
    if ( heap->gpu_alloc.mapped_address ) {
        std_assert_m ( alloc.mapped_address );
    }
    
    std_assert_m ( alloc.offset < heap->gpu_alloc.size );

    return alloc;
}

void xg_vk_tlsf_heap_free ( xg_vk_allocator_tlsf_heap_t* heap, xg_memory_h handle ) {
    std_auto_m segment = ( xg_vk_allocator_tlsf_segment_t* ) handle.id;

    std_mutex_lock ( &heap->mutex );

    // load segment
    //xg_vk_allocator_tlsf_segment_t new_segment = *segment;
    std_assert_m ( segment->size == handle.size );
    std_assert_m ( !segment->free );

    std_assert_m ( segment->left == NULL || segment->left != segment->right );

    // check prev
    if ( segment->left ) {
        xg_vk_allocator_tlsf_segment_t* prev_segment = xg_vk_allocator_tlsf_get_segment_m ( segment->left, right );
        if ( prev_segment->free ) {
            //xg_vk_allocator_tlsf_segment_t* retired_segment = new_segment.left;

            // remove prev from freelist
            xg_vk_allocator_tlsf_remove_from_freelist ( heap, prev_segment );
#if xg_vk_allocator_tlsf_debug_print
            std_log_info_m ( "pop     " std_fmt_ptr_m, prev_segment );
#endif

            // merge
            segment->size += prev_segment->size;
            segment->offset = prev_segment->offset;
            //std_dlist_remove ( &prev_segment->right );
            //if ( prev_segment.left ) {
            //    prev_segment.left->right = segment;
            //}
            //new_segment.left = prev_segment.left;

            xg_vk_allocator_tlsf_retire_segment ( heap, prev_segment );
            //std_mem_zero_m ( retired_segment );
            //std_list_push ( &heap->unused_segments_freelist, retired_segment );
            //++heap->unused_segments_count;
#if xg_vk_allocator_tlsf_debug_print
            std_log_info_m ( "retire  " std_fmt_ptr_m, prev_segment );
#endif

            std_assert_m ( segment->left == NULL || segment->left != segment->right );
        }
    }

    // check next
    if ( segment->right ) {
        xg_vk_allocator_tlsf_segment_t* next_segment = xg_vk_allocator_tlsf_get_segment_m ( segment->right, right );
        if ( next_segment->free ) {
            //xg_vk_allocator_tlsf_segment_t* retired_segment = new_segment.right;
            
            // remove next from freelist
            xg_vk_allocator_tlsf_remove_from_freelist ( heap, next_segment );
#if xg_vk_allocator_tlsf_debug_print
            std_log_info_m ( "pop     " std_fmt_ptr_m, next_segment );
#endif

            // merge
            segment->size += next_segment->size;
            //std_dlist_remove ( &next_segment->right );
            //if ( next_segment.right ) {
            //    next_segment.right->left = segment;
            //}
            //new_segment.right = next_segment.right;

            xg_vk_allocator_tlsf_retire_segment ( heap, next_segment );
            //std_mem_zero_m ( retired_segment );
            //std_list_push ( &heap->unused_segments_freelist, retired_segment );
            //++heap->unused_segments_count;
#if xg_vk_allocator_tlsf_debug_print
            std_log_info_m ( "retire  " std_fmt_ptr_m, next_segment );
#endif
            
            std_assert_m ( segment->left == NULL || segment->left != segment->right );
        }
    }

    // write and add to freelist
    segment->free = true;
    //*segment = new_segment;
    xg_vk_allocator_tlsf_add_to_freelist ( heap, segment );
#if xg_vk_allocator_tlsf_debug_print
    std_log_info_m ( "push    " std_fmt_ptr_m, segment );
    std_log_info_m ( "freed   " std_fmt_ptr_m, segment );
#endif

    heap->allocated_size -= handle.size;

    std_mutex_unlock ( &heap->mutex );
}

// --------------------------

xg_alloc_t xg_alloc ( xg_vk_alloc_params_t* params ) {
    uint64_t device_idx = xg_vk_device_get_idx ( params->device );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];
    xg_vk_allocator_tlsf_heap_t* heap = &context->heaps[params->type];
    return xg_vk_tlsf_heap_alloc ( heap, params->size, params->align );
}

void xg_free ( xg_memory_h handle ) {
    uint64_t device_idx = handle.device;
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];
    xg_vk_allocator_tlsf_heap_t* heap = &context->heaps[handle.type];
    xg_vk_tlsf_heap_free ( heap, handle );
}

#if 0
static xg_alloc_t xg_vk_allocator_default_alloc ( void* allocator, xg_device_h device_handle, size_t size, size_t align ) {
    std_unused_m ( align );
    xg_memory_type_e type = ( xg_memory_type_e ) allocator;
    return xg_vk_allocator_simple_alloc ( device_handle, size, type );
}

static void xg_vk_allocator_default_free ( void* allocator, xg_memory_h memory_handle ) {
    std_unused_m ( allocator );
    xg_vk_allocator_simple_free ( memory_handle );
}

xg_allocator_i xg_allocator_default ( xg_memory_type_e type ) {
    xg_allocator_i allocator;
    allocator.impl = ( void* ) type;
    allocator.alloc = xg_vk_allocator_default_alloc;
    allocator.free = xg_vk_allocator_default_free;
    return allocator;
}
#endif

void xg_vk_allocator_activate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];

    for ( uint32_t i = 0; i < xg_memory_type_count_m; ++i ) {
        uint64_t size = device->memory_heaps[i].size;
        
        switch (i) {
        case xg_memory_type_gpu_only_m:
        case xg_memory_type_upload_m:
        case xg_memory_type_readback_m:
            size = size * 0.8;
            size = std_min_u64 ( size, 1024 * 1024 * 512 );
            break;
        case xg_memory_type_gpu_mapped_m:
            size = size * 0.8;
            size = std_min_u64 ( size, 1024 * 1024 * 32 );
            break;
        }

        if ( size > 0 ) {
            xg_vk_allocator_tlsf_heap_init ( &context->heaps[i], device_handle, i, size );
        }
    }
}

void xg_vk_allocator_deactivate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];

    for ( uint32_t i = 0; i < xg_memory_type_count_m; ++i ) {
        if ( !xg_memory_handle_is_null_m ( context->heaps[i].gpu_alloc.handle ) ) {
            xg_vk_allocator_tlsf_heap_deinit ( &context->heaps[i], device_handle );
        }
    }
}

// --------------------------

void xg_vk_allocator_get_info ( xg_allocator_info_t* info, xg_device_h device_handle, xg_memory_type_e type ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];

    xg_vk_allocator_tlsf_heap_t* heap = &context->heaps[type];

    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    uint64_t device_size = device->memory_heaps[type].size;
    info->system_size = device_size;

    std_mutex_lock ( &heap->mutex );
    if ( !xg_memory_handle_is_null_m ( heap->gpu_alloc.handle ) ) {
        info->reserved_size = heap->gpu_alloc.size;
        info->allocated_size = heap->allocated_size;
    } else {
        info->reserved_size = 0;
        info->allocated_size = 0;
    }
    std_mutex_unlock ( &heap->mutex );
}
