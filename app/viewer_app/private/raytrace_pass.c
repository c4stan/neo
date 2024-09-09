#include <shadow_pass.h>

#include <viewapp_state.h>

typedef struct {
    xs_database_pipeline_h pipeline;
    xg_raytrace_world_h world;
    xg_texture_h target;
    uint32_t width;
    uint32_t height;
} raytrace_pass_args_t;

std_unused_static_m()
static void raytrace_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    std_auto_m pass_args = ( raytrace_pass_args_t* ) user_args;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    xg->cmd_set_raytrace_pipeline_state ( cmd_buffer, xs->get_pipeline_state ( pass_args->pipeline ), key );

    xg_pipeline_resource_bindings_t draw_bindings = xg_pipeline_resource_bindings_m (
        .set = xg_resource_binding_set_per_draw_m,
        .raytrace_world_count = 1,
        .raytrace_worlds = { xg_raytrace_world_resource_binding_m (
            .shader_register = 0,
            .world = pass_args->world,
        )},
        .texture_count = 1,
        .textures = {  
            xf_shader_texture_binding_m ( node_args->io->shader_texture_writes[0], 1 ),
        },
    );

    xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

    xg->cmd_trace_rays ( cmd_buffer, pass_args->width, pass_args->height, 1, key );
}

xf_node_h add_raytrace_pass ( xf_graph_h graph, xf_texture_h target ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    xf_texture_info_t target_info;
    xf->get_texture_info ( &target_info, target );

    raytrace_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "raytrace" ) ),
        .world = state->render.raytrace_world,
        .target = target,
        .width = target_info.width,
        .height = target_info.height,
    };

    xf_node_params_t node_params = xf_node_params_m (
        .shader_texture_writes_count = 1,
        .shader_texture_writes = { xf_storage_texture_dependency_m ( target, xg_default_texture_view_m, xg_pipeline_stage_bit_fragment_shader_m ) },
        .execute_routine = raytrace_pass,
        .user_args = std_buffer_m ( &args ),
        .debug_name = "raytrace",
        .passthrough = {
            .enable = false
        }
    );
    xf_node_h raytrace_node = xf->create_node ( graph, &node_params );
    return raytrace_node;
}
