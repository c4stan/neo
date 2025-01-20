#pragma once

#include <std_module.h>
#include <std_platform.h>
#include <std_hash.h>

#include <xg.h>

#define xs_module_name_m xs
std_module_export_m void* xs_load ( void* );
std_module_export_m void xs_reload ( void*, void* );
std_module_export_m void xs_unload ( void );

#define xs_hash_string_m( str, len ) std_hash_metro( (str), (len) )
#define xs_hash_static_string_m( str ) xs_hash_string_m ( str, sizeof ( str ) - 1 ) // -1 to match str_len
typedef uint64_t xs_string_hash_t;
//typedef uint64_t xs_pipeline_hash_t;

#define xs_pipeline_hash_m( name, permutation ) ( 3 * std_str_hash_64 ( name ) + std_hash_64_m(permutation) )

#define xs_null_handle_m UINT64_MAX

#if 0
#define xs_int_undefined_m INT32_MAX
#define xs_u32_undefined_m UINT32_MAX
#define xs_size_undefined_m SIZE_MAX
#define xs_float_undefined_m FLT_MAX
#define xs_enum_undefined_m xs_int_undefined_m
#define xs_bool_undefined_m (bool)INT8_MAX

#define xs_graphics_state_enum_members_list_m \
    xs_graphics_state_member_list_item_m(rasterizer_state.polygon_mode) \
    xs_graphics_state_member_list_item_m(rasterizer_state.cull_mode) \
    xs_graphics_state_member_list_item_m(rasterizer_state.frontface_winding_mode) \
    xs_graphics_state_member_list_item_m(rasterizer_state.antialiasing_state.mode) \
    xs_graphics_state_member_list_item_m(rasterizer_state.antialiasing_state.sample_count) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.depth.compare_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.stencil_fail_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.stencil_depth_pass_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.stencil_pass_depth_fail_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.compare_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.stencil_fail_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.stencil_depth_pass_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.stencil_pass_depth_fail_op) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.compare_op) \
    xs_graphics_state_member_list_item_m(blend_state.blend_logic_op)

#define xs_graphics_state_bool_members_list_m \
    xs_graphics_state_member_list_item_m(rasterizer_state.depth_bias_state.enable) \
    xs_graphics_state_member_list_item_m(rasterizer_state.enable_depth_clamp) \
    xs_graphics_state_member_list_item_m(rasterizer_state.enable_rasterization) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.depth.enable_bound_test) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.depth.enable_test) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.depth.enable_write) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.enable_test) \
    xs_graphics_state_member_list_item_m(blend_state.enable_blend_logic_op)

#define xs_graphics_state_float_members_list_m \
    xs_graphics_state_member_list_item_m(rasterizer_state.depth_bias_state.const_factor) \
    xs_graphics_state_member_list_item_m(rasterizer_state.depth_bias_state.slope_factor) \
    xs_graphics_state_member_list_item_m(rasterizer_state.depth_bias_state.clamp) \
    xs_graphics_state_member_list_item_m(rasterizer_state.line_width) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.depth.min_bound) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.depth.max_bound) \
    xs_graphics_state_member_list_item_m(viewport_state.min_depth) \
    xs_graphics_state_member_list_item_m(viewport_state.max_depth)

#define xs_graphics_state_u32_members_list_m \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.compare_mask) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.write_mask) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.front_face_op.reference) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.compare_mask) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.write_mask) \
    xs_graphics_state_member_list_item_m(depth_stencil_state.stencil.back_face_op.reference) \
    xs_graphics_state_member_list_item_m(viewport_state.x) \
    xs_graphics_state_member_list_item_m(viewport_state.y) \
    xs_graphics_state_member_list_item_m(viewport_state.width) \
    xs_graphics_state_member_list_item_m(viewport_state.height)

#define xs_graphics_state_size_members_list_m \
    xs_graphics_state_member_list_item_m(input_layout.count) \
    xs_graphics_state_member_list_item_m(blend_state.render_targets_count)


#define xs_null_graphics_pipeline_state_m(state)                                                          \
    (state)->ps_bytecode = std_null_buffer_m;                                                             \
    (state)->vs_bytecode = std_null_buffer_m;                                                             \
    (state)->input_layout.count = xs_size_undefined_m;                                                    \
    (state)->rasterizer_state.polygon_mode = xs_enum_undefined_m;                                         \
    (state)->rasterizer_state.cull_mode = xs_enum_undefined_m;                                            \
    (state)->rasterizer_state.frontface_winding_mode = xs_enum_undefined_m;                               \
    (state)->rasterizer_state.antialiasing_state.mode = xs_enum_undefined_m;                              \
    (state)->rasterizer_state.antialiasing_state.sample_count = xs_enum_undefined_m;                      \
    (state)->rasterizer_state.depth_bias_state.enable = xs_bool_undefined_m;                              \
    (state)->rasterizer_state.depth_bias_state.const_factor = xs_float_undefined_m;                       \
    (state)->rasterizer_state.depth_bias_state.slope_factor = xs_float_undefined_m;                       \
    (state)->rasterizer_state.depth_bias_state.clamp = xs_float_undefined_m;                              \
    (state)->rasterizer_state.line_width = xs_float_undefined_m;                                          \
    (state)->rasterizer_state.enable_depth_clamp = xs_bool_undefined_m;                                   \
    (state)->rasterizer_state.enable_rasterization = xs_bool_undefined_m;                                 \
    (state)->depth_stencil_state.depth.min_bound = xs_float_undefined_m;                                  \
    (state)->depth_stencil_state.depth.max_bound = xs_float_undefined_m;                                  \
    (state)->depth_stencil_state.depth.enable_bound_test = xs_bool_undefined_m;                           \
    (state)->depth_stencil_state.depth.compare_op = xs_enum_undefined_m;                                  \
    (state)->depth_stencil_state.depth.enable_test = xs_bool_undefined_m;                                 \
    (state)->depth_stencil_state.depth.enable_write = xs_bool_undefined_m;                                \
    (state)->depth_stencil_state.stencil.enable_test = xs_bool_undefined_m;                               \
    (state)->depth_stencil_state.stencil.front_face_op.stencil_fail_op = xs_enum_undefined_m;             \
    (state)->depth_stencil_state.stencil.front_face_op.stencil_depth_pass_op = xs_enum_undefined_m;       \
    (state)->depth_stencil_state.stencil.front_face_op.stencil_pass_depth_fail_op = xs_enum_undefined_m;  \
    (state)->depth_stencil_state.stencil.front_face_op.compare_op = xs_enum_undefined_m;                  \
    (state)->depth_stencil_state.stencil.front_face_op.compare_mask = xs_u32_undefined_m;                 \
    (state)->depth_stencil_state.stencil.front_face_op.write_mask = xs_u32_undefined_m;                   \
    (state)->depth_stencil_state.stencil.front_face_op.reference = xs_u32_undefined_m;                    \
    (state)->depth_stencil_state.stencil.back_face_op.stencil_fail_op = xs_enum_undefined_m;              \
    (state)->depth_stencil_state.stencil.back_face_op.stencil_depth_pass_op = xs_enum_undefined_m;        \
    (state)->depth_stencil_state.stencil.back_face_op.stencil_pass_depth_fail_op = xs_enum_undefined_m;   \
    (state)->depth_stencil_state.stencil.back_face_op.compare_op = xs_enum_undefined_m;                   \
    (state)->depth_stencil_state.stencil.back_face_op.compare_mask = xs_u32_undefined_m;                  \
    (state)->depth_stencil_state.stencil.back_face_op.write_mask = xs_u32_undefined_m;                    \
    (state)->depth_stencil_state.stencil.back_face_op.reference = xs_u32_undefined_m;                     \
    (state)->blend_state.render_targets_count = xs_size_undefined_m;                                      \
    (state)->blend_state.blend_logic_op = xs_enum_undefined_m;                                            \
    (state)->blend_state.enable_blend_logic_op = xs_bool_undefined_m;                                     \
    (state)->viewport_state.x = xs_u32_undefined_m;                                                       \
    (state)->viewport_state.y = xs_u32_undefined_m;                                                       \
    (state)->viewport_state.width = xs_u32_undefined_m;                                                   \
    (state)->viewport_state.height = xs_u32_undefined_m;                                                  \
    (state)->viewport_state.min_depth = xs_float_undefined_m;                                             \
    (state)->viewport_state.max_depth = xs_float_undefined_m;

#define xs_null_graphics_pipeline_render_textures_m(textures)             \
    (textures)->render_targets_count = xs_size_undefined_m;               \
    (textures)->depth_stencil.format = xs_enum_undefined_m;               \
    (textures)->depth_stencil.samples_per_pixel = xs_enum_undefined_m;

#define xs_null_graphics_pipeline_resource_bindings_m(bindings)   \
    (bindings)->binding_points_count = xs_size_undefined_m;

#define xs_null_graphics_pipeline_params_m(params)                                    \
    xs_null_graphics_pipeline_state_m( &(params)->state );                            \
    xs_null_graphics_pipeline_render_textures_m( &(params)->render_textures );        \
    xs_null_graphics_pipeline_resource_bindings_m( &(params)->resource_bindings );
#endif

typedef uint64_t xs_database_h;
//typedef uint64_t xs_database_pipeline_h;
typedef uint64_t xs_database_pipeline_h;

typedef struct {
    int32_t value;
    char name[xs_shader_definition_name_max_len_m];
} xs_shader_definition_t;

// TODO use dynamic pipeline state for viewport size
// https://stackoverflow.com/questions/57950008/must-a-vulkan-pipeline-be-recreate-when-changing-the-size-of-the-window
typedef struct {
    //size_t viewport_width;
    //size_t viewport_height;
    //xs_shader_permutation_t permutation;
    xg_graphics_pipeline_state_t* base_graphics_state;
    xg_compute_pipeline_state_t* base_compute_state;
    xg_raytrace_pipeline_state_t* base_raytrace_state;
    xs_shader_definition_t global_definitions[xs_database_build_max_global_definitions_m];
    uint32_t global_definition_count;
} xs_database_build_params_t;

#define xs_database_build_params_m( ... ) ( xs_database_build_params_t ) { \
    .base_graphics_state = NULL, \
    .base_compute_state = NULL, \
    .base_raytrace_state = NULL, \
    .global_definitions = { 0 }, \
    .global_definition_count = 0, \
    ##__VA_ARGS__ \
}

typedef struct {
    uint32_t successful_shaders;
    uint32_t failed_shaders;
    uint32_t skipped_shaders;
    uint32_t successful_pipeline_states;
    uint32_t failed_pipeline_states;
    uint32_t skipped_pipeline_states;
} xs_database_build_result_t;

#define xs_database_build_result_m( ... ) ( xs_database_build_result_t ) { \
    .successful_shaders = 0, \
    .failed_shaders = 0, \
    .skipped_shaders = 0, \
    .successful_pipeline_states = 0, \
    .failed_pipeline_states = 0, \
    .skipped_pipeline_states = 0, \
}

typedef struct {
    xg_device_h device;
    char debug_name[32];
} xs_database_params_t;

#define xs_database_params_m(...) ( xs_database_params_t ) { \
    .device = xg_null_handle_m, \
    .debug_name = "", \
    ##__VA_ARGS__ \
}

typedef enum {
    // Viewport
    xs_state_viewport_x_m,
    xs_state_viewport_y_m,
    xs_state_viewport_width_m,
    xs_state_viewport_height_m,
    xs_state_viewport_min_depth_m,
    xs_state_viewport_max_depth_m,
    // Scissor
    xs_state_scissor_x_m,
    xs_state_scissor_y_m,
    xs_state_scissor_width_m,
    xs_state_scissor_height_m,
    // Blend
    xs_state_blend_render_target_count_m,
    xs_state_blend_logic_enable_m,
    xs_state_blend_logic_op_m,
    xs_state_blend_render_target_id_m,
    xs_state_blend_render_target_enabled_m,
    xs_state_blend_render_target_color_src_m,
    xs_state_blend_render_target_color_dst_m,
    xs_state_blend_render_target_color_op_m,
    xs_state_blend_render_target_alpha_src_m,
    xs_state_blend_render_target_alpha_dst_m,
    xs_state_blend_render_target_alpha_op_m,
    // Depth
    xs_state_depth_bound_min_m,
    xs_state_depth_bound_max_m,
    xs_state_depth_bound_enabled_m,
    xs_state_depth_test_op_m,
    xs_state_depth_test_enabled_m,
    xs_state_depth_write_enabled_m,
    // TODO Stencil
    xs_state_stencil_enabled_m,
    // Shader
    xs_state_shader_m,
    // Input layout
    xs_state_input_stream_count_m,
    xs_state_input_stream_m, // xg_vertex_stream_t
    // Rasterizer
    xs_state_rasterizer_polygon_mode_m,
    xs_state_rasterizer_cull_mode_m,
    xs_state_rasterizer_winding_mode_m,
    xs_state_rasterizer_antialiasing_mode_m,
    xs_state_rasterizer_antialiasing_sample_count_m,
    xs_state_rasterizer_depth_bias_enable_m,
    xs_state_rasterizer_depth_bias_const_factor_m,
    xs_state_rasterizer_depth_bias_slope_factor_m,
    xs_state_rasterizer_depth_clamp_enable_m,
    xs_state_rasterizer_depth_clamp_m,
    xs_state_rasterizer_line_width_m,
    xs_state_rasterizer_disabled_m,
} xs_state_e;

typedef enum {
    // Viewport
    xs_state_bit_viewport_x_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_viewport_y_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_viewport_width_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_viewport_height_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_viewport_min_depth_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_viewport_max_depth_m = 1 << xs_state_viewport_x_m,
    // Scissor
    xs_state_bit_scissor_x_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_scissor_y_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_scissor_width_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_scissor_height_m = 1 << xs_state_viewport_x_m,
    // Blend
    xs_state_bit_blend_render_target_count_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_logic_enable_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_logic_op_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_id_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_enabled_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_color_src_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_color_dst_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_color_op_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_alpha_src_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_alpha_dst_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_blend_render_target_alpha_op_m = 1 << xs_state_viewport_x_m,
    // Depth
    xs_state_bit_depth_bound_min_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_depth_bound_max_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_depth_bound_enabled_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_depth_test_op_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_depth_test_enabled_m = 1 << xs_state_viewport_x_m,
    xs_state_bit_depth_write_enabled_m = 1 << xs_state_viewport_x_m,
    // TODO Stencil
    xs_state_bit_stencil_enabled_m,
    // Shader
    xs_state_bit_shader_m,
    // Input layout
    xs_state_bit_input_stream_count_m,
    xs_state_bit_input_stream_m, // xg_vertex_stream_t
    // Rasterizer
    xs_state_bit_rasterizer_polygon_mode_m,
    xs_state_bit_rasterizer_cull_mode_m,
    xs_state_bit_rasterizer_winding_mode_m,
    xs_state_bit_rasterizer_antialiasing_mode_m,
    xs_state_bit_rasterizer_antialiasing_sample_count_m,
    xs_state_bit_rasterizer_depth_bias_enable_m,
    xs_state_bit_rasterizer_depth_bias_const_factor_m,
    xs_state_bit_rasterizer_depth_bias_slope_factor_m,
    xs_state_bit_rasterizer_depth_clamp_enable_m,
    xs_state_bit_rasterizer_depth_clamp_m,
    xs_state_bit_rasterizer_line_width_m,
    xs_state_bit_rasterizer_disabled_m,
} xs_state_bit_e;

typedef struct {
    // TODO should permutations be all grouped under the same xs pipeline object instead of each having its own separate xs pipeline?
    // TODO new API:
#if 0
    xs_database_h           ( *create_database ) ( const char* path );
    void                    ( *build_database ) ( xs_database_build_result_t* result, xs_database_h database );
    void                    ( *clear_database ) ( xs_database_h database );
    void                    ( *update_database ) ( xs_database_h database, xg_workload_h workload );
    xs_database_pipeline_h  ( *get_pipeline ) ( xs_pipeline_hash_t hash );
    xg_pipeline_state_h     ( *get_xg_pipeline_state ) ( xs_database_pipeline_h databse_pipeline );
#endif
    xs_database_h ( *create_database ) ( const xs_database_params_t* params );
    void ( *destroy_database ) ( xs_database_h database );

    bool ( *add_database_folder ) ( xs_database_h database, const char* path );
    bool ( *set_output_folder ) ( xs_database_h database, const char* path );
    void ( *clear_database ) ( xs_database_h database );
    void ( *set_build_params ) ( xs_database_h database, const xs_database_build_params_t* params );
    xs_database_build_result_t ( *build_database ) ( xs_database_h database );
    void ( *rebuild_databases ) ( void );

    // TODO go hash->fx and fx->technique(pipeline state) instead of hash->pipeline state
    xs_database_pipeline_h ( *get_database_pipeline ) ( xs_database_h database, xs_string_hash_t name_hash );
    xg_graphics_pipeline_state_h ( *get_pipeline_state ) ( xs_database_pipeline_h xs_state );
    //void ( *release_pipeline_state ) ( xg_graphics_pipeline_state_h pipeline );

    void ( *update_pipeline_states ) ( xg_workload_h workload );
} xs_i;














