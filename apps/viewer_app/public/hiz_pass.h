#pragma once

#include <xf.h>
#include <xs.h>

xf_node_h add_hiz_mip0_gen_pass ( xf_graph_h graph, xf_texture_h hiz, xf_texture_h depth );
xf_node_h add_hiz_submip_gen_pass ( xf_graph_h graph, xf_texture_h hiz, uint32_t mip );
