#pragma once

#include <std_platform.h>

#include <xf.h>

#include "geometry_pass.h"

xf_node_h add_tessellation_setup_pass ( xf_graph_h graph, xf_buffer_h vertex_buffer, xf_buffer_h index_buffer, xf_buffer_h indirect_dispatch_buffer, xf_buffer_h prev_subdivision_buffer, xf_buffer_h culled_subdivision_buffer );
xf_node_h add_tessellation_draw_pass ( xf_graph_h graph, xf_buffer_h vertex_buffer, xf_buffer_h index_buffer, xf_buffer_h subdivision_buffer, xf_buffer_h indirect_buffer, xg_buffer_h instance_vertex_buffer, const gbuffer_textures_t* gbuffer, xf_texture_h depth, xs_database_h sdb );
