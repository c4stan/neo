#include <lighting_pass.h>

#include <std_string.h>
#include <std_log.h>

#include <viewapp_state.h>

#include <xs.h>
#include <se.h>

typedef struct {
    xs_pipeline_state_h pipeline_state;
    xg_sampler_h sampler;
    uint32_t width;
    uint32_t height;
} lighting_pass_args_t;

// Keep in sync with lighting.frag!
#define MAX_LIGHTS_COUNT 32

typedef struct {
    float pos[3];
    float intensity;
    float color[3];
    uint32_t _pad0;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t view_from_world;
} draw_cbuffer_light_t;

typedef struct {
    uint32_t light_count;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    draw_cbuffer_light_t lights[MAX_LIGHTS_COUNT];
} draw_cbuffer_t;
//

static void lighting_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m args = ( lighting_pass_args_t* ) user_args;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;
    xs_i* xs = state->modules.xs;
    rv_i* rv = state->modules.rv;

    {
        xg_render_textures_binding_t render_textures = xg_null_render_texture_bindings_m;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    draw_cbuffer_t cbuffer;
    std_mem_zero_m ( &cbuffer );

    {
        se_query_h lights_query;
        {
            se_query_params_t query_params = se_default_query_params_m;
            std_bitset_set ( query_params.request_component_flags, LIGHT_COMPONENT_ID );
            lights_query = se->create_query ( &query_params );
        }

        se->resolve_pending_queries();
        const se_query_result_t* lights_query_result = se->get_query_result ( lights_query );
        uint64_t light_count = lights_query_result->count;
        std_assert_m ( light_count <= MAX_LIGHTS_COUNT );

        cbuffer.light_count = light_count;

        for ( uint64_t i = 0; i < light_count; ++i ) {
            se_entity_h entity = lights_query_result->entities[i];
            se_component_h component = se->get_component ( entity, LIGHT_COMPONENT_ID );
            std_auto_m light_component = ( viewapp_light_component_t* ) component;

            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, light_component->view );

            cbuffer.lights[i].pos[0] = light_component->position[0];
            cbuffer.lights[i].pos[1] = light_component->position[1];
            cbuffer.lights[i].pos[2] = light_component->position[2];
            cbuffer.lights[i].intensity = light_component->intensity;
            cbuffer.lights[i].color[0] = light_component->color[0];
            cbuffer.lights[i].color[1] = light_component->color[1];
            cbuffer.lights[i].color[2] = light_component->color[2];
            cbuffer.lights[i].proj_from_view = view_info.proj_matrix;
            cbuffer.lights[i].view_from_world = view_info.view_matrix;
        }

        se->dispose_query_results();
    }

    xg_buffer_range_t cbuffer_range = xg->write_workload_uniform ( node_args->workload, &cbuffer, sizeof ( cbuffer ) );

    {
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( args->pipeline_state );
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        xg_texture_resource_binding_t textures[5];
        textures[0] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 );
        textures[1] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[1], 1 );
        textures[2] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[2], 2 );
        textures[3] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[3], 3 );
        textures[4] = xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[4], 4 );

        xg_sampler_resource_binding_t sampler;
        sampler.sampler = args->sampler;
        sampler.shader_register = 5;

        xg_buffer_resource_binding_t buffer;
        buffer.shader_register = 6;
        buffer.type = xg_buffer_binding_type_uniform_m;
        buffer.range = cbuffer_range;

        xg_pipeline_resource_bindings_t bindings = xg_default_pipeline_resource_bindings_m;
        bindings.set = xg_resource_binding_set_per_draw_m;
        bindings.texture_count = 5;
        bindings.sampler_count = 1;
        bindings.buffer_count = 1;
        bindings.textures = textures;
        bindings.samplers = &sampler;
        bindings.buffers = &buffer;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, key );

        xg_viewport_state_t viewport = xg_default_viewport_state_m;
        viewport.width = args->width;
        viewport.height = args->height;
        xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

        xg->cmd_draw ( cmd_buffer, 3, 0, key );
    }
}

xf_node_h add_lighting_pass ( xf_graph_h graph, xf_texture_h target, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth, xf_texture_h shadows ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    lighting_pass_args_t args;
    args.pipeline_state = xs->lookup_pipeline_state ( "lighting" );
    args.sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m );
    args.width = color_info.width;
    args.height = color_info.height;

    xf_node_h lighting_node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.render_targets[params.render_targets_count++] = xf_render_target_dependency_m ( target, xg_default_texture_view_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( color, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( normal, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( material, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( depth, xg_shading_stage_fragment_m );
        params.shader_texture_reads[params.shader_texture_reads_count++] = xf_sampled_texture_dependency_m ( shadows, xg_shading_stage_fragment_m );
        params.execute_routine = lighting_pass;
        params.user_args = &args;
        params.user_args_allocator = std_virtual_heap_allocator();
        params.user_args_alloc_size = sizeof ( args );
        std_str_copy_m ( params.debug_name, "lighting" );
        params.passthrough.enable = true;
        params.passthrough.render_targets[0].mode = xf_node_passthrough_mode_alias_m;
        params.passthrough.render_targets[0].alias = color;
        lighting_node = xf->create_node ( graph, &params );
    }

    std_module_release ( xg );
    std_module_release ( xs );
    std_module_release ( xf );

    return lighting_node;
}
