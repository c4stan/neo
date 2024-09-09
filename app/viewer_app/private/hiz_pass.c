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
static void hz_gen_mip0_copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    hz_gen_pass_args_t* pass_args = ( hz_gen_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
        .render_targets_count = 1,
        .render_targets = { xf_render_target_binding_m ( node_args->io->render_targets[0] ) },
    );
    xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_draw_m,
        .texture_count = 1,
        .textures = { xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 ) },
        .sampler_count = 1,
        .samplers = xg_sampler_resource_binding_m ( .sampler = pass_args->sampler, .shader_register = 1 ),
    );

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = pass_args->width,
        .height = pass_args->height,
    );
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xg->cmd_draw ( cmd_buffer, 3, 0, key );
}
#endif

static void hz_gen_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    hz_gen_pass_args_t* pass_args = ( hz_gen_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
        .render_targets_count = 1,
        .render_targets = { xf_render_target_binding_m ( node_args->io->render_targets[0] ) },
    );
    xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_draw_m,
        .texture_count = 1,
        .sampler_count = 1,
        .buffer_count = 1,
        .textures = { xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 ) },
        .samplers = { xg_sampler_resource_binding_m ( .sampler = pass_args->sampler, .shader_register = 1 ) },
        .buffers = { xg_buffer_resource_binding_m ( 
            .shader_register = 2,
            .type = xg_buffer_binding_type_uniform_m,
            .range = xg->write_workload_uniform ( node_args->workload, &pass_args->draw_data, sizeof ( hz_gen_draw_data_t ) ),
        ) }
    );

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = pass_args->width,
        .height = pass_args->height,
    );
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xg->cmd_draw ( cmd_buffer, 3, 0, key );
}

#if 1
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

    xf_node_params_t params = xf_node_params_m (
        .render_targets_count = 1,
        .render_targets = { xf_render_target_dependency_m ( hiz, xg_texture_view_m ( .mip_base = 0, .mip_count = 1 ) ) },
        .shader_texture_reads_count = 1,
        .shader_texture_reads = { xf_shader_texture_dependency_m ( depth, xg_default_texture_view_m, xg_pipeline_stage_bit_fragment_shader_m ) },
        .execute_routine = hz_gen_mip0_copy_pass,
        .user_args = std_buffer_m ( &args ),
        .debug_name = "hiz_gen_mip0",
    );
    xf_node_h hiz_mip0_gen_node = xf->create_node ( graph, &params );
    return hiz_mip0_gen_node;
}
#else
xf_node_h add_hiz_mip0_gen_pass ( xf_graph_h graph, xf_texture_h hiz, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_node_params_t params = xf_default_node_params_m;
    params.copy_texture_reads[params.copy_texture_reads_count++] = xf_copy_texture_dependency_m ( depth, xg_default_texture_view_m );
    params.copy_texture_writes[params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( hiz, xg_texture_view_m ( .mip_base = 0, .mip_count = 1 ) );
    params.execute_routine = hz_gen_mip0_copy_pass;
    std_str_copy_static_m ( params.debug_name, "hiz_gen_mip0" );
    xf_node_h hiz_mip0_gen_node = xf->create_node ( graph, &params );

    return hiz_mip0_gen_node;
}
#endif

xf_node_h add_hiz_submip_gen_pass ( xf_graph_h graph, xf_texture_h hiz, uint32_t mip_level ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;
    xg_i* xg = state->modules.xg;

    xf_texture_info_t hiz_info;
    xf->get_texture_info ( &hiz_info, hiz );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    hz_gen_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "hz_gen_fs" ) ),
        .sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m ),
        .draw_data.src_resolution_x_f32 = ( float ) ( hiz_info.width / ( 1 << ( mip_level - 1 ) ) ),
        .draw_data.src_resolution_y_f32 = ( float ) ( hiz_info.height / ( 1 << ( mip_level - 1 ) ) ),
        .draw_data.dst_resolution_x_f32 = ( float ) ( hiz_info.width / ( 1 << mip_level ) ),
        .draw_data.dst_resolution_y_f32 = ( float ) ( hiz_info.height / ( 1 << mip_level ) ),
        .width = hiz_info.width / ( 1 << mip_level ),
        .height = hiz_info.height / ( 1 << mip_level ),
    };

    xf_node_params_t params = xf_node_params_m (
        .render_targets_count = 1,
        .render_targets = { xf_render_target_dependency_m ( hiz, xg_texture_view_m ( .mip_base = mip_level, .mip_count = 1 ) ) },
        .shader_texture_reads_count = 1,
        .shader_texture_reads = { 
            xf_shader_texture_dependency_m ( hiz, xg_texture_view_m ( .mip_base = mip_level - 1, .mip_count = 1 ), xg_pipeline_stage_bit_fragment_shader_m ),
        },
        .execute_routine = hz_gen_pass,
        .user_args = std_buffer_m ( &args ),
    );
    std_str_format_m ( params.debug_name, "hiz_gen_mip" std_fmt_u32_m, mip_level );
    xf_node_h hiz_submip_gen_node = xf->create_node ( graph, &params );
    return hiz_submip_gen_node;
}
