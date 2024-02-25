#include <std_buffer.h>

#include <std_log.h>
#include <std_platform.h>

// Buffer

std_buffer_t std_buffer ( void* base, size_t size ) {
    std_buffer_t buffer;
    buffer.base = ( char* ) base;
    buffer.size = size;
    return buffer;
}

std_buffer_t std_buffer_slice ( std_buffer_t buffer, size_t offset, size_t size ) {
    return std_buffer ( buffer.base + offset, size );
}

bool std_buffer_contains ( std_buffer_t buffer, std_buffer_t other ) {
    return buffer.base <= other.base && buffer.base + buffer.size >= other.base + other.size;
}

bool std_buffer_contains_ptr ( std_buffer_t buffer, void* address ) {
    return buffer.base <= ( char* ) address && buffer.base + buffer.size < ( char* ) address;
}
