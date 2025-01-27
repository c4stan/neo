#include <std_queue.h>

#include <std_atomic.h>
#include <std_byte.h>
#include <std_log.h>

#include <std_platform.h>

#include <memoryapi.h>

#if defined ( std_platform_win32_m )
// https://fgiesen.wordpress.com/2012/07/21/the-magic-ring-buffer/
// https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2#examples
static void std_queue_virtual_alloc_aliased ( char** base, char** alias, size_t* size ) {
    size_t queue_size = std_pow2_round_up ( std_virtual_page_align ( *size ) );
    *base = NULL;
    *alias = NULL;

    void* base1 = NULL;
    void* base2 = NULL;
    HANDLE section = NULL;
    void* view1 = NULL;
    void* view2 = NULL;

    base1 = VirtualAlloc2 ( NULL, NULL, 2 * queue_size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, NULL, 0 );
    if ( !base1 ) {
        std_log_os_error_m();
        goto exit;
    }

    BOOL result = VirtualFree ( base1, queue_size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER );
    if ( result == FALSE ) {
        std_log_os_error_m();
        goto exit;
    }

    base2 = base1 + queue_size;
    
    section = CreateFileMapping ( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, queue_size, NULL );
    if ( !section ) {
        std_log_os_error_m();
        goto exit;
    }

    view1 = MapViewOfFile3 ( section, NULL, base1, 0, queue_size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0 );
    if ( !view1 ) {
        std_log_os_error_m();
        goto exit;
    }
    base1 = NULL;

    view2 = MapViewOfFile3 ( section, NULL, base2, 0, queue_size, MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, NULL, 0 );
    if ( !view2 ) {
        std_log_os_error_m();
        goto exit;
    }
    base2 = NULL;

    *base = view1;
    *alias = view2;
    *size = queue_size;
    return;

exit:
    if ( base1 ) VirtualFree ( base1, 0, MEM_RELEASE );
    if ( base2 ) VirtualFree ( base2, 0, MEM_RELEASE );
    if ( section ) CloseHandle ( section );
    if ( view1 ) UnmapViewOfFileEx ( view1, 0 );
    if ( view2 ) UnmapViewOfFileEx ( view2, 0 );
}
#elif defined ( std_platform_linux_m ) 
    // TODO
#endif

//==============================================================================

std_queue_local_t std_queue_local ( void* base, size_t size ) {
    if ( !std_pow2_test ( size ) ) {
        size = std_pow2_round_down ( size );
    }

    std_queue_local_t queue;
    queue.base = base;
    queue.ring = std_ring ( size );
    return queue;
}

std_queue_local_t std_queue_local_create ( size_t size ) {
    std_queue_local_t queue;
    std_queue_virtual_alloc_aliased ( &queue.base, &queue.alias, &size );
    queue.ring = std_ring ( size );
    return queue;
}

void std_queue_local_destroy ( std_queue_local_t* queue ) {
    UnmapViewOfFile ( queue->base );
    UnmapViewOfFile ( queue->alias );
}

void std_queue_local_clear ( std_queue_local_t* queue ) {
    std_ring_clear ( &queue->ring );
}

size_t std_queue_local_size ( const std_queue_local_t* queue ) {
    return std_ring_capacity ( &queue->ring );
}

size_t std_queue_local_used_size ( const std_queue_local_t* queue ) {
    return std_ring_count ( &queue->ring );
}

void std_queue_local_push ( std_queue_local_t* queue, const void* item, size_t size ) {
    std_assert_m ( item != NULL );
    char* base = queue->base;
    uint64_t offset = std_ring_top_idx ( &queue->ring );
    std_mem_copy ( base + offset, item, size );
    std_ring_push ( &queue->ring, size );
}

void std_queue_local_pop_discard ( std_queue_local_t* queue, size_t size ) {
    std_ring_pop ( &queue->ring, size );
}

void std_queue_local_pop_move ( std_queue_local_t* queue, void* dest, size_t size ) {
    char* base = queue->base;
    uint64_t offset = std_ring_bot_idx ( &queue->ring );
    std_mem_copy ( dest, base + offset, size );
    std_ring_pop ( &queue->ring, size );
}

void* std_queue_local_emplace ( std_queue_local_t* queue, size_t size ) {
    char* base = queue->base;
    uint64_t offset = std_ring_top_idx ( &queue->ring );
    std_ring_push ( &queue->ring, size );
    return base + offset;
}

void std_queue_local_align_push ( std_queue_local_t* queue, size_t align ) {
    // Load
    size_t top = queue->ring.top;
    size_t new_top = std_align ( top, align );

#if std_log_assert_enabled_m
    // Test for enough free space
    size_t size = new_top - top;
    size_t cap = queue->ring.mask + 1;
    size_t used_size = top - queue->ring.bot;
    std_assert_m ( cap - used_size >= size );
#endif

    // Align
    queue->ring.top = new_top;
}

void std_queue_local_align_pop ( std_queue_local_t* queue, size_t align ) {
    // Load
    size_t bot = queue->ring.bot;
    size_t new_bot = std_align ( bot, align );

#if std_log_assert_enabled_m
    // Make sure enough allocated space
    size_t top = queue->ring.top;
    size_t size = new_bot - bot;
    std_assert_m ( top - bot >= size );
#endif

    // Pop
    queue->ring.bot = new_bot;
}

// --------------------------

std_ring_t std_ring ( uint64_t capacity ) {
    std_assert_m ( std_pow2_test ( capacity ) );
    std_ring_t ring = {
        .top = 0,
        .bot = 0,
        .mask = capacity - 1
    };
    return ring;
}

uint64_t std_ring_count ( const std_ring_t* ring ) {
    return ring->top - ring->bot;
}

uint64_t std_ring_capacity ( const std_ring_t* ring ) {
    return ring->mask + 1;
}

uint64_t std_ring_top_idx ( const std_ring_t* ring ) {
    return ring->top & ring->mask;
}

uint64_t std_ring_bot_idx ( const std_ring_t* ring ) {
    return ring->bot & ring->mask;
}

uint64_t std_ring_idx ( const std_ring_t* ring, uint64_t virtual_idx ) {
    return virtual_idx & ring->mask;
}

void std_ring_push ( std_ring_t* ring, uint64_t count ) {
    uint64_t top = ring->top;

#if std_log_assert_enabled_m
    uint64_t mask = ring->mask;
    uint64_t capacity = mask + 1;
    uint64_t used = top - ring->bot;
    std_assert_m ( capacity - used >= count );
#endif

    ring->top = top + count;
}

void std_ring_pop ( std_ring_t* ring, uint64_t count ) {
    uint64_t bot = ring->bot;

#if std_log_assert_enabled_m
    size_t used = ring->top - bot;
    std_assert_m ( used >= count );
#endif

    ring->bot = bot + count;
}

void std_ring_clear ( std_ring_t* ring ) {
    ring->top = 0;
    ring->bot = 0;
}

#if 0
std_circular_pool_t std_circular_pool ( std_buffer_t buffer, size_t stride ) {
    size_t cap = buffer.size / stride;

    if ( !std_pow2_test ( cap ) ) {
        std_log_warn_m ( "Circular pool capacity is not power of two, rounding it down." );
        cap = std_pow2_round_down ( cap );
        buffer.size = cap * stride;
    }

    std_circular_pool_t pool;
    pool.base = buffer.base;
    pool.mask = cap - 1;
    pool.top = 0;
    pool.bot = 0;
    pool.stride = stride;
    return pool;
}

void std_circular_pool_clear ( std_circular_pool_t* pool ) {
    pool->top = 0;
    pool->bot = 0;
}

size_t std_circular_pool_count ( const std_circular_pool_t* pool ) {
    return pool->top - pool->bot;
}

size_t std_circular_pool_capacity ( const std_circular_pool_t* pool ) {
    return pool->mask + 1;
}

void* std_circular_pool_push ( std_circular_pool_t* pool ) {
    char* base = pool->base;
    size_t top = pool->top;
    size_t mask = pool->mask;
    size_t stride = pool->stride;

#if std_log_assert_enabled_m
    // Test for enough free space
    size_t bot = pool->bot;
    size_t cap = mask + 1;
    size_t used_size = top - bot;
    std_assert_m ( cap - used_size >= 1 );
#endif

    size_t offset = top & mask;
    char* item = base + offset * stride;

    pool->top = top + 1;

    return item;
}

void std_circular_pool_pop ( std_circular_pool_t* pool ) {
    size_t bot = pool->bot;

#if std_log_assert_enabled_m
    // Test for enough allocated space
    size_t top = pool->top;
    size_t used_size = top - bot;
    std_assert_m ( used_size >= 1 );
#endif

    pool->bot = bot + 1;
}

void* std_circular_pool_peek ( std_circular_pool_t* pool ) {
    char* base = pool->base;
    size_t stride = pool->stride;
    size_t mask = pool->mask;
    size_t bot = pool->bot;

    return base + stride * ( bot & mask );
}

void* std_circular_pool_at ( const std_circular_pool_t* pool, size_t idx ) {
    char* base = pool->base;
    size_t mask = pool->mask;
    size_t stride = pool->stride;

    return base + stride * ( idx & mask );
}

void* std_circular_pool_at_buffer ( const std_circular_pool_t* pool, size_t idx ) {
    char* base = pool->base;
    size_t stride = pool->stride;

#if std_log_assert_enabled_m
    size_t mask = pool->mask;
    size_t cap = mask + 1;
    std_assert_m ( cap > idx );
#endif

    return base + stride * idx;
}

void* std_circular_pool_at_pool ( const std_circular_pool_t* pool, size_t idx ) {
    char* base = pool->base;
    size_t bot = pool->bot;
    size_t mask = pool->mask;
    size_t stride = pool->stride;

#if std_log_assert_enabled_m
    size_t top = pool->top;
    size_t count = top - bot;
    std_assert_m ( count > idx );
#endif

    idx = ( idx + bot ) & mask;
    return base + stride * idx;
}
#endif

//==============================================================================
// Shared

std_queue_shared_t std_queue_shared_create ( size_t size ) {
    std_queue_shared_t queue;
    std_queue_virtual_alloc_aliased ( &queue.base, &queue.alias, &size );
    std_mem_zero ( queue.base, size );
    queue.mask = size - 1;
    queue.top = 0;
    queue.bot = 0;
    return queue;
}

void std_queue_shared_destroy ( std_queue_shared_t* queue ) {
    UnmapViewOfFile ( queue->base );
    UnmapViewOfFile ( queue->alias );
}

void std_queue_shared_clear ( std_queue_shared_t* queue ) {
    queue->top = 0;
    queue->bot = 0;
}

size_t std_queue_shared_size ( std_queue_shared_t* queue ) {
    return queue->mask + 1;
}

size_t std_queue_shared_used_size ( std_queue_shared_t* queue ) {
    return queue->top - queue->bot;
}

const void* std_queue_shared_sc_peek ( std_queue_shared_t* queue ) {
    return queue->base + ( queue->bot & queue->mask );
}

//==============================================================================
// SPSC
// Same as local, except different queue type, and necessary barrier at the end of each function.

void std_queue_spsc_push ( std_queue_shared_t* queue, const void* item, size_t size ) {
    // Load
    char* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;

    size_t cap = mask + 1;

#if std_log_assert_enabled_m
    size_t  bot = queue->bot;
    // Test for enough free space
    size_t used_size = top - bot;
    std_assert_m ( cap - used_size >= size );
#endif

    // Push the data
    size_t offset = top & mask;
    std_mem_copy ( base + offset, item, size );

    std_compiler_fence();
    queue->top = top + size;
}

void std_queue_spsc_pop_discard ( std_queue_shared_t* queue, size_t size ) {
    // Load
    size_t bot = queue->bot;

#if std_log_assert_enabled_m
    size_t top = queue->top;
    // Test for enough allocated space for the data
    std_assert_m ( top - bot >= size );
#endif

    std_compiler_fence();
    queue->bot = bot + size;
}

void std_queue_spsc_pop_move ( std_queue_shared_t* queue, void* dest, size_t size ) {
    // Load
    char* base = queue->base;
    size_t  mask = queue->mask;
    size_t  bot = queue->bot;

#if std_log_assert_enabled_m
    size_t  top = queue->top;
    // Test for enough allocated space for the data
    std_assert_m ( top - bot >= size );
#endif

    // Read the data
    size_t offset = bot & mask;
    std_mem_copy ( dest, base + offset, size );

    std_compiler_fence();
    queue->bot = bot + size;
}

//==============================================================================
// MPMC
// http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
// CAS on top/bot increment and dedicated 4 bytes tag slot per item. Contains
// item size or 0.
// Needs more testing.
//

bool std_queue_mpmc_push ( std_queue_shared_t* queue, const void* item, size_t size ) {
    std_assert_m ( std_align_test ( size, 4 ), "Queue payload size must be a multiple of 4." );
    std_assert_m ( size < UINT32_MAX );

    char* base = queue->base;
    size_t mask = queue->mask;
    size_t bot = queue->bot;
    size_t top = queue->top;

    // Protect vs. full queue
    size_t cap = mask + 1;
    size_t used_size = top - bot;
    if ( cap - used_size < sizeof ( uint32_t ) + size ) {
        return false;
    }

    // Protect vs. writing to a slot that's currently being read (pending read)
    size_t offset = top & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    if ( *tag != 0 ) {
        return false;
    }

    // Acquire the slot and begin the pending write
    size_t new_top = top + sizeof ( uint32_t ) + size;
    if ( std_compare_and_swap_u64 ( &queue->top, &top, new_top ) ) {
        top += sizeof ( uint32_t );
        offset = top & mask;
        std_mem_copy ( base + offset, item, size );
        std_compiler_fence();
        // Write the size to end the pending write
        *tag = ( uint32_t ) size;
        return true;
    }

    return false;
}

size_t std_queue_mpmc_pop_discard ( std_queue_shared_t* queue ) {
    size_t mask = queue->mask;
    char* base = queue->base;
    size_t top = queue->top;
    size_t bot = queue->bot;

    // Protect vs. writing to a slot that's currently being read (pending read)
    size_t offset = bot & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    uint32_t size = *tag;
    if ( size == 0 ) {
        return 0;
    }

    // Protect vs. empty queue
    size_t used_size = top - bot;
    if ( used_size < size + sizeof ( uint32_t ) ) {
        return 0;
    }

    // Consume the slot and begin the pending read
    size_t new_bot = bot + sizeof ( uint32_t ) + size;
    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        std_mem_zero ( base + offset, size );
        std_compiler_fence();
        // Zero the tag to end the pending read
        *tag = 0;
        return size;
    }

    return 0;
}

size_t std_queue_mpmc_pop_move ( std_queue_shared_t* queue, void* dest, size_t dest_cap ) {
    char* base = queue->base;
    size_t mask = queue->mask;
    size_t top = queue->top;
    size_t bot = queue->bot;

    // Protect vs. writing to a slot that's currently being read (pending read)
    size_t offset = bot & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    uint32_t size = *tag;
    if ( size == 0 ) {
        return 0;
    }

    // Protect vs. empty queue
    std_assert_m ( size % 4 == 0 );
    std_assert_m ( size <= dest_cap );
    size_t used_size = top - bot;
    if ( used_size < size + sizeof ( uint32_t ) ) {
        return 0;
    }

    // Consume the slot and begin the pending read
    size_t new_bot = bot + sizeof ( uint32_t ) + size;
    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        bot += sizeof ( uint32_t );
        offset = bot & mask;
        std_mem_copy ( dest, base + offset, size );
        // TODO this mem_zero call is bad. Is there a way to avoid this?
        // The reason for it is that if there's no guarantee on data alignment, after a wrap around on the buffer
        // the tag bytes might end up located where before was stored some user data, which is likely not to be 0
        // To solve that need to clear all popped data to 0, or establish a stride and just clear the tag on pop
        std_mem_zero ( base + offset, size );
        std_compiler_fence();
        // Zero the tag to end the pending read
        *tag = 0;
        return size;
    }

    return 0;
}


//==============================================================================
// 4 bytes MPMC
// Avoids allocating 4 extra bytes per item, instead reserves just one value
// to flag items.
#if 0
std_queue_shared_t std_queue_mpmc_32 ( void* base, size_t size ) {
    std_queue_shared_t queue = std_queue_shared ( base, size );
    std_mem_set ( queue.base, queue.mask + 1, 0xff );
    return queue;
}
#endif

std_queue_shared_t std_queue_mpmc_32_create ( size_t size ) {
    std_queue_shared_t queue = std_queue_shared_create ( size );
    std_mem_set ( queue.base, queue.mask + 1, 0xff );
    return queue;
}

bool std_queue_mpmc_push_32 ( std_queue_shared_t* queue, const void* item ) {
    char* base = queue->base;
    size_t mask = queue->mask;
    size_t top = queue->top;
    size_t bot = queue->bot;

    // Protect vs. full queue
    if ( top - bot == mask + 1 ) {
        return false;
    }

    // Protect vs. writing to a slot that's currently being read (pending read)
    size_t offset = top & mask;
    uint32_t* dest = ( uint32_t* ) ( base + offset );
    if ( *dest != std_queue_shared_32_reserved_value_m ) {
        return false;
    }

    // Acquire the slot and begin the pending write
    size_t new_top = top + sizeof ( uint32_t );
    if ( std_compare_and_swap_u64 ( &queue->top, &top, new_top ) ) {
        // Write the data to end the pending write
        *dest = * ( const uint32_t* ) item;
        return true;
    }

    return false;
}

bool std_queue_mpmc_pop_discard_32 ( std_queue_shared_t* queue ) {
    size_t  mask = queue->mask;
    char* base = queue->base;
    size_t top = queue->top;
    size_t bot = queue->bot;

    // Protect vs. empty queue
    if ( top - bot < 4 ) {
        return false;
    }

    // Protect vs. reading a slot that's currently being written (pending write)
    size_t offset = bot & mask;
    uint32_t* data = ( uint32_t* ) ( base + offset );
    if ( *data == std_queue_shared_32_reserved_value_m ) {
        return false;
    }

    // Consume the slot and begin the pending read
    size_t new_bot = bot + sizeof ( uint32_t );
    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        // Write back the invalid value to end the pending read
        *data = std_queue_shared_32_reserved_value_m;
        return true;
    }

    return false;
}

bool std_queue_mpmc_pop_move_32 ( std_queue_shared_t* queue, void* dest ) {
    // Load
    size_t  mask = queue->mask;
    char* base = queue->base;
    size_t top = queue->top;
    size_t bot = queue->bot;

    if ( top - bot < 4 ) {
        return false;
    }

    // Read and validate data
    size_t offset = bot & mask;
    uint32_t* data = ( uint32_t* ) ( base + offset );

    if ( *data == std_queue_shared_32_reserved_value_m ) {
        return false;
    }

    // Advance bot, read the data, set the publish flag to 0
    size_t new_bot = bot + sizeof ( uint32_t );

    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        * ( uint32_t* ) dest = *data;
        std_compiler_fence();
        *data = std_queue_shared_32_reserved_value_m;
        return true;
    }

    return false;
}

//==============================================================================
// 8 bytes MPMC
// 64
#if 0
std_queue_shared_t std_queue_mpmc_64 ( void* base, size_t size ) {
    std_queue_shared_t queue = std_queue_shared ( base, size );
    std_mem_set ( queue.base, queue.mask + 1, 0xff );
    return queue;
}
#endif

std_queue_shared_t std_queue_mpmc_64_create ( size_t size ) {
    std_queue_shared_t queue = std_queue_shared_create ( size );
    std_mem_set ( queue.base, queue.mask + 1, 0xff );
    return queue;
}

bool std_queue_mpmc_push_64 ( std_queue_shared_t* queue, const void* item ) {
    // Load
    size_t  mask = queue->mask;
    char* base = queue->base;
#if std_log_assert_enabled_m
    //size_t bot = queue->bot;
#endif
    size_t top = queue->top;

    // If dest is reserved value, no consumer will try to consume this until a producer sets it
    size_t offset = top & mask;
    uint64_t* dest = ( uint64_t* ) ( base + offset );

    if ( *dest != std_queue_shared_64_reserved_value_m ) {
        return false;
    }

    // Advance top, write the data
    size_t new_top = top + sizeof ( uint64_t );

    if ( std_compare_and_swap_u64 ( &queue->top, &top, new_top ) ) {
        *dest = * ( const uint64_t* ) item;
        return true;
    }

    return false;
}

bool std_queue_mpmc_pop_discard_64 ( std_queue_shared_t* queue ) {
    // Load
    size_t  mask = queue->mask;
    char* base = queue->base;
    size_t top = queue->top;
    size_t bot = queue->bot;

    if ( top - bot < 8 ) {
        return false;
    }

    // Read and validate the publish flag
    size_t offset = bot & mask;
    uint64_t* data = ( uint64_t* ) ( base + offset );

    if ( *data == std_queue_shared_64_reserved_value_m ) {
        return false;
    }

    // Advance bot, set data to reserved value
    size_t new_bot = bot + sizeof ( uint64_t );

    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        *data = std_queue_shared_64_reserved_value_m;
        return true;
    }

    return false;
}

bool std_queue_mpmc_pop_move_64 ( std_queue_shared_t* queue, void* dest ) {
    // Load
    size_t  mask = queue->mask;
    char* base = queue->base;
    size_t top = queue->top;
    size_t bot = queue->bot;

    if ( top - bot < 8 ) {
        return false;
    }

    // Read and validate data
    size_t offset = bot & mask;
    uint64_t* data = ( uint64_t* ) ( base + offset );

    if ( *data == std_queue_shared_64_reserved_value_m ) {
        return false;
    }

    // Advance bot, read the data, set the publish flag to 0
    size_t new_bot = bot + sizeof ( uint64_t );

    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        * ( uint64_t* ) dest = *data;
        std_compiler_fence();
        *data = std_queue_shared_64_reserved_value_m;
        return true;
    }

    return false;
}

//==============================================================================
// MPSC
// The queue is very similar to a MPMC. The only difference is the more relaxed
// pop, since there aren't potentially multiple threads running it in parallel.
// This allows to use a more simple fence instead of a full CAS.

bool std_queue_mpsc_push ( std_queue_shared_t* queue, const void* item, size_t size ) {
    return std_queue_mpmc_push ( queue, item, size );
}

size_t std_queue_mpsc_pop_discard ( std_queue_shared_t* queue ) {
    // Load
    char* base = queue->base;
    size_t mask = queue->mask;
    size_t top = queue->top;
    size_t bot = queue->bot;

    // Read and validate the publish flag (size value)
    size_t offset = bot & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    uint32_t size = *tag;

    if ( size == 0 ) {
        // Item not there or not yet published
        return 0;
    }

    std_assert_m ( size % 4 == 0 );

    if ( top - bot < size + sizeof ( uint32_t ) ) {
        // Not enough stored data in the queue? Should this even ever happen?
        return 0;
    }

    // Advance bot, set the publish flag to 0
    queue->bot = bot + sizeof ( uint32_t ) + size;
    std_compiler_fence();
    *tag = 0;
    return size;
}

size_t std_queue_mpsc_pop_move ( std_queue_shared_t* queue, void* dest, size_t dest_cap ) {
    // Load
    char* base = queue->base;
    size_t mask = queue->mask;
    size_t top = queue->top;
    size_t bot = queue->bot;

    // Read and validate the publish flag (size value)
    size_t offset = bot & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    uint32_t size = *tag;

    if ( size == 0 ) {
        // Item not there or not yet published
        return 0;
    }

    std_assert_m ( size % 4 == 0 );

    if ( top - bot < size + sizeof ( uint32_t ) ) {
        // Not enough stored data in the queue? Should this even ever happen?
        return 0;
    }

    std_assert_m ( size <= dest_cap );

    // Advance bot, read the data, set the publish flag to 0
    queue->bot = bot + sizeof ( uint32_t ) + size;

    bot += sizeof ( uint32_t );
    offset = bot & mask;
    std_mem_copy ( dest, base + offset, size );
    std_compiler_fence();
    *tag = 0;
    return size;
}

//==============================================================================
// SPMC
// The queue is very similar to a MPMC. The only difference is the more relaxed
// psuh, since there aren't potentially multiple threads running it in parallel.
// This allows to use a more simple fence instead of a full CAS.

void std_queue_spmc_push ( std_queue_shared_t* queue, const void* item, size_t size ) {
    std_assert_m ( std_align_test ( size, 4 ), "Queue payload size must be a multiple of 8." );
    std_assert_m ( size < UINT32_MAX );

    // Load
    char* base = queue->base;
    size_t mask = queue->mask;
    size_t top = queue->top;
    size_t cap = mask + 1;

    // Test for enough free space
    // TODO remove the std_queue_shared_used_size call, just compute locally using the already loaded values
    std_assert_m ( cap - std_queue_shared_used_size ( queue ) >= size + sizeof ( uint32_t ) );

    uint32_t* tag = ( uint32_t* ) ( base + queue->top );
    top += sizeof ( uint32_t );
    queue->top = top + size;

    // Push the data
    size_t offset = top & mask;
    std_mem_copy ( base + offset, item, size );

    std_compiler_fence();
    // Push the size
    *tag = ( uint32_t ) size;
}

size_t std_queue_spmc_pop_discard ( std_queue_shared_t* queue ) {
    return std_queue_mpmc_pop_discard ( queue );
}

size_t std_queue_spmc_pop_move ( std_queue_shared_t* queue, void* dest, size_t dest_cap ) {
    return std_queue_mpmc_pop_move ( queue, dest, dest_cap );
}

//==============================================================================
