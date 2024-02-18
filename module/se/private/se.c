#include <se.h>

#include "se_entity.h"
#include "se_query.h"

static se_i s_api;

static se_i se_api ( void ) {
    se_i se = {0};

    se.create_entity = se_entity_create;
    se.destroy_entity = se_entity_destroy;

    se.add_component = se_entity_component_add;
    se.remove_component = se_entity_component_remove;
    se.get_component = se_entity_component_get;

    se.create_query = se_query_create;
    se.resolve_pending_queries = se_query_resolve;
    se.dispose_query_results = se_query_results_dispose;
    se.get_query_result = se_query_results_get;

    return se;
}

void* se_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    se_entity_init();
    se_query_init();

    s_api = se_api();

    return &s_api;
}

void se_unload ( void ) {
    std_noop_m;
}

// https://advances.realtimerendering.com/destiny/gdc_2015/Tatarchuk_GDC_2015__Destiny_Renderer_web.pdf
// https://www.youtube.com/watch?v=p65Yt20pw0g
// https://www.youtube.com/watch?v=ZHqFrNyLlpA

/*
ideas
    optimize for static vs dynamic component layout
    optimize all the way starting from scene layout (e.g. allocate static layout entities together so that potentially when queried for a component only a base and a count needs to be returned)
*/
