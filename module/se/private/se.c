#include <se.h>

#include "se_state.h"

static void se_api_init ( se_i* se ) {
    se->create_entity_family = se_entity_family_create;
    se->create_entity = se_entity_create_init;
    se->destroy_entity = se_entity_destroy;
    se->query_entities = se_entity_query;

    se->get_entity_name = se_entity_name;
    se->set_entity_name = se_entity_set_name;
    se->get_entity_component = se_entity_get_component;
    se->set_component_properties = se_entity_set_component_properties;
    se->get_entity_list = se_entity_list;
    se->get_entity_properties = se_entity_property_get;
}

void* se_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    se_state_t* state = se_state_alloc();

    se_entity_load ( &state->entity );

    se_api_init ( &state->api );
    return &state->api;
}

void* se_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    se_state_t* state = ( se_state_t* ) api;

    se_entity_reload ( &state->entity );

    se_api_init ( &state->api );
    return &state->api;
}

void se_unload ( void ) {
    se_entity_unload();
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
