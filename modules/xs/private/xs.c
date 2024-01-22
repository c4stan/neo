#include <xs.h>

#include "xs_shader_compiler.h"
#include "xs_state.h"

static void xs_api_init ( xs_i* xs ) {
    xs->add_database_folder = xs_database_add_folder;
    xs->set_output_folder = xs_database_set_output_folder;
    xs->clear_database = xs_database_clear;
    xs->build_database_shaders = xs_database_build_shaders;
    xs->lookup_pipeline_state = xs_database_pipeline_lookup;
    xs->lookup_pipeline_state_hash = xs_database_pipeline_lookup_hash;
    xs->get_pipeline_state = xs_database_pipeline_get;
    xs->update_pipeline_states = xs_database_update_pipelines;
}

void* xs_load ( void* std_runtime ) {
    std_attach ( std_runtime );

    xs_state_t* state = xs_state_alloc();

    xs_database_load ( &state->database );

    xs_api_init ( &state->api );
    return &state->api;
}

void xs_reload ( void* std_runtime, void* api ) {
    std_attach ( std_runtime );

    xs_state_t* state = ( xs_state_t* ) api;

    xs_database_reload ( &state->database );

    xs_api_init ( &state->api );
}

void xs_unload ( void ) {
    xs_database_unload();
    xs_state_free();
}
