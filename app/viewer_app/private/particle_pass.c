#include <particle_pass.h>

#include <viewapp_state.h>

typedef struct {
    xs_database_pipeline_h pipeline;
    uint32_t particle_count;
} rainfall_spawn_pass_data_t;

#if 0
static void rainfall_spawn_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;
    std_auto_m pass_data = ( rainfall_spawn_pass_data_t* ) user_args;

    uint32_t spawn_count = 32;

    viewapp_state_t* state = viewapp_state_get();

    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    struct {
        uint32_t particle_count;
        uint32_t spawn_count;
    } cbuffer_data;
    cbuffer_data.particle_count = pass_data->particle_count;
    cbuffer_data.spawn_count = spawn_count;
    xg_buffer_range_t cbuffer_range = xg->write_workload_uniform ( node_args->workload, &cbuffer_data, sizeof ( cbuffer_data ) );

    xg->cmd_set_compute_pipeline_state ( cmd_buffer, xs->get_pipeline_state ( pass_data->pipeline ), key );

    xg->cmd_set_pipeline_resources ( cmd_buffer,
        &xg_pipeline_resource_bindings_m(),
        key );

    xg->cmd_dispatch_compute ( cmd_buffer, std_div_ceil_u32 ( spawn_count, 64 ), 1, 1, key );

    pass_data->particle_count += spawn_count;
}

xf_node_h add_particle_rainfall_spawn_pass ( xf_graph_h graph, xf_buffer_h particles ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    xf_node_h node = xf->create_node (
        graph,
        &xf_node_params_m (
            .execute_routine = rainfall_spawn_pass,
            .shader_buffer_writes = {
                xf_shader_buffer_dependency_m ( .buffer = particles, .shading_stage = xg_shading_stage_compute_m )
            },
        )
    );
}

//xf_node_h add_particle_rainfall_sim_pass ( xf_graph_h graph, xf_buffer_h particles ) {
//
//}

void particle_rainfall_render_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {

}

xf_node_h add_particle_rainfall_render_pass ( xf_graph_h graph, xf_texture_h color, xf_buffer_h particles ) {

}
#endif
