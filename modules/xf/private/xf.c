#include <xf.h>

#include "xf_state.h"

static void xf_api_init ( xf_i* xf ) {
    xf->declare_texture = xf_resource_texture_declare;
    xf->declare_buffer = xf_resource_buffer_declare;
    xf->create_graph = xf_graph_create;
    xf->create_node = xf_graph_add_node;
    //xf.build_graph = xf_graph_build;
    xf->execute_graph = xf_graph_execute;
    //xf.declare_swapchain = xf_resource_swapchain_declare;
    //xf.bind_texture = xf_resource_texture_bind;

    xf->enable_node = xf_graph_node_enable;
    xf->disable_node = xf_graph_node_disable;

    xf->declare_multi_texture = xf_resource_multi_texture_declare;
    //xf.declare_multi_buffer = xf_multi_resource_declare_multi_buffer;
    xf->advance_multi_texture = xf_resource_multi_texture_advance;
    //xf.advance_multi_buffer = xf_multi_resource_advance_multi_buffer;
    //xf.get_multi_texture = xf_multi_resource_get_multi_texture;
    //xf.get_multi_buffer = xf_multi_resource_get_multi_buffer;
    xf->multi_texture_from_swapchain = xf_resource_multi_texture_declare_from_swapchain;

    xf->texture_from_external = xf_resource_texture_declare_from_external;
    xf->get_multi_texture = xf_resource_multi_texture_get;

    xf->refresh_external_texture = xf_resource_texture_refresh_external;

    xf->get_texture_info = xf_resource_texture_get_info;
    xf->get_graph_info = xf_graph_get_info;
    xf->get_node_info = xf_graph_get_node_info;

    xf->debug_print_graph = xf_graph_debug_print;
    xf->debug_ui_graph = xf_graph_debug_ui;
}

void* xf_load ( void* std_runtime ) {
    std_attach ( std_runtime );

    xf_state_t* state = xf_state_alloc();

    xf_resource_load ( &state->resource );
    xf_graph_load ( &state->graph );

    xf_api_init ( &state->api );

    return &state->api;
}

void xf_reload ( void* std_runtime, void* api ) {
    std_attach ( std_runtime );

    xf_state_t* state = ( xf_state_t* ) api;

    xf_resource_reload ( &state->resource );
    xf_graph_reload ( &state->graph );

    xf_api_init ( &state->api );
}

void xf_unload ( void ) {
    xf_resource_unload();
    xf_graph_unload();

    xf_state_free();
}
