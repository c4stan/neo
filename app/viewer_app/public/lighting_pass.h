#pragma once

#include <xf.h>

// Keep in sync with lighting.frag!
#define viewapp_max_lights_m 32

uint32_t uniform_light_size ( void );

xf_node_h add_lighting_pass ( xf_graph_h graph, xf_texture_h target, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth, xf_texture_h shadows );

xf_node_h add_light_cull_pass ( xf_graph_h graph, xf_buffer_h light_buffer, xf_buffer_h light_list_buffer, xf_buffer_h light_grid_buffer );

xf_node_h add_light_update_pass ( xf_graph_h graph, xf_buffer_h upload_buffer, xf_buffer_h light_buffer );
void get_light_uniform_scale_bias ( float* scale, float* bias );
