#include <xf.h>

#include "xf_state.h"

static void xf_api_init ( xf_i* xf ) {
    xf->create_texture = xf_resource_texture_create;
    xf->create_buffer = xf_resource_buffer_create;
    xf->create_graph = xf_graph_create;
    xf->add_node = xf_graph_add_node;
    xf->finalize_graph = xf_graph_finalize;
    xf->build_graph = xf_graph_build;
    xf->execute_graph = xf_graph_execute;
    xf->advance_graph_multi_textures = xf_graph_advance_multi_textures;
    xf->destroy_graph = xf_graph_destroy;
    //xf.declare_swapchain = xf_resource_swapchain_declare;
    //xf.bind_texture = xf_resource_texture_bind;
    xf->destroy_unreferenced_resources = xf_resource_clear_unreferenced;

    xf->enable_node = xf_graph_node_enable;
    xf->disable_node = xf_graph_node_disable;
    xf->node_set_enabled = xf_graph_node_set_enabled;

    xf->create_multi_texture = xf_resource_multi_texture_create;
    //xf.declare_multi_buffer = xf_multi_resource_declare_multi_buffer;
    xf->advance_multi_texture = xf_resource_multi_texture_advance;
    //xf.advance_multi_buffer = xf_multi_resource_advance_multi_buffer;
    //xf.get_multi_texture = xf_multi_resource_get_multi_texture;
    //xf.get_multi_buffer = xf_multi_resource_get_multi_buffer;
    xf->create_multi_texture_from_swapchain = xf_resource_multi_texture_create_from_swapchain;

    xf->destroy_texture = xf_resource_texture_destroy;

    xf->create_texture_from_external = xf_resource_texture_create_from_external;
    xf->get_multi_texture = xf_resource_multi_texture_get_texture;

    xf->get_texture_info = xf_resource_texture_get_info;
    xf->get_graph_info = xf_graph_get_info;
    xf->get_node_info = xf_graph_get_node_info;

    xf->debug_print_graph = xf_graph_debug_print;
}

void* xf_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xf_state_t* state = xf_state_alloc();

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
    xf_resource_unload();
    xf_graph_unload();

    xf_state_free();
}
