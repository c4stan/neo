#pragma once

#include <xf.h>

xf_node_h add_temporal_accumulation_pass ( xf_graph_h graph, xf_texture_h output, xf_texture_h color, xf_texture_h history, xf_texture_h depth, xf_texture_h prev_depth, const char* debug_name );
