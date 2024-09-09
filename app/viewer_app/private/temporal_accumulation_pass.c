#include <temporal_accumulation_pass.h>

#include <xs.h>

#include <std_string.h>
#include <std_log.h>

#include "viewapp_state.h"

typedef struct {
    float resolution_x;
    float resolution_y;
} temporal_accumulation_draw_data_t;

typedef struct {
    xs_database_pipeline_h pipeline;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
    temporal_accumulation_draw_data_t draw_data;
} temporal_accumulation_pass_args_t;

static void temporal_accumulation_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( temporal_accumulation_pass_args_t* ) user_args;
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
        .texture_count = 4,
        .buffer_count = 1,
        .sampler_count = 1,
        .textures = {
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 ),
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[1], 1 ),
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[2], 2 ),
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[3], 3 ),
        },
        .buffers = { xg_buffer_resource_binding_m (
            .shader_register = 5,
            .type = xg_buffer_binding_type_uniform_m,
            .range = xg->write_workload_uniform ( node_args->workload, &pass_args->draw_data, sizeof ( temporal_accumulation_draw_data_t ) ),
        ) },
        .samplers = { xg_sampler_resource_binding_m (
            .sampler = pass_args->sampler,
            .shader_register = 4,
        ) },
    );
    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = pass_args->width,
        .height = pass_args->height,
    );
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xg->cmd_draw ( cmd_buffer, 3, 0, key );
}

xf_node_h add_temporal_accumulation_pass ( xf_graph_h graph, xf_texture_h accumulation, xf_texture_h color, xf_texture_h history, xf_texture_h depth, xf_texture_h prev_depth, const char* debug_name ) {
    viewapp_state_t* state = viewapp_state_get ();
    xf_i* xf = state->modules.xf;
    xs_i* xs = state->modules.xs;
    xg_i* xg = state->modules.xg;

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t accumulation_info;
    xf->get_texture_info ( &accumulation_info, accumulation );

    temporal_accumulation_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "ta" ) ),
        .sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m ),
        .draw_data.resolution_x = ( float ) accumulation_info.width,
        .draw_data.resolution_y = ( float ) accumulation_info.height,
        .width = accumulation_info.width,
        .height = accumulation_info.height,
    };

    xf_node_params_t params = xf_node_params_m (
        .render_targets_count = 1,
        .render_targets = { xf_render_target_dependency_m ( accumulation, xg_default_texture_view_m ) },
        .shader_texture_reads_count = 4,
        .shader_texture_reads = {
            xf_sampled_texture_dependency_m ( color, xg_pipeline_stage_bit_fragment_shader_m ),
            xf_sampled_texture_dependency_m ( history, xg_pipeline_stage_bit_fragment_shader_m ),
            xf_sampled_texture_dependency_m ( depth, xg_pipeline_stage_bit_fragment_shader_m ),
            xf_sampled_texture_dependency_m ( prev_depth, xg_pipeline_stage_bit_fragment_shader_m ),
        },
        .execute_routine = temporal_accumulation_pass,
        .user_args = std_buffer_m ( &args ),
        .passthrough.enable = true,
        .passthrough.render_targets = { xf_node_render_target_passthrough_m ( .mode = xf_node_passthrough_mode_alias_m, .alias = color ) },
    );
    std_str_copy_static_m ( params.debug_name, debug_name );
    xf_node_h temporal_accumulation_node = xf->create_node ( graph, &params );
    return temporal_accumulation_node;
}
