#pragma once

#include <xf.h>

xf_node_h add_ssgi_raymarch_pass ( xf_graph_h graph, const char* name, xf_texture_h ssgi_raymarch, xf_texture_h normals, xf_texture_h color, xf_texture_h direct_lighting, xf_texture_h hiz );
