#pragma once

#include <std_byte.h>
#include <std_compiler.h>

typedef struct {
    byte_t* base;
    size_t size;
} std_buffer_t;

typedef struct {
    size_t offset;
    size_t size;
} std_buffer_range_t;

#define                         std_null_buffer_m (std_buffer_t) { NULL, 0 }

std_buffer_t                    std_buffer ( void* base, size_t size );
#define                         std_static_buffer_m( array ) std_buffer ( (array), sizeof( (array) ) )

bool                            std_buffer_contains     ( std_buffer_t buffer, std_buffer_t other );
bool                            std_buffer_contains_ptr ( std_buffer_t buffer, void* address );

// Slicing returns a buffer that references memory that's part of the original one.
// The original is not affected.
std_buffer_t                    std_buffer_slice        ( std_buffer_t buffer, size_t offset, size_t size );
std_buffer_t                    std_buffer_slice_range  ( std_buffer_t buffer, std_buffer_range_t range );

// Splitting returns a buffer of the specified size that's separate from the original one.
// In doing so the original gets shrinked. The new buffer stands before the old one.
std_buffer_t                    std_buffer_split       ( std_buffer_t* buffer, size_t size );
std_buffer_t                    std_buffer_split_align ( std_buffer_t* buffer, size_t size, size_t align );
#define                         std_buffer_split_array_m( buffer, type, count ) std_buffer_split_align ( (buffer), sizeof ( type ) * (count), std_alignof_m ( type ) )

// Utility function to compute the size required to allocate a bunch of different data types (e.g. several arrays) in a single alloc call, considering internal alignments.
// The range structs returned can then be used to access each individual subdata.
// TODO make this some kind of separate utility struct located in allocator.h that also computes required alignment for the alloc call and is there
// specifically to help with allocating multiple data types in one alloc call?
// TODO just expand the allocator api to be able to allocate multiple aligned buffers in one single block, referenced by one single handle?
std_buffer_range_t              std_size_grow_align ( size_t* size, size_t grow_size, size_t grow_alignment );
#define                         std_size_grow_array_m( size, type, count ) std_size_grow_align ( (size), sizeof ( type ) * (count), std_alignof_m ( type ) )

// Simple helpers to use std_buffers as typed C arrays
// TODO take buffer ptrs instead of values? would be more coherent with the rest
#define                         std_buffer_load_m(dest, buffer, idx)    *(dest) = ( ( std_typeof_m ( (dest) ) ) (buffer).base ) [idx];
#define                         std_buffer_store_m(buffer, idx, src)    ( ( std_typeof_m ( (src) ) ) (buffer).base ) [idx] = *(src);
#define                         std_buffer_ptr_m(dest, buffer, idx)     *(dest) = & ( ( std_typeof_m ( (*dest) ) ) (buffer).base ) [idx];
#define                         std_buffer_idx_m(idx, buffer, item)     *(idx) = ( size_t ) ( (item) - ( ( std_typeof_m ( (item) ) ) (buffer).base ) )
