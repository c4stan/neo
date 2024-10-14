#include <lighting_pass.h>

#include <std_string.h>
#include <std_log.h>

#include <viewapp_state.h>

#include <xs.h>
#include <se.h>
#include <se.inl>

typedef struct {
    xs_database_pipeline_h pipeline_state;
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

    xg_render_textures_binding_t render_textures = xg_render_textures_binding_m (
        .render_targets_count = 1,
        .render_targets = { xf_render_target_binding_m ( node_args->io->render_targets[0] ) },
    );
    xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );

    draw_cbuffer_t cbuffer;
    std_mem_zero_m ( &cbuffer );

    se_query_result_t light_query_result;
    se->query_entities ( &light_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_light_component_id_m } ) );
    uint64_t light_count = light_query_result.entity_count;
    se_component_iterator_t light_iterator = se_component_iterator_m ( &light_query_result.components[0], 0 );
    std_assert_m ( light_count <= MAX_LIGHTS_COUNT );

    cbuffer.light_count = light_count;

    for ( uint64_t i = 0; i < light_count; ++i ) {
        viewapp_light_component_t* light_component = se_component_iterator_next ( &light_iterator );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, light_component->view );

        cbuffer.lights[i] = ( draw_cbuffer_light_t ) {
            .pos =  {
                light_component->position[0],
                light_component->position[1],
                light_component->position[2],
            },
            .intensity = light_component->intensity,
            .color = { 
                light_component->color[0],
                light_component->color[1],
                light_component->color[2],
            },
            .proj_from_view = view_info.proj_matrix,
            .view_from_world = view_info.view_matrix,
        };
    }

    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( args->pipeline_state );
    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

    xg_pipeline_resource_bindings_t bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_draw_m,
        .texture_count = 5,
        .textures = {
            xf_shader_texture_binding_m ( node_args->io->sampled_textures[0], 0 ),
            xf_shader_texture_binding_m ( node_args->io->sampled_textures[1], 1 ),
            xf_shader_texture_binding_m ( node_args->io->sampled_textures[2], 2 ),
            xf_shader_texture_binding_m ( node_args->io->sampled_textures[3], 3 ),
            xf_shader_texture_binding_m ( node_args->io->sampled_textures[4], 4 ),
        },
        .sampler_count = 1,
        .samplers = { xg_sampler_resource_binding_m ( .sampler = args->sampler, .shader_register = 5 ) },
        .buffer_count = 1,
        .buffers = { xg_buffer_resource_binding_m ( 
            .shader_register = 6,
            .type = xg_buffer_binding_type_uniform_m,
            .range = xg->write_workload_uniform ( node_args->workload, &cbuffer, sizeof ( cbuffer ) ),
        ) }
    );

    xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, key );

    xg_viewport_state_t viewport = xg_viewport_state_m (
        .width = args->width,
        .height = args->height,
    );
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xg->cmd_draw ( cmd_buffer, 3, 0, key );
}

xf_node_h add_lighting_pass ( xf_graph_h graph, xf_texture_h target, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth, xf_texture_h shadows ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    lighting_pass_args_t args = {
        .pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "lighting" ) ),
        .sampler = xg->get_default_sampler ( graph_info.device, xg_default_sampler_point_clamp_m ),
        .width = color_info.width,
        .height = color_info.height,
    };

    xf_node_h lighting_node = xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "lighting",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = lighting_pass,
            .user_args = std_buffer_m ( &args ),
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = target ) },
            .sampled_textures_count = 5,
            .sampled_textures = { 
                xf_fragment_texture_dependency_m ( .texture = color ),
                xf_fragment_texture_dependency_m ( .texture = normal ),
                xf_fragment_texture_dependency_m ( .texture = material ),
                xf_fragment_texture_dependency_m ( .texture = depth ),
                xf_fragment_texture_dependency_m ( .texture = shadows ),
            },
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_texture_passthrough_m ( .mode = xf_passthrough_mode_alias_m, .alias = color ) },
        )
    ) );

    return lighting_node;
}
