#pragma once

#include <xf.h>

xf_node_h add_ssr_raymarch_pass ( xf_graph_h graph, xf_texture_h ssr_raymarch, xf_texture_h ssr_intersect_dist, xf_texture_h normal, xf_texture_h material, xf_texture_h lighting, xf_texture_h hiz );
