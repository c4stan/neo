#include <simple_pass.h>

#include <xs.h>

#include <std_log.h>
#include <std_string.h>

#include <viewapp_state.h>

typedef struct {
    simple_screen_pass_params_t params;
    uint32_t width;
    uint32_t height;
} simple_screen_pass_args_t;

static void simple_screen_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( const simple_screen_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    xg_render_textures_binding_t render_textures = xg_null_render_texture_bindings_m;
    render_textures.render_targets_count = pass_args->params.render_targets_count;

    for ( uint32_t i = 0; i < pass_args->params.render_targets_count; ++i ) {
        render_textures.render_targets[i] = xf_render_target_binding_m ( node_args->io->render_targets[i] );
    }

    xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->params.pipeline );
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

#if 0
    xg_buffer_range_t shader_buffer_range = xg->write_workload_uniform ( node_args->workload, &pass_args->draw_data, sizeof ( blur_draw_data_t ) );

    xg_buffer_resource_binding_t buffer;
    buffer.shader_register = 4;
    buffer.type = xg_buffer_binding_type_uniform_m;
    buffer.range = shader_buffer_range;
#endif

    xg_texture_resource_binding_t texture_reads[xf_node_max_shader_texture_reads_m];

    uint32_t draw_binding = 0;

    for ( uint32_t i = 0; i < pass_args->params.texture_reads_count; ++i ) {
        texture_reads[i] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[i], draw_binding );
        ++draw_binding;
    }

    xg_sampler_resource_binding_t samplers[xg_pipeline_resource_max_samplers_per_set_m];

    for ( uint32_t i = 0; i < pass_args->params.samplers_count; ++i ) {
        samplers[i].sampler = pass_args->params.samplers[i];
        samplers[i].shader_register = draw_binding++;
    }

    xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
    draw_bindings.set = xg_resource_binding_set_per_draw_m;
    draw_bindings.texture_count = pass_args->params.texture_reads_count;
    draw_bindings.sampler_count = pass_args->params.samplers_count;
    draw_bindings.buffer_count = 0; // todo
    draw_bindings.textures = texture_reads;
    draw_bindings.samplers = samplers;

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    xg_viewport_state_t viewport = xg_default_viewport_state_m;
    viewport.width = pass_args->width;
    viewport.height = pass_args->height;
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xg->cmd_draw ( cmd_buffer, 3, 0, key );
}

xf_node_h add_simple_screen_pass ( xf_graph_h graph, const char* name, const simple_screen_pass_params_t* params ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    std_assert_m ( params->render_targets_count > 0 );
    xf_texture_info_t render_target_info;
    xf->get_texture_info ( &render_target_info, params->render_targets[0] );
    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    simple_screen_pass_args_t args;
    args.params = *params;
    args.width = render_target_info.width;
    args.height = render_target_info.height;

    xf_node_h node;
    {
        xf_node_params_t node_params = xf_default_node_params_m;
        node_params.render_targets_count = params->render_targets_count;

        for ( uint32_t i = 0; i < node_params.render_targets_count; ++i ) {
            node_params.render_targets[i] = xf_render_target_dependency_m ( params->render_targets[i], xg_default_texture_view_m );
        }

        node_params.shader_texture_reads_count = params->texture_reads_count;

        for ( uint32_t i = 0; i < node_params.shader_texture_reads_count; ++i ) {
            node_params.shader_texture_reads[i] = xf_sampled_texture_dependency_m ( params->texture_reads[i], xg_shading_stage_fragment_m );
        }

        node_params.execute_routine = simple_screen_pass_routine;
        node_params.user_args = std_buffer_m ( &args );
        node_params.passthrough = params->passthrough;
        std_str_copy_m ( node_params.debug_name, name );
        node = xf->create_node ( graph, &node_params );
    }

    return node;
}

typedef struct {
    simple_clear_pass_params_t params;
} simple_clear_pass_args_t;

static void simple_clear_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m args = ( simple_clear_pass_args_t* ) user_args;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;

    uint32_t write_count = 0;

    for ( uint32_t i = 0; i < args->params.color_textures_count; ++i ) {
        xf_copy_texture_resource_t target = node_args->io->copy_texture_writes[write_count++];
        xg->cmd_clear_texture ( cmd_buffer, target.texture, args->params.color_clears[i], key );
    }

    for ( uint32_t i = 0; i < args->params.depth_stencil_textures_count; ++i ) { 
        xf_copy_texture_resource_t target = node_args->io->copy_texture_writes[write_count++];
        xg->cmd_clear_depth_stencil_texture ( cmd_buffer, target.texture, args->params.depth_stencil_clears[i], key );
    }
}

xf_node_h add_simple_clear_pass ( xf_graph_h graph, const char* name, const simple_clear_pass_params_t* params ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    simple_clear_pass_args_t args;
    args.params = *params;

    xf_node_h node;
    {
        xf_node_params_t node_params = xf_default_node_params_m;
        node_params.copy_texture_writes_count = params->color_textures_count + params->depth_stencil_textures_count;

        uint32_t write_count = 0;

        for ( uint32_t i = 0; i < params->color_textures_count; ++i ) {
            node_params.copy_texture_writes[write_count++] = xf_copy_texture_dependency_m ( params->color_textures[i], xg_default_texture_view_m );
        }

        for ( uint32_t i = 0; i < params->depth_stencil_textures_count; ++i ) {
            node_params.copy_texture_writes[write_count++] = xf_copy_texture_dependency_m ( params->depth_stencil_textures[i], xg_default_texture_view_m );
        }

        node_params.execute_routine = simple_clear_pass;
        node_params.user_args = std_buffer_m ( &args );
        std_str_copy_m ( node_params.debug_name, name );
        node = xf->create_node ( graph, &node_params );
    }

    return node;
}
