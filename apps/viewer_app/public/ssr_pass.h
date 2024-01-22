#pragma once

#include <xf.h>

xf_node_h add_ssr_raymarch_pass ( xf_graph_h graph, xf_texture_h ssr_raymarch, xf_texture_h normals, xf_texture_h color, xf_texture_h hiz );
