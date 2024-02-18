#pragma once

#include <xf.h>

xf_node_h add_particle_rainfall_spawn_pass ( xf_graph_h graph, xf_buffer_h particles );
//xf_node_h add_particle_rainfall_sim_pass ( xf_graph_h graph, xf_buffer_h particles, xf_texture_h depth );
xf_node_h add_particle_rainfall_render_pass ( xf_graph_h graph, xf_texture_h color, xf_buffer_h particles );
