#pragma once

#include "geometry_pass.h"

xf_node_h add_sky_convolution_node ( xf_graph_h graph );
xf_node_h add_sky_node ( xf_graph_h graph, const gbuffer_textures_t* gbuffer, xf_texture_h depth );
