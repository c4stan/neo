#include <rv.h>

#include "rv_view.h"

#include "rv_state.h"

static void rv_api_init ( rv_i* rv ) {
    rv->create_view = rv_view_create;
    rv->destroy_view = rv_view_destroy;
    rv->get_view_info = rv_view_get_info;

    rv->update_view_transform = rv_view_update_transform;
    rv->update_prev_frame_data = rv_view_update_prev_frame_data;
    rv->update_proj_jitter = rv_view_update_jitter;
}

void* rv_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    rv_state_t* state = rv_state_alloc();

    rv_view_load ( &state->view );

    rv_api_init ( &state->api );

    return &state->api;
}

void rv_unload ( void ) {
    rv_view_unload();
    rv_state_free();
}

void rv_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    std_auto_m state = ( rv_state_t* ) api;

    rv_view_reload ( &state->view );

    rv_api_init ( &state->api );
}

// https://advances.realtimerendering.com/destiny/gdc_2015/Tatarchuk_GDC_2015__Destiny_Renderer_web.pdf

/*
    features needed:
        foreach view
            foreach visibility comp
                test view layer with visibility layer
                <if there is a model component> test view frustum with model bounds
                if all tests pass append the entity (together with a cache of the already extracted component data?) to the array of visibles for current view

        return array of visibles for the requested view
        return view data (component?) for the requested view
*/
