#pragma once

#include <std_byte.h>
#include <std_compiler.h>

typedef struct {
    char* base;
    size_t size;
} std_buffer_t;

#define         std_null_buffer_m (std_buffer_t) { NULL, 0 }

std_buffer_t    std_buffer ( void* base, size_t size );
#define         std_static_buffer_m( array ) std_buffer ( (array), sizeof ( (array) ) )

bool            std_buffer_contains ( std_buffer_t buffer, std_buffer_t other );
bool            std_buffer_contains_ptr ( std_buffer_t buffer, void* address );

std_buffer_t    std_buffer_slice ( std_buffer_t buffer, size_t offset, size_t size );

#define         std_buffer_ptr_m( dest, buffer, idx ) *(dest) = & ( ( std_typeof_m ( (*dest) ) ) (buffer).base ) [idx];
#define         std_buffer_at_m( buffer, type, idx ) & ( ( type* ) (buffer).base ) [idx];
