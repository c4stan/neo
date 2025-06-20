#pragma once

#include <std_platform.h>

#include <xf.h>

xf_node_h add_geometry_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h radiosity, xf_texture_h object_id, xf_texture_h velocity, xf_texture_h depth );

xf_node_h add_object_id_node ( xf_graph_h graph, xf_texture_h object_id, xf_texture_h depth );
