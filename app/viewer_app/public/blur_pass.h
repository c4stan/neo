#pragma once

#include <xf.h>

typedef enum {
    blur_pass_direction_horizontal_m,
    blur_pass_direction_vertical_m,
} blur_pass_direction_e;

xf_node_h add_bilateral_blur_pass ( xf_graph_h graph, xf_texture_h result, xf_texture_h color, xf_texture_h normals, xf_texture_h depth, uint32_t kernel_size, float kernel_sigma, blur_pass_direction_e direction, const char* debug_name );
