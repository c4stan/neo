#pragma once

#include <xf.h>

uint32_t raytrace_light_data_size ( void );
uint32_t raytrace_instance_data_size ( void );
uint32_t raytrace_reservoir_data_size ( void );

xf_node_h add_raytrace_setup_pass ( xf_graph_h graph, xf_buffer_h instances, xf_buffer_h lights );
void bind_raytrace_setup_routine ( xf_graph_h graph );
