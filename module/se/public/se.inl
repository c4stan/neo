#pragma once

#include <std_allocator.h>

// Component iterator

typedef struct {
    se_component_stream_t* stream;
    uint32_t page;
    uint32_t page_count;
} se_component_iterator_t;

#define se_component_iterator_m( component, stream_id ) ( se_component_iterator_t ) { \
    .stream = &(component)->streams[stream_id], \
    .page = 0, \
    .page_count = 0 \
}

static inline void* se_component_iterator_next ( se_component_iterator_t* iterator ) {
    if ( iterator->page_count >= iterator->stream->pages[iterator->page].count ) {
        if ( iterator->page == iterator->stream->page_count - 1 ) { 
            return NULL;
        } else {
            iterator->page = 0;
            iterator->page_count = 0;
        }
    }

    void* data = iterator->stream->pages[iterator->page].data + iterator->page_count * iterator->stream->data_stride;
    iterator->page_count += 1;
    return data;
}

// Entity params allocator

typedef struct {
    std_virtual_stack_t* stack;
    se_entity_params_t* entities;
    se_entity_update_t* entity;
    se_component_update_t* component;
} se_entity_params_allocator_t;

#define se_entity_params_allocator_m( _stack ) ( se_entity_params_allocator_t ) { \
    .stack = _stack, \
    .entities = NULL, \
    .entity = NULL, \
    .component = NULL \
}

static inline se_entity_params_allocator_t se_entity_params_allocator ( std_virtual_stack_t* stack ) {
    se_entity_params_allocator_t allocator;
    allocator.stack = stack;
    allocator.entities = std_virtual_stack_alloc_m ( stack, se_entity_params_t );
    allocator.entities->entity_count = 0;
    allocator.entity = NULL;
    allocator.component = NULL;
    return allocator;
}

static inline void se_entity_params_alloc_entity ( se_entity_params_allocator_t* allocator, se_entity_h entity_handle ) {
    allocator->entities->entity_count += 1;
    se_entity_update_t* entity = std_virtual_stack_alloc_m ( allocator->stack, se_entity_update_t );
    entity->entity = entity_handle;
    entity->component_count = 0;
    std_mem_zero_m ( &entity->mask );
    if ( allocator->entity ) {
        allocator->entity->next = entity;
    }
    allocator->entity = entity;
}

static inline void se_entity_params_alloc_component ( se_entity_params_allocator_t* allocator, uint32_t id ) {
    allocator->entity->component_count += 1;
    se_component_update_t* component = std_virtual_stack_alloc_m ( allocator->stack, se_component_update_t );
    component->id = id;
    component->stream_count = 0;
    std_bitset_set ( allocator->entity->mask.u64, id );
    allocator->component = component;
}

static inline void se_entity_params_alloc_stream ( se_entity_params_allocator_t* allocator, uint32_t id, void* data ) {
    allocator->component->stream_count += 1;
    se_component_stream_update_t* stream = std_virtual_stack_alloc_m ( allocator->stack, se_component_stream_update_t );
    stream->has_inline_data = 0;
    stream->id = id;
    stream->data = data;
}

static inline void se_entity_params_alloc_stream_inline ( se_entity_params_allocator_t* allocator, uint32_t id, void* data, uint32_t size ) {
    allocator->component->stream_count += 1;
    // This allocates only the first 8 bytes of the stream struct, used for the first 2 u8 fields. The remaining is allocated below and fully used for storing inline_data
    se_component_stream_update_t* stream = ( se_component_stream_update_t* ) std_virtual_stack_alloc_align ( allocator->stack, 8, 8 );
    stream->has_inline_data = 1;
    stream->id = id;
    void* data_alloc = std_virtual_stack_alloc ( allocator->stack, size );
    std_mem_copy ( data_alloc, data, size );
    std_virtual_stack_align ( allocator->stack, 8 );
}

static inline void se_entity_params_alloc_monostream_component ( se_entity_params_allocator_t* allocator, uint32_t component_id, void* data ) {
    se_entity_params_alloc_component ( allocator, component_id );
    se_entity_params_alloc_stream ( allocator, 0, data );
}

static inline void se_entity_params_alloc_monostream_component_inline ( se_entity_params_allocator_t* allocator, uint32_t component_id, void* data, uint32_t size ) {
    se_entity_params_alloc_component ( allocator, component_id );
    se_entity_params_alloc_stream_inline ( allocator, 0, data, size );
}

#define se_entity_params_alloc_monostream_component_inline_m( allocator, id, data ) se_entity_params_alloc_monostream_component_inline ( allocator, id, data, sizeof ( *data ) )
