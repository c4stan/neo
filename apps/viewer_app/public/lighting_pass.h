#pragma once

#include <xf.h>

xf_node_h add_lighting_pass ( xf_graph_h graph, xf_texture_h target, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth, xf_texture_h shadows );
