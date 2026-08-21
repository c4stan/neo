#include "xg_vk_allocator.h"

#include <std_allocator.h>
#include <std_mutex.h>
#include <std_byte.h>

#include <xg_enum.h>

#include "xg_vk.h"
#include "xg_vk_device.h"
#include "xg_vk_instance.h"
#include "xg_vk_enum.h"

// --------------------------

static xg_vk_allocator_state_t* xg_vk_allocator_state;

#if std_allocator_log_allocations_to_file_m
typedef struct {
    uint64_t base;
    uint64_t size;
} xg_vk_allocator_log_record_segment_info_t;
#endif

// --------------------------

void xg_vk_allocator_load ( xg_vk_allocator_state_t* state ) {
    xg_vk_allocator_state = state;

    for ( uint32_t i = 0; i < xg_max_active_devices_m; ++i ) {
        for ( uint32_t j = 0; j < xg_memory_type_count_m; ++j ) {
            state->device_contexts[i].global_allocators[j] = ( xg_vk_allocator_global_t ) {};
        }
    }
}

void xg_vk_allocator_reload ( xg_vk_allocator_state_t* state ) {
    xg_vk_allocator_state = state;
}

void xg_vk_allocator_unload ( void ) {
}

// --------------------------

static xg_vk_alloc_t xg_vk_allocator_vk_alloc ( xg_device_h device_handle, size_t size, xg_memory_type_e type, const char* debug_name ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    uint32_t memory_type_index = device->memory_types[type].vk_memory_type_idx;
    xg_memory_flag_bit_e memory_flags = xg_memory_flags_from_vk ( device->memory_types[type].vk_flags );

    VkMemoryAllocateFlagsInfo flags_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
    };
#if xg_enable_raytracing_m
    if ( device->supports_raytrace ) {
        flags_info.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
#endif

    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &flags_info,
        .allocationSize = size,
        .memoryTypeIndex = memory_type_index,
    };
    VkDeviceMemory memory;
    VkResult vk_result = vkAllocateMemory ( device->vk_handle, &info, xg_vk_cpu_allocator(), &memory );
    std_assert_m ( vk_result == VK_SUCCESS );

    VkDebugUtilsObjectNameInfoEXT vk_debug_name = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .pNext = NULL,
        .objectType = VK_OBJECT_TYPE_DEVICE_MEMORY,
        .objectHandle = ( uint64_t ) memory,
        .pObjectName = debug_name,
    };
    xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &vk_debug_name );

    // map if needed
    void* mapped_address = NULL;
    if ( memory_flags & xg_memory_type_bit_mapped_m ) {
        vk_result = vkMapMemory ( device->vk_handle, memory, 0, size, 0, &mapped_address );
        std_assert_m ( vk_result == VK_SUCCESS );
    }

    xg_vk_alloc_t alloc = {
        .device = device_handle,
        .vk_handle = memory,
        .memory_flags = memory_flags,
        .size = size,
        .mapped_address = mapped_address,
    };
    std_str_copy_static_m ( alloc.debug_name, debug_name );
    return alloc;
}

static void xg_vk_allocator_vk_free ( const xg_vk_alloc_t* alloc ) {
    const xg_vk_device_t* device = xg_vk_device_get ( alloc->device );
    vkFreeMemory ( device->vk_handle, alloc->vk_handle, xg_vk_cpu_allocator() );
}

// --------------------------

#define xg_vk_allocator_tlsf_min_segment_size_m ( 1 << xg_vk_allocator_tlsf_min_x_level_m )
#define xg_vk_allocator_tlsf_max_segment_size_m ( ( 1ull << xg_vk_allocator_tlsf_max_x_level_m ) - 1 )

typedef struct {
    uint64_t x;
    uint64_t y;
} xg_vk_allocator_tlsf_freelist_idx_t;

#define xg_vk_allocator_tlsf_freelist_idx_null_m() ( xg_vk_allocator_tlsf_freelist_idx_t ) { -1, -1 }
#define xg_vk_allocator_tlsf_freelist_idx_is_null_m( idx ) ( idx.x == -1 && idx.y == -1 )

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
        if ( t == 0 ) {
            // OOM
            return xg_vk_allocator_tlsf_freelist_idx_null_m();
        }
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
    if ( xg_vk_allocator_tlsf_freelist_idx_is_null_m ( idx ) ) {
        return NULL;
    }
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
    *heap = xg_vk_allocator_tlsf_heap_m();

    std_mutex_init ( &heap->mutex );

    uint64_t segment_count = std_div_round_up ( size, xg_vk_allocator_tlsf_min_segment_size_m );
    heap->segments = std_virtual_heap_alloc_array_m ( xg_vk_allocator_tlsf_segment_t, segment_count );

    for (uint32_t i = 0; i < segment_count; ++i ) {
        std_mem_zero_m ( &heap->segments[i] );
        heap->segments[i].retired = true;
    }

    heap->unused_segments_freelist = std_freelist_m ( heap->segments, segment_count );
    heap->unused_segments_count = segment_count;

    heap->gpu_alloc = xg_vk_allocator_vk_alloc ( device, size, type, "tlsf_heap" ); // TODO

    heap->memory_type = type;
    heap->device_idx = xg_vk_device_get_idx ( device );

#if std_build_debug_m
    heap->debug_records_array = std_virtual_heap_alloc_array_m ( xg_vk_allocator_debug_record_t, xg_vk_allocator_max_debug_records_m );
    heap->debug_records_freelist = std_freelist_m ( heap->debug_records_array, xg_vk_allocator_max_debug_records_m );
    heap->debug_records_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xg_vk_allocator_max_debug_records_m ) );
    std_mem_zero_array_m ( heap->debug_records_bitset, std_bitset_u64_count_m ( xg_vk_allocator_max_debug_records_m ));
#endif

    xg_vk_allocator_tlsf_segment_t* segment = xg_vk_allocator_tlsf_acquire_new_segment ( heap );
    segment->offset = 0;
    segment->size = size;
    segment->free = true;
    xg_vk_allocator_tlsf_add_to_freelist ( heap, segment );
}

static void xg_vk_allocator_tlsf_heap_deinit ( xg_vk_allocator_tlsf_heap_t* heap ) {
//#if std_allocator_log_allocations_to_file_m
//    std_file_close ( heap->log_file );
//#endif

#if std_build_debug_m
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, heap->debug_records_bitset, idx, std_bitset_u64_count_m ( xg_vk_allocator_max_debug_records_m ) ) ) {
        xg_vk_allocator_debug_record_t* record = &heap->debug_records_array[idx];
        std_log_warn_m ( "MEMLEAK: " std_fmt_str_m " " std_fmt_str_m ":" std_fmt_size_m, record->scope.file, record->scope.function, record->scope.line );
        ++idx;
    }

    std_virtual_heap_free ( heap->debug_records_array );
    std_virtual_heap_free ( heap->debug_records_bitset );
#endif

    xg_vk_allocator_vk_free ( &heap->gpu_alloc );
    std_virtual_heap_free ( heap->segments );
    std_mutex_deinit ( &heap->mutex );
    *heap = xg_vk_allocator_tlsf_heap_m();
}

#define xg_vk_allocator_tlsf_debug_print 0

xg_alloc_t xg_vk_tlsf_heap_alloc ( xg_vk_allocator_tlsf_heap_t* heap, uint64_t size, uint64_t align
#if std_build_debug_m
    , std_alloc_scope_t scope
#if std_allocator_log_allocations_to_file_m
    , xg_vk_allocator_log_record_segment_info_t* segment_info
#endif
#endif
    ) {
    // check size
    size += align;
    size = std_max_u64 ( size, xg_vk_allocator_tlsf_min_segment_size_m );
    std_assert_m ( size <= xg_vk_allocator_tlsf_max_segment_size_m );
    uint64_t size_roundup = xg_vk_allocator_tlsf_heap_size_roundup ( size );

    std_mutex_lock ( &heap->mutex );

    // grab from freelist
    xg_vk_allocator_tlsf_segment_t* segment = xg_vk_allocator_tlsf_pop_from_freelist ( heap, size_roundup );
    if ( !segment ) {
        std_mutex_unlock ( &heap->mutex );
        return xg_null_alloc_m;
    }

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
    xg_alloc_t alloc = {
        .handle.id = ( uint64_t ) segment,
        .handle.size = segment_size,
        .handle.device = heap->device_idx,
        .handle.type = heap->memory_type,
        .base = ( uint64_t ) heap->gpu_alloc.vk_handle,
        .offset = std_align_u64 ( segment_offset, align ),
        .size = segment_size,
        .flags = heap->gpu_alloc.memory_flags,
        .device = heap->gpu_alloc.device,
        .mapped_address = heap->gpu_alloc.mapped_address + alloc.offset,
    };
    
    if ( heap->gpu_alloc.mapped_address ) {
        std_assert_m ( alloc.mapped_address );
    }
    
    std_assert_m ( alloc.offset < heap->gpu_alloc.size );

#if std_build_debug_m
    xg_vk_allocator_debug_record_t* debug_record = std_list_pop_m ( &heap->debug_records_freelist );
    if ( debug_record ) {
        debug_record->scope = scope;
        debug_record->user = alloc.handle;
        std_bitset_set ( heap->debug_records_bitset, debug_record - heap->debug_records_array );
    }
    segment->debug_record = debug_record;
#if std_allocator_log_allocations_to_file_m
    segment_info->base = alloc.base + segment_offset;
    segment_info->size = segment_size;
#endif
#endif

    return alloc;
}

void xg_vk_tlsf_heap_free ( xg_vk_allocator_tlsf_heap_t* heap, xg_memory_h handle
#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    , xg_vk_allocator_log_record_segment_info_t* segment_info
#endif
    ) {
    std_auto_m segment = ( xg_vk_allocator_tlsf_segment_t* ) handle.id;

    std_mutex_lock ( &heap->mutex );

#if std_build_debug_m
    std_auto_m debug_record = segment->debug_record;
    if ( debug_record ) {
        std_list_push ( &heap->debug_records_freelist, debug_record );
        std_bitset_clear ( heap->debug_records_bitset, debug_record - heap->debug_records_array );
    }
#endif

#if std_allocator_log_allocations_to_file_m
    uint64_t offset = segment->offset;
#endif

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

#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    segment_info->base = ( uint64_t ) heap->gpu_alloc.vk_handle + offset;
    segment_info->size = handle.size;
#endif

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

#define xg_vk_allocator_tlsf_pool_max_heaps_m 16

xg_alloc_t xg_vk_allocator_tlsf_pool_alloc ( xg_vk_allocator_tlsf_pool_t* pool, uint64_t size, uint64_t align
#if std_build_debug_m
    , std_alloc_scope_t scope
#endif
    ) {
    xg_alloc_t alloc = xg_null_alloc_m;
#if std_allocator_log_allocations_to_file_m
    xg_vk_allocator_log_record_segment_info_t segment_info;
#endif

    // Try to allocate into existing heaps
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, pool->heaps_bitset, idx, std_bitset_u64_count_m ( xg_vk_allocator_tlsf_pool_max_heaps_m ) ) ) {
        xg_vk_allocator_tlsf_heap_t* heap = &pool->heaps_array[idx];
        alloc = xg_vk_tlsf_heap_alloc ( heap, size, align
#if std_build_debug_m
            , scope
#if std_allocator_log_allocations_to_file_m
            , &segment_info
#endif
#endif
        );
        if ( !xg_memory_handle_is_null_m ( alloc.handle ) ) {
            alloc.handle.heap = idx;
            goto end;
        }
        ++idx;
    }

    // Create a new heap
    xg_vk_allocator_tlsf_heap_t* heap = std_list_pop_m ( &pool->heaps_freelist );
    std_assert_m ( heap );
    xg_vk_allocator_tlsf_heap_init ( heap, pool->device, pool->memory_type, 1024ull * 1024ull * 256 ); // TODO
    std_bitset_set ( pool->heaps_bitset, heap - pool->heaps_array );
    alloc = xg_vk_tlsf_heap_alloc ( heap, size, align
#if std_build_debug_m
        , scope
#if std_allocator_log_allocations_to_file_m
        , &segment_info
#endif
#endif
    );
    alloc.handle.heap = heap - pool->heaps_array;

    // Log to file if enabled and return
end:
#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    // TODO lock
    std_allocator_log_record_t log_record = {
        .allocator = "xg_tlsf",
        .type = std_allocator_log_record_alloc_m,
        .timestamp = std_timestamp_now_local(),
        .address = segment_info.base,
        .size = segment_info.size,
    };
    uint64_t log_count = pool->log_count + 1;
    // TODO explicit async write?
    std_file_seek ( pool->log_file, std_file_start_m, 0 );
    std_file_write ( pool->log_file, &log_count, sizeof ( log_count ) );
    std_file_seek ( pool->log_file, std_file_end_m, 0 );
    std_file_write ( pool->log_file, &log_record, sizeof ( log_record ) );
    pool->log_count = log_count;
    pool->file_offset += sizeof ( log_record ); // TODO remove?
#endif

    return alloc;
}

void xg_vk_allocator_tlsf_pool_free ( xg_vk_allocator_tlsf_pool_t* pool, xg_memory_h handle ) {
    xg_vk_allocator_tlsf_heap_t* heap = &pool->heaps_array[handle.heap];

#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    xg_vk_allocator_log_record_segment_info_t segment_info;
#endif

    xg_vk_tlsf_heap_free ( heap, handle
#if std_build_debug_m && std_allocator_log_allocations_to_file_m
        , &segment_info
#endif
    );

#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    // TODO lock
    std_allocator_log_record_t log_record = {
        .allocator = "xg_tlsf",
        .type = std_allocator_log_record_free_m,
        .timestamp = std_timestamp_now_local(),
        .address = segment_info.base,
        .size = segment_info.size,
    };
    uint64_t log_count = pool->log_count + 1;
    // TODO explicit async write?
    std_file_seek ( pool->log_file, std_file_start_m, 0 );
    std_file_write ( pool->log_file, &log_count, sizeof ( log_count ) );
    std_file_seek ( pool->log_file, std_file_end_m, 0 );
    std_file_write ( pool->log_file, &log_record, sizeof ( log_record ) );
    pool->log_count = log_count;
    pool->file_offset += sizeof ( log_record ); // TODO remove?
#endif
}

// --------------------------

xg_alloc_t xg_vk_allocator_reserved_pool_alloc ( xg_vk_allocator_reserved_pool_t* allocator, uint64_t size
#if std_build_debug_m
    , std_alloc_scope_t scope
#endif
    ) {
    xg_vk_alloc_t* alloc = std_list_pop_m ( &allocator->allocations_freelist );
    uint64_t id = alloc - allocator->allocations_array;
    std_bitset_set ( allocator->allocations_bitset, id );
    *alloc = xg_vk_allocator_vk_alloc ( allocator->device, size, allocator->memory_type, "reserved_pool_alloc" );

    xg_alloc_t user_alloc = {
        .handle.id = id,
        .handle.device = allocator->device_idx,
        .handle.heap = 0,
        .handle.type = allocator->memory_type,
        .handle.size = size,
        .base = ( uint64_t ) alloc->vk_handle,
        .offset = 0,
        .size = size,
        .flags = alloc->memory_flags,
        .device = allocator->device,
        .mapped_address = alloc->mapped_address,
    };

#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    // TODO lock
    std_allocator_log_record_t log_record = {
        .allocator = "xg_reserved",
        .type = std_allocator_log_record_alloc_m,
        .timestamp = std_timestamp_now_local(),
        .address = ( uint64_t ) alloc->vk_handle,
        .size = size,
    };
    uint64_t log_count = allocator->log_count + 1;
    // TODO explicit async write?
    std_file_seek ( allocator->log_file, std_file_start_m, 0 );
    std_file_write ( allocator->log_file, &log_count, sizeof ( log_count ) );
    std_file_seek ( allocator->log_file, std_file_end_m, 0 );
    std_file_write ( allocator->log_file, &log_record, sizeof ( log_record ) );
    allocator->log_count = log_count;
    allocator->file_offset += sizeof ( log_record ); // TODO remove?
#endif

    return user_alloc;
}

void xg_vk_allocator_reserved_pool_free ( xg_vk_allocator_reserved_pool_t* allocator, xg_memory_h handle ) {
    xg_vk_alloc_t* alloc = &allocator->allocations_array[handle.id];
    xg_vk_allocator_vk_free ( alloc );

#if std_build_debug_m && std_allocator_log_allocations_to_file_m
    // TODO lock
    std_allocator_log_record_t log_record = {
        .allocator = "xg_reserved",
        .type = std_allocator_log_record_free_m,
        .timestamp = std_timestamp_now_local(),
        .address = ( uint64_t ) alloc->vk_handle,
        .size = alloc->size,
    };
    uint64_t log_count = allocator->log_count + 1;
    // TODO explicit async write?
    std_file_seek ( allocator->log_file, std_file_start_m, 0 );
    std_file_write ( allocator->log_file, &log_count, sizeof ( log_count ) );
    std_file_seek ( allocator->log_file, std_file_end_m, 0 );
    std_file_write ( allocator->log_file, &log_record, sizeof ( log_record ) );
    allocator->log_count = log_count;
    allocator->file_offset += sizeof ( log_record ); // TODO remove?
#endif

    std_bitset_clear ( allocator->allocations_bitset, handle.id );
    std_list_push ( &allocator->allocations_freelist, alloc );
}

// --------------------------

#define xg_vk_allocator_reserved_threshold_m ( 1024ull * 1024 * 128 )

xg_alloc_t xg_vk_allocator_global_alloc ( xg_vk_allocator_global_t* allocator, uint64_t size, uint64_t align
#if std_build_debug_m
    , std_alloc_scope_t scope
#endif
    ) {
    if ( size < xg_vk_allocator_reserved_threshold_m ) {
        return xg_vk_allocator_tlsf_pool_alloc ( &allocator->tlsf_pool, size, align
#if std_build_debug_m
            , scope
#endif
        );
    } else {
        return xg_vk_allocator_reserved_pool_alloc ( &allocator->reserved_pool, size
#if std_build_debug_m
            , scope
#endif
        );
    }
}

void xg_vk_allocator_global_free ( xg_vk_allocator_global_t* allocator, xg_memory_h handle ) {
    if ( handle.size < xg_vk_allocator_reserved_threshold_m ) {
        xg_vk_allocator_tlsf_pool_free ( &allocator->tlsf_pool, handle );
    } else {
        xg_vk_allocator_reserved_pool_free ( &allocator->reserved_pool, handle );
    }
}

// --------------------------

xg_alloc_t xg_alloc ( const xg_alloc_params_t* params
#if std_build_debug_m
    , std_alloc_scope_t scope
#endif
    ) {
    uint64_t device_idx = xg_vk_device_get_idx ( params->device );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];
    xg_memory_type_e type = params->type;
    std_assert_m ( type < xg_memory_type_count_m );
    xg_vk_allocator_global_t* global_allocator = &context->global_allocators[type];
    return xg_vk_allocator_global_alloc ( global_allocator, params->size, params->align
#if std_build_debug_m
        , scope
#endif
    );
}

void xg_free ( xg_memory_h handle ) {
    uint64_t device_idx = handle.device;
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];
    xg_vk_allocator_global_t* global_allocator = &context->global_allocators[handle.type];
    xg_vk_allocator_global_free ( global_allocator, handle );
}

// --------------------------

// TODO differentiate per memory type
#define xg_vK_allocator_reserved_pool_max_allocs_m 1024

void xg_vk_allocator_activate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];

    for ( xg_memory_type_e i = 0; i < xg_memory_type_count_m; ++i ) {
        const xg_vk_device_memory_type_t* memory_type = &device->memory_types[i];
        xg_vk_allocator_global_t* global_allocator = &context->global_allocators[i];

        if ( !memory_type->found ) {
            global_allocator->enabled = false;
            std_log_warn_m ( "Missing memory type " std_fmt_str_m " from device " std_fmt_str_m, xg_memory_type_str ( i ), device->generic_properties.deviceName );
            continue;
        }

        global_allocator->enabled = true;

        xg_vk_allocator_tlsf_pool_t* tlsf_pool = &global_allocator->tlsf_pool;
        tlsf_pool->memory_type = i;
        tlsf_pool->device = device_handle;
        switch (i) {
        case xg_memory_type_gpu_only_m:
            tlsf_pool->grow_size = 1024ull * 1024 * 256;
            break;
        case xg_memory_type_upload_m:
            tlsf_pool->grow_size = 1024ull * 1024 * 256;
            break;
        case xg_memory_type_readback_m:
            tlsf_pool->grow_size = 1024ull * 1024 * 256;
            break;
        case xg_memory_type_gpu_mapped_m:
            tlsf_pool->grow_size = 1024ull * 1024 * 4;
            break;
        default:
            break;
        }
        tlsf_pool->heaps_array = std_virtual_heap_alloc_array_m ( xg_vk_allocator_tlsf_heap_t, xg_vk_allocator_tlsf_pool_max_heaps_m );
        tlsf_pool->heaps_freelist = std_freelist_m ( tlsf_pool->heaps_array, xg_vk_allocator_tlsf_pool_max_heaps_m );
        tlsf_pool->heaps_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xg_vk_allocator_tlsf_pool_max_heaps_m ) );
        std_mem_zero_array_m ( tlsf_pool->heaps_bitset, std_bitset_u64_count_m ( xg_vk_allocator_tlsf_pool_max_heaps_m ) );
        std_mutex_init ( &tlsf_pool->heaps_mutex );

        xg_vk_allocator_reserved_pool_t* reserved_pool = &global_allocator->reserved_pool;
        reserved_pool->memory_type = i;
        reserved_pool->device = device_handle;
        reserved_pool->device_idx = device_idx;
        reserved_pool->allocations_array = std_virtual_heap_alloc_array_m ( xg_vk_alloc_t, xg_vK_allocator_reserved_pool_max_allocs_m );
        reserved_pool->allocations_freelist = std_freelist_m ( reserved_pool->allocations_array, xg_vK_allocator_reserved_pool_max_allocs_m );
        reserved_pool->allocations_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_bitset_u64_count_m ( xg_vK_allocator_reserved_pool_max_allocs_m ) );
        std_mem_zero_array_m ( reserved_pool->allocations_bitset, std_bitset_u64_count_m ( xg_vK_allocator_reserved_pool_max_allocs_m ) );
        std_mutex_init ( &reserved_pool->allocations_mutex );

#if std_allocator_log_allocations_to_file_m
        std_timestamp_t timestamp = std_program_start_timestamp_local();
        char log_path_buffer[std_path_size_m];
        std_string_t log_path = std_static_string_m ( log_path_buffer );
        std_string_copy ( &log_path, "std_allocator_log" );
        std_directory_create ( log_path.str );
        const char* type_str = xg_memory_type_str ( i );

        char filename[64] = {};
        std_string_t filename_string = std_static_string_m ( filename );
        std_string_append ( &filename_string, "xg_tlsf_" );
        std_string_append ( &filename_string, type_str );
        std_string_append ( &filename_string, "_log_" );
        std_path_append_file ( &log_path, filename );
        std_string_append_format ( &log_path, std_fmt_u64_m, timestamp.count );
        std_string_append ( &log_path, ".txt" );
        tlsf_pool->log_file = std_file_create ( log_path.str, std_file_write_m, std_path_already_existing_overwrite_m );
        tlsf_pool->log_count = 0;
        tlsf_pool->file_offset = 0;

        filename[0] = 0;
        filename_string = std_static_string_m ( filename );
        std_string_append ( &filename_string, "xg_reserved_" );
        std_string_append ( &filename_string, type_str );
        std_string_append ( &filename_string, "_log_" );
        std_path_pop ( &log_path );
        std_path_append_file ( &log_path, filename );
        std_string_append_format ( &log_path, std_fmt_u64_m, timestamp.count );
        std_string_append ( &log_path, ".txt" );
        reserved_pool->log_file = std_file_create ( log_path.str, std_file_write_m, std_path_already_existing_overwrite_m );
        reserved_pool->log_count = 0;
        reserved_pool->file_offset = 0;
#endif
    }
}

void xg_vk_allocator_deactivate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];

    for ( uint32_t i = 0; i < xg_memory_type_count_m; ++i ) {
        xg_vk_allocator_global_t* global_allocator = &context->global_allocators[i];

        xg_vk_allocator_tlsf_pool_t* tlsf_pool = &global_allocator->tlsf_pool;
        uint64_t idx = 0;
        while ( std_bitset_scan ( &idx, tlsf_pool->heaps_bitset, idx, std_bitset_u64_count_m ( xg_vk_allocator_tlsf_pool_max_heaps_m ) ) ) {
            xg_vk_allocator_tlsf_heap_deinit ( &tlsf_pool->heaps_array[idx] );
            ++idx;
        }
        std_virtual_heap_free ( tlsf_pool->heaps_array );
        std_virtual_heap_free ( tlsf_pool->heaps_bitset );
        std_mutex_deinit ( &tlsf_pool->heaps_mutex );

        xg_vk_allocator_reserved_pool_t* reserved_pool = &global_allocator->reserved_pool;
        idx = 0;
        while ( std_bitset_scan ( &idx, reserved_pool->allocations_bitset, idx, std_bitset_u64_count_m ( xg_vK_allocator_reserved_pool_max_allocs_m ) ) ) {
            xg_vk_allocator_vk_free ( &reserved_pool->allocations_array[idx] );
            ++idx;
        }
        std_virtual_heap_free ( reserved_pool->allocations_array );
        std_virtual_heap_free ( reserved_pool->allocations_bitset );
        std_mutex_deinit ( &reserved_pool->allocations_mutex );

#if std_build_debug_m && std_allocator_log_allocations_to_file_m
        std_file_close ( tlsf_pool->log_file );
        std_file_close ( reserved_pool->log_file );
#endif

        global_allocator->enabled = false;
    }
}

// --------------------------

void xg_vk_allocator_get_info ( xg_allocator_info_t* info, xg_device_h device_handle, xg_memory_type_e type ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_allocator_device_context_t* context = &xg_vk_allocator_state->device_contexts[device_idx];

    xg_vk_allocator_global_t* global_allocator = &context->global_allocators[type];

    uint64_t allocated_size = 0;
    uint64_t reserved_size = 0;

    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, global_allocator->tlsf_pool.heaps_bitset, idx, std_bitset_u64_count_m ( xg_vk_allocator_tlsf_pool_max_heaps_m ) ) ) {
        xg_vk_allocator_tlsf_heap_t* heap = &global_allocator->tlsf_pool.heaps_array[idx];
        reserved_size += heap->gpu_alloc.size;
        allocated_size = heap->allocated_size;
        ++idx;
    }

    info->allocated_size = allocated_size;
    info->reserved_size = reserved_size;

    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    uint64_t device_size = device->memory_types[type].size;
    info->system_size = device_size;
}
