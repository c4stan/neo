#include <std_allocator.h>

#include <std_log.h>
#include <std_hash.h>
#include <std_list.h>
#include <std_platform.h>
#include <std_atomic.h>
#include <std_byte.h>

#include <malloc.h>

#include "std_state.h"

//==============================================================================

static std_allocator_state_t* std_allocator_state;

//==============================================================================

// Virtual memory

size_t std_virtual_page_size ( void ) {
    return std_allocator_state->virtual_page_size;
}

size_t std_virtual_page_align ( size_t size ) {
    return std_align ( size, std_allocator_state->virtual_page_size );
}

#if 0
std_virtual_buffer_t std_virtual_buffer ( void* base, size_t mapped, size_t reserved ) {
    std_virtual_buffer_t buffer;
    buffer.base = base;
    buffer.mapped_size = mapped;
    buffer.reserved_size = reserved;
    return buffer;
}
#endif

static bool std_virtual_map_address ( void* base, size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );
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

static bool std_virtual_unmap_address ( void* base, size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );
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

static bool std_virtual_free_address ( void* base, size_t size ) {
    std_assert_m ( base != NULL );
    std_assert_m ( std_align_test_ptr ( base, std_allocator_state->virtual_page_size ) );

    bool result;
#ifdef std_platform_win32_m
    std_unused_m ( size );
    result = VirtualFree ( base, 0, MEM_RELEASE ) == TRUE;
#elif defined(std_platform_linux_m)
    result = munmap ( base, size ) == 0;
#endif
    return result;
}

static void* std_virtual_reserve_address ( size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );

    char* base;
#ifdef std_platform_win32_m
    base = ( char* ) VirtualAlloc ( NULL, size, MEM_RESERVE, PAGE_NOACCESS );
#elif defined(std_platform_linux_m)
    base = ( char* ) mmap ( NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0 );
#endif
    std_assert_m ( base != NULL );

    return base;
}

#if 0
std_virtual_buffer_t std_virtual_alloc ( size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );

    char* base;
#ifdef std_platform_win32_m
    base = ( char* ) VirtualAlloc ( NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS );
#elif defined(std_platform_linux_m)
    base = ( char* ) mmap ( NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0 );
#endif
    std_assert_m ( base != NULL );
    return std_virtual_buffer ( base, size, size );
}

std_virtual_buffer_t std_virtual_reserve ( size_t size ) {
    std_assert_m ( std_align_test ( size, std_allocator_state->virtual_page_size ) );

    char* base;
#ifdef std_platform_win32_m
    base = ( char* ) VirtualAlloc ( NULL, size, MEM_RESERVE, PAGE_NOACCESS );
#elif defined(std_platform_linux_m)
    base = ( char* ) mmap ( NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0 );
#endif
    std_assert_m ( base != NULL );
    return std_virtual_buffer ( base, 0, size );
}

bool std_virtual_map ( std_virtual_buffer_t* buffer, size_t size ) {
    bool result = std_virtual_map_address ( buffer->base + buffer->mapped_size, size );
    
    if ( result ) {
        buffer->mapped_size += size;
    }

    return result;
}

bool std_virtual_unmap ( std_virtual_buffer_t* buffer, size_t size ) {
    size_t old_size = buffer->mapped_size;
    size_t new_size = old_size > size ? old_size - size : 0;

    bool result = std_virtual_unmap_address ( buffer->base + new_size, size );

    if ( result ) {
        buffer->mapped_size = new_size;
    }

    return result;
}

bool std_virtual_free ( const std_virtual_buffer_t* buffer ) {
    return std_virtual_free_address ( buffer->base, buffer->reserved_size ) ;
}
#else
void* std_virtual_reserve ( size_t size ) {
    return std_virtual_reserve_address ( size );
}

bool std_virtual_map ( void* from, void* to ) {
    return std_virtual_map_address ( from, to - from );
}

bool std_virtual_unmap ( void* from, void* to ) {
    return std_virtual_unmap_address ( from, to - from );
}

bool std_virtual_free ( void* from, void* to ) {
    return std_virtual_free_address ( from, to - from );
}
#endif

//==============================================================================

/*
    Shared first-fit heap allocator that borrows memory pages from the virtual page allocator and uses them to satisfy requests.
    The implementation is very simple and performs very slow in situations where there's a high number of varying sized
    allocs and frees happening out of order. It doesn't allocate any extra upfront space (other than the virtual page size)
    to make later allocations cheaper and all its logic is guarded by a single mutex.
    Using this over allocating a number of raw virtual pages through the virtual page allocator has the only advantage that
    unused memory within the page boundary can get reused in later allocations that also go through this allocator, instead of being wasted.

    // TODO rename to something else (first fit heap? ffh?), this heap is not currently used
*/

#if 0
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
        char* prev = ( char* ) &node->freelist;
        char* curr = ( char* ) node->freelist;

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
            char* base = std_align_ptr ( curr, align );
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
    char* base = new_node->alloc.buffer.base;
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

        if ( node->alloc.buffer.base <= ( char* ) handle.id && node->alloc.buffer.base + node->alloc.buffer.size >= ( char* ) handle.id + handle.size ) {
            char* left = ( char* ) &node->freelist;
            char* right = ( char* ) node->freelist;

            // Go through the freelist and find the two list elements that surround the memory address
            while ( right != NULL ) {
                if ( right > ( char* ) handle.id ) {
                    break;
                }

                left = right;
                right = std_list_next_m ( right );
            }

            std_auto_m left_segment = ( std_allocator_virtual_heap_segment_t* ) left;
            std_auto_m right_segment = ( std_allocator_virtual_heap_segment_t* ) right;

            bool left_adjacent = left != ( char* ) &node->freelist && left + left_segment->size == ( char* ) handle.id;
            bool right_adjacent = ( char* ) handle.id + handle.size == right;

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
#endif

// ====================================================================================
// TODO

#if 0
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
#endif

//==============================================================================
// TODO delete this

#if 0
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

char* std_virtual_buffer_push ( std_virtual_buffer_t* buffer, size_t size ) {
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
#endif

//==============================================================================

std_buffer_t std_buffer ( void* base, size_t size ) {
    std_buffer_t buffer;
    buffer.base = base;
    buffer.size = size;
    return buffer;
}

std_stack_t std_stack ( void* base, size_t size ) {
    std_stack_t stack;
    stack.begin = base;
    stack.top = base;
    stack.end = base + size;
    return stack;
}

void* std_stack_alloc ( std_stack_t* stack, size_t size ) {
    void* top = stack->top;

    if ( top + size > stack->end ) {
        return NULL;
    }

    stack->top = top + size;
    return top;
}

static void* std_stack_alloc_zero ( std_stack_t* stack, size_t size ) {
    void* top = std_stack_alloc ( stack, size );

    if ( top ) {
        std_mem_zero ( top, size );
    }

    return top;
}

void* std_stack_write ( std_stack_t* stack, const void* data, size_t size ) {
    void* alloc = std_stack_alloc ( stack, size );

    if ( alloc ) {
        std_mem_copy ( alloc, data, size );
    }   

    return alloc;
}

bool std_stack_align ( std_stack_t* stack, size_t align ) {
    void* top = stack->top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_stack_alloc ( stack, align_size ) != NULL;
}

bool std_stack_align_zero ( std_stack_t* stack, size_t align ) {
    void* top = stack->top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_stack_alloc_zero ( stack, align_size ) != NULL;
}

void* std_stack_alloc_align ( std_stack_t* stack, size_t size, size_t align ) {
    void* top = stack->top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_stack_alloc ( stack, align_size + size );
}

void* std_stack_write_align ( std_stack_t* stack, const void* data, size_t size, size_t align ) {
    void* alloc = std_stack_alloc_align ( stack, size, align );

    if ( alloc ) {
        std_mem_copy ( alloc, data, size );
    }

    return alloc;
}

void std_stack_clear ( std_stack_t* stack ) {
    stack->top = stack->begin;
}

static char* std_stack_string_copy_n ( std_stack_t* stack, const char* str, size_t len ) {
    void* alloc = std_stack_alloc ( stack, len );
    
    if ( alloc ) {
        std_str_copy ( alloc, len, str );
    }

    return alloc;
}

char* std_stack_string_copy ( std_stack_t* stack, const char* str ) {
    size_t len = std_str_len ( str ) ;
    return std_stack_string_copy_n ( stack, str, len + 1 );
}

static void std_stack_string_pop_terminator ( std_stack_t* stack ) {
    char* begin = ( char* ) stack->begin;
    char* top = ( char* ) stack->top;

    if ( top > begin && *( top - 1 ) == '\0') {
        stack->top = top - 1;
    }    
}

char* std_stack_string_append ( std_stack_t* stack, const char* str ) {
    std_stack_string_pop_terminator ( stack );
    return std_stack_string_copy ( stack, str );
}

char* std_stack_string_append_char ( std_stack_t* stack, char c ) {
    std_stack_string_pop_terminator ( stack );
    char buffer[2] = { c, '\0' };
    return std_stack_string_copy ( stack, buffer );
}

void std_stack_string_pop ( std_stack_t* stack ) {
    char* begin = ( char* ) stack->begin;
    char* top = ( char* ) stack->top;

    if ( top - begin >= 2 ) {
        top -= 2;
        *top = '\0';
        stack->top = top;
    }
}

void std_stack_free ( std_stack_t* stack, size_t size ) {
    void* top = stack->top - size;
    void* begin = stack->begin;
    stack->top = top >= begin ? top : begin;
}

//==============================================================================

std_virtual_stack_t std_virtual_stack ( void* base, size_t mapped_size, size_t virtual_size ) {
    std_virtual_stack_t stack;
    stack.mapped = std_stack ( base, mapped_size );
    stack.virtual_end = base + virtual_size;
    return stack;
}

std_virtual_stack_t std_virtual_stack_create ( size_t virtual_size ) {
    size_t page_size = std_virtual_page_size();
    virtual_size = std_max ( virtual_size, page_size );
    virtual_size = std_align ( virtual_size, page_size );
    void* base = std_virtual_reserve ( virtual_size );
    std_virtual_map ( base, base + page_size );
    std_virtual_stack_t stack;
    stack.mapped = std_stack ( base, page_size );
    stack.virtual_end = base + virtual_size;
    return stack;
}

void std_virtual_stack_destroy ( std_virtual_stack_t* stack ) {
    std_virtual_free ( stack->mapped.begin, stack->virtual_end );
}

static bool std_virtual_stack_size_check ( std_virtual_stack_t* stack, size_t alloc_size ) {
    void* top = stack->mapped.top;
    void* mapped_end = stack->mapped.end;
    void* new_top = top + alloc_size;

    if ( new_top <= mapped_end ) {
        return true;
    }

    void* virtual_end = stack->virtual_end;

    if ( new_top <= virtual_end ) {
        size_t page_size = std_virtual_page_size();
        void* new_mapped_end = std_align_ptr ( new_top, page_size );
        std_assert_m ( new_mapped_end <= virtual_end );
        std_virtual_map ( mapped_end, new_mapped_end );
        stack->mapped.end = new_mapped_end;
        return true;
    }

    // The stack ran out of virtual memory.
    return false;
}

void* std_virtual_stack_alloc ( std_virtual_stack_t* stack, size_t size ) {
    void* top = stack->mapped.top;

    if ( !std_virtual_stack_size_check ( stack, size ) ) {
        return NULL;
    }

    stack->mapped.top = top + size;
    return top;
}

static void* std_virtual_stack_alloc_zero ( std_virtual_stack_t* stack, size_t size ) {
    void* alloc = std_virtual_stack_alloc ( stack, size );

    if ( alloc ) {
        std_mem_zero ( alloc, size );
    }

    return alloc;
}

void* std_virtual_stack_alloc_align ( std_virtual_stack_t* stack, size_t size, size_t align ) {
    void* top = stack->mapped.top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_virtual_stack_alloc ( stack, align_size + size );
}

void* std_virtual_stack_write_align ( std_virtual_stack_t* stack, const void* data, size_t size, size_t align ) {
    void* alloc = std_virtual_stack_alloc_align ( stack, size, align );
    
    if ( alloc ) {
        std_mem_copy ( alloc, data, size );
    }

    return alloc;
}

void* std_virtual_stack_write ( std_virtual_stack_t* stack, const void* data, size_t size ) {
    void* alloc = std_virtual_stack_alloc ( stack, size );

    if ( alloc ) {
        std_mem_copy ( alloc, data, size );
    }

    return alloc;
}

bool std_virtual_stack_align ( std_virtual_stack_t* stack, size_t align ) {
    void* top = stack->mapped.top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_virtual_stack_alloc ( stack, align_size ) != NULL;
}

bool std_virtual_stack_align_zero ( std_virtual_stack_t* stack, size_t align ) {
    void* top = stack->mapped.top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_virtual_stack_alloc_zero ( stack, align_size ) != NULL;
}

void std_virtual_stack_free ( std_virtual_stack_t* stack, size_t size ) {
    std_stack_free ( &stack->mapped, size );
}

void std_virtual_stack_clear ( std_virtual_stack_t* stack ) {
    std_stack_clear ( &stack->mapped );
}

char* std_virtual_stack_string_copy ( std_virtual_stack_t* stack, const char* str ) {
    size_t len = std_str_len ( str ) ;

    if ( !std_virtual_stack_size_check ( stack, len + 1 ) ) {
        return NULL;
    }

    return std_stack_string_copy_n ( &stack->mapped, str, len + 1 );
}

char* std_virtual_stack_string_append ( std_virtual_stack_t* stack, const char* str ) {
    std_stack_string_pop_terminator ( &stack->mapped );
    return std_virtual_stack_string_copy ( stack, str );
}

#if 0
std_arena_t std_arena ( void* base, size_t size, size_t virtual_size, std_arena_allocator_e allocator ) {
    std_arena_t arena;
    arena.base = base;
    arena.size = size;
    arena.virtual_size = virtual_size;
    arena.used_size = 0;
    arena.allocator = allocator;
    return arena;
}

#define std_allocator_arena_growth_factor_m 2

std_arena_t std_arena_create ( std_arena_allocator_e allocator, size_t size ) {
    std_arena_t arena;
    arena.allocator = allocator;
    arena.used_size = 0;
    
    if ( allocator == std_arena_allocator_none_m ) {
        std_log_error_m ( "Tried to initialize arena with null allocator and no pre-allocated memory" );
        arena.base = NULL;
        arena.size = 0;
        arena.virtual_size = 0;
    } else if ( allocator == std_arena_allocator_heap_m ) {
        arena.base = std_virtual_heap_alloc ( size, 16 );
        arena.size = size;
        arena.virtual_size = 0;
    } else if ( allocator == std_arena_allocator_virtual_m ) {
        size_t reserved_size = std_align ( size, std_allocator_state->virtual_page_size );
        void* base = std_virtual_reserve ( reserved_size );// * std_allocator_arena_growth_factor_m );
        std_virtual_map ( &buffer, std_allocator_state->virtual_page_size );
        arena.base = buffer.base;
        arena.size = buffer.mapped_size;
        arena.virtual_size = buffer.reserved_size;
    }

    return arena;
}

void std_arena_destroy ( std_arena_t* arena ) {
    std_arena_allocator_e allocator = arena->allocator;

    if ( allocator == std_arena_allocator_heap_m ) {
        std_virtual_heap_free ( arena->base );
    } else if ( allocator == std_arena_allocator_virtual_m ) {
        std_virtual_buffer_t buffer = std_virtual_buffer ( arena->base, arena->size, arena->virtual_size );
        std_virtual_free ( &buffer );
    }
}

static bool std_arena_grow_check ( std_arena_t* arena, size_t alloc_size ) {
    size_t required_size = arena->used_size + alloc_size;

    if ( required_size <= arena->size ) {
        return true;
    }

    if ( arena->allocator == std_arena_allocator_none_m ) {
        return false;
    } else if ( arena->allocator == std_arena_allocator_heap_m ) {
        size_t new_size = std_max ( alloc_size, arena->size * std_allocator_arena_growth_factor_m );
        void* buffer = std_virtual_heap_alloc ( new_size, 16 );
        if ( !buffer ) {
            return false;
        }
        
        std_mem_copy ( buffer, arena->base, arena->used_size );
        std_verify_m ( std_virtual_heap_free ( arena->base ) );
        arena->base = buffer;
        arena->size = new_size;
        return true;
    } else if ( arena->allocator == std_arena_allocator_virtual_m ) {
        std_virtual_buffer_t buffer = std_virtual_buffer ( arena->base, arena->size, arena->virtual_size );
        size_t required_page_size = std_align ( required_size, std_allocator_state->virtual_page_size );
        
        if ( required_page_size <= arena->virtual_size ) {
            size_t map_size = std_align ( alloc_size, std_allocator_state->virtual_page_size );
            
            if ( !std_virtual_map ( &buffer, map_size ) ) {
                return false;
            }
            
            arena->size = buffer.mapped_size;
        } else {
            size_t new_size = std_max ( alloc_size, arena->size * std_allocator_arena_growth_factor_m );
            new_size = std_align ( new_size, std_allocator_state->virtual_page_size );
            std_virtual_buffer_t new_buffer = std_virtual_alloc ( new_size );
            if ( !new_buffer.base ) {
                return false;
            }
            
            std_mem_copy ( new_buffer.base, arena->base, arena->used_size );
            std_verify_m ( std_virtual_free ( &buffer ) );
            arena->base = new_buffer.base;
            arena->size = new_buffer.mapped_size;
            arena->virtual_size = new_buffer.reserved_size;
        }
        return true;
    }
    return false;
}

void* std_arena_alloc ( std_arena_t* arena, size_t size ) {
    if ( !std_arena_grow_check ( arena, size ) ) {
        return NULL;
    }

    return std_buffer_alloc ( &arena->buffer );
}

static void* std_arena_alloc_zero ( std_arena_t arena, size_t size ) {
    void* alloc = std_arena_alloc ( arena, size );

    if ( alloc ) {
        std_mem_zero ( alloc, size );
    }

    return alloc;
}

void* std_arena_alloc_align ( std_arena_t* arena, size_t size, size_t align ) {
    void* top = arena->buffer.top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_arena_alloc ( arena, align_size + size );
}

void* std_arena_write_align ( std_arena_t* arena, const void* data, size_t size, size_t align ) {
    void* alloc = std_arena_alloc_align ( arena, size, align );
    
    if ( alloc ) {
        std_mem_copy ( alloc, data, size );
    }

    return alloc;
}

bool std_arena_write ( std_arena_t* arena, const void* data, size_t size ) {
    void* alloc = std_arena_alloc ( arena, size );

    if ( alloc ) {
        std_mem_copy ( alloc, data, size );
    }

    return alloc;
}

bool std_arena_align ( std_arena_t* arena, size_t align ) {
    void* top = arena->buffer.top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_arena_alloc ( arena, align_size ) != NULL;
}

bool std_arena_align_zero ( std_arena_t* arena, size_t align ) {
    void* top = arena->buffer.top;
    size_t align_size = std_align_size_ptr ( top, align );
    return std_arena_alloc_zero ( arena, align_size ) != NULL;
}

void std_arena_free ( std_arena_t* arena, size_t size ) {
    std_buffer_free ( &arena->buffer, size );
}

void std_arena_clear ( std_arena_t* arena ) {
    std_buffer_clear ( &arena->buffer );
}

char* std_arena_string_copy ( std_arena_t* arena, const char* str ) {
    size_t len = std_str_len ( str ) ;

    if ( !std_arena_grow_check ( arena, len + 1 ) ) {
        return NULL;
    }

    return std_buffer_string_copy_n ( &arena->buffer, str, len + 1 );
}

char* std_arena_string_append ( std_arena_t* arena, const char* str ) {
    std_buffer_string_pop_terminator ( &arena->buffer );
    return std_arena_string_copy ( arena, str );
}
#endif

#if 0
static std_alloc_t std_arena_allocator_alloc ( void* allocator, size_t size, size_t align ) {
    std_auto_m stack = ( std_arena_t* ) allocator;
    char* base = stack->buffer.base;
    size_t old_top = stack->top;
    char* user_ptr = ( char* ) std_align_ptr ( base + old_top, align );
    size_t new_top = ( size_t ) ( user_ptr - base ) + size;
    stack->top = new_top;
    std_assert_m ( stack->top <= stack->buffer.size );
    std_alloc_t alloc;
    alloc.handle.id = ( uint64_t ) old_top;
    alloc.handle.size = new_top - old_top;
    alloc.buffer.base = user_ptr;
    alloc.buffer.size = size;
    return alloc;
}

static bool std_arena_allocator_free ( void* allocator, std_memory_h memory ) {
    std_auto_m stack = ( std_arena_t* ) allocator;
    size_t top = ( size_t ) ( memory.id + memory.size );

    if ( top == stack->top ) {
        std_assert_m ( memory.id < stack->top );
        stack->top = ( size_t ) memory.id;
        return true;
    } else {
        return false;
    }
}

std_allocator_i std_arena_allocator ( std_arena_t* allocator ) {
    std_allocator_i i;
    i.alloc = std_arena_allocator_alloc;
    i.free = std_arena_allocator_free;
    i.impl = allocator;
    return i;
}
#endif

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
        ----------------------------------------------------------------------------------
        |       header       |      unused       |     alignment     |     user data     |
        ----------------------------------------------------------------------------------
        |  size + flags : 8  |        0/+        |    offset : 0/8   |        8/+        |
        ----------------------------------------------------------------------------------

        all blocks are 8 byte aligned, so header is also 8 byte aligned, so first 3 bits in size can be used to store additional flags
            bit 0 is used to tag whether the block is free or not
            bit 1 is used to tag whether the *prev* block (the one adjacent on the left of the memory address space) is free or not.
            bit 2 is used to tag whether the block is an alignment block (see below)
        alignment info is only stored if the allocation requested an alignment greater than 8
            the user data is offset the amount needed inside the block to achieve the requested alignment
            the alignment info is stored right before the user data, in 8 bytes. it contains the size in bytes between the end of the header and the start of the user data. this value is also guaranteed to be multiple of 8, and bit 2 is set to 1 to indicate that this is alignment info and not the actual header.
            the header has its regular info, and its bit 2 is set to 0. the memory in between header and alignment is unused.
        if alignment is 8, no alignment info is stored and user data comes right after the header
            the header has its regular info, and its bit 2 is set to 0
        min block size is 128 bytes. size of user data depends on requested size and alignment.

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
*/

//#define std_allocator_tlsf_header_size_m 8 // freelist pointers excluded
//#define std_allocator_tlsf_footer_size_m 8
//#define std_allocator_tlsf_min_segment_size_m ( 1 << std_allocator_tlsf_min_x_level_m )
//#define std_allocator_tlsf_max_segment_size_m ( ( 1ull << std_allocator_tlsf_max_x_level_m ) - 1 )
//#define std_allocator_tlsf_free_segment_bit_m (1 << 0)
//#define std_allocator_tlsf_free_prev_segment_bit_m (1 << 1)
//#define std_allocator_tlsf_size_mask_m ( ( ~0ull ) << 3 )

// Indexes the tlsf freelist table
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

std_unused_function_m()
static void std_allocator_tlsf_print_state ( std_allocator_tlsf_heap_t* heap ) {
    char buffer[1024];
    std_log_info_m ( "-- TLSF heap state --" );

    std_size_to_str_approx ( buffer, 1024, heap->stack.end - heap->stack.begin );
    std_log_info_m ( "Total: " std_fmt_str_m, buffer );
    std_size_to_str_approx ( buffer, 1024, heap->allocated_size );
    std_log_info_m ( "Used: " std_fmt_str_m, buffer );

    std_log_info_m ( "Free:" );
    for ( uint32_t x = 0; x < std_allocator_tlsf_x_size_m; ++x ) {
        char stack_buffer[1024] = {0};
        std_stack_t stack = std_static_stack_m ( stack_buffer );

        std_size_to_str_approx ( buffer, 1024, 1ull << ( x + std_allocator_tlsf_min_x_level_m ) );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, std_fmt_tab_m );

        //std_str_format_m ( buffer, std_fmt_u32_m, heap->available_rows & ( 1ull << x ) ? 1 : 0 );
        //uint32_t idx = std_bit_scan_64 ( heap->available_rows );
        std_str_format_m ( buffer, std_fmt_u64_pad_m ( 2 ), x );
        std_stack_string_append ( &stack, buffer );
        std_stack_string_append ( &stack, " | " );

        for ( uint32_t y = 0; y < std_allocator_tlsf_y_size_m; ++y ) {
            bool available = heap->available_freelists[x] & ( 1ull << y );
            std_assert_m ( ( !available && heap->freelists[x][y] == NULL ) || ( available && heap->freelists[x][y] ) );
            uint32_t count = 0;
            if ( available ) {
                void* item = heap->freelists[x][y];
                while ( item ) {
                    ++count;
                    std_allocator_tlsf_header_t* header = item - std_allocator_tlsf_header_size_m;
                    item = header->next;
                }
            }
            std_str_format_m ( buffer, std_fmt_u32_m, count );
            std_stack_string_append ( &stack, buffer );
            std_stack_string_append ( &stack, "  " );
        }
        std_log_info_m ( std_fmt_str_m, stack_buffer );
    }
}

std_allocator_tlsf_freelist_idx_t std_allocator_tlsf_freelist_idx_first_available ( std_allocator_tlsf_heap_t* heap, std_allocator_tlsf_freelist_idx_t base ) {
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

char* std_allocator_tlsf_pop_from_freelist ( std_allocator_tlsf_heap_t* heap, uint64_t size ) {
    std_allocator_tlsf_freelist_idx_t start_idx = std_allocator_tlsf_freelist_idx ( size );
    std_allocator_tlsf_freelist_idx_t idx = std_allocator_tlsf_freelist_idx_first_available ( heap, start_idx );
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

    void* top = heap->stack.top;
    bool empty = top == heap->stack.begin;
    void* new_segment = std_virtual_stack_alloc ( &heap->stack, size );
    //uint64_t prev_top = heap->arena.used_size;
    //bool empty = prev_top == 0;
    //void* new_segment = std_arena_alloc ( &heap->arena, size );

    if ( !empty ) {
        // not the first grow
        //char* base = heap->arena.base;

        char* top_segment_end = top;
        std_auto_m top_segment_footer = ( std_allocator_tlsf_footer_t* ) ( top_segment_end - std_allocator_tlsf_footer_size_m );
        uint64_t top_segment_size = top_segment_footer->size;

        char* top_segment_start = top_segment_end - top_segment_size;
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

        //std_allocator_tlsf_print_state ( heap );

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

    heap->stack = std_virtual_stack_create ( std_allocator_tlsf_max_segment_size_m );
    std_allocator_tlsf_heap_grow ( heap, size );
}

void* std_tlsf_heap_alloc ( std_allocator_tlsf_heap_t* heap, uint64_t size, uint64_t align ) {
    // check size
    std_static_assert_m ( std_allocator_tlsf_header_size_m == 8 );
    size = std_align ( size, 8 );
    align = std_align ( align, 8 );
    size = align > std_allocator_tlsf_header_size_m ? size + align - std_allocator_tlsf_header_size_m : size;
    size += std_allocator_tlsf_header_size_m;
    size = std_max_u64 ( size, std_allocator_tlsf_min_segment_size_m );
    std_assert_m ( size <= std_allocator_tlsf_max_segment_size_m );

    uint64_t size_roundup = std_allocator_tlsf_heap_size_roundup ( size );

    std_mutex_lock ( &heap->mutex );

    // grab from freelist
    char* segment = std_allocator_tlsf_pop_from_freelist ( heap, size_roundup );

    // load segment
    std_auto_m segment_header = ( std_allocator_tlsf_header_t* ) segment;
    std_allocator_tlsf_header_t new_header = *segment_header;
    uint64_t segment_size = ( new_header.size_flags & std_allocator_tlsf_size_mask_m );
    std_assert_m ( segment_size >= std_allocator_tlsf_min_segment_size_m );
    std_auto_m segment_footer = ( std_allocator_tlsf_footer_t* ) ( segment + segment_size - std_allocator_tlsf_footer_size_m );
    std_allocator_tlsf_footer_t new_footer = *segment_footer;
    std_assert_m ( new_header.size_flags & std_allocator_tlsf_free_segment_bit_m );
    std_assert_m ( segment_size >= size );

    // split
    bool needs_split = segment_size - size >= std_allocator_tlsf_min_segment_size_m;

    if ( needs_split ) {
        uint64_t extra_size = segment_size - size;

        std_auto_m extra_segment = ( char* ) segment + size;
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
    //if ( !needs_split && segment + segment_size != heap->arena.base + heap->arena.used_size ) {
    if ( !needs_split && segment + segment_size != heap->stack.top ) {
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

    // align
    char* user_data = ( char* ) std_align_ptr ( segment + std_allocator_tlsf_header_size_m, align );
    if ( user_data > segment + std_allocator_tlsf_header_size_m ) {
        uint64_t* alignment_block = ( uint64_t* ) ( user_data - 8 );
        *alignment_block = ( user_data - ( segment + std_allocator_tlsf_header_size_m ) ) | std_allocator_tlsf_alignment_bit_m;
    }

    heap->allocated_size += segment_size;

    std_mutex_unlock ( &heap->mutex );

    //char* user_data = std_align_ptr ( segment + std_allocator_tlsf_header_size_m, align );
    //uint64_t user_size = segment + segment_size - user_data;

    //std_log_info_m ( "ALLOC " std_fmt_u64_m, segment_size );

    return user_data;
}

void std_tlsf_heap_free ( std_allocator_tlsf_heap_t* heap, void* address ) {
    if ( !address ) {
        return;
    }

    std_auto_m segment = ( char* ) address - std_allocator_tlsf_header_size_m;

    std_mutex_lock ( &heap->mutex );

    // check for alignment
    std_auto_m alignment_block = *( uint64_t* ) segment;
    if ( alignment_block & std_allocator_tlsf_alignment_bit_m ) {
        uint64_t align_offset = alignment_block & std_allocator_tlsf_size_mask_m;
        segment -= align_offset;
    }

    //std_log_info_m ( std_fmt_str_m std_fmt_ptr_m, "F ", segment );

    // load segment
    std_auto_m segment_header = ( std_allocator_tlsf_header_t* ) segment;
    //std_auto_m segment_footer = ( std_allocator_tlsf_footer_t* ) ( segment + segment_size - std_allocator_tlsf_footer_size_m );

    char* new_segment = segment;
    std_allocator_tlsf_header_t new_header = *segment_header;
    uint64_t segment_size = new_header.size_flags & std_allocator_tlsf_size_mask_m;
    std_assert_m ( segment_size >= std_allocator_tlsf_min_segment_size_m );
    //std_allocator_tlsf_footer_t new_footer = *segment_footer;
    std_allocator_tlsf_footer_t new_footer;
    new_footer.size = segment_size;

    std_assert_m ( ( new_header.size_flags & std_allocator_tlsf_free_segment_bit_m ) == 0 );
    new_header.size_flags |= std_allocator_tlsf_free_segment_bit_m;

    // check prev
    if ( segment != heap->stack.begin ) {
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
            new_segment = ( char* ) prev_header_ptr;
        }
    }

    // check next
    //if ( segment + segment_size != heap->arena.base + heap->arena.used_size ) {
    if ( segment + segment_size != heap->stack.top ) {

        char* next_segment = segment + segment_size;
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

    heap->allocated_size -= segment_size;

    std_mutex_unlock ( &heap->mutex );

    //std_log_info_m ( "FREE " std_fmt_u64_m, segment_size );
}

void std_allocator_tlsf_heap_deinit ( std_allocator_tlsf_heap_t* heap ) {
    std_virtual_stack_destroy ( &heap->stack );
}

//==============================================================================

void* std_tlsf_alloc ( uint64_t size, uint64_t align ) {
    std_allocator_tlsf_heap_t* heap = &std_allocator_state->tlsf_heap;
    return std_tlsf_heap_alloc ( heap, size, align );
}

void std_tlsf_free ( void* address ) {
    std_allocator_tlsf_heap_t* heap = &std_allocator_state->tlsf_heap;
    std_tlsf_heap_free ( heap, address );
}

//==============================================================================

void* std_virtual_heap_alloc ( size_t size, size_t align ) {
    return std_tlsf_alloc ( size, align );
    //return std_allocator_virtual_heap_alloc ( size, align );
}

bool std_virtual_heap_free ( void* address ) {
    std_tlsf_free ( address );
    return true; // TODO
    //return std_allocator_virtual_heap_free ( handle );
}

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

    // Query RAM size, for later usage
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

    // Set up a pointer to the boot state
    // Will be used by the following virtual heap init code and by the incoming std_allocator_init call
    std_allocator_attach ( &state );

    // Virtual heap
    // This reserves virtual space for total_ram_size/virtual_page_size nodes, which on a 8GiB
    // system is about 2 million nodes, but only commits memory that's actually used.
    #if 0
    {
        size_t max_nodes_count = ( total_ram_size + total_swap_size ) / state.virtual_page_size;
        size_t nodes_reserve_size = sizeof ( std_allocator_virtual_heap_node_t ) * max_nodes_count;
        nodes_reserve_size = std_align ( nodes_reserve_size, state.virtual_page_size );
        state.virtual_heap.nodes_buffer = std_virtual_reserve ( nodes_reserve_size );
        state.virtual_heap.total_size = 0;
        state.virtual_heap.nodes_count = 0;
        state.virtual_heap.nodes_mapped_size = state.virtual_page_size;
        std_virtual_map ( &state.virtual_heap.nodes_buffer, state.virtual_page_size );
        std_mutex_init ( &state.virtual_heap.mutex );
    }
    #endif

    // Tagged heap
#if 0
    {
        uint64_t tagged_page_size = 1024 * 1024 * 2; // TODO take as param?
        state.tagged_page_size = tagged_page_size;
        state.tagged_heap.tag_bin_map = std_static_hash_map_m ( state.tagged_heap.tag_bin_map_keys, state.tagged_heap.tag_bin_map_payloads );
        state.tagged_heap.bins_buffer = std_virtual_alloc ( tagged_page_size ); // TODO should this be a literal? separate param?
        size_t bin_size = tagged_page_size / std_tagged_allocator_max_bins_m;
        state.tagged_heap.bin_size = bin_size;
        state.tagged_heap.bin_capacity = bin_size / sizeof ( uint64_t ) - 1;
        state.tagged_heap.bins_freelist = std_freelist ( state.tagged_heap.bins_buffer.mapped, bin_size );
        std_mutex_init ( &state.tagged_heap.mutex );
    }
#endif

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
