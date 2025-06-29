#include <xf.h>

#include "xf_state.h"

static void xf_load_shaders ( xg_device_h device ) {
    xs_i* xs = std_module_get_m ( xs_module_name_m );

    char path[std_process_path_max_len_m];
    std_stack_t stack = std_static_stack_m ( path );
    std_stack_string_append ( &stack, std_module_path_m );
    std_stack_string_append ( &stack, "shader/");
    
    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "xf_sdb" ) );
    xs->add_database_folder ( sdb, path );
    xs->set_output_folder ( sdb, "output/shader/" );
    xf_graph_load_shaders ( xs, sdb );
    xs->build_database ( sdb );
    xf_state_set_sdb ( sdb );
}

static void xf_api_init ( xf_i* xf ) {
    xf->load_shaders = xf_load_shaders;

    xf->create_texture = xf_resource_texture_create;
    xf->create_buffer = xf_resource_buffer_create;
    xf->create_graph = xf_graph_create;
    xf->create_node = xf_graph_node_create;
    xf->finalize_graph = xf_graph_finalize;
    //xf->build_graph = xf_graph_build;
    xf->execute_graph = xf_graph_execute;
    xf->advance_graph_multi_textures = xf_graph_advance_multi_textures;
    xf->destroy_graph = xf_graph_destroy;
    //xf.declare_swapchain = xf_resource_swapchain_declare;
    //xf.bind_texture = xf_resource_texture_bind;
    xf->destroy_unreferenced_resources = xf_resource_destroy_unreferenced;

    xf->enable_node = xf_graph_node_enable;
    xf->disable_node = xf_graph_node_disable;
    xf->node_set_enabled = xf_graph_node_set_enabled;

    xf->create_multi_texture = xf_resource_multi_texture_create;
    xf->create_multi_buffer = xf_resource_multi_buffer_create;
    xf->advance_multi_texture = xf_resource_multi_texture_advance;
    xf->advance_multi_buffer = xf_resource_multi_buffer_advance;
    xf->create_multi_texture_from_swapchain = xf_resource_multi_texture_create_from_swapchain;

    xf->destroy_texture = xf_resource_texture_destroy;

    xf->create_texture_from_external = xf_resource_texture_create_from_external;
    xf->get_multi_texture = xf_resource_multi_texture_get_texture;
    xf->get_multi_buffer = xf_resource_multi_buffer_get_buffer;

    xf->get_texture_info = xf_resource_texture_get_info;
    xf->get_graph_info = xf_graph_get_info;
    xf->get_node_info = xf_graph_get_node_info;

    xf->get_graph_timings = xf_graph_get_timings;

    xf->debug_print_graph = xf_graph_debug_print;

    xf->list_textures = xf_resource_texture_list;
    xf->set_graph_texture_export = xf_graph_set_texture_export;
}

void* xf_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xf_state_t* state = xf_state_alloc();
    xf_state_set_sdb ( xf_null_handle_m );

    xf_resource_load ( &state->resource );
    xf_graph_load ( &state->graph );

    xf_api_init ( &state->api );

    return &state->api;
}

void xf_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    xf_state_t* state = ( xf_state_t* ) api;

    xf_resource_reload ( &state->resource );
    xf_graph_reload ( &state->graph );

    xf_api_init ( &state->api );
    xf_state_bind ( state );
}

void xf_unload ( void ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg->wait_all_workload_complete();
    
    xf_graph_unload();
    xf_resource_unload();

    xs_database_h sdb = xf_state_get_sdb();
    if ( sdb != xf_null_handle_m ) {
        xs_i* xs = std_module_get_m ( xs_module_name_m );
        xs->destroy_database ( sdb );
        xf_state_set_sdb ( xf_null_handle_m );
    }

    xf_state_free();
}
