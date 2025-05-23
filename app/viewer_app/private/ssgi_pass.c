#include <ssgi_pass.h>

#include <std_string.h>
#include <std_log.h>

#include <xs.h>

#include "viewapp_state.h"

typedef struct {
    float resolution_x_f32;
    float resolution_y_f32;
    uint32_t hiz_mip_count;
} ssgi_draw_data_t;

xf_node_h add_ssgi_raymarch_pass ( xf_graph_h graph, const char* name, xf_texture_h ssgi_raymarch, xf_texture_h normals, xf_texture_h color, xf_texture_h direct_lighting, xf_texture_h hiz ) {
    viewapp_state_t* state = viewapp_state_get ();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t dst_info;
    xf->get_texture_info ( &dst_info, ssgi_raymarch );

    xf_texture_info_t hiz_info;
    xf->get_texture_info ( &hiz_info, hiz );

    xs_database_pipeline_h xs_pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "ssgi" ) );
    xg_compute_pipeline_state_h pipeline_state = xs->get_pipeline_state ( xs_pipeline );

    ssgi_draw_data_t uniform_data = {
        .resolution_x_f32 = ( float ) dst_info.width,
        .resolution_y_f32 = ( float ) dst_info.height,
        .hiz_mip_count = ( uint32_t ) hiz_info.mip_levels,
    };

    xf_node_params_t params = xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = pipeline_state,
            .workgroup_count = { std_div_ceil_u32 ( dst_info.width, 8 ), std_div_ceil_u32 ( dst_info.height, 8 ), 1 },
            .uniform_data = std_buffer_struct_m ( &uniform_data ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( graph_info.device, xg_default_sampler_linear_clamp_m ) },
        ),
        .resources = xf_node_resource_params_m (
            .sampled_textures_count = 4,
            .sampled_textures = {
                xf_compute_texture_dependency_m ( .texture = normals ),
                xf_compute_texture_dependency_m ( .texture = color ),
                xf_compute_texture_dependency_m ( .texture = direct_lighting ),
                xf_compute_texture_dependency_m ( .texture = hiz ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_shader_texture_dependency_m ( .texture = ssgi_raymarch, .stage = xg_pipeline_stage_bit_compute_shader_m )
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .storage_texture_writes = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_clear_m ) }
        ),
    );
    std_str_copy_static_m ( params.debug_name, name );
    xf_node_h ssgi_trace_node = xf->create_node ( graph, &params );
    return ssgi_trace_node;
}
