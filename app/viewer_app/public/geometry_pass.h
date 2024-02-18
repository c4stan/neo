#pragma once

#include <std_platform.h>

#include <xf.h>

xf_node_h add_geometry_clear_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normals, xf_texture_h materials, xf_texture_h depth );
xf_node_h add_geometry_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normals, xf_texture_h materials, xf_texture_h depth );
