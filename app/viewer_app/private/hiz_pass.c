#include "hiz_pass.h"

#include <std_string.h>
#include <std_log.h>

#include "viewapp_state.h"

typedef struct {
    float src_resolution_x_f32;
    float src_resolution_y_f32;
    float dst_resolution_x_f32;
    float dst_resolution_y_f32;
} hz_gen_draw_data_t;

typedef struct {
    xs_database_pipeline_h pipeline;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
    hz_gen_draw_data_t draw_data;
} hz_gen_pass_args_t;

#if 0
// TODO https://stackoverflow.com/questions/58063803/how-to-copy-a-depth-image-to-a-color-image
static void hz_gen_mip0_copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    {
        xg_texture_to_buffer_copy_params_t copy = xg_default_texture_to_buffer_copy_params_m;
        copy.source = node_args->io->copy_texture_reads[0].texture;
        copy.mip_base = node_args->io->copy_texture_reads[0].view.mip_base;
        copy.array_base = node_args->io->copy_texture_reads[0].view.array_base;
        copy.array_count = node_args->io->copy_texture_reads[0].view.array_count;
        copy.destination = node_args->io->copy_buffer_writes[0];
        copy.destination_offset = 0;
    }

    {
        xg_buffer_to_texture_copy_params_t copy = xg_default_buffer_to_texture_copy_params_m;
        copy.source = node_args->io->copy_texture_reads[0].texture;
        copy.mip_base = node_args->io->copy_texture_reads[0].view.mip_base;
        copy.array_base = node_args->io->copy_texture_reads[0].view.array_base;
        copy.array_count = node_args->io->copy_texture_reads[0].view.array_count;
        copy.destination = node_args->io->copy_buffer_writes[0];
        copy.destination_offset = 0;
    }

    xg_texture_copy_params_t copy_params = xg_default_texture_copy_params_m;
    copy_params.source = xf_copy_texture_resource_m ( node_args->io->copy_texture_reads[0] );
    copy_params.destination = xf_copy_texture_resource_m ( node_args->io->copy_texture_writes[0] );
    copy_params.mip_count = 1;
    copy_params.aspect = xg_texture_aspect_depth_m;
    xg->cmd_copy_texture ( cmd_buffer, &copy_params, key );
}
#else
std_unused_static_m()
static void hz_gen_mip0_copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    hz_gen_pass_args_t* pass_args = ( hz_gen_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );

    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );
    //xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( node_args->resource_cmd_buffer, &xg_resource_bindings_params_m (
        .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
        .bindings = xg_pipeline_resource_bindings_m (
            .texture_count = 1,
            .textures = { xf_shader_texture_binding_m ( node_args->io->sampled_textures[0], 0 ) },
            .sampler_count = 1,
            .samplers = xg_sampler_resource_binding_m ( .sampler = pass_args->sampler, .shader_register = 1 ),
        )
    ) );

    xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
        .pipeline = pipeline_state,
        .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings,
        .primitive_count = 1,
    ) );
}
#endif

xf_node_h add_hiz_mip0_gen_pass ( xf_graph_h graph, xf_texture_h hiz, xf_texture_h depth ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;
    xg_i* xg = state->modules.xg;

    xf_texture_info_t hiz_info, depth_info;
    xf->get_texture_info ( &hiz_info, hiz );
    xf->get_texture_info ( &depth_info, depth );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    hz_gen_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "copy" ) ),
        .sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m ),
        .width = hiz_info.width,
        .height = hiz_info.height,
    };
;
    xf_node_h hiz_mip0_gen_node = xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "hiz_gen_mip0",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = hz_gen_mip0_copy_pass,
            .user_args = std_buffer_m ( &args ),
            .auto_renderpass = true,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = hiz, .view = xg_texture_view_m ( .mip_base = 0, .mip_count = 1 ) ) },
            .sampled_textures_count = 1,
            .sampled_textures = { xf_fragment_texture_dependency_m ( .texture = depth ) },
        ),
    ) );
    return hiz_mip0_gen_node;
}

xf_node_h add_hiz_submip_gen_pass ( xf_graph_h graph, xf_texture_h hiz, uint32_t mip_level ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;
    xg_i* xg = state->modules.xg;

    xf_texture_info_t hiz_info;
    xf->get_texture_info ( &hiz_info, hiz );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    uint32_t dst_width = hiz_info.width / ( 1 << mip_level );
    uint32_t dst_height = hiz_info.height / ( 1 << mip_level );

    hz_gen_draw_data_t uniform_data = {
        .src_resolution_x_f32 = ( float ) ( hiz_info.width / ( 1 << ( mip_level - 1 ) ) ),
        .src_resolution_y_f32 = ( float ) ( hiz_info.height / ( 1 << ( mip_level - 1 ) ) ),
        .dst_resolution_x_f32 = ( float ) ( hiz_info.width / ( 1 << mip_level ) ),
        .dst_resolution_y_f32 = ( float ) ( hiz_info.height / ( 1 << mip_level ) ),
    };

    xs_database_pipeline_h xs_pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "hiz_gen" ) );
    xg_compute_pipeline_state_h pipeline_state = xs->get_pipeline_state ( xs_pipeline );

    xf_node_params_t params = xf_node_params_m (
        .type = xf_node_type_compute_pass_m,
        .pass.compute = xf_node_compute_pass_params_m (
            .pipeline = pipeline_state,
            .workgroup_count = { std_div_ceil_u32 ( dst_width, 8 ), std_div_ceil_u32 ( dst_height, 8 ), 1 },
            .uniform_data = std_buffer_m ( &uniform_data ),
            .samplers_count = 1,
            .samplers = { xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m ) }
        ),
        .resources = xf_node_resource_params_m (
            .sampled_textures_count = 1,
            .sampled_textures = { 
                xf_shader_texture_dependency_m ( .texture = hiz, .view = xg_texture_view_m ( .mip_base = mip_level - 1, .mip_count = 1 ), .stage = xg_pipeline_stage_bit_compute_shader_m ),
            },
            .storage_texture_writes_count = 1,
            .storage_texture_writes = {
                xf_shader_texture_dependency_m ( .texture = hiz, .view = xg_texture_view_m ( .mip_base = mip_level, .mip_count = 1 ), .stage = xg_pipeline_stage_bit_compute_shader_m ),
            },
        )
    );
    std_str_format_m ( params.debug_name, "hiz_gen_mip" std_fmt_u32_m, mip_level );

    xf_node_h hiz_submip_gen_node = xf->create_node ( graph, &params );

    return hiz_submip_gen_node;
}
