#pragma once

#include <std_byte.h>
#include <std_buffer.h>

// Generally, each list item is required to have a void* as their first member, to store an intrusive pointer to the next element.
// In the case of freelist this is only true when the element is unused, so in practice it only needs its elements to be of size >= std_pointer_size_m.

// In the following, list head = pointer to the first element in the list (or NULL for an empty list)

// Linked list
// Takes a pointer to the list head, and a pointer to the element to push/pop
void std_list_push ( void* head, void* item );
void* std_list_pop ( void* list );

// Takes a pointer to a list element (inclusing the list head)
void* std_list_next ( void* item );

// Takes a pointer to the element whose next will be changed, and a pointer to the element to insert/remove
// Note: the parent param can be the list head, but in that case it is necessary to pass a pointer to the head instead of the head itself, otherwise it will become stale
// Note: when calling remove the item param must be the parent's next. TODO remove this param and just get it from parent?
void std_list_insert ( void* parent, void* item );
void std_list_remove ( void* parent, void* item );

#define std_list_pop_m(list) ( std_typeof_m(*list) ) std_list_pop ( list )
#define std_list_next_m(item) ( std_typeof_m(item) ) std_list_next ( item )

// Doubly linked list
void std_dlist_push ( void* head, void* item );
void* std_dlist_pop ( void* list );
void std_dlist_remove ( void* item );

void std_dlist_push_offset ( void* head, void* item, uint64_t offset );
void* std_dlist_pop_offset ( void* list, uint64_t offset );
void std_dlist_remove_offset ( void* item, uint64_t offset );

#define std_dlist_pop_offset_m( list, offset ) ( std_typeof_m ( *list ) ) std_dlist_pop_offset ( list, offset );

//void* std_dlist_next ( void* item );
//void* std_dlist_prev ( void* item );

//void std_dlist_insert ( void* parent, void* item );

#define std_dlist_pop_m(list) ( std_typeof_m(*list) ) std_dlist_pop ( list )
#define std_dlist_next_m(item) ( std_typeof_m(item) ) std_dlist_next ( item )
#define std_dlist_prev_m(item) ( std_typeof_m(item) ) std_dlist_prev ( item )

// Freelist
// The buffer base and the stride must both be pointer aligned.
void* std_freelist ( std_buffer_t buffer, size_t stride );

#define std_freelist_array_m( array, count ) std_array_typecast_m(array) std_freelist ( std_buffer (array, std_array_stride_m(array) * count), std_array_stride_m(array) )
#define std_static_freelist_m( array ) std_array_typecast_m(array) std_freelist ( std_static_buffer_m ( (array) ), std_array_stride_m( (array) ) )

// Freelist array
// When pooling back, only the fist n bytes (where n is pointer size) are overwritten on the item, the rest is untouched.
// std_pool_pop takes an unused available item from the pool. std_pool_push pools back an item into the pool.
#if 0
typedef struct {
    std_buffer_t buffer;
    void* freelist;
    size_t pop;
    size_t cap;
    size_t stride;
} std_pool_t;

std_pool_t                      std_pool            ( std_buffer_t buffer, size_t stride );
void*                           std_pool_pop        ( std_pool_t* pool );
void                            std_pool_push       ( std_pool_t* pool, void* item );
void                            std_pool_push_idx   ( std_pool_t* pool, size_t idx );
void*                           std_pool_at         ( const std_pool_t* pool, size_t idx );
size_t                          std_pool_index      ( const std_pool_t* pool, void* item );

#define                         std_static_pool_m(array) std_pool(std_static_buffer_m(array), sizeof(*array));
#endif

#if 0
/*
    Segmented pool
*/
// Interface is similar to that of a pool, with the addition of being able to allocate a contiguous segment of pool items instead of single items
// Implementation uses multiple freelists to track free segments, depending on their size (binning)
// To be able to merge adjacent free items into a single segment, some values are reserved, not to be ever used by the user:
//      the first u64 word in a segment can't have its highest bit set to 1
//      the last  u64 word in a segment can't have its highest bit set to 1
// Minumum item stride is 32 bytes, and greater strides must be 8 byte aligned.
// Generally speaking one should use std_pool_t instead of this one if the segmented allocation is not required.
// TODO cap bin size, bin count to u32
// TODO avoid storing size at all, use top 16 bits in the list pointer to store the size?
typedef struct {
    std_list_t list;
    size_t size;
} std_segment_pool_bin_t;

typedef struct {
    std_buffer_t buffer;
    size_t pop;
    size_t cap;
    size_t stride;
    size_t bin_count;
    std_segment_pool_bin_t bins[std_segment_pool_max_bins_m];
} std_segment_pool_t;

std_segment_pool_t std_segment_pool ( std_buffer_t buffer, size_t stride, const size_t* bins, size_t bin_count );
void* std_segment_pool_pop ( std_pool_t* pool, size_t count );
void std_segment_pool_push ( std_pool_t* pool, void* base, size_t count );
void* std_segment_pool_at ( const std_pool_t* pool, size_t idx );
size_t std_segment_pool_index ( const std_pool_t* pool, void* item );
#endif
