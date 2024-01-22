#include <std_queue.h>

#include <std_atomic.h>
#include <std_byte.h>
#include <std_log.h>

//==============================================================================

std_queue_local_t std_queue_local ( std_buffer_t buffer ) {
    if ( !std_pow2_test ( buffer.size ) ) {
        std_log_warn_m ( "Local queue buffer size is not power of two, rounding it down." );
        buffer.size = std_pow2_round_down ( buffer.size );
    }

    std_queue_local_t queue;
    queue.base = buffer.base;
    queue.mask = buffer.size - 1;
    queue.top = 0;
    queue.bot = 0;
    return queue;
}

void std_queue_local_clear ( std_queue_local_t* queue ) {
    queue->top = 0;
    queue->bot = 0;
}

size_t std_queue_local_size ( const std_queue_local_t* queue ) {
    return queue->mask + 1;
}

size_t std_queue_local_used_size ( const std_queue_local_t* queue ) {
    return queue->top - queue->bot;
}

void std_queue_local_push ( std_queue_local_t* queue, const void* item, size_t size ) {
    std_assert_m ( item != NULL );
    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;
    size_t  bot = queue->bot;

#if std_assert_enabled_m
    // Test for enough free space
    size_t cap = mask + 1;
    size_t used_size = top - bot;
    std_assert_m ( cap - used_size >= size );
#endif

    // Push the data
    size_t offset = top & mask;
    size_t pre = cap > offset + size ? size : offset + size - cap;
    size_t post = size - pre;
    std_mem_copy ( base + offset, item, pre );
    std_mem_copy ( base, ( byte_t* ) item + pre, post );

    queue->top = top + size;
}

void std_queue_local_pop_discard ( std_queue_local_t* queue, size_t size ) {
    // Load
    size_t  top = queue->top;
    size_t  bot = queue->bot;

#if std_assert_enabled_m
    // Make sure enough allocated space for the data
    size_t used_size = top - bot;
    std_assert_m ( used_size >= size );
#endif

    queue->bot = bot + size;
}

void std_queue_local_pop_move ( std_queue_local_t* queue, void* dest, size_t size ) {
    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;
    size_t  bot = queue->bot;

#if std_assert_enabled_m
    // Make sure enough allocated space for the data
    size_t used_size = top - bot;
    std_assert_m ( used_size >= size );
#endif

    // Read the data
    size_t offset = bot & mask;
    size_t cap = mask + 1;
    size_t pre = cap > offset + size ? size : offset + size - cap;
    size_t post = size - pre;
    std_mem_copy ( dest, base + offset, pre );
    std_mem_copy ( ( byte_t* ) dest + pre, base, post );

    queue->bot = bot + size;
}

void* std_queue_local_emplace ( std_queue_local_t* queue, size_t size ) {
    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;

    size_t offset = top & mask;

#if std_assert_enabled_m
    // Check space available and no wrap
    size_t cap = mask + 1;
    std_assert_m ( cap > offset + size );
#endif

    queue->top += size;
    return base + offset;
}

void* std_queue_local_ptr ( std_queue_local_t* queue, size_t size ) {
    return queue->base + size;
}

void std_queue_local_align_push ( std_queue_local_t* queue, size_t align ) {
    // Load
    size_t top = queue->top;

    size_t new_top = std_align ( top, align );

#if std_assert_enabled_m
    // Test for enough free space
    size_t size = new_top - top;
    size_t cap = queue->mask + 1;
    size_t used_size = top - queue->bot;
    std_assert_m ( cap - used_size >= size );
#endif

    // Align
    queue->top = new_top;
}

void std_queue_local_align_pop ( std_queue_local_t* queue, size_t align ) {
    // Load
    size_t bot = queue->bot;

    size_t new_bot = std_align ( bot, align );

#if std_assert_enabled_m
    // Make sure enough allocated space
    size_t top = queue->top;
    size_t size = new_bot - bot;
    std_assert_m ( top - bot >= size );
#endif

    // Pop
    queue->bot = new_bot;
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
    uint64_t mask = ring->mask;

#if std_assert_enabled_m
    uint64_t capacity = mask + 1;
    uint64_t used = top - ring->bot;
    std_assert_m ( capacity - used >= count );
#endif

    ring->top = top + count;
}

void std_ring_pop ( std_ring_t* ring, uint64_t count ) {
    uint64_t bot = ring->bot;

#if std_assert_enabled_m
    size_t used = ring->top - bot;
    std_assert_m ( used >= count );
#endif

    ring->bot = bot + count;
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
    byte_t* base = pool->base;
    size_t top = pool->top;
    size_t mask = pool->mask;
    size_t stride = pool->stride;

#if std_assert_enabled_m
    // Test for enough free space
    size_t bot = pool->bot;
    size_t cap = mask + 1;
    size_t used_size = top - bot;
    std_assert_m ( cap - used_size >= 1 );
#endif

    size_t offset = top & mask;
    byte_t* item = base + offset * stride;

    pool->top = top + 1;

    return item;
}

void std_circular_pool_pop ( std_circular_pool_t* pool ) {
    size_t bot = pool->bot;

#if std_assert_enabled_m
    // Test for enough allocated space
    size_t top = pool->top;
    size_t used_size = top - bot;
    std_assert_m ( used_size >= 1 );
#endif

    pool->bot = bot + 1;
}

void* std_circular_pool_peek ( std_circular_pool_t* pool ) {
    byte_t* base = pool->base;
    size_t stride = pool->stride;
    size_t mask = pool->mask;
    size_t bot = pool->bot;

    return base + stride * ( bot & mask );
}

void* std_circular_pool_at ( const std_circular_pool_t* pool, size_t idx ) {
    byte_t* base = pool->base;
    size_t mask = pool->mask;
    size_t stride = pool->stride;

    return base + stride * ( idx & mask );
}

void* std_circular_pool_at_buffer ( const std_circular_pool_t* pool, size_t idx ) {
    byte_t* base = pool->base;
    size_t stride = pool->stride;

#if std_assert_enabled_m
    size_t mask = pool->mask;
    size_t cap = mask + 1;
    std_assert_m ( cap > idx );
#endif

    return base + stride * idx;
}

void* std_circular_pool_at_pool ( const std_circular_pool_t* pool, size_t idx ) {
    byte_t* base = pool->base;
    size_t bot = pool->bot;
    size_t mask = pool->mask;
    size_t stride = pool->stride;

#if std_assert_enabled_m
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

std_queue_shared_t std_queue_shared ( std_buffer_t buffer ) {
    if ( !std_pow2_test ( buffer.size ) ) {
        std_log_warn_m ( "Shared queue buffer size is not power of two, rounding it down." );
        buffer.size = std_pow2_round_down ( buffer.size );
    }

    std_queue_shared_t queue;
    queue.base = buffer.base;
    queue.mask = buffer.size - 1;
    queue.top = 0;
    queue.bot = 0;
    return queue;
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
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;
    size_t  bot = queue->bot;

    size_t cap = mask + 1;

#if std_assert_enabled_m
    // Test for enough free space
    size_t used_size = top - bot;
    std_assert_m ( cap - used_size >= size );
#endif

    // Push the data
    size_t offset = top & mask;
    size_t pre = cap > offset + size ? size : offset + size - cap;
    size_t post = size - pre;
    std_mem_copy ( base + offset, item, pre );
    std_mem_copy ( base, ( byte_t* ) item + pre, post );

    std_compiler_fence();
    queue->top = top + size;
}

void std_queue_spsc_pop_discard ( std_queue_shared_t* queue, size_t size ) {
    // Load
    size_t top = queue->top;
    size_t bot = queue->bot;

#if std_assert_enabled_m
    // Test for enough allocated space for the data
    std_assert_m ( top - bot >= size );
#endif

    std_compiler_fence();
    queue->bot = bot + size;
}

void std_queue_spsc_pop_move ( std_queue_shared_t* queue, void* dest, size_t size ) {
    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;
    size_t  bot = queue->bot;

#if std_assert_enabled_m
    // Test for enough allocated space for the data
    std_assert_m ( top - bot >= size );
#endif

    // Read the data
    size_t offset = bot & mask;
    size_t cap = mask + 1;
    size_t pre = cap > offset + size ? size : offset + size - cap;
    size_t post = size - pre;
    std_mem_copy ( dest, base + offset, pre );
    std_mem_copy ( ( byte_t* ) dest + pre, base, post );

    std_compiler_fence();
    queue->bot = bot + size;
}

//==============================================================================
// MPMC
// http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
// WARNING: comments are outdated for the most part, TODO update them
// - Each item in the queue is preceeded by a small u64 header (called tag in the code)
//   that effectively contains the size of the item that comes after but also works as
//   a publish flag for the item itself.
// - We clear it to 0 before even trying to advance top because we want to make sure that
//   when that memory initially starts to be a part of the valid segment (between bot and top)
//   it is marked as not yet valid (tag == 0) and any pop on it will fail.
// - Then we copy the actual item data and finally use the tag as publish flag, and write the
//   item size to it. This way we can write generic sizes to the queue aswell (although it
//   does come at the small cost of having to handle possible truncated items at the buffer end)

bool std_queue_mpmc_push ( std_queue_shared_t* queue, const void* item, size_t size ) {
    std_assert_m ( std_align_test ( size, 4 ), "Queue payload size must be a multiple of 8." );
    std_assert_m ( size < UINT32_MAX );

    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  bot = queue->bot;
    size_t  top = queue->top;

    // If tag is 0, no consumer will try to consume this until a producer sets it
    size_t offset = top & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );

    if ( *tag != 0 ) {
        return false;
    }

    size_t cap = mask + 1;
    size_t used_size = top - bot;

    // Check for enough free size
    // TODO branching here might slow things down, just revert the CAS if not enough free size?
    if ( cap - used_size < sizeof ( uint32_t ) + size ) {
        return false;
    }

    // Advance top, write the data, set the publish flag to size
    size_t new_top = top + sizeof ( uint32_t ) + size;

    if ( std_compare_and_swap_u64 ( &queue->top, &top, new_top ) ) {
        top += sizeof ( uint32_t );
        offset = top & mask;
        // Test for wrap around
        size_t pre = cap > offset + size ? size : offset + size - cap;
        size_t post = size - pre;
        std_mem_copy ( base + offset, item, pre );
        std_mem_copy ( base, ( byte_t* ) item + pre, post );
        std_compiler_fence();
        *tag = ( uint32_t ) size;
        return true;
    }

    return false;
}

size_t std_queue_mpmc_pop_discard ( std_queue_shared_t* queue ) {
    // Load
    size_t  mask = queue->mask;
    byte_t* base = queue->base;
#if std_assert_enabled_m
    size_t top = queue->top;
#endif
    size_t bot = queue->bot;

    // Read and validate the publish flag (size value)
    size_t offset = bot & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    uint32_t size = *tag;

    if ( size == 0 ) {
        return 0;
    }

#if std_assert_enabled_m
    size_t used_size = top - bot;
    std_assert_m ( size % 4 == 0 );
    std_assert_m ( used_size >= size + sizeof ( uint32_t ) );
#endif

    // Advance bot, set the publish flag to 0
    size_t new_bot = bot + sizeof ( uint32_t ) + size;

    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        *tag = 0;
        std_compiler_fence();
        return size;
    }

    return 0;
}

size_t std_queue_mpmc_pop_move ( std_queue_shared_t* queue, void* dest, size_t dest_cap ) {
    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;
    size_t  bot = queue->bot;

    // Read and validate the publish flag (size value)
    size_t offset = bot & mask;
    uint32_t* tag = ( uint32_t* ) ( base + offset );
    uint32_t size = *tag;

    if ( size == 0 ) {
        return 0;
    }

    std_assert_m ( size % 4 == 0 );
    std_assert_m ( size <= dest_cap );

    size_t used_size = top - bot;

    if ( used_size < size + sizeof ( uint32_t ) ) {
        return 0;
    }

    // Advance bot, read the data, set the publish flag to 0
    size_t new_bot = bot + sizeof ( uint32_t ) + size;

    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        bot += sizeof ( uint32_t );
        offset = bot & mask;
        // Test for wrap around
        size_t cap = mask + 1;
        size_t pre = cap > offset + size ? size : offset + size - cap;
        size_t post = size - pre;
        std_mem_copy ( dest, base + offset, pre );
        std_mem_copy ( ( byte_t* ) dest + pre, base, post );
        // TODO this mem_zero call is bad. Is there a way to avoid this?
        // The issue is that if there's no guarantee on data alignment, after a wrap around on the buffer
        // the tag bytes might end up located where before was stored some user data, which is likely not to be 0
        // To solve that need to set all popped data to 0, or establish a stride and just clear the tag on pop
        std_mem_zero ( base + offset, pre );
        std_mem_zero ( base, post );
        std_compiler_fence();
        *tag = 0;
        return size;
    }

    return 0;
}

// 32
std_queue_shared_t std_queue_mpmc_32 ( std_buffer_t buffer ) {
    std_queue_shared_t queue = std_queue_shared ( buffer );
    std_mem_set ( queue.base, queue.mask + 1, 0xff );
    return queue;
}

bool std_queue_mpmc_push_32 ( std_queue_shared_t* queue, const void* item ) {
    // Load
    byte_t* base = queue->base;
    size_t  mask = queue->mask;
    size_t  top = queue->top;

    // If dest is reserved value, no consumer will try to consume this until a producer sets it
    size_t offset = top & mask;
    uint32_t* dest = ( uint32_t* ) ( base + offset );

    if ( *dest != std_queue_shared_32_reserved_value_m ) {
        return false;
    }

    // Advance top, write the data
    size_t new_top = top + sizeof ( uint32_t );

    if ( std_compare_and_swap_u64 ( &queue->top, &top, new_top ) ) {
        *dest = * ( const uint32_t* ) item;
        return true;
    }

    return false;
}

bool std_queue_mpmc_pop_discard_32 ( std_queue_shared_t* queue ) {
    // Load
    size_t  mask = queue->mask;
    byte_t* base = queue->base;
    size_t top = queue->top;
    size_t bot = queue->bot;

    if ( top - bot < 4 ) {
        return false;
    }

    // Read and validate the publish flag
    size_t offset = bot & mask;
    uint32_t* data = ( uint32_t* ) ( base + offset );

    if ( *data == std_queue_shared_32_reserved_value_m ) {
        return false;
    }

    // Advance bot, set data to reserved value
    size_t new_bot = bot + sizeof ( uint32_t );

    if ( std_compare_and_swap_u64 ( &queue->bot, &bot, new_bot ) ) {
        *data = std_queue_shared_32_reserved_value_m;
        return true;
    }

    return false;
}

bool std_queue_mpmc_pop_move_32 ( std_queue_shared_t* queue, void* dest ) {
    // Load
    size_t  mask = queue->mask;
    byte_t* base = queue->base;
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

// 64
std_queue_shared_t std_queue_mpmc_64 ( std_buffer_t buffer ) {
    std_queue_shared_t queue = std_queue_shared ( buffer );
    std_mem_set ( queue.base, queue.mask + 1, 0xff );
    return queue;
}

bool std_queue_mpmc_push_64 ( std_queue_shared_t* queue, const void* item ) {
    // Load
    size_t  mask = queue->mask;
    byte_t* base = queue->base;
#if std_assert_enabled_m
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
    byte_t* base = queue->base;
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
    byte_t* base = queue->base;
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
    byte_t* base = queue->base;
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
    byte_t* base = queue->base;
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
    // Test for wrap around
    size_t cap = mask + 1;
    size_t pre = cap > offset + size ? size : offset + size - cap;
    size_t post = size - pre;
    std_mem_copy ( dest, base + offset, pre );
    std_mem_copy ( ( byte_t* ) dest + pre, base, post );
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
    byte_t* base = queue->base;
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
    size_t pre = cap > offset + size ? size : offset + size - cap;
    size_t post = size - pre;
    std_mem_copy ( base + offset, item, pre );
    std_mem_copy ( base, ( byte_t* ) item + pre, post );

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
