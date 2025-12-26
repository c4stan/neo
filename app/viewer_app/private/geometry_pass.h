#pragma once

#include <std_platform.h>

#include <xf.h>

typedef struct {
    xf_texture_h color;
    xf_texture_h normal;
    xf_texture_h material;
    xf_texture_h radiosity;
    xf_texture_h object_id;
    xf_texture_h velocity;
} gbuffer_textures_t;

xf_node_h add_geometry_node ( xf_graph_h graph, const gbuffer_textures_t* gbuffer, xf_texture_h depth );
void bind_geometry_routine ( xf_graph_h graph );

xf_node_h add_object_id_node ( xf_graph_h graph, xf_texture_h object_id, xf_texture_h depth );
void bind_object_id_routine ( xf_graph_h graph );
