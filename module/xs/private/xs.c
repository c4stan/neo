#include <xs.h>

#include "xs_shader_compiler.h"
#include "xs_state.h"

static void xs_api_init ( xs_i* xs ) {
    xs->create_database = xs_database_create;
    xs->destroy_database = xs_database_destroy;
    xs->add_database_folder = xs_database_add_folder;
    xs->set_output_folder = xs_database_set_output_folder;
    xs->set_build_params = xs_database_set_build_params;
    xs->clear_database = xs_database_clear;
    xs->build_database = xs_database_build;
    xs->rebuild_databases = xs_database_rebuild_all;
    xs->get_database_pipeline = xs_database_pipeline_get;
    xs->get_pipeline_state = xs_database_pipeline_state_get;
    xs->update_pipeline_states = xs_database_update_pipelines;
}

void* xs_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xs_state_t* state = xs_state_alloc();

    xs_database_load ( &state->database );

    xs_api_init ( &state->api );
    return &state->api;
}

void xs_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );
    xs_state_t* state = ( xs_state_t* ) api;
    xs_database_reload ( &state->database );
    xs_api_init ( &state->api );
    xs_state_bind ( state );
}

void xs_unload ( void ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg->wait_all_workload_complete();

    xs_database_unload();
    xs_state_free();
}
