#include <std_buffer.h>

#include <std_log.h>
#include <std_platform.h>

// Buffer

std_buffer_t std_buffer ( void* base, size_t size ) {
    std_buffer_t buffer;
    buffer.base = ( byte_t* ) base;
    buffer.size = size;
    return buffer;
}

std_buffer_t std_buffer_slice ( std_buffer_t buffer, size_t offset, size_t size ) {
    return std_buffer ( buffer.base + offset, size );
}

std_buffer_t std_buffer_slice_range ( std_buffer_t buffer, std_buffer_range_t range ) {
    return std_buffer ( buffer.base + range.offset, range.size );
}

std_buffer_t std_buffer_split ( std_buffer_t* buffer, size_t size ) {
    std_buffer_t new_buffer;
    new_buffer.base = buffer->base;
    new_buffer.size = size;
    buffer->base += size;
    buffer->size -= size;
    return new_buffer;
}

std_buffer_t std_buffer_split_align ( std_buffer_t* buffer, size_t size, size_t align ) {
    std_buffer_t new_buffer;
    new_buffer.base = buffer->base;
    new_buffer.size = size;
    buffer->base += size;
    byte_t* aligned_base = ( byte_t* ) std_align_ptr ( buffer->base, align );
    size_t align_size = ( size_t ) ( aligned_base - buffer->base );
    buffer->size -= ( size + align_size );
    return new_buffer;
}

bool std_buffer_contains ( std_buffer_t buffer, std_buffer_t other ) {
    return buffer.base <= other.base && buffer.base + buffer.size >= other.base + other.size;
}

bool std_buffer_contains_ptr ( std_buffer_t buffer, void* address ) {
    return buffer.base <= ( byte_t* ) address && buffer.base + buffer.size < ( byte_t* ) address;
}

std_buffer_range_t std_size_grow_align ( size_t* size, size_t grow_size, size_t grow_alignment ) {
    std_buffer_range_t range;
    range.offset = *size;
    range.offset = std_align ( range.offset, grow_alignment );
    *size = range.offset + grow_size;
    range.size = grow_size;
    return range;
}
