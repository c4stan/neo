#include <std_main.h>
#include <std_log.h>

#include <se.h>
#include <fs.h>

std_warnings_ignore_m ( "-Wunused-function" )
std_warnings_ignore_m ( "-Wunused-variable" )

#define se_test_component_0_m 0
#define se_test_component_1_m 1

typedef struct {
    uint64_t u64;
} se_test_component_0_t;

typedef struct {
    uint64_t u64;
} se_test_component_1_t;

static void run_se_test_1 ( void ) {
    se_i* se = std_module_load_m ( se_module_name_m );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 2,
        .components = {
            se_component_layout_m (
                .id = se_test_component_0_m,
                .stream_count = 1,
                .streams = { sizeof ( se_component_h ) }
            ),
            se_component_layout_m(
                .id = se_test_component_1_m,
                .stream_count = 1,
                .streams = { sizeof ( se_component_h ) }
            )
        }
    ) );

    se->set_component_properties ( se_test_component_0_m, "Test component 0", &se_component_properties_params_m (
        .count = 1,
        .properties = {
            se_field_property_m ( 0, se_test_component_0_t, u64, se_property_u64_m ),
        }
    ) );
    
    se_component_h c0 = 1;
    se_component_h c1 = 2;

    se->create_entity ( &se_entity_params_m (
        .debug_name = "entity",
        .update = se_entity_update_m (
            .component_count = 2,
            .components = { 
                se_component_update_m ( 
                    .id = se_test_component_0_m,
                    .streams = { se_stream_update_m (
                        .data = &c0
                    ) }
                ),
                se_component_update_m (
                    .id = se_test_component_1_m,
                    .streams = { se_stream_update_m (
                        .data = &c1
                    ) }
                )
            }
        )
    ) );


    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m (
        .component_count = 2,
        .components = { se_test_component_0_m, se_test_component_1_m }
    ) );

    se_stream_iterator_t c0_it = se_component_iterator_m ( &query_result.components[0], 0 );
    se_stream_iterator_t c1_it = se_component_iterator_m ( &query_result.components[1], 0 );

    for ( uint32_t i = 0; i < query_result.entity_count; ++i ) {
        uint64_t* c0_i = ( uint64_t* ) se_stream_iterator_next ( &c0_it );
        uint64_t* c1_i = ( uint64_t* ) se_stream_iterator_next ( &c1_it );

        std_assert_m ( *c0_i == c0 );
        std_assert_m ( *c1_i == c1 );
    }

    std_module_unload_m ( se_module_name_m );
}

#include <xf.h>
#include <xs.h>

#include <std_time.h>

#include <math.h>
#include <stdlib.h>

#if 1
static void se_clear_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    // TODO pass this with node_args?
    //      it is possible that the right solution is to have separate APIs in xg for command buffer recording vs the rest,
    //      and here is passed the cmd buffer API instead of the whole xg api
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    {
        xg_color_clear_t color_clear;
        uint64_t t = 500;
        color_clear.f32[0] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        color_clear.f32[1] = ( float ) ( ( sin ( std_tick_to_milli_f64 ( std_tick_now() / t ) + 3.14f ) + 1 ) / 2.f );
        color_clear.f32[2] = ( float ) ( ( cos ( std_tick_to_milli_f64 ( std_tick_now() / t ) ) + 1 ) / 2.f );
        xg->cmd_clear_texture ( cmd_buffer, key++, node_args->io->copy_texture_writes[0].texture, color_clear );
    }
}

typedef struct {
    float pos_x;
    float pos_y;
    float scale;
} vertex_cbuffer_data_t;

typedef struct {
    xs_database_pipeline_h pipeline_state;
    float vel_x;
    float vel_y;
    vertex_cbuffer_data_t vertex_cbuffer_data;
} se_test_pass_component_t;

#define se_test_pass_component_m 0

static void se_test_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );

    xg->cmd_begin_renderpass ( cmd_buffer, key, &xg_cmd_renderpass_params_m (
        .renderpass = node_args->renderpass,
        .render_targets_count = 1,
        .render_targets = xf_render_target_binding_m ( node_args->io->render_targets[0] )
    ) );

    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m (
        .component_count = 1,
        .components = { se_test_pass_component_m }
    ) );

    se_stream_iterator_t it = se_component_iterator_m ( &query_result.components[0], 0 );

    for ( uint64_t i = 0; i < query_result.entity_count; ++i ) {
        se_test_pass_component_t* test_pass_component = ( se_test_pass_component_t* ) se_stream_iterator_next ( &it );

        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( test_pass_component->pipeline_state );

        xg_buffer_range_t vert_uniform_range = xg->write_workload_uniform ( node_args->workload, &test_pass_component->vertex_cbuffer_data, sizeof ( vertex_cbuffer_data_t ) );
        xg_buffer_resource_binding_t buffer = xg_buffer_resource_binding_m ( .shader_register = 0, .range = vert_uniform_range );

        xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( node_args->resource_cmd_buffer, &xg_resource_bindings_params_m ( 
            .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 1,
                .buffers = { buffer }
            )
        ) );

        // Draw
        xg->cmd_draw ( cmd_buffer, key, &xg_cmd_draw_params_m (
            .pipeline = pipeline_state,
            .primitive_count = 1,
            .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings
        ) );
    }

    xg->cmd_end_renderpass ( cmd_buffer, key );
}

static void update_entities ( float t ) {
    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( .component_count = 1, .components = { se_test_pass_component_m } ) );
    se_stream_iterator_t iterator = se_component_iterator_m ( &query_result.components[0], 0 );

    for ( uint32_t i = 0; i < query_result.entity_count; ++i ) {
        se_test_pass_component_t* component = se_stream_iterator_next ( &iterator );
        float o = ( std_hash_32_m ( i ) / ( (float)UINT32_MAX ) );
        float oscillation_x = sinf ( t * 0.001 + o * 10 ) * 0.002;
        float oscillation_y = cosf ( t * 0.001 + o * 10 ) * 0.002;
        component->vertex_cbuffer_data.pos_x += oscillation_x;
        component->vertex_cbuffer_data.pos_y += oscillation_y;
    }
}

static void run_se_test_2 ( void ) {
    wm_i* wm = std_module_load_m ( wm_module_name_m );
    xg_i* xg = std_module_load_m ( xg_module_name_m );
    xs_i* xs = std_module_load_m ( xs_module_name_m );
    xf_i* xf = std_module_load_m ( xf_module_name_m );
    se_i* se = std_module_load_m ( se_module_name_m );
    std_module_load_m ( fs_module_name_m );

    wm_window_params_t window_params = { .name = "se_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
    wm_window_h window = wm->create_window ( &window_params );
    std_log_info_m ( "Creating window "std_fmt_str_m std_fmt_newline_m, window_params.name );

    xg_device_h device;
    xg_swapchain_h swapchain;
    {
        size_t device_count = xg->get_devices_count();
        std_assert_m ( device_count > 0 );
        xg_device_h devices[16];
        xg->get_devices ( devices, 16 );
        device = devices[0];
        bool activate_result = xg->activate_device ( device );
        std_assert_m ( activate_result );
        xg_device_info_t device_info;
        xg->get_device_info ( &device_info, device );
        std_log_info_m ( "Picking device 0 (" std_fmt_str_m ") as default device", device_info.name );

        xg_swapchain_window_params_t swapchain_params = xg_swapchain_window_params_m (
            .window = window,
            .device = device,
            .texture_count = 3,
            .format = xg_format_b8g8r8a8_unorm_m,//xg_format_b8g8r8a8_srgb_m;
            .color_space = xg_colorspace_srgb_m,
            .present_mode = xg_present_mode_fifo_m,
            .debug_name = "swapchain",
        );
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "se_test_sdb" ) );
    xs->add_database_folder ( sdb, "shader/" );
    xs->set_output_folder ( sdb, "output/shader/" );
    xs->build_database ( sdb );

    xs_database_pipeline_h pipeline_state = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "triangle") );
    std_assert_m ( pipeline_state != xg_null_handle_m );

    xf_texture_h swapchain_multi_texture = xf->create_multi_texture_from_swapchain ( swapchain );
    xf_graph_h graph = xf->create_graph( &xf_graph_params_m ( .device = device, .debug_name = "se_test" ) );
    
    xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "clear",
        .type = xf_node_type_clear_pass_m,
        .pass.clear = xf_node_clear_pass_params_m (
            .textures = { xf_texture_clear_m ( .color = xg_color_clear_m ( .f32 = { 0, 0, 1, 1 } ) ) }
        ),
        .resources = xf_node_resource_params_m (
            .copy_texture_writes_count = 1,
            .copy_texture_writes = { xf_copy_texture_dependency_m ( .texture = swapchain_multi_texture ) },
        )
    ) );

    xf->add_node ( graph, &xf_node_params_m (
        .debug_name = "se_pass",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = se_test_pass,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 1,
            .render_targets = { xf_render_target_dependency_m ( .texture = swapchain_multi_texture ) },
            .presentable_texture = swapchain_multi_texture,
        ),
    ) );

    se->create_entity_family ( &se_entity_family_params_m ( 
        .component_count = 1,
        .components = { se_component_layout_m ( 
            .id = se_test_pass_component_m, 
            .stream_count = 1, 
            .streams = { sizeof ( se_test_pass_component_t ) } 
        ) }
    ) );

    se->set_component_properties ( se_test_pass_component_m, "Test pass component", &se_component_properties_params_m (
        .count = 2,
        .properties = {
            se_field_property_m ( 0, se_test_pass_component_t, pipeline_state, se_property_u64_m ),
            se_field_property_m ( 0, se_test_pass_component_t, vertex_cbuffer_data, se_property_3f32_m ),
        }
    ) );

    srand ( ( unsigned int ) std_tick_now() );

    for ( uint64_t i = 0; i < 1024*2; ++i ) {
        se_test_pass_component_t component_data;
        component_data.vertex_cbuffer_data.pos_x = ( float ) ( rand() ) / ( float ) ( RAND_MAX ) * 2.f - 1.f;
        component_data.vertex_cbuffer_data.pos_y = ( float ) ( rand() ) / ( float ) ( RAND_MAX ) * 2.f - 1.f;
        component_data.vertex_cbuffer_data.scale = 0.05f;
        component_data.pipeline_state = pipeline_state;

        se->create_entity ( &se_entity_params_m (
            .debug_name = "entity",
            .update = se_entity_update_m (
                .components = { se_component_update_m ( 
                    .id = se_test_pass_component_m, 
                    .streams = { se_stream_update_m ( 
                        .data = &component_data 
                    ) } 
                ) }
            )
        ) );
    }    

    xf->debug_print_graph ( graph );

    std_tick_t begin = std_tick_now();

    while ( true ) {
        wm->update_window ( window );

        if ( !wm->is_window_alive ( window ) ) {
            break;
        }

        wm_input_state_t input_state;
        wm->get_window_input_state ( window, &input_state );

        if ( input_state.keyboard[wm_keyboard_state_esc_m] ) {
            break;
        }

        std_tick_t now = std_tick_now();
        float t = std_tick_to_milli_f32 ( now - begin );
        update_entities ( t );

        xg_workload_h workload = xg->create_workload ( device );

        xf->execute_graph ( graph, workload, 0 );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );
    }

    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );
    std_module_unload_m ( se_module_name_m );
}
#endif

void std_main ( void ) {
    run_se_test_2();
    std_log_info_m ( "se_test_m COMPLETE!" );
}
