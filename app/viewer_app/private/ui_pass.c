#include <ui_pass.h>

#include <xs.h>

#include <std_log.h>
#include <std_string.h>

typedef struct {
    xg_device_h device;
    uint64_t resolution_x;
    uint64_t resolution_y;
    xg_format_e render_target_format;
} ui_pass_args_t;

// TODO avoid this
static xi_workload_h xi_workload;

void set_ui_pass_xi_workload ( xi_workload_h workload ) {
    xi_workload = workload;
}

static void ui_pass_routine ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    ui_pass_args_t* args = ( ui_pass_args_t* ) user_args;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // Bind swapchain texture as render target
    {
        xg_render_textures_binding_t render_textures;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        render_textures.depth_stencil.texture = xg_null_handle_m;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    xi_i* xi = std_module_get_m ( xi_module_name_m );
    {
        xi_flush_params_t params;
        params.device = args->device;
        params.workload = node_args->workload;
        params.cmd_buffer = cmd_buffer;
        params.resource_cmd_buffer = resource_cmd_buffer;
        params.key = key;
        params.render_target_format = args->render_target_format;
        params.viewport.width = args->resolution_x;
        params.viewport.height = args->resolution_y;

        xi->flush_workload ( xi_workload, &params );
    }
}

xf_node_h add_ui_pass ( xf_graph_h graph, xf_texture_h color ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    ui_pass_args_t args;
    {
        args.device = graph_info.device;
        args.resolution_x = color_info.width;
        args.resolution_y = color_info.height;
        args.render_target_format = color_info.format;
    }

    xf_node_h ui_node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.render_targets[params.render_targets_count++] = xf_render_target_dependency_m ( color, xg_default_texture_view_m );
        params.execute_routine = ui_pass_routine;
        std_str_copy_m ( params.debug_name, "ui" );
        params.debug_color = xg_debug_region_color_green_m;
        params.key_space_size = 128;
        params.user_args = std_buffer_m ( &args );

        ui_node = xf->create_node ( graph, &params );
    }

    return ui_node;
}
