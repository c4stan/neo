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

#if 0
    xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
        .render_targets_count = pass_args->params.render_targets_count
    );

    for ( uint32_t i = 0; i < pass_args->params.render_targets_count; ++i ) {
        render_textures.render_targets[i] = xf_render_target_binding_m ( node_args->io->render_targets[i] );
    }

    xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->params.pipeline );
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_draw_m,
        .texture_count = pass_args->params.texture_reads_count,
        .sampler_count = pass_args->params.samplers_count,
        .buffer_count = 0, // todo
    );

    uint32_t draw_binding = 0;

    for ( uint32_t i = 0; i < pass_args->params.texture_reads_count; ++i ) {
        draw_bindings.textures[i] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[i], draw_binding );
        ++draw_binding;
    }

    for ( uint32_t i = 0; i < pass_args->params.samplers_count; ++i ) {
        draw_bindings.samplers[i].sampler = pass_args->params.samplers[i];
        draw_bindings.samplers[i].shader_register = draw_binding++;
    }

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = pass_args->width,
        .height = pass_args->height,
    );
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xg->cmd_draw ( cmd_buffer, 3, 0, key );
#else
    xg_compute_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->params.pipeline );
    xg->cmd_set_compute_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_draw_m,
        .texture_count = pass_args->params.texture_reads_count + pass_args->params.render_targets_count,
        .sampler_count = pass_args->params.samplers_count,
    );

    uint32_t draw_binding = 0;
    uint32_t texture_idx = 0;

    for ( uint32_t i = 0; i < pass_args->params.render_targets_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->shader_texture_writes[i], draw_binding );
        ++draw_binding;
    }

    for ( uint32_t i = 0; i < pass_args->params.texture_reads_count; ++i ) {
        draw_bindings.textures[texture_idx++] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[i], draw_binding );
        ++draw_binding;
    }

    for ( uint32_t i = 0; i < pass_args->params.samplers_count; ++i ) {
        draw_bindings.samplers[i].sampler = pass_args->params.samplers[i];
        draw_bindings.samplers[i].shader_register = draw_binding++;
    }

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    uint32_t workgroup_count_x = std_div_ceil_u32 ( pass_args->width, 8 );
    uint32_t workgroup_count_y = std_div_ceil_u32 ( pass_args->height, 8 );
    xg->cmd_dispatch_compute ( cmd_buffer, workgroup_count_x, workgroup_count_y, 1, key );
#endif
}

xf_node_h add_simple_screen_pass ( xf_graph_h graph, const char* name, const simple_screen_pass_params_t* params ) {
    viewapp_state_t* state = viewapp_state_get ();
    xf_i* xf = state->modules.xf;

    std_assert_m ( params->render_targets_count > 0 );
    xf_texture_info_t render_target_info;
    xf->get_texture_info ( &render_target_info, params->render_targets[0] );

    simple_screen_pass_args_t args = {
        .params = *params,
        .width = render_target_info.width,
        .height = render_target_info.height,
    };

    xf_node_params_t node_params = xf_node_params_m (
        .shader_texture_writes_count = params->render_targets_count,
        .shader_texture_reads_count = params->texture_reads_count,
        .execute_routine = simple_screen_pass_routine,
        .user_args = std_buffer_m ( &args ),
        .passthrough = params->passthrough,
    );

    for ( uint32_t i = 0; i < node_params.shader_texture_writes_count; ++i ) {
        //node_params.render_targets[i] = xf_render_target_dependency_m ( params->render_targets[i], xg_default_texture_view_m );
        node_params.shader_texture_writes[i] = xf_storage_texture_dependency_m ( params->render_targets[i], xg_default_texture_view_m, xg_pipeline_stage_bit_compute_shader_m );
    }

    for ( uint32_t i = 0; i < node_params.shader_texture_reads_count; ++i ) {
        node_params.shader_texture_reads[i] = xf_sampled_texture_dependency_m ( params->texture_reads[i], xg_pipeline_stage_bit_compute_shader_m );
    }

    std_str_copy_static_m ( node_params.debug_name, name );

    xf_node_h node = xf->create_node ( graph, &node_params );
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

    xf_node_params_t node_params = xf_node_params_m (
        .copy_texture_writes_count = params->color_textures_count + params->depth_stencil_textures_count,
        .execute_routine = simple_clear_pass,
        .user_args = std_buffer_m ( &args ),
    );

    uint32_t write_count = 0;

    for ( uint32_t i = 0; i < params->color_textures_count; ++i ) {
        node_params.copy_texture_writes[write_count++] = xf_copy_texture_dependency_m ( params->color_textures[i], xg_default_texture_view_m );
    }

    for ( uint32_t i = 0; i < params->depth_stencil_textures_count; ++i ) {
        node_params.copy_texture_writes[write_count++] = xf_copy_texture_dependency_m ( params->depth_stencil_textures[i], xg_default_texture_view_m );
    }

    std_str_copy_static_m ( node_params.debug_name, name );

    xf_node_h node = xf->create_node ( graph, &node_params );
    return node;
}

typedef struct {
    simple_copy_pass_params_t params;
} simple_copy_pass_args_t;

static void simple_copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m args = ( simple_copy_pass_args_t* ) user_args;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;

    xg_texture_copy_params_t copy_params = xg_texture_copy_params_m (
        .source = xf_copy_texture_resource_m ( node_args->io->copy_texture_reads[0] ),
        .destination = xf_copy_texture_resource_m ( node_args->io->copy_texture_writes[0] ),
        .filter = args->params.filter,
    );

    xg->cmd_copy_texture ( cmd_buffer, &copy_params, key );
}

xf_node_h add_simple_copy_pass ( xf_graph_h graph, const char* name, const simple_copy_pass_params_t* params ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    simple_copy_pass_args_t args = { .params = *params };

    xf_node_params_t node_params = xf_node_params_m (
        .copy_texture_writes_count = 1,
        .copy_texture_writes = { xf_copy_texture_dependency_m ( params->dest, params->dest_view ) },
        .copy_texture_reads_count = 1,
        .copy_texture_reads = { xf_copy_texture_dependency_m ( params->source, params->source_view ) },
        .presentable_texture = params->presentable ? params->dest : xf_null_handle_m,
        .execute_routine = simple_copy_pass,
        .user_args = std_buffer_m ( &args ),
    );

    std_str_copy_static_m ( node_params.debug_name, name );

    xf_node_h node = xf->create_node ( graph, &node_params );
    return node;
}
