#pragma once

#include <xf.h>
#include <xs.h>

// TODO move this into xf?

#if 0
typedef struct {
    xs_database_pipeline_h pipeline;
    uint32_t texture_writes_count;
    xf_texture_h texture_writes[xf_node_max_shader_texture_writes_m];
    uint32_t texture_reads_count;
    xf_texture_h texture_reads[xf_node_max_shader_texture_reads_m];
    uint32_t buffer_writes_count;
    xf_buffer_h buffer_writes[xf_node_max_shader_buffer_writes_m];
    uint32_t buffer_reads_count;
    xf_buffer_h buffer_reads[xf_node_max_shader_buffer_reads_m];
    uint32_t samplers_count;
    xg_sampler_h samplers[xg_pipeline_resource_max_samplers_per_set_m];
    std_buffer_t uniform_buffer;
    uint32_t workgroup_count[3];
    xf_node_passthrough_params_t passthrough;
} simple_compute_pass_params_t;

#define simple_compute_pass_params_m( ... ) ( simple_compute_pass_params_t ) { \
    .pipeline = xs_null_handle_m, \
    .texture_writes_count = 0, \
    .texture_reads_count = 0, \
    .buffer_writes_count = 0, \
    .buffer_reads_count = 0, \
    .samplers_count = 0, \
    .uniform_buffer = std_buffer_m(), \
    .workgroup_count = { 0, 0, 0 }, \
    .passthrough = xf_node_passthrough_params_m(), \
    ##__VA_ARGS__ \
}
#endif

typedef struct {
    xs_database_pipeline_h pipeline;
    uint32_t render_targets_count;
    xf_texture_h render_targets[xf_node_max_render_targets_m];
    uint32_t texture_reads_count;
    xf_texture_h texture_reads[xf_node_max_shader_texture_reads_m];
    uint32_t samplers_count;
    xg_sampler_h samplers[xg_pipeline_resource_max_samplers_per_set_m];
    xf_node_passthrough_params_t passthrough;
} simple_screen_pass_params_t;

#define simple_screen_pass_params_m( ... ) ( simple_screen_pass_params_t ) { \
    .pipeline = xs_null_handle_m, \
    .render_targets_count = 0, \
    .texture_reads_count = 0, \
    .samplers_count = 0, \
    .passthrough = xf_node_passthrough_params_m(), \
    ##__VA_ARGS__ \
}

xf_node_h add_simple_screen_pass ( xf_graph_h graph, const char* name, const simple_screen_pass_params_t* params );


typedef struct {
    uint32_t color_textures_count;
    xf_texture_h color_textures[xf_node_max_copy_texture_writes_m];
    xg_color_clear_t color_clears[xf_node_max_copy_texture_writes_m];
    uint32_t depth_stencil_textures_count;
    xf_texture_h depth_stencil_textures[xf_node_max_copy_texture_writes_m];
    xg_depth_stencil_clear_t depth_stencil_clears[xf_node_max_copy_texture_writes_m];
} simple_clear_pass_params_t;

#define simple_clear_pass_params_m( ... ) ( simple_clear_pass_params_t ) { \
    .color_textures_count = 0, \
    .color_textures = { [0 ... xf_node_max_copy_texture_writes_m - 1] = xf_null_handle_m }, \
    .color_clears = { [0 ... xf_node_max_copy_texture_writes_m - 1] = xg_color_clear_m() }, \
    .depth_stencil_textures_count = 0, \
    .depth_stencil_textures = { [0 ... xf_node_max_copy_texture_writes_m - 1] = xf_null_handle_m }, \
    .depth_stencil_clears = { [0 ... xf_node_max_copy_texture_writes_m - 1] =  xg_depth_stencil_clear_m() }, \
    ##__VA_ARGS__ \
}

xf_node_h add_simple_clear_pass ( xf_graph_h graph, const char* name, const simple_clear_pass_params_t* params );


typedef struct {
    xg_sampler_filter_e filter;
    bool presentable;
    xf_texture_h source;
    xf_texture_h dest;
    xg_texture_view_t source_view;
    xg_texture_view_t dest_view;
} simple_copy_pass_params_t;

#define simple_copy_pass_params_m( ... ) ( simple_copy_pass_params_t ) { \
    .filter = xg_sampler_filter_point_m, \
    .presentable = false, \
    .source = xf_null_handle_m, \
    .dest = xf_null_handle_m, \
    .source_view = xg_texture_view_m(), \
    .dest_view = xg_texture_view_m(), \
    ##__VA_ARGS__ \
}

xf_node_h add_simple_copy_pass ( xf_graph_h graph, const char* name, const simple_copy_pass_params_t* params );
