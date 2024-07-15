#pragma once

#include <std_module.h>
#include <std_platform.h>
#include <std_hash.h>

#include <xg.h>

#define xs_module_name_m xs
std_module_export_m void* xs_load ( void* );
std_module_export_m void xs_reload ( void*, void* );
std_module_export_m void xs_unload ( void );

#define xs_hash_string_m(str, len) std_hash_metro( (str), (len) )
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
typedef uint64_t xs_pipeline_state_h;

typedef struct {
    int32_t value;
    char name[xs_shader_definition_name_max_len_m];
} xs_shader_definition_t;

// TODO use dynamic pipeline state for viewport size
// https://stackoverflow.com/questions/57950008/must-a-vulkan-pipeline-be-recreate-when-changing-the-size-of-the-window
typedef struct {
    size_t viewport_width;
    size_t viewport_height;
    //xs_shader_permutation_t permutation;
    xs_shader_definition_t* global_definitions;
    uint32_t global_definition_count;
} xs_database_build_params_t;

typedef struct {
    size_t successful_shaders;
    size_t failed_shaders;
    size_t skipped_shaders;
    size_t successful_pipeline_states;
    size_t failed_pipeline_states;
} xs_database_build_result_t;

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

    bool ( *add_database_folder ) ( const char* path );
    bool ( *set_output_folder ) ( const char* path );
    void ( *clear_database ) ( void );
    xs_database_build_result_t ( *build_database_shaders ) ( xg_device_h device, const xs_database_build_params_t* params );

    // TODO go hash->fx and fx->technique(pipeline state) instead of hash->pipelinestate
    //xg_graphics_pipeline_state_h ( *get_pipeline_state ) ( xs_string_hash_t name_hash );
    // TODO find better names
    xs_pipeline_state_h ( *lookup_pipeline_state ) ( const char* name );
    xs_pipeline_state_h ( *lookup_pipeline_state_hash ) ( xs_string_hash_t name_hash );
    xg_graphics_pipeline_state_h ( *get_pipeline_state ) ( xs_pipeline_state_h xs_state );
    //void ( *release_pipeline_state ) ( xg_graphics_pipeline_state_h pipeline );

    void ( *update_pipeline_states ) ( xg_workload_h workload );
} xs_i;
