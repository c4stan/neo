#include <downsample_pass.h>

#include <xs.h>

#include <std_log.h>
#include <std_string.h>

typedef struct {
    float src_resolution_x_f32;
    float src_resolution_y_f32;
    float dst_resolution_x_f32;
    float dst_resolution_y_f32;
} downsample_draw_data_t;

typedef struct {
    xs_pipeline_state_h pipeline_state;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
    downsample_draw_data_t draw_data;
} downsample_pass_args_t;

static void downsample_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( downsample_pass_args_t* ) user_args;

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
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline_state );
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        xg_texture_resource_binding_t texture = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 );

        xg_sampler_resource_binding_t sampler;
        sampler.sampler = pass_args->sampler;
        sampler.shader_register = 1;

        xg_buffer_resource_binding_t buffer;
        buffer.shader_register = 2;
        buffer.type = xg_buffer_binding_type_uniform_m;
        buffer.range = xg->write_workload_uniform ( node_args->workload, &pass_args->draw_data, sizeof ( downsample_draw_data_t ) );

        xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
        draw_bindings.set = xg_resource_binding_set_per_draw_m;
        draw_bindings.texture_count = 1;
        draw_bindings.sampler_count = 1;
        draw_bindings.buffer_count = 1;
        draw_bindings.textures = &texture;
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

xf_node_h add_downsample_pass ( xf_graph_h graph, xf_texture_h source, xf_texture_h destination, const char* name ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    xf_texture_info_t src_info, dst_info;
    xf->get_texture_info ( &src_info, source );
    xf->get_texture_info ( &dst_info, destination );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    downsample_pass_args_t pass_args;
    const char* pipeline_name;

    switch ( src_info.format ) {
        case xg_format_r8g8b8a8_unorm_m:
            pipeline_name = "downsample_r8g8b8a8_unorm";
            break;

        case xg_format_d24_unorm_s8_uint_m:
            pipeline_name = "downsample_d24s8";
            break;

        default:
            std_log_error_m ( "downsample format not supported" );
            pipeline_name = NULL;
    }

    pass_args.pipeline_state = xs->lookup_pipeline_state ( pipeline_name );
    pass_args.sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m );
    pass_args.draw_data.src_resolution_x_f32 = ( float ) src_info.width;
    pass_args.draw_data.src_resolution_y_f32 = ( float ) src_info.height;
    pass_args.draw_data.dst_resolution_x_f32 = ( float ) dst_info.width;
    pass_args.draw_data.dst_resolution_y_f32 = ( float ) dst_info.height;
    pass_args.width = dst_info.width;
    pass_args.height = dst_info.height;

    xf_node_params_t params = xf_default_node_params_m;
    params.render_targets[params.render_targets_count++] = xf_render_target_dependency_m ( destination, xg_default_texture_view_m );
    params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( source, xg_shading_stage_fragment_m );
    params.execute_routine = downsample_pass;
    params.user_args = std_buffer_m ( &pass_args );
    std_str_copy_static_m ( params.debug_name, name );
    xf_node_h node = xf->create_node ( graph, &params );

    return node;
}
