#include <se.h>

#include "se_state.h"

static void se_api_init ( se_i* se ) {
    se->create_entity_family = se_entity_family_create;
    //se->init_entities = se_entity_create;
    se->query_entities = se_entity_query;
    //se->create_entity = se_entity_reserve;
    se->create_entity = se_entity_create_init;

    se->get_entity_component = se_entity_get_component;

    se->get_entity_name = se_entity_name;
    se->set_entity_name = se_entity_set_name;
    se->set_component_properties = se_entity_set_component_properties;
    se->get_entity_list = se_entity_list;
    se->get_entity_properties = se_entity_property_get;
    #if 0
    se->create_entity = se_entity_create;
    se->destroy_entity = se_entity_destroy;

    se->add_component = se_entity_component_add;
    se->remove_component = se_entity_component_remove;
    se->get_component = se_entity_component_get;

    se->create_query = se_query_create;
    se->resolve_pending_queries = se_query_resolve;
    se->dispose_query_results = se_query_results_dispose;
    se->get_query_result = se_query_results_get;
    #endif
}

void* se_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    se_state_t* state = se_state_alloc();

    se_entity_load ( &state->entity );
    //se_query_load ( &state->query );

    se_api_init ( &state->api );
    return &state->api;
}

void* se_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    se_state_t* state = ( se_state_t* ) api;

    se_entity_reload ( &state->entity );
    //se_query_reload ( &state->query );

    se_api_init ( &state->api );
    return &state->api;
}

void se_unload ( void ) {
    se_entity_unload();
    //se_query_unload();

    se_state_free();
}

// https://advances.realtimerendering.com/destiny/gdc_2015/Tatarchuk_GDC_2015__Destiny_Renderer_web.pdf
// https://www.youtube.com/watch?v=p65Yt20pw0g
// https://www.youtube.com/watch?v=ZHqFrNyLlpA

/*
ideas
    optimize for static vs dynamic component layout
    optimize all the way starting from scene layout (e.g. allocate static layout entities together so that potentially when queried for a component only a base and a count needs to be returned)
*/

#if 0
extern inline void* se_stream_iterator_next ( se_stream_iterator_t* iterator );
extern inline se_entity_params_allocator_t se_entity_params_allocator ( std_virtual_stack_t* stack );
extern inline void se_entity_params_alloc_entity ( se_entity_params_allocator_t* allocator, se_entity_h entity_handle );
extern inline void se_entity_params_alloc_component ( se_entity_params_allocator_t* allocator, uint32_t id );
extern inline void se_entity_params_alloc_stream ( se_entity_params_allocator_t* allocator, uint32_t id, void* data );
extern inline void se_entity_params_alloc_stream_inline ( se_entity_params_allocator_t* allocator, uint32_t id, void* data, uint32_t size );
extern inline void se_entity_params_alloc_monostream_component ( se_entity_params_allocator_t* allocator, uint32_t component_id, void* data );
extern inline void se_entity_params_alloc_monostream_component_inline ( se_entity_params_allocator_t* allocator, uint32_t component_id, void* data, uint32_t size );
#endif
