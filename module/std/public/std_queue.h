#pragma once

#include <std_compiler.h>
#include <std_allocator.h>
#include <std_log.h>
#include <std_byte.h>

// Ring logic
// https://fgiesen.wordpress.com/2010/12/14/ring-buffers-and-queues/
typedef struct {
    uint64_t top;
    uint64_t bot;
    uint64_t mask; // Equals to (size - 1). Size must be pow2.
} std_ring_t;

std_ring_t std_ring ( uint64_t capacity );
uint64_t std_ring_count ( const std_ring_t* ring );
uint64_t std_ring_capacity ( const std_ring_t* ring );
uint64_t std_ring_top_idx ( const std_ring_t* ring );
uint64_t std_ring_bot_idx ( const std_ring_t* ring );
uint64_t std_ring_idx ( const std_ring_t* ring, uint64_t virtual_idx );
void std_ring_push ( std_ring_t* ring, uint64_t count );
void std_ring_pop ( std_ring_t* ring, uint64_t count );
void std_ring_clear ( std_ring_t* ring );

#if 0
typedef struct {
    uint64_t top;
    char _p0[std_l1d_size_m - sizeof ( uint64_t )];
    uint64_t bot;
    char _p1[std_l1d_size_m - sizeof ( uint64_t )];
    uint64_t mask;
    char _p2[std_l1d_size_m - sizeof ( uint64_t )];
} std_atomic_ring_t;

std_atomic_ring_t std_atomic_ring ( uint64_t capacity );
uint64_t std_atomic_ring_count ( const std_atomic_ring_t* ring );
uint64_t std_atomic_ring_capacity ( const std_atomic_ring_t* ring );
uint64_t std_atomic_ring_top_idx ( const std_atomic_ring_t* ring );
uint64_t std_atomic_ring_bot_idx ( const std_atomic_ring_t* ring );
uint64_t std_atomic_ring_idx ( const std_atomic_ring_t* ring, uint64_t virtual_idx );
// try to force wrap if elements can't all be stored contiguously
uint64_t std_atomic_ring_push_align_wrap ( std_atomic_ring_t* ring, uint64_t count, uint64_t align );
// TODO
//bool std_atomic_ring_push ( std_atomic_ring_t* ring, uint64_t count );
//bool std_atomic_ring_pop ( std_atomic_ring_t* ring, uint64_t count );
void std_atomic_ring_clear ( std_atomic_ring_t* ring );
#endif

// Single thread access queue
typedef struct {
    void* base;
    void* alias;
    std_ring_t ring;
} std_queue_local_t;

std_queue_local_t   std_queue_local             ( void* base, size_t size );
std_queue_local_t   std_queue_local_create      ( size_t size );
void                std_queue_local_destroy     ( std_queue_local_t* queue );
void                std_queue_local_clear       ( std_queue_local_t* queue );
size_t              std_queue_local_size        ( const std_queue_local_t* queue );
size_t              std_queue_local_used_size   ( const std_queue_local_t* queue );

void                std_queue_local_push        ( std_queue_local_t* queue, const void* item, size_t size );
void                std_queue_local_pop_discard ( std_queue_local_t* queue, size_t size );
void                std_queue_local_pop_move    ( std_queue_local_t* queue, void* dest, size_t size );

void*               std_queue_local_emplace     ( std_queue_local_t* queue, size_t size ); // TODO rename to reserve?

void                std_queue_local_align_push  ( std_queue_local_t* queue, size_t align );
void                std_queue_local_align_pop   ( std_queue_local_t* queue, size_t align );

#define std_queue_local_emplace_array_m( queue, type, count ) ( type* ) std_queue_local_emplace( (queue), sizeof ( type ) * (count) )
#define std_static_local_queue_m( array )           std_queue_local ( std_static_buffer_m ( array ) )

// Multi thread access queue
// pad contents to avoid false sharing cache lines
std_static_align_m ( std_l1d_size_m ) typedef struct {
    void*    base;
    void*   alias;
    char    _p0[std_l1d_size_m - sizeof ( char* ) - sizeof ( char* )];
    size_t  top;
    char    _p1[std_l1d_size_m - sizeof ( size_t )];
    size_t  bot;
    char    _p2[std_l1d_size_m - sizeof ( size_t )];
    size_t  mask;       // Equals to (size - 1). Size must be pow2.
    char    _p3[std_l1d_size_m - sizeof ( size_t )];
} std_queue_shared_t;

// Must be allocated though the heap, to be able to map adjacent virtual pages to the same physical memory
std_queue_shared_t  std_queue_shared_create     ( size_t size );
void                std_queue_shared_destroy    ( std_queue_shared_t* queue );
void                std_queue_shared_clear      ( std_queue_shared_t* queue );
size_t              std_queue_shared_size       ( std_queue_shared_t* queue );
size_t              std_queue_shared_used_size  ( std_queue_shared_t* queue );

const void*         std_queue_shared_sc_peek    ( std_queue_shared_t* queue );

#define std_static_queue_shared( array ) std_queue_shared ( array, sizeof ( array ) )

// SPSC queue API never fails, because it assumes that the caller checks free/used size before calling push/pop.
// It also doesn't store any additional info in the queue other than the payload, because no publish flag is necessary.
// If dynamic payload size is required the user can implement that anyway using a 2-message protocol, where the first
// push is of constant size and contains the size of the second. Since there's only 1 producer and 1 consumer, this is
// thread safe.
void                std_queue_spsc_push         ( std_queue_shared_t* queue, const void* item, size_t size );
void                std_queue_spsc_pop_discard  ( std_queue_shared_t* queue, size_t size );
void                std_queue_spsc_pop_move     ( std_queue_shared_t* queue, void* dest, size_t size );

// MPSC, SPMC and MPMC queue APIs can all fail because of concurrency, so calling code should check the return code
// and react accordingly. They also store the size of the payload in the queue along with the payload
// itself. This allows for popping without knowing the exact size of the item. A publish flag is already necessary 
// for the implementation to be correct. So instead of a flag, 4 bytes are used to contain the size.
// This also enforces the queue alignment to 4 bytes, meaning that the user payload size must be a multiple of 4,
// and the max size of an item to UINT32_MAX.
bool                std_queue_mpsc_push         ( std_queue_shared_t* queue, const void* item, size_t size );
size_t              std_queue_mpsc_pop_discard  ( std_queue_shared_t* queue );
size_t              std_queue_mpsc_pop_move     ( std_queue_shared_t* queue, void* dest, size_t dest_cap );
#define             std_queue_mpsc_pop_m( queue, item )     std_queue_mpsc_pop_move ( queue, item, sizeof ( *(item) ) )

void                std_queue_spmc_push         ( std_queue_shared_t* queue, const void* item, size_t size );
size_t              std_queue_spmc_pop_discard  ( std_queue_shared_t* queue );
size_t              std_queue_spmc_pop_move     ( std_queue_shared_t* queue, void* dest, size_t dest_cap );

bool                std_queue_mpmc_push         ( std_queue_shared_t* queue, const void* item, size_t size );
size_t              std_queue_mpmc_pop_discard  ( std_queue_shared_t* queue );
size_t              std_queue_mpmc_pop_move     ( std_queue_shared_t* queue, void* dest, size_t dest_cap );
#define             std_queue_mpmc_push_m( queue, item )    std_queue_mpmc_push ( queue, item, sizeof ( *(item) ) )
#define             std_queue_mpmc_pop_m( queue, item )     std_queue_mpmc_pop_move ( queue, item, sizeof ( *(item) ) )

// 32 and 64 bit element size queues specialization - the APIs can't be mixed!
// No extra space per item is allocated for these, but the reserved values cannot be stored as data.
#define             std_queue_shared_32_reserved_value_m 0xffffffff
#define             std_queue_shared_64_reserved_value_m 0xffffffffffffffff

std_queue_shared_t  std_queue_mpmc_32_create        ( size_t size );
bool                std_queue_mpmc_push_32          ( std_queue_shared_t* queue, const void* item );
bool                std_queue_mpmc_pop_discard_32   ( std_queue_shared_t* queue );
bool                std_queue_mpmc_pop_move_32      ( std_queue_shared_t* queue, void* dest );

std_queue_shared_t  std_queue_mpmc_64_create        ( size_t size );
bool                std_queue_mpmc_push_64          ( std_queue_shared_t* queue, const void* item );
bool                std_queue_mpmc_pop_discard_64   ( std_queue_shared_t* queue );
bool                std_queue_mpmc_pop_move_64      ( std_queue_shared_t* queue, void* dest );

// TODO 32 and 64 bit specializations for mpsc queue
// TODO name the calls that can fail with a `try` and include the loop inside the call for others?
// TODO implement peek for sc
