#pragma once

#include <xf.h>

uint32_t raytrace_light_data_size ( void );
uint32_t raytrace_instance_data_size ( void );

xf_node_h add_raytrace_setup_pass ( xf_graph_h graph, xf_buffer_h instances, xf_buffer_h lights );

xf_node_h add_raytrace_pass ( xf_graph_h graph, xf_texture_h color_texture, xf_texture_h normal_texture, xf_texture_h material_texture, xf_texture_h radiosity_texture, xf_texture_h depth_texture, xf_texture_h target, xf_buffer_h instances, xf_buffer_h lights, xg_texture_h reservoir );
