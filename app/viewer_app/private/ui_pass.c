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

    xi_i* xi = std_module_get_m ( xi_module_name_m );
    xi_flush_params_t params = {
        .device = args->device,
        .workload = node_args->workload,
        .cmd_buffer = cmd_buffer,
        .resource_cmd_buffer = resource_cmd_buffer,
        .key = key,
        .render_target_format = args->render_target_format,
        .viewport.width = args->resolution_x,
        .viewport.height = args->resolution_y,
        .render_target_binding = xf_render_target_binding_m ( node_args->io->render_targets[0] )
    };
    xi->flush_workload ( xi_workload, &params );
}

xf_node_h add_ui_pass ( xf_graph_h graph, xf_texture_h color, xf_texture_h export ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_graph_info_t graph_info;
    xf->get_graph_info ( &graph_info, graph );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    ui_pass_args_t args = {
        .device = graph_info.device,
        .resolution_x = color_info.width,
        .resolution_y = color_info.height,
        .render_target_format = color_info.format,
    };

    xf_node_params_t params = xf_node_params_m (
        .debug_name = "ui",
        .debug_color = xg_debug_region_color_green_m,
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = ui_pass_routine,
            .key_space_size = 128,
            .user_args = std_buffer_m ( &args ),
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = color ) },
        ),
    );

    if ( export != xf_null_handle_m ) {
        params.resources.sampled_textures_count = 1;
        params.resources.sampled_textures[0] = xf_shader_texture_dependency_m ( .texture = export, .stage = xg_pipeline_stage_bit_fragment_shader_m );
    }

    xf_node_h ui_node = xf->add_node ( graph, &params );
    return ui_node;
}
