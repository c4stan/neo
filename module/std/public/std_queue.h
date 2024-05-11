#pragma once

#include <std_compiler.h>
#include <std_allocator.h>
#include <std_log.h>
#include <std_byte.h>

/*
    Is it better to return a pointer on push and let the user do the memcpy?
        would not be doable in the case of a wrap-around queue
        would allow to use the same struct/api for queue and circular pool
*/

typedef struct {
    char* base;
    size_t top;
    size_t bot;
    size_t mask;        // Equals to (size - 1). Size must be pow2.
} std_queue_local_t;    // TODO rename to std_queue_t

// TODO base the implementation on std_ring_t
// The implementation wraps the stores when the queue buffer wraps around. In that case alignment is not guaranteed.
std_queue_local_t   std_queue_local             ( void* base, size_t size );
void                std_queue_local_clear       ( std_queue_local_t* queue );
size_t              std_queue_local_size        ( const std_queue_local_t* queue );
size_t              std_queue_local_used_size   ( const std_queue_local_t* queue );

void                std_queue_local_push        ( std_queue_local_t* queue, const void* item, size_t size );
void                std_queue_local_pop_discard ( std_queue_local_t* queue, size_t size );
void                std_queue_local_pop_move    ( std_queue_local_t* queue, void* dest, size_t size );

void*               std_queue_local_emplace     ( std_queue_local_t* queue, size_t size );

void*               std_queue_local_ptr         ( std_queue_local_t* queue, size_t size );

void                std_queue_local_align_push  ( std_queue_local_t* queue, size_t align );
void                std_queue_local_align_pop   ( std_queue_local_t* queue, size_t align );

#define std_queue_local_emplace_array_m( queue, type, count ) ( type* ) std_queue_local_emplace( (queue), sizeof ( type ) * (count) )
#define std_queue_local_ptr_m( queue, type, idx )   ( type* ) std_queue_local_ptr( (queue), sizeof ( type ) * idx )
#define std_static_local_queue_m( array )           std_queue_local ( std_static_buffer_m ( array ) )

// Ring buffer
// https://fgiesen.wordpress.com/2010/12/14/ring-buffers-and-queues/
typedef struct {
    uint64_t top;
    uint64_t bot;
    uint64_t mask;
} std_ring_t;

std_ring_t std_ring ( uint64_t capacity );
uint64_t std_ring_count ( const std_ring_t* ring );
uint64_t std_ring_capacity ( const std_ring_t* ring );
uint64_t std_ring_top_idx ( const std_ring_t* ring );
uint64_t std_ring_bot_idx ( const std_ring_t* ring );
uint64_t std_ring_idx ( const std_ring_t* ring, uint64_t virtual_idx );
void std_ring_push ( std_ring_t* ring, uint64_t count );
void std_ring_pop ( std_ring_t* ring, uint64_t count );

// ----------

std_static_align_m ( std_l1d_size_m ) typedef struct {
    char* base;
    char  _p0[std_l1d_size_m - sizeof ( char* )];
    size_t  top;
    char  _p1[std_l1d_size_m - sizeof ( size_t )];
    size_t  bot;
    char  _p2[std_l1d_size_m - sizeof ( size_t )];
    size_t  mask;       // Equals to (size - 1). Size must be pow2.
    char  _p3[std_l1d_size_m - sizeof ( size_t )];
} std_queue_shared_t;

/*
    https://fastcompression.blogspot.com/2015/08/accessing-unaligned-memory.html
*/

std_queue_shared_t  std_queue_shared            ( void* base, size_t size );
void                std_queue_shared_clear      ( std_queue_shared_t* queue );
size_t              std_queue_shared_size       ( std_queue_shared_t* queue );
size_t              std_queue_shared_used_size  ( std_queue_shared_t* queue );

const void*         std_queue_shared_sc_peek    ( std_queue_shared_t* queue );

#define std_static_queue_shared( array ) std_queue_shared ( array, sizeof ( array ) )

// TODO implement peek for sc ?

// SPSC queue API never fails, because it assumes that the caller checks free/used size before calling push/pop.
// It also doesn't store any additional info in the queue other than the payload, because no publish flag is necessary.
// If dynamic payload size is required the user can implement that anyway using a 2-message protocol, where the first
// push is of constant size and contains the size of the second. Since there's only 1 producer and 1 consumer, this is
// thread safe.
void                std_queue_spsc_push         ( std_queue_shared_t* queue, const void* item, size_t size );
void                std_queue_spsc_pop_discard  ( std_queue_shared_t* queue, size_t size );
void                std_queue_spsc_pop_move     ( std_queue_shared_t* queue, void* dest, size_t size );

// MPSC, SPMC and MPMC queue APIs can all fail because of concurrency, so calling push/pop should always happen in
// a loop that checks the returned value. They also store the size of the payload in the queue along with the payload
// itself. This allows for popping without knowing the exact size of the item. This is because a publish flag is
// necessary in the implementation to ensure correctness. So instead of a flag, 4 bytes are used to contain the size.
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
#define             std_queue_shared_32_reserved_value_m 0xffffffff
#define             std_queue_shared_64_reserved_value_m 0xffffffffffffffff

std_queue_shared_t  std_queue_mpmc_32               ( void* base, size_t size );
bool                std_queue_mpmc_push_32          ( std_queue_shared_t* queue, const void* item );
bool                std_queue_mpmc_pop_discard_32   ( std_queue_shared_t* queue );
bool                std_queue_mpmc_pop_move_32      ( std_queue_shared_t* queue, void* dest );

std_queue_shared_t  std_queue_mpmc_64               ( void* base, size_t size );
bool                std_queue_mpmc_push_64          ( std_queue_shared_t* queue, const void* item );
bool                std_queue_mpmc_pop_discard_64   ( std_queue_shared_t* queue );
bool                std_queue_mpmc_pop_move_64      ( std_queue_shared_t* queue, void* dest );

// TODO 32 and 64 bit specializations for mpsc queue
// TODO name the calls that can fail with a `try` and include the loop inside the call for others?
