#include <blur_pass.h>

#include <xs.h>

#include <std_log.h>
#include <std_string.h>

#include <math.h>

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
    xs_pipeline_state_h pipeline;
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

static void blur_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    blur_pass_args_t* pass_args = ( blur_pass_args_t* ) user_args;

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

        {
            xg_buffer_range_t shader_buffer_range = xg->write_workload_uniform ( node_args->workload, &pass_args->draw_data, sizeof ( blur_draw_data_t ) );

            xg_buffer_resource_binding_t buffer;
            buffer.shader_register = 4;
            buffer.type = xg_buffer_binding_type_uniform_m;
            buffer.range = shader_buffer_range;

            xg_texture_resource_binding_t textures[3];
            textures[0] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 );
            textures[1] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[1], 1 );
            textures[2] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[2], 2 );

            xg_sampler_resource_binding_t sampler;
            sampler.shader_register = 3;
            sampler.sampler = pass_args->sampler;

            xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
            draw_bindings.set = xg_resource_binding_set_per_draw_m;
            draw_bindings.texture_count = 3;
            draw_bindings.sampler_count = 1;
            draw_bindings.buffer_count = 1;
            draw_bindings.textures = textures;
            draw_bindings.samplers = &sampler;
            draw_bindings.buffers = &buffer;

            xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );
        }

        xg_viewport_state_t viewport = xg_default_viewport_state_m;
        viewport.width = pass_args->width;
        viewport.height = pass_args->height;
        xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

        xg->cmd_draw ( cmd_buffer, 3, 0, key );
    }
}

xf_node_h add_bilateral_blur_pass ( xf_graph_h graph, xf_texture_h dst, xf_texture_h color, xf_texture_h normals, xf_texture_h depth, uint32_t kernel_size, float sigma, blur_pass_direction_e direction, const char* debug_name ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t dst_info, src_info;
    xf->get_texture_info ( &dst_info, dst );
    xf->get_texture_info ( &src_info, color );

    std_assert_m ( kernel_size < 32 );

    kernel_size = 11;
    sigma = 5;

    blur_pass_args_t args;
    {
        xs_pipeline_state_h pipeline_state = xs->lookup_pipeline_state ( "bi_blur" );
        std_assert_m ( pipeline_state != xs_null_handle_m );

        args.pipeline = pipeline_state;
        args.sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m );
        args.draw_data.src_resolution_x = ( float ) src_info.width;
        args.draw_data.src_resolution_y = ( float ) src_info.height;
        args.draw_data.dst_resolution_x = ( float ) dst_info.width;
        args.draw_data.dst_resolution_y = ( float ) dst_info.height;

        if ( direction == blur_pass_direction_horizontal_m ) {
            args.draw_data.direction_x = 1;
            args.draw_data.direction_y = 0;
        } else {
            args.draw_data.direction_x = 0;
            args.draw_data.direction_y = 1;
        }

        args.draw_data.kernel_size = kernel_size;
        generate_blur_kernel ( args.draw_data.kernel, kernel_size, sigma );

        args.width = dst_info.width;
        args.height = dst_info.height;
    }

    xf_node_h blur_node;
#if 0
    xf_node_h blur_node = xf->create_node ( graph, &xf_node_params_m (
        .render_targets_count = 1,
        .shader_texture_reads = {
            xf_sampled_texture_dependency_m ( color, xg_shading_stage_fragment_m ),
            xf_sampled_texture_dependency_m ( normals, xg_shading_stage_fragment_m ),
            xf_sampled_texture_dependency_m ( depth, xg_shading_stage_fragment_m ),
        },
        .execute_routine = blur_pass_routine,
        .user_args = &args,
        .user_args_allocator = std_virtual_heap_allocator(),
        .user_args_alloc_size = sizeof ( args ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_node_render_target_passthrough_m (
                    .mode = xf_node_passthrough_mode_alias_m,
                    .alias = color,
                )
            },
        ),
    ) );
#endif
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.render_targets[params.render_targets_count++] = xf_render_target_dependency_m ( dst, xg_default_texture_view_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( color, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( normals, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( depth, xg_shading_stage_fragment_m );
        params.execute_routine = blur_pass_routine;
        params.user_args = std_buffer_m ( &args );
        params.passthrough.enable = true;
        params.passthrough.render_targets[0].mode = xf_node_passthrough_mode_alias_m;
        params.passthrough.render_targets[0].alias = color;
        std_str_copy_m ( params.debug_name, debug_name );
        blur_node = xf->create_node ( graph, &params );
    }

    return blur_node;
}
