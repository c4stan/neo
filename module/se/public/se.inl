#pragma once

#include <std_allocator.h>

typedef struct {
    se_component_stream_t* stream;
    uint32_t page;
    uint32_t page_count;
} se_component_iterator_t;

#define se_component_iterator_m( component, stream_id ) ( se_component_iterator_t ) { \
    .stream = &(component)->streams[stream_id], \
    .page = 0, \
    .page_count = 0, \
}

inline void* se_component_iterator_next ( se_component_iterator_t* iterator ) {
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

inline se_entity_params_allocator_t se_entity_params_allocator ( std_virtual_stack_t* stack ) {
    se_entity_params_allocator_t allocator;
    allocator.stack = stack;
    allocator.entities = std_virtual_stack_alloc_m ( stack, se_entity_params_t );
    allocator.entities->entity_count = 0;
    allocator.entity = NULL;
    allocator.component = NULL;
    return allocator;
}

inline void se_entity_params_alloc_entity ( se_entity_params_allocator_t* allocator ) {
    allocator->entities->entity_count += 1;
    se_entity_update_t* entity = std_virtual_stack_alloc_m ( allocator->stack, se_entity_update_t );
    entity->component_count = 0;
    std_mem_zero_m ( &entity->mask );
    if ( allocator->entity ) {
        allocator->entity->next = entity;
    }
    allocator->entity = entity;
}

inline void se_entity_params_alloc_component ( se_entity_params_allocator_t* allocator, uint32_t id ) {
    allocator->entity->component_count += 1;
    se_component_update_t* component = std_virtual_stack_alloc_m ( allocator->stack, se_component_update_t );
    component->id = id;
    component->stream_count = 0;
    std_bitset_set ( &allocator->entity->mask, id );
    allocator->component = component;
}

inline void se_entity_params_alloc_stream ( se_entity_params_allocator_t* allocator, uint32_t id, void* data ) {
    allocator->component->stream_count += 1;
    se_component_stream_update_t* stream = std_virtual_stack_alloc_m ( allocator->stack, se_component_stream_update_t );
    stream->has_inline_data = 0;
    stream->id = id;
    stream->data = data;
}

inline void se_entity_params_alloc_stream_inline ( se_entity_params_allocator_t* allocator, uint32_t id, void* data, uint32_t size ) {
    allocator->component->stream_count += 1;
    se_component_stream_update_t* stream = ( se_component_stream_update_t* ) std_virtual_stack_alloc_align ( allocator->stack, 8, 8 );
    stream->has_inline_data = 1;
    stream->id = id;
    void* data_alloc = std_virtual_stack_alloc ( allocator->stack, size );
    std_mem_copy ( data_alloc, data, size );
    std_virtual_stack_align ( allocator->stack, 8 );
}

inline void se_entity_params_alloc_monostream_component ( se_entity_params_allocator_t* allocator, uint32_t component_id, void* data ) {
    se_entity_params_alloc_component ( allocator, component_id );
    se_entity_params_alloc_stream ( allocator, 0, data );
}

inline void se_entity_params_alloc_monostream_component_inline ( se_entity_params_allocator_t* allocator, uint32_t component_id, void* data, uint32_t size ) {
    se_entity_params_alloc_component ( allocator, component_id );
    se_entity_params_alloc_stream_inline ( allocator, 0, data, size );
}

#define se_entity_params_alloc_monostream_component_inline_m( allocator, id, data ) se_entity_params_alloc_monostream_component_inline ( allocator, id, data, sizeof ( *data ) )
