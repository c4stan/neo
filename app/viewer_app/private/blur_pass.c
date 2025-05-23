#include <blur_pass.h>

#include <xs.h>

#include <std_log.h>
#include <std_string.h>

#include <math.h>

#include "viewapp_state.h"

typedef struct {
    float src_resolution_x;
    float src_resolution_y;
    float dst_resolution_x;
    float dst_resolution_y;
    float direction_x;
    float direction_y;
    uint32_t kernel_size;
    uint32_t _pad0[1];
    float kernel[32 * 4];
} blur_draw_data_t;

typedef struct {
    xs_database_pipeline_h pipeline;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
    blur_draw_data_t draw_data;
} blur_pass_args_t;

static void generate_blur_kernel ( float* buffer, uint32_t size, float sigma ) {
    //float sigma = 15;
    float sum = 0;
    int32_t radius = size / 2;

    for ( int32_t i = 0; i < size; ++i ) {
        float j = ( float ) ( i - radius );
        float k = expf ( -j * j / sigma / sigma  );
        sum += k;
        buffer[i * 4 + 0] = k;
        buffer[i * 4 + 1] = 0;
        buffer[i * 4 + 2] = 0;
        buffer[i * 4 + 3] = 0;
    }

    for ( uint32_t i = 0; i < size; ++i ) {
        buffer[i * 4] /= sum;
    }
}

xf_node_h add_bilateral_blur_pass ( xf_graph_h graph, xf_texture_h dst, xf_texture_h color, xf_texture_h normals, xf_texture_h depth, uint32_t kernel_size, float sigma, blur_pass_direction_e direction, const char* debug_name ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;;
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t dst_info, src_info;
    xf->get_texture_info ( &dst_info, dst );
    xf->get_texture_info ( &src_info, color );

    std_assert_m ( kernel_size < 32 );

    // TODO
    //kernel_size = 11;
    //sigma = 5;

    xs_database_pipeline_h database_pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "bi_blur" ) );
    xg_compute_pipeline_state_h pipeline_state = xs->get_pipeline_state ( database_pipeline );

    blur_draw_data_t uniform_data = {
        .src_resolution_x = ( float ) src_info.width,
        .src_resolution_y = ( float ) src_info.height,
        .dst_resolution_x = ( float ) dst_info.width,
        .dst_resolution_y = ( float ) dst_info.height,
        .kernel_size = kernel_size,
    };

    if ( direction == blur_pass_direction_horizontal_m ) {
        uniform_data.direction_x = 1;
        uniform_data.direction_y = 0;
    } else {
        uniform_data.direction_x = 0;
        uniform_data.direction_y = 1;
    }

    generate_blur_kernel ( uniform_data.kernel, kernel_size, sigma );

    xf_node_params_t params = xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = pipeline_state,
            .uniform_data = std_buffer_struct_m ( &uniform_data ),
            .workgroup_count = { std_div_ceil_u32 ( dst_info.width, 8 ), std_div_ceil_u32 ( dst_info.height, 8 ), 1 },
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .sampled_textures_count = 3,
            .sampled_textures =  {
                xf_compute_texture_dependency_m ( .texture = color ),
                xf_compute_texture_dependency_m ( .texture = normals ),
                xf_compute_texture_dependency_m ( .texture = depth ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_shader_texture_dependency_m ( .texture = dst, .stage = xg_pipeline_stage_bit_compute_shader_m )
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_copy_m, .copy_source = xf_copy_texture_dependency_m ( .texture = color ) ) }
        ),
    );
    std_str_copy_static_m ( params.debug_name, debug_name );
    xf_node_h blur_node = xf->create_node ( graph, &params );
    return blur_node;
}
