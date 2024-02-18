#include <lighting_pass.h>

#include <xs.h>

#include <std_string.h>
#include <std_log.h>

#include <viewapp_state.h>

#if 0
typedef struct {
    xs_pipeline_state_h pipeline_state;
} depth_pass_args_t;

static void depth_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {

}
#endif

static void clear_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;

    {
        xf_copy_texture_resource_t depth_stencil = node_args->io->copy_texture_writes[0];
        xg_depth_stencil_clear_t ds_clear;
        ds_clear.depth = 1;
        ds_clear.stencil = 0;
        xg->cmd_clear_depth_stencil_texture ( cmd_buffer, depth_stencil.texture, ds_clear, key );
    }
}

xf_node_h add_depth_clear_pass ( xf_graph_h graph, xf_texture_h depth, const char* name ) {
    viewapp_state_t* state = viewapp_state_get();
    xf_i* xf = state->modules.xf;

    xf_node_h node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.copy_texture_writes[params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( depth, xg_default_texture_view_m );
        params.execute_routine = clear_pass;
        std_str_copy_m ( params.debug_name, name );
        node = xf->create_node ( graph, &params );
    }

    return node;
}

#if 0
xf_node_h add_depth_pass ( xf_graph_h graph, xf_texture_h depth ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    depth_pass_args_t args;
    {
        const char* pipeline_name = "depth";
        xs_string_hash_t pipeline_hash = xs_hash_string_m ( pipeline_name, std_str_len ( pipeline_name ) );
        xs_pipeline_state_h pipeline_state = xs->lookup_pipeline_state ( pipeline_hash );
        std_assert_m ( pipeline_state != xs_null_handle_m );

        args.pipeline_state = pipeline_state;
    }

    xf_node_h depth_node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.depth_stencil_target = depth;
        params.execute_routine = depth_pass;
        params.user_args = &args;
        params.user_args_allocator = std_virtual_heap_allocator();
        params.user_args_alloc_size = sizeof ( args );
        std_str_copy_m ( params.debug_name, "depth" );
        depth_node = xf->create_node ( graph, &params );
    }

    std_module_release ( xg );
    std_module_release ( xs );
    std_module_release ( xf );

    return depth_node;
}
#endif
