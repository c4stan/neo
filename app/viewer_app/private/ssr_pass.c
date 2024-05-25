#include <ssr_pass.h>

#include <xs.h>

#include <std_log.h>
#include <std_string.h>

typedef struct {
    float resolution_x_f32;
    float resolution_y_f32;
    uint32_t hiz_mip_count;
} ssr_draw_data_t;

typedef struct {
    xg_graphics_pipeline_state_h pipeline;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
    ssr_draw_data_t draw_data;
} ssr_pass_args_t;

static void xf_ssr_trace_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    ssr_pass_args_t* pass_args = ( ssr_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        xg_render_textures_binding_t render_textures = xg_null_render_texture_bindings_m;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    xs_i* xs = std_module_get_m ( xs_module_name_m );
    {
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        xg_texture_resource_binding_t textures[3];
        textures[0] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 );
        textures[1] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[1], 1 );
        textures[2] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[2], 2 );

        xg_sampler_resource_binding_t sampler;
        sampler.shader_register = 3;
        sampler.sampler = pass_args->sampler;

        xg_buffer_range_t shader_buffer_range = xg->write_workload_uniform ( node_args->workload, &pass_args->draw_data, sizeof ( ssr_draw_data_t ) );

        xg_buffer_resource_binding_t buffer;
        buffer.shader_register = 4;
        buffer.range = shader_buffer_range;
        buffer.type = xg_buffer_binding_type_uniform_m;

        xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
        draw_bindings.set = xg_resource_binding_set_per_draw_m;
        draw_bindings.texture_count = 3;
        draw_bindings.sampler_count = 1;
        draw_bindings.buffer_count = 1;
        draw_bindings.textures = textures;
        draw_bindings.samplers = &sampler;
        draw_bindings.buffers = &buffer;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

        xg_viewport_state_t viewport = xg_default_viewport_state_m;
        viewport.width = pass_args->width;
        viewport.height = pass_args->height;
        xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

        xg->cmd_draw ( cmd_buffer, 3, 0, key );
    }
}

xf_node_h add_ssr_raymarch_pass ( xf_graph_h graph, xf_texture_h ssr_raymarch, xf_texture_h normals, xf_texture_h color, xf_texture_h hiz ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t dst_info;
    xf->get_texture_info ( &dst_info, ssr_raymarch );

    xf_texture_info_t hiz_info;
    xf->get_texture_info ( &hiz_info, hiz );

    ssr_pass_args_t args;
    {
        args.pipeline = xs->lookup_pipeline_state ( "ssr" );
        args.sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m );
        args.width = dst_info.width;
        args.height = dst_info.height;
        args.draw_data.resolution_x_f32 = ( float ) args.width;
        args.draw_data.resolution_y_f32 = ( float ) args.height;
        args.draw_data.hiz_mip_count = ( uint32_t ) hiz_info.mip_levels;
    }

    xf_node_h ssr_trace_node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.render_targets[params.render_targets_count++] = xf_render_target_dependency_m ( ssr_raymarch, xg_default_texture_view_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( normals, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( color, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( hiz, xg_shading_stage_fragment_m );
        params.execute_routine = xf_ssr_trace_pass;
        params.user_args = std_buffer_m ( &args );
        std_str_copy_m ( params.debug_name, "ssr" );
        params.passthrough.enable = true;
        params.passthrough.render_targets[0].mode = xf_node_passthrough_mode_clear_m;
        ssr_trace_node = xf->create_node ( graph, &params );
    }

    return ssr_trace_node;
}
