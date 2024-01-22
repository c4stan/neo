#include <std_allocator.h>

#include <std_log.h>
#include <std_hash.h>
#include <std_buffer.h>
#include <std_list.h>
#include <std_platform.h>
#include <std_atomic.h>
#include <std_byte.h>

#include <malloc.h>

#include "std_init.h"

//==============================================================================

static std_allocator_state_t* std_allocator_state;

//==============================================================================

// 16 bytes. Guaranteed to be 16 bytes aligned.
typedef struct std_allocator_virtual_heap_segment_t {
    struct std_allocator_virtual_heap_segment_t* next;
    uint64_t size;
} std_allocator_virtual_heap_segment_t;

typedef struct {
    //size_t      max_free_segment_size;
    size_t      total_size;
    std_alloc_t alloc;
    //std_list_t  freelist;
    std_allocator_virtual_heap_segment_t* freelist;
} std_allocator_virtual_heap_node_t;

void std_allocator_tlsf_heap_init ( std_allocator_tlsf_heap_t* heap, uint64_t size );

//==============================================================================

// This function is supposed to do the minimum necessary to enable virtual memory allocations
// With the introduction of a shared virtual heap, this function now boots the virtual memory
// allocator and the virtual heap.
void std_allocator_boot ( void ) {
    std_assert_m ( std_allocator_state == NULL );
    static std_allocator_state_t state;

    // Get virtual page size
    {
#ifdef std_platform_win32_m
        SYSTEM_INFO si;
        GetSystemInfo ( &si );
        state.virtual_page_size = si.dwPageSize;
#elif defined(std_platform_linux_m)
        state.virtual_page_size = getpagesize();
#endif
        std_assert_m ( std_pow2_test ( state.virtual_page_size ) );
        //std_assert_m ( std_bit_scan_64 ( state.virtual_page_size, &state.virtual_page_size_bit_idx ) );
    }

    // Set up a pointer to the boot state
    // Will be used by the following virtual heap init code and by the incoming std_allocator_init call
    std_allocator_attach ( &state );

    // Virtual heap
    // This reserves virtual space for total_ram_size/virtual_page_size nodes, which on a 8GiB
    // system is about 2 million nodes, but only commits memory that's actually used.
    // TODO is this even correct? what about swap memory?
    // TODO read this from std_platform? can't, allocator needs to be init'd first... fix that?
    size_t total_ram_size;
    size_t total_swap_size;
    {
#if defined(std_platform_win32_m)
        ULONGLONG total_memory_kb;
        GetPhysicallyInstalledSystemMemory ( &total_memory_kb );
        total_ram_size = ( size_t ) ( total_memory_kb * 1024 );
        total_swap_size = 0; // TODO
#elif defined(std_platform_linux_m)
        struct sysinfo si;
        sysinfo ( &si );
        total_ram_size = ( size_t ) si.totalram;
        total_swap_size = ( size_t ) si.totalswap;
#endif
    }
    {
        size_t max_nodes_count = ( total_ram_size + total_swap_size ) / state.virtual_page_size;
        size_t nodes_reserve_size = sizeof ( std_allocator_virtual_heap_node_t ) * max_nodes_count;
        nodes_reserve_size = std_align ( nodes_reserve_size, state.virtual_page_size );
        state.virtual_heap.nodes_alloc = std_virtual_reserve ( nodes_reserve_size );
        state.virtual_heap.total_size = 0;
        state.virtual_heap.nodes_count = 0;
        state.virtual_heap.nodes_mapped_size = state.virtual_page_size;
        std_virtual_map ( state.virtual_heap.nodes_alloc.handle, 0, state.virtual_page_size );
        std_mutex_init ( &state.virtual_heap.mutex );
    }

    // Tagged heap
    {
        uint64_t tagged_page_size = 1024 * 1024 * 2; // TODO take as param?
        state.tagged_page_size = tagged_page_size;
        state.tagged_heap.tag_bin_map = std_static_hash_map_m ( state.tagged_heap.tag_bin_map_keys, state.tagged_heap.tag_bin_map_payloads );
        std_alloc_t bins_alloc = std_virtual_alloc ( tagged_page_size ); // TODO should this be a literal? separate param?
        state.tagged_heap.bins_alloc = bins_alloc;
        size_t bin_size = tagged_page_size / std_tagged_allocator_max_bins_m;
        state.tagged_heap.bin_size = bin_size;
        state.tagged_heap.bin_capacity = bin_size / sizeof ( uint64_t ) - 1;
        state.tagged_heap.bins_freelist = std_freelist ( bins_alloc.buffer, bin_size );
        std_mutex_init ( &state.tagged_heap.mutex );
    }

    // tlsf heap
    {
        uint64_t initial_size = 1024ull * 1024 * 1024 * 4;
        std_allocator_tlsf_heap_init ( &state.tlsf_heap, initial_size );
    }
}

// This is assumed to be called right after boot
// Copy the local boot state into the external state
void std_allocator_init ( std_allocator_state_t* state ) {
    std_mem_copy_m ( state, std_allocator_state );
}

void std_allocator_attach ( std_allocator_state_t* state ) {
    std_allocator_state = state;
}

//==============================================================================

std_alloc_t std_allocator_virtual_heap_alloc ( size_t size, size_t align ) {
    std_assert_m ( align < std_allocator_state->virtual_page_size );
    std_assert_m ( size > 0 );

    if ( std_unlikely_m ( size == 0 ) ) {
        return std_null_alloc_m;
    }

    size_t aligned_size = std_align ( size, 16 );

    // Redirect to the virtual allocator if we're simply allocating one or more full pages and early out.
    if ( aligned_size % std_allocator_state->virtual_page_size == 0 ) {
        // Tag the lsb. Because of enforced 16 bytes minimum alignment we have a bunch of unused ls bits.
        std_alloc_t alloc = std_virtual_alloc ( aligned_size );
        std_bit_set_64 ( &alloc.handle.id, 0 );
        return alloc;
    }

    // Lock
    std_mutex_lock ( &std_allocator_state->virtual_heap.mutex );

    // Traverse the index looking for suitable free space
    for ( size_t i = 0 ; i < std_allocator_state->virtual_heap.nodes_count; ++i ) {
        std_allocator_virtual_heap_node_t* node;
        std_buffer_ptr_m ( &node, std_allocator_state->virtual_heap.nodes_alloc.buffer, i );

        //if ( node->max_free_segment_size >= size ) {

        // Traverse the freelist
        // Make sure that the segment is still big enough after taking alignment into consideration
        byte_t* prev = ( byte_t* ) &node->freelist;
        byte_t* curr = ( byte_t* ) node->freelist;

        while ( curr != NULL ) {
            /*
                If 16 bytes alignment isn't enough, that means that an extra segment can be created
                with the memory that's getting aligned out (guaranteed to be of size >= 16).
                The actual allocation size must also be aligned to 16, in order to guarantee minimum
                deallocable size and natural 16 bytes alignment for other segments.
                If an allocation of unaligned size X can fit in a segment, then also the aligned size
                can fit, because alignment is always respected. So it only needs to be accounted for
                when calculating total allocation size and potential segment splitting.
                If alignment required is less than 16, then it's fine because natural alignment coming
                from enforced padding and virtual pages alignment satisfies that. No additional alignment
                or checks are necessary.
            */
            byte_t* base = std_align_ptr ( curr, align );
            size_t align_cost = ( size_t ) ( base - curr );

            std_allocator_virtual_heap_segment_t* segment = ( std_allocator_virtual_heap_segment_t* ) curr;
            size_t size_available = segment->size - align_cost;
            size_t size_left = size_available - size;

            if ( segment->size < align_cost || size_available < size ) {
                // Next
                prev = curr;
                curr = std_list_next_m ( curr );
                continue;
            } else if ( size_left < 16 ) {
                // Take entire segment
                std_list_remove ( prev, curr );

                std_mutex_unlock ( &std_allocator_state->virtual_heap.mutex );

                std_alloc_t alloc;
                alloc.handle.id = ( uint64_t ) segment;
                alloc.handle.size = segment->size;
                alloc.buffer.base = base;
                alloc.buffer.size = size;
                return alloc;
            } else {
                // Take segment part
                size_t new_segment_size = size_available - aligned_size;

                std_allocator_virtual_heap_segment_t* new_segment = ( std_allocator_virtual_heap_segment_t* ) ( base + aligned_size );
                std_list_remove ( prev, curr );
                std_list_insert ( prev, new_segment );
                new_segment->size = new_segment_size;

                std_mutex_unlock ( &std_allocator_state->virtual_heap.mutex );

                std_alloc_t alloc;
                alloc.handle.id = ( uint64_t ) segment;
                alloc.handle.size = aligned_size; //segment->size;
                alloc.buffer.base = base;
                alloc.buffer.size = size;
                return alloc;
            }
        }

        //}
    }

    //std_assert_m ( std_allocator_state->virtual_heap.nodes_count == 0 );

    // Allocate new space if nothing was found
    std_allocator_virtual_heap_node_t* new_node;
    size_t idx = std_allocator_state->virtual_heap.nodes_count++;

    if ( std_allocator_state->virtual_heap.nodes_count * sizeof ( std_allocator_virtual_heap_node_t ) > std_allocator_state->virtual_heap.nodes_mapped_size ) {
        std_virtual_map ( std_allocator_state->virtual_heap.nodes_alloc.handle, std_allocator_state->virtual_heap.nodes_mapped_size, std_allocator_state->virtual_page_size );
        std_allocator_state->virtual_heap.nodes_mapped_size += std_allocator_state->virtual_page_size;
    }

    std_buffer_ptr_m ( &new_node, std_allocator_state->virtual_heap.nodes_alloc.buffer, idx );
    size_t node_size = std_align ( size, std_allocator_state->virtual_page_size );
    new_node->alloc = std_virtual_alloc ( node_size );
    std_allocator_state->virtual_heap.total_size += std_allocator_state->virtual_page_size;

    // Reserve requested space and set up the node
    byte_t* base = new_node->alloc.buffer.base;
    size_t segment_size = node_size - aligned_size;
    void* segment_base = base + aligned_size;
    std_auto_m segment = ( std_allocator_virtual_heap_segment_t* ) segment_base;
    segment->size = segment_size;
    new_node->freelist = segment;
    //new_node->max_free_segment_size = segment_size;
    new_node->total_size = node_size;

    std_mutex_unlock ( &std_allocator_state->virtual_heap.mutex );

    std_alloc_t alloc;
    alloc.handle.id = ( uint64_t ) base;
    alloc.handle.size = aligned_size;
    alloc.buffer.base = base;
    alloc.buffer.size = size;
    return alloc;
}

bool std_allocator_virtual_heap_free ( std_memory_h handle ) {
    if ( std_memory_handle_is_null_m ( handle ) ) {
        return true;
    }

    // Redirect to the virtual allocator if we're simply freeing one or more full pages and early out.
    if ( std_bit_test_64 ( handle.id, 0 ) ) {
        std_assert_m ( handle.size % std_allocator_state->virtual_page_size == 0 );
        std_bit_clear_64 ( &handle.id, 0 );
        return std_virtual_free ( handle );
    }

    std_mutex_lock ( &std_allocator_state->virtual_heap.mutex );

    // Look for the node containing the memory address
    for ( size_t i = 0; i < std_allocator_state->virtual_heap.nodes_count; ++i ) {
        std_allocator_virtual_heap_node_t* node;
        std_buffer_ptr_m ( &node, std_allocator_state->virtual_heap.nodes_alloc.buffer, i );

        if ( node->alloc.buffer.base <= ( byte_t* ) handle.id && node->alloc.buffer.base + node->alloc.buffer.size >= ( byte_t* ) handle.id + handle.size ) {
            byte_t* left = ( byte_t* ) &node->freelist;
            byte_t* right = ( byte_t* ) node->freelist;

            // Go through the freelist and find the two list elements that surround the memory address
            while ( right != NULL ) {
                if ( right > ( byte_t* ) handle.id ) {
                    break;
                }

                left = right;
                right = std_list_next_m ( right );
            }

            std_auto_m left_segment = ( std_allocator_virtual_heap_segment_t* ) left;
            std_auto_m right_segment = ( std_allocator_virtual_heap_segment_t* ) right;

            bool left_adjacent = left != ( byte_t* ) &node->freelist && left + left_segment->size == ( byte_t* ) handle.id;
            bool right_adjacent = ( byte_t* ) handle.id + handle.size == right;

            if ( left_adjacent ) {
                size_t size_increase = handle.size;

                if ( right_adjacent ) {
                    std_list_remove ( left, right );
                    size_increase += right_segment->size;
                }

                left_segment->size += size_increase;
            } else if ( right_adjacent ) {
                std_auto_m new_right_segment = ( std_allocator_virtual_heap_segment_t* ) handle.id;

                std_list_remove ( left, right );
                std_list_insert ( left, new_right_segment );

                new_right_segment->next = right_segment->next;
                new_right_segment->size = right_segment->size + handle.size;
            } else {
                std_auto_m new_segment = ( std_allocator_virtual_heap_segment_t* ) handle.id;

                std_list_insert ( left, new_segment );
                new_segment->size = handle.size;
            }

            std_mutex_unlock ( &std_allocator_state->virtual_heap.mutex );
            return true;
        }
    }

    // TODO return memory to the virtual allocator if the whole page is clean after this free?

    std_mutex_unlock ( &std_allocator_state->virtual_heap.mutex );
    return false;
}

static std_alloc_t std_virtual_heap_allocator_alloc ( void* impl, size_t size, size_t alignment ) {
    std_unused_m ( impl );
    return std_allocator_virtual_heap_alloc ( size, alignment );
}

static bool std_virtual_heap_allocator_free ( void* impl, std_memory_h handle ) {
    std_unused_m ( impl );
    return std_allocator_virtual_heap_free ( handle );
}

std_allocator_i std_virtual_heap_allocator ( void ) {
    std_allocator_i i;
    i.impl = NULL;

    i.alloc = std_virtual_heap_allocator_alloc;
    i.free = std_virtual_heap_allocator_free;
    return i;
}

// ====================================================================================
// TODO

typedef struct {
    uint64_t count;
    std_memory_h pages[];
} std_allocator_tagged_heap_bin_t;

size_t std_tagged_page_size ( void ) {
    return std_allocator_state->tagged_page_size;
}

std_buffer_t std_tagged_alloc ( size_t size, uint64_t tag ) {
    // TODO profile this mutex, find an alternative way if locking on every page alloc is too slow.
    std_assert_m ( size % std_allocator_state->tagged_page_size == 0 );
    std_alloc_t alloc = std_virtual_alloc ( size );

    std_mutex_lock ( &std_allocator_state->tagged_heap.mutex );
    uint64_t* lookup = std_hash_map_lookup ( &std_allocator_state->tagged_heap.tag_bin_map, tag );
    std_allocator_tagged_heap_bin_t* bin;

    if ( lookup == NULL ) {
        bin = ( std_allocator_tagged_heap_bin_t* ) std_list_pop ( std_allocator_state->tagged_heap.bins_freelist );
        std_assert_m ( bin );
        bin->count = 0;
        std_hash_map_insert ( &std_allocator_state->tagged_heap.tag_bin_map, tag, ( uint64_t ) bin );
    } else {
        bin = ( std_allocator_tagged_heap_bin_t* ) ( *lookup );
    }

    if ( std_unlikely_m ( bin->count == UINT64_MAX ) ) {
        std_log_warn_m ( "Trying to allocate into a bin that is currently being free'd. Fix your allocation pattern or increase max bin count." );
        return std_null_buffer_m;
    }

    uint64_t idx = bin->count++;
    std_assert_m ( idx < std_allocator_state->tagged_heap.bin_capacity );

    bin->pages[idx] = alloc.handle;
    std_mutex_unlock ( &std_allocator_state->tagged_heap.mutex );

    return alloc.buffer;
}

void std_tagged_free ( uint64_t tag ) {
    std_mutex_lock ( &std_allocator_state->tagged_heap.mutex );
    uint64_t* lookup = std_hash_map_lookup ( &std_allocator_state->tagged_heap.tag_bin_map, tag );
    std_assert_m ( lookup );
    std_allocator_tagged_heap_bin_t* bin = ( std_allocator_tagged_heap_bin_t* ) ( *lookup );
    uint64_t count = bin->count;
    bin->count = UINT64_MAX;
    std_mutex_unlock ( &std_allocator_state->tagged_heap.mutex );

    for ( uint64_t i = 0; i < count; ++i ) {
        std_virtual_free ( bin->pages[i] );
    }

    std_mutex_lock ( &std_allocator_state->tagged_heap.mutex );
    std_list_push ( &std_allocator_state->tagged_heap.bins_freelist, bin );
    std_mutex_unlock ( &std_allocator_state->tagged_heap.mutex );
}

//==============================================================================

std_virtual_buffer_t std_virtual_buffer_reserve ( size_t size ) {
    size_t page_size = std_virtual_page_size();

    std_virtual_buffer_t buffer;
    std_alloc_t alloc = std_virtual_reserve ( size );
    buffer.handle = alloc.handle;
    buffer.base = alloc.buffer.base;
    buffer.reserved_size = alloc.buffer.size;
    buffer.mapped_size = page_size;
    std_virtual_map ( buffer.handle, 0, page_size );
    buffer.top = 0;

    return buffer;
}

byte_t* std_virtual_buffer_push ( std_virtual_buffer_t* buffer, size_t size ) {
    size_t page_size = std_virtual_page_size();

    size_t old_top = buffer->top;
    size_t top = old_top + size;
    std_assert_m ( top < buffer->reserved_size );
    size_t mapped_size = buffer->mapped_size;

    if ( top > mapped_size ) {
        size_t extra_size = top - mapped_size;
        size_t map_size = std_align ( extra_size, page_size );

        std_assert_m ( std_virtual_map ( buffer->handle, mapped_size, map_size ) );
        buffer->mapped_size += map_size;
    }

    buffer->top = top;
    return buffer->base + old_top;
}

void std_virtual_buffer_free ( std_virtual_buffer_t* buffer ) {
    std_virtual_free ( buffer->handle );
    buffer->handle = std_null_memory_handle_m;
}

//==============================================================================

size_t std_virtual_page_size ( void ) {
    return std_allocator_state->virtual_page_size;
}

std_alloc_t std_virtual_alloc ( size_t size ) {
    std_alloc_t alloc = std_virtual_reserve ( size );
    bool map = std_virtual_map ( alloc.handle, 0, size );
    std_assert_m ( map );
    return alloc;
}

std_alloc_t std_virtual_reserve ( size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );

    byte_t* base;
#ifdef std_platform_win32_m
    base = ( byte_t* ) VirtualAlloc ( NULL, size, MEM_RESERVE, PAGE_NOACCESS );
#elif defined(std_platform_linux_m)
    base = ( byte_t* ) mmap ( NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0 );
#endif
    std_assert_m ( base != NULL );
    std_alloc_t alloc;
    alloc.handle.id = ( uint64_t ) base;
    alloc.handle.size = size;
    alloc.buffer.base = base;
    alloc.buffer.size = size;
    return alloc;
}

bool std_virtual_map ( std_memory_h reserve_handle, size_t offset, size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );
    std_assert_m ( std_align_test ( offset, std_allocator_state->virtual_page_size ) );

    // TODO do some validation/state tracking on the handle?
    byte_t* base = ( byte_t* ) reserve_handle.id + offset;
    std_assert_m ( base != NULL );
    std_assert_m ( std_align_test_ptr ( base, std_allocator_state->virtual_page_size ) );

    bool result = false;
#ifdef std_platform_win32_m
    result = VirtualAlloc ( base, size, MEM_COMMIT, PAGE_READWRITE ) == ( void* ) base;
#elif defined(std_platform_linux_m)
    result = mprotect ( base, size, PROT_READ | PROT_WRITE ) == 0;
#endif
    return result;
}

bool std_virtual_unmap ( std_memory_h handle, size_t offset, size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );
    std_assert_m ( std_align_test ( offset, std_allocator_state->virtual_page_size ) );

    byte_t* base = ( byte_t* ) handle.id + offset;
    std_assert_m ( base != NULL );
    std_assert_m ( std_align_test_ptr ( base, std_allocator_state->virtual_page_size ) );

    bool result;
#ifdef std_platform_win32_m
    result = VirtualFree ( base, size, MEM_DECOMMIT ) == TRUE;
#elif defined(std_platform_linux_m)
    result = mprotect ( base, size, PROT_NONE ) == 0;
#endif
    return result;
}

bool std_virtual_free ( std_memory_h handle ) {
    byte_t* base = ( byte_t* ) handle.id;
    std_assert_m ( base != NULL );
    std_assert_m ( std_align_test_ptr ( base, std_allocator_state->virtual_page_size ) );

    bool result;
#ifdef std_platform_win32_m
    result = VirtualFree ( base, 0, MEM_RELEASE ) == TRUE;
#elif defined(std_platform_linux_m)
    result = munmap ( base, handle.size ) == 0;
#endif
    return result;
}

static std_alloc_t std_virtual_allocator_alloc ( void* allocator, size_t size, size_t align ) {
    std_unused_m ( allocator );
    std_unused_m ( align );
    return std_virtual_alloc ( size );
}

static bool std_virtual_allocator_free ( void* allocator, std_memory_h memory ) {
    std_unused_m ( allocator );
    return std_virtual_free ( memory );
}

std_allocator_i std_virtual_allocator ( void ) {
    std_allocator_i i;
    i.alloc = std_virtual_allocator_alloc;
    i.free = std_virtual_allocator_free;
    return i;
}

//==============================================================================

std_stack_t std_stack ( std_buffer_t buffer ) {
    std_stack_t allocator;
    allocator.buffer = buffer;
    allocator.top = 0;
    return allocator;
}

std_alloc_t std_stack_alloc ( std_stack_t* allocator, size_t size, size_t align ) {
    byte_t* base = allocator->buffer.base;
    size_t old_top = allocator->top;
    byte_t* user_ptr = ( byte_t* ) std_align_ptr ( base + old_top, align );
    size_t new_top = ( size_t ) ( user_ptr - base ) + size;
    allocator->top = new_top;
    std_assert_m ( allocator->top <= allocator->buffer.size );
    std_alloc_t alloc;
    alloc.handle.id = ( uint64_t ) old_top;
    alloc.handle.size = new_top - old_top;
    alloc.buffer.base = user_ptr;
    alloc.buffer.size = size;
    return alloc;
}

byte_t* std_stack_push ( std_stack_t* allocator, size_t size, size_t align ) {
    byte_t* base = allocator->buffer.base;
    size_t old_top = allocator->top;
    byte_t* user_ptr = ( byte_t* ) std_align_ptr ( base + old_top, align );
    size_t new_top = ( size_t ) ( user_ptr - base ) + size;
    allocator->top = new_top;
    std_assert_m ( allocator->top <= allocator->buffer.size );
    return user_ptr;
}

byte_t* std_stack_push_noalign ( std_stack_t* allocator, size_t size ) {
    size_t old_top = allocator->top;
    allocator->top = old_top + size;
    std_assert_m ( old_top + size < allocator->buffer.size );
    return allocator->buffer.base + old_top;
}

void std_stack_push_copy ( std_stack_t* allocator, const void* data, size_t size, size_t align ) {
    byte_t* dest = std_stack_push ( allocator, size, align );
    std_mem_copy ( dest, data, size );
}

void std_stack_push_copy_noalign ( std_stack_t* allocator, const void* data, size_t size ) {
    byte_t* dest = std_stack_push_noalign ( allocator, size );
    std_mem_copy ( dest, data, size );
}

void std_stack_align ( std_stack_t* allocator, size_t align ) {
    size_t old_top = allocator->top;
    size_t new_top = std_align ( old_top, align );
    allocator->top = new_top;
    std_assert_m ( allocator->top <= allocator->buffer.size );
}

void std_stack_align_zero ( std_stack_t* allocator, size_t align ) {
    size_t old_top = allocator->top;
    size_t new_top = std_align ( old_top, align );
    std_mem_zero ( allocator->buffer.base + old_top, new_top - old_top );
    allocator->top = new_top;
    std_assert_m ( allocator->top <= allocator->buffer.size );
}

bool std_stack_free ( std_stack_t* allocator, std_memory_h memory ) {
    size_t top = ( size_t ) ( memory.id + memory.size );

    if ( top == allocator->top ) {
        std_assert_m ( memory.id < allocator->top );
        allocator->top = ( size_t ) memory.id;
        return true;
    } else {
        return false;
    }

}

void std_stack_pop ( std_stack_t* allocator, size_t size ) {
    std_assert_m ( allocator->top >= size );
    allocator->top -= size;
}

void std_stack_clear ( std_stack_t* allocator ) {
    allocator->top = 0;
}

static std_alloc_t std_stack_allocator_alloc ( void* allocator, size_t size, size_t align ) {
    return std_stack_alloc ( ( std_stack_t* ) allocator, size, align );
}

static bool std_stack_allocator_free ( void* allocator, std_memory_h memory ) {
    return std_stack_free ( ( std_stack_t* ) allocator, memory );
}

std_allocator_i std_stack_allocator ( std_stack_t* allocator ) {
    std_allocator_i i;
    i.alloc = std_stack_allocator_alloc;
    i.free = std_stack_allocator_free;
    i.impl = allocator;
    return i;
}

//==============================================================================
// TLSF allocator
// https://github.com/sebbbi/OffsetAllocator
// http://www.gii.upv.es/tlsf/files/papers/tlsf_desc.pdf
// https://github.com/mattconte/tlsf
// http://www.gii.upv.es/tlsf/main/docs.html

/*
    Data structure:
        2D table of freelists, each containing blocks of a certain unique size range
        X dimension is power of 2 size ranges, starting from an arbitrary value > 0 (e.g. 5) up to an arbitrary value <= 64. these determine the min/max block sizes
        Y dimension is further linear subdivision of the range into an arbitrary (e.g. 8/16/32) number of buckets
            Y: [0 -> max_y - 1]
            X: [min_x -> max_x - 1]
            t = (2^(x+1) - 2^x) / max_y
            bin[x][y] contains segments of size [2^x + t*y -> 2^x + t*(y+1) - 1 = 2^(x+1) - 1]

        2D table of bits of the same size as the freelist table, each bit indicating whether each corresponding freelist is empty or not

        bitfield, each bit indicating whether at least one bit is set to 1 in the corresponding row in the 2D table of bits along the X dimension

        The following uses an X range that goes from 7 to 32(supporting up to 64), and 16 buckets per level. It follows that the 2D table of bits is a u16 array and the bitfield is a u64. This guarantees 8 alignment on all blocks, support for possible higher segment size, and a decent spread along Y.

    Indexing:
        To find the freelist corresponding to a given size:
        x = floor ( log2 ( size ) )                 = bit_scan_rev ( size )
        y = floor ( size - 2^x ) / 2 ^ ( x - L )    = ( size >> ( x - L ) ) - 2^L
        where 2^L is the number of Y subdivision (e.g. L=4 for 16 subdivisions).

        Since buckets contain size ranges, to find a free block >= size it rounds up the size to the next bucket, to avoid having to traverse the freelist:
        size += 1 << ( bit_scan_rev ( size ) - L ) - 1

        The 2D table of bits and the u64 bitfield are both used to account for empty freelists and quickly get to the closest non-empty one:
        t = bits[x] & ( 0xffff << y )
        if t != 0
            y = bit_scan ( t )
        else
            t = bitfield & ( 0xffff << ( x + 1 ) )
            x = bit_scan ( t )
            y = bit_scan ( bits[x] )

    Allocation
        When allocating:
            round up the requested size to the next bucket, so that all segments in it are guaranteed to be able to contain the requested allocation size 
            starting from that bucket, find the first bucket with a non-empty freelist
            pop its freelist head
            if the returned block is too big, split it and pool back the extra part to some other bucket
        When freeing:
            check left and right block margins for possible mergings
            if mergings happen remove the left/right merged blocks from their freelists
            find the bucket corresponding to the final merged block and pool it back into the bucket freelist
        Minimum allocation size requestable by the user is 2^min_x - sizeof(header). Any size smaller than that will get padded to that.
        Maximum allocation size requestable by the user is 2^max_x - 1 - sizeof(header).

    Allocated block contents:
        ------------------------------------------
        |       header       |     user data     |
        ------------------------------------------
        |  size + flags : 8  |       120/+       |
        ------------------------------------------

        size goes up to u32_max = 4gib. for bigger allocations just redirect to the virtual allocator and tag the allocation handle appropriately
        all blocks are 4 byte aligned, so size is also 4 byte aligned, so first 2 bits in size can be used to store additional flags
        bit 0 is used to tag whether the block is free or not
        bit 1 is used to tag whether the *prev* block (the one adjacent on the left of the memory address space) is free or not.
        //bit 2 is used to tag whether the *next* block (the one adjacent on the left of the memory address space) is free or not.
        min block size is 128 bytes, so user data is 120 bytes or more

    Free block contents:
        ---------------------------------------------------------------------------------------------------------------------------
        |                                             header                              |       unused        |      footer     |
        ---------------------------------------------------------------------------------------------------------------------------
        |     size + flags : 8    |  next free block ptr : 8  |  prev free block ptr : 8  |        96/+         |     size : 8    |
        ---------------------------------------------------------------------------------------------------------------------------

        size and flags are the same as above
        next and prev free block ptrs are just that, they form the doubly linked freelist
        unused will be either 96 bytes or more if the block is bigger than 128 bytes
        final size is the same value that's at the beginning, without the flags. it is there so that when an allocated block gets freed, it can check its 'prev free' bit, and possibly go read this size and use it to find the beginning of the block

    TODO use 6 as min level instead of 5. use 8 bytes to store sizes. guarantees 8 align on all blocks and allows to store additional "next phys block exists" flag.
*/

#define std_allocator_tlsf_header_size_m 8 // freelist pointers excluded
#define std_allocator_tlsf_footer_size_m 8
#define std_allocator_tlsf_min_segment_size_m ( 1 << std_allocator_tlsf_min_x_level_m )
#define std_allocator_tlsf_max_segment_size_m ( ( 1ull << std_allocator_tlsf_max_x_level_m ) - 1 )
#define std_allocator_tlsf_free_segment_bit_m (1 << 0)
#define std_allocator_tlsf_free_prev_segment_bit_m (1 << 1)
#define std_allocator_tlsf_size_mask_m ( ( ~0ull ) << 3 )

typedef struct {
    uint64_t x;
    uint64_t y;
} std_allocator_tlsf_freelist_idx_t;

typedef struct std_allocator_tlsf_header_t {
    uint64_t size_flags;
    struct std_allocator_tlsf_header_t* next;
    struct std_allocator_tlsf_header_t* prev;
} std_allocator_tlsf_header_t;

typedef struct {
    uint64_t size;
} std_allocator_tlsf_footer_t;

std_allocator_tlsf_freelist_idx_t std_allocator_tlsf_freelist_idx ( uint64_t size ) {
    std_allocator_tlsf_freelist_idx_t idx;
    idx.x = 63 - std_bit_scan_rev_64 ( size );
    idx.y = ( size >> ( idx.x - std_allocator_tlsf_log2_y_size_m ) ) - std_allocator_tlsf_y_size_m;
    // offset the x level so that the min level indexes the tables at 0
    idx.x = std_max_u64 ( idx.x, std_allocator_tlsf_min_x_level_m ) - std_allocator_tlsf_min_x_level_m;
    return idx;
}

uint64_t std_allocator_tlsf_heap_size_roundup ( uint64_t size ) {
    size += ( 1ull << ( 63 - std_bit_scan_rev_64 ( size ) - std_allocator_tlsf_log2_y_size_m ) ) - 1;
    return size;
}

std_allocator_tlsf_freelist_idx_t std_allocator_tlsf_freelist_idx_first_available ( std_allocator_tlsf_heap_t* heap, uint64_t size, std_allocator_tlsf_freelist_idx_t base ) {
    std_allocator_tlsf_freelist_idx_t idx;

    uint32_t mask = ( 1 << std_allocator_tlsf_y_size_m ) - 1;
    uint32_t t = heap->available_freelists[base.x] & ( mask << base.y );

    if ( t != 0 ) {
        idx.x = base.x;
        idx.y = std_bit_scan_32 ( t );
    } else {
        uint32_t mask = ( 1 << std_allocator_tlsf_x_size_m ) - 1;
        t = heap->available_rows & ( mask << ( base.x + 1 ) );
        std_assert_m ( t );
        idx.x = std_bit_scan_32 ( t );
        std_assert_m ( heap->available_freelists[idx.x] );
        idx.y = std_bit_scan_32 ( heap->available_freelists[idx.x] );
    }

    return idx;
}

void std_allocator_tlsf_add_to_freelist ( std_allocator_tlsf_heap_t* heap, std_allocator_tlsf_header_t* header, uint64_t size ) {
    std_allocator_tlsf_freelist_idx_t idx = std_allocator_tlsf_freelist_idx ( size );
    std_dlist_push ( &heap->freelists[idx.x][idx.y], &header->next );
    heap->available_freelists[idx.x] |= 1 << idx.y;
    heap->available_rows |= 1ull << idx.x;
}

void std_allocator_tlsf_remove_from_freelist ( std_allocator_tlsf_heap_t* heap, std_allocator_tlsf_header_t* header, uint64_t size ) {
    std_allocator_tlsf_freelist_idx_t idx = std_allocator_tlsf_freelist_idx ( size );
    std_dlist_remove ( &header->next );

    if ( heap->freelists[idx.x][idx.y] == NULL ) {
        heap->available_freelists[idx.x] &= ~ ( 1 << idx.y );

        if ( heap->available_freelists[idx.x] == 0 ) {
            heap->available_rows &= ~ ( 1ull << idx.x );
        }
    }
}

byte_t* std_allocator_tlsf_pop_from_freelist ( std_allocator_tlsf_heap_t* heap, uint64_t size ) {
    std_allocator_tlsf_freelist_idx_t start_idx = std_allocator_tlsf_freelist_idx ( size );
    std_allocator_tlsf_freelist_idx_t idx = std_allocator_tlsf_freelist_idx_first_available ( heap, size, start_idx );
    std_auto_m segment = std_dlist_pop ( &heap->freelists[idx.x][idx.y] ) - std_allocator_tlsf_header_size_m;

    if ( heap->freelists[idx.x][idx.y] == NULL ) {
        heap->available_freelists[idx.x] &= ~ ( 1 << idx.y );

        if ( heap->available_freelists[idx.x] == 0 ) {
            heap->available_rows &= ~ ( 1ull << idx.x );
        }
    }

    return segment;
}

void std_allocator_tlsf_heap_grow ( std_allocator_tlsf_heap_t* heap, uint64_t size ) {
    std_assert_m ( size >= std_allocator_tlsf_min_segment_size_m );
    std_assert_m ( std_align_test_u64 ( size, 8 ) );

    uint64_t prev_top = heap->buffer.top;
    bool empty = prev_top == 0;
    byte_t* new_segment = std_virtual_buffer_push ( &heap->buffer, size );

    if ( !empty ) {
        // not the first grow
        byte_t* base = heap->buffer.base;

        byte_t* top_segment_end = base + prev_top;
        std_auto_m top_segment_footer = ( std_allocator_tlsf_footer_t* ) ( top_segment_end - std_allocator_tlsf_footer_size_m );
        uint64_t top_segment_size = top_segment_footer->size;

        byte_t* top_segment_start = top_segment_end - top_segment_size;
        std_auto_m top_segment_header = ( std_allocator_tlsf_header_t* ) top_segment_start;
        uint64_t top_size_flags = top_segment_header->size_flags;
        std_assert_m ( ( top_size_flags & std_allocator_tlsf_size_mask_m ) == top_segment_size );

        if ( top_size_flags & std_allocator_tlsf_free_segment_bit_m ) {
            std_assert_m ( ( top_size_flags & std_allocator_tlsf_free_prev_segment_bit_m ) == 0 );
            //std_assert_m ( ( top_size_flags & std_allocator_tlsf_free_next_segment_bit_m ) == 0 );
            top_segment_footer->size = top_segment_size + size;
            top_segment_header->size_flags = top_size_flags + size;

            std_allocator_tlsf_remove_from_freelist ( heap, top_segment_header, top_segment_size );
            std_allocator_tlsf_add_to_freelist ( heap, top_segment_header, top_segment_size + size );
        } else {
            std_auto_m header = ( std_allocator_tlsf_header_t* ) new_segment;
            header->size_flags = size | std_allocator_tlsf_free_segment_bit_m;

            std_auto_m footer = ( std_allocator_tlsf_footer_t* ) ( new_segment + size - std_allocator_tlsf_footer_size_m );
            footer->size = size;

            std_allocator_tlsf_add_to_freelist ( heap, header, size );
        }

    } else {
        // first grow, single segment
        std_auto_m header = ( std_allocator_tlsf_header_t* ) new_segment;
        header->size_flags = size | std_allocator_tlsf_free_segment_bit_m;

        std_auto_m footer = ( std_allocator_tlsf_footer_t* ) ( new_segment + size - std_allocator_tlsf_footer_size_m );
        footer->size = size;

        std_allocator_tlsf_add_to_freelist ( heap, header, size );

        std_noop_m;
    }
}

void std_allocator_tlsf_heap_init ( std_allocator_tlsf_heap_t* heap, uint64_t size ) {
    std_mem_zero_m ( &heap->freelists );
    std_mem_zero_m ( &heap->available_freelists );
    std_mem_zero_m ( &heap->available_rows );

    std_mutex_init ( &heap->mutex );

#if 0
    uint64_t s = std_allocator_tlsf_min_segment_size_m;
    while ( s < std_allocator_tlsf_max_segment_size_m ) {
        std_allocator_tlsf_freelist_idx_t idx = std_allocator_tlsf_freelist_idx ( s );
        std_log_info_m ( std_fmt_u64_m " " std_fmt_u64_m " " std_fmt_u64_m, s, idx.x, idx.y );
        s = std_allocator_tlsf_heap_size_roundup ( s + 1 );
    }
#endif

    heap->buffer = std_virtual_buffer_reserve ( std_allocator_tlsf_max_segment_size_m + 1 ); // +1 to align to page size
    std_allocator_tlsf_heap_grow ( heap, size );
}

std_alloc_t std_tlsf_heap_alloc ( std_allocator_tlsf_heap_t* heap, uint64_t size, uint64_t align ) {
    // check size
    std_static_assert_m ( std_allocator_tlsf_header_size_m == 8 );
    size = std_align ( size, 8 );
    size = align > std_allocator_tlsf_header_size_m ? size + align - std_allocator_tlsf_header_size_m : size;
    size += std_allocator_tlsf_header_size_m;
    size = std_max_u64 ( size, std_allocator_tlsf_min_segment_size_m );
    std_assert_m ( size <= std_allocator_tlsf_max_segment_size_m );

    uint64_t size_roundup = std_allocator_tlsf_heap_size_roundup ( size );

    std_mutex_lock ( &heap->mutex );

    // grab from freelist
    byte_t* segment = std_allocator_tlsf_pop_from_freelist ( heap, size_roundup );

    // load segment
    std_auto_m segment_header = ( std_allocator_tlsf_header_t* ) segment;
    std_allocator_tlsf_header_t new_header = *segment_header;
    uint64_t segment_size = ( new_header.size_flags & std_allocator_tlsf_size_mask_m );
    std_auto_m segment_footer = ( std_allocator_tlsf_footer_t* ) ( segment + segment_size - std_allocator_tlsf_footer_size_m );
    std_allocator_tlsf_footer_t new_footer = *segment_footer;
    std_assert_m ( new_header.size_flags & std_allocator_tlsf_free_segment_bit_m );
    std_assert_m ( segment_size >= size );

    // split
    bool needs_split = segment_size - size >= std_allocator_tlsf_min_segment_size_m;

    if ( needs_split ) {
        uint64_t extra_size = segment_size - size;

        std_auto_m extra_segment = ( byte_t* ) segment + size;
        std_auto_m extra_segment_header = ( std_allocator_tlsf_header_t* ) extra_segment;
        std_auto_m extra_segment_footer = ( std_allocator_tlsf_footer_t* ) ( extra_segment + extra_size - std_allocator_tlsf_footer_size_m );
        std_assert_m ( extra_segment_footer == segment_footer );

        new_header.size_flags = size | ( new_header.size_flags & ~std_allocator_tlsf_size_mask_m );// | std_allocator_tlsf_free_next_segment_bit_m;
        segment_footer = ( std_allocator_tlsf_footer_t* ) ( segment + size - std_allocator_tlsf_footer_size_m );
        new_footer.size = size;
        segment_size = size;

        uint64_t extra_flags = ( new_header.size_flags & ~std_allocator_tlsf_size_mask_m ) & ~std_allocator_tlsf_free_prev_segment_bit_m;
        extra_segment_header->size_flags = extra_size | extra_flags;
        extra_segment_footer->size = extra_size;

        std_allocator_tlsf_add_to_freelist ( heap, extra_segment_header, extra_size );
    }

    // update next
    // if a split was made, next has already been updated, no need to do it again
    if ( !needs_split && segment + segment_size != heap->buffer.base + heap->buffer.top ) {
        std_auto_m next_header = ( std_allocator_tlsf_header_t* ) ( segment + segment_size );
        uint64_t next_size_flags = next_header->size_flags;
        std_assert_m ( next_size_flags & std_allocator_tlsf_free_prev_segment_bit_m );
        next_size_flags &= ~std_allocator_tlsf_free_prev_segment_bit_m;
        next_header->size_flags = next_size_flags;
    }

    // update this
    uint64_t segment_flags = ( new_header.size_flags & ~std_allocator_tlsf_size_mask_m ) & ~std_allocator_tlsf_free_segment_bit_m;
    new_header.size_flags = segment_size | segment_flags;
    *segment_header = new_header;
    *segment_footer = new_footer;

    std_mutex_unlock ( &heap->mutex );

    byte_t* user_data = std_align_ptr ( segment + std_allocator_tlsf_header_size_m, align );
    uint64_t user_size = segment + segment_size - user_data;
    std_alloc_t alloc;
    alloc.handle.id = ( uint64_t ) segment;
    alloc.handle.size = segment_size;
    alloc.buffer.base = user_data;
    alloc.buffer.size = user_size;
    std_assert_m ( user_data );
    return alloc;
}

void std_tlsf_heap_free ( std_allocator_tlsf_heap_t* heap, std_memory_h handle ) {
    if ( std_memory_handle_is_null_m ( handle ) ) {
        return;
    }

    std_auto_m segment = ( byte_t* ) handle.id;
    uint64_t segment_size = handle.size;

    std_mutex_lock ( &heap->mutex );

    //std_log_info_m ( std_fmt_str_m std_fmt_ptr_m, "F ", segment );

    // load segment
    std_auto_m segment_header = ( std_allocator_tlsf_header_t* ) segment;
    //std_auto_m segment_footer = ( std_allocator_tlsf_footer_t* ) ( segment + segment_size - std_allocator_tlsf_footer_size_m );

    byte_t* new_segment = segment;
    std_allocator_tlsf_header_t new_header = *segment_header;
    //std_allocator_tlsf_footer_t new_footer = *segment_footer;
    std_allocator_tlsf_footer_t new_footer;
    new_footer.size = segment_size;

    std_assert_m ( ( new_header.size_flags & std_allocator_tlsf_size_mask_m ) == segment_size );
    std_assert_m ( ( new_header.size_flags & std_allocator_tlsf_free_segment_bit_m ) == 0 );
    new_header.size_flags |= std_allocator_tlsf_free_segment_bit_m;

    // check prev
    if ( segment != heap->buffer.base ) {
        if ( new_header.size_flags & std_allocator_tlsf_free_prev_segment_bit_m ) {
            std_auto_m prev_footer = * ( std_allocator_tlsf_footer_t* ) ( new_segment - std_allocator_tlsf_footer_size_m );
            std_auto_m prev_header_ptr = ( std_allocator_tlsf_header_t* ) ( segment - prev_footer.size );
            std_allocator_tlsf_header_t prev_header = *prev_header_ptr;
            std_assert_m ( prev_header.size_flags & std_allocator_tlsf_free_segment_bit_m );

            uint64_t prev_size = prev_header.size_flags & std_allocator_tlsf_size_mask_m;
            uint64_t prev_flags = prev_header.size_flags & ~std_allocator_tlsf_size_mask_m;
            // remove prev from freelist
            std_allocator_tlsf_remove_from_freelist ( heap, &prev_header, prev_header.size_flags & std_allocator_tlsf_size_mask_m );

            // merge
            uint64_t new_size = ( new_header.size_flags & std_allocator_tlsf_size_mask_m ) + prev_size;
            uint64_t new_flags = prev_flags & std_allocator_tlsf_free_prev_segment_bit_m;
            new_header.size_flags = new_size | new_flags;
            new_footer.size = new_size;
            new_segment = ( byte_t* ) prev_header_ptr;
        }
    }

    // check next
    if ( segment + segment_size != heap->buffer.base + heap->buffer.top ) {

        byte_t* next_segment = segment + segment_size;
        std_auto_m next_header_ptr = ( std_allocator_tlsf_header_t* ) next_segment;
        std_allocator_tlsf_header_t next_header = *next_header_ptr;
        uint64_t next_size = next_header.size_flags & std_allocator_tlsf_size_mask_m;
        uint64_t next_flags = next_header.size_flags & ~std_allocator_tlsf_size_mask_m;
        std_assert_m ( ( next_flags & std_allocator_tlsf_free_prev_segment_bit_m ) == 0 );

        if ( next_flags & std_allocator_tlsf_free_segment_bit_m ) {
            // remove next from freelist
            std_allocator_tlsf_remove_from_freelist ( heap, &next_header, next_header.size_flags & std_allocator_tlsf_size_mask_m );

            // merge
            uint64_t new_size = ( new_header.size_flags & std_allocator_tlsf_size_mask_m ) + next_size;
            uint64_t new_flags = new_header.size_flags & ~std_allocator_tlsf_size_mask_m ;
            new_header.size_flags = new_size | new_flags;
            new_footer.size = new_size;
        } else {
            next_header_ptr->size_flags = next_header.size_flags | std_allocator_tlsf_free_prev_segment_bit_m;
        }
    }

    // write and add to freelist
    new_header.size_flags |= std_allocator_tlsf_free_segment_bit_m;
    segment_header = ( std_allocator_tlsf_header_t* ) new_segment;
    std_auto_m segment_footer = ( std_allocator_tlsf_footer_t* ) ( new_segment + new_footer.size - std_allocator_tlsf_footer_size_m );
    *segment_header = new_header;
    *segment_footer = new_footer;
    std_allocator_tlsf_add_to_freelist ( heap, segment_header, new_footer.size );

    std_mutex_unlock ( &heap->mutex );
}

void std_allocator_tlsf_heap_deinit ( std_allocator_tlsf_heap_t* heap ) {
    std_virtual_buffer_free ( &heap->buffer );
}

//==============================================================================

std_alloc_t std_tlsf_alloc ( uint64_t size, uint64_t align ) {
    std_allocator_tlsf_heap_t* heap = &std_allocator_state->tlsf_heap;
    return std_tlsf_heap_alloc ( heap, size, align );
}

void std_tlsf_free ( std_memory_h handle ) {
    std_allocator_tlsf_heap_t* heap = &std_allocator_state->tlsf_heap;
    std_tlsf_heap_free ( heap, handle );
}

//==============================================================================

std_alloc_t std_virtual_heap_alloc ( size_t size, size_t align ) {
    return std_tlsf_alloc ( size, align );
    //return std_allocator_virtual_heap_alloc ( size, align );
}

bool std_virtual_heap_free ( std_memory_h handle ) {
    std_tlsf_free ( handle );
    return true; // TODO
    //return std_allocator_virtual_heap_free ( handle );
}


//==============================================================================

/*
TODO implement realloc?

std_alloc_t std_atomic_stack_alloc_i ( void* allocator, size_t size, size_t align ) {
    return std_atomic_stack_alloc ( ( std_atomic_stack_allocator_t* ) allocator, size, align );
}

std_alloc_t std_atomic_stack_realloc_i ( void* allocator, std_memory_h old_memory, size_t new_size, size_t align ) {
    return std_atomic_stack_realloc ( ( std_atomic_stack_allocator_t* ) allocator, old_memory, new_size, align );
}

bool std_atomic_stack_free_i ( void* allocator, std_memory_h memory ) {
    return std_atomic_stack_free ( ( std_atomic_stack_allocator_t* ) allocator, memory );
}

std_allocator_i std_atomic_stack_i ( std_atomic_stack_allocator_t* allocator ) {
    std_allocator_i i;
    i.alloc = std_atomic_stack_alloc_i;
    i.realloc = std_atomic_stack_realloc_i;
    i.free = std_atomic_stack_free_i;
    i.impl = allocator;
    return i;
}
*/

/*
#if std_enabled_m(std_memory_track_m)

// ====================o=============================
// Memory tracker
// =================================================
// The only aim for this is to help with tracking leaking allocations.
// Every allocator that wants to use this has to register itself as user,
// de-register itself on destruction, and record all memory allocations and frees.
//
// ( allocator ) -> ( allocations list )
// allocations number can grow indefinitely and items are likely to be removed sparsely
// they also don't require any order and can be defragmented
// the API must support multi threaded operations

typedef struct {
    char name[std_memory_track_user_name_size_m];
    void* allocator;
    std_list_t freelist;
    std_buffer_t allocations;
    std_mutex_t mutex;
} std_memory_tracker_user_t;

typedef struct {
    void* map_keys[std_memory_track_max_users_m * 2];                             // allocator ptr
    std_memory_tracker_user_t map_payloads[std_memory_track_max_users_m * 2];     // user
    std_map_t map;                                                              // ( allocator ) -> ( user )
    std_mutex_t mutex;
} std_memory_tracker_t;

#endif
*/
