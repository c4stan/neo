#include <std_main.h>
#include <std_log.h>

#include <se.h>

std_warnings_ignore_m ( "-Wunused-function" )
std_warnings_ignore_m ( "-Wunused-macros" )
std_warnings_ignore_m ( "-Wunused-variable" )

#define se_test_component_0_m 0
#define se_test_component_1_m 1
#define se_test_component_2_m 2
#define se_test_component_3_m 3

static void run_se_test ( void ) {
    se_i* se = std_module_get_m ( se_module_name_m );
    std_assert_m ( se );

    for ( size_t i = 0; i < 32; ++i ) {
        se_entity_params_t create_params;
        create_params.max_component_count = 8;
        se_entity_h e1 = se->create_entity ( &create_params );
        se_entity_h e2 = se->create_entity ( &create_params );

        se_component_h c1 = 1;
        se->add_component ( e1, se_test_component_0_m, c1 );

        se_component_h c2 = 2;
        se->add_component ( e2, se_test_component_1_m, c2 );

        se_query_params_t query_params;
        std_mem_zero_m ( &query_params );
        std_bitset_set ( query_params.request_component_flags, se_test_component_0_m );
        se_query_h q1 = se->create_query ( &query_params );

        std_mem_zero_m ( &query_params );
        std_bitset_set ( query_params.request_component_flags, se_test_component_1_m );
        se_query_h q2 = se->create_query ( &query_params );

        se->resolve_pending_queries();

        const se_query_result_t* r1 = se->get_query_result ( q1 );
        std_assert_m ( r1->count == 1 );
        std_assert_m ( r1->entities[0] == e1 );

        const se_query_result_t* r2 = se->get_query_result ( q2 );
        std_assert_m ( r2->count == 1 );
        std_assert_m ( r2->entities[0] == e2 );

        se_component_h g1 = se->get_component ( r1->entities[0], se_test_component_0_m );
        std_assert_m ( g1 == c1 );

        se_component_h g2 = se->get_component ( r2->entities[0], se_test_component_1_m );
        std_assert_m ( g2 == c2 );

        se->dispose_query_results();

        se->destroy_entity ( e1 );
        se->destroy_entity ( e2 );
    }
}

#include <xf.h>
#include <xs.h>

#include <std_time.h>

#include <math.h>
#include <stdlib.h>

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
        xg->cmd_clear_texture ( cmd_buffer, node_args->io->copy_texture_writes[0].texture, color_clear, key++ );
    }
}

typedef struct {
    xg_graphics_pipeline_state_h pipeline_state;
    xg_buffer_h vertex_cbuffer;
} se_test_pass_component_t;

#define se_test_pass_component_m 0

static void se_test_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // Bind swapchain texture as render target
    {
        xg_color_clear_t color_clear;
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        xg_render_textures_binding_t render_textures;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        render_textures.depth_stencil.texture = xg_null_handle_m;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    se_i* se = std_module_get_m ( se_module_name_m );

    // Query for entities having a test pass component
    se_query_params_t query_params;
    std_mem_zero_m ( &query_params );
    std_bitset_set ( query_params.request_component_flags, se_test_pass_component_m );
    se_query_h query = se->create_query ( &query_params );
    se->resolve_pending_queries();
    const se_query_result_t* query_result = se->get_query_result ( query );

    for ( uint64_t i = 0; i < query_result->count; ++i ) {
        se_entity_h entity = query_result->entities[i];
        se_component_h component = se->get_component ( entity, se_test_pass_component_m );
        se_test_pass_component_t* test_pass_component = ( se_test_pass_component_t* ) component;

        // Set pipeline
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, test_pass_component->pipeline_state, key );

        // Bind resources
        {
            xg_buffer_resource_binding_t buffer;
            buffer.shader_register = 0;
            buffer.type = xg_buffer_binding_type_uniform_m;
            buffer.range.handle = test_pass_component->vertex_cbuffer;
            buffer.range.offset = 0;
            buffer.range.size = 12; // TODO ...

            xg_pipeline_resource_bindings_t bindings = xg_default_pipeline_resource_bindings_m;
            bindings.set = xg_resource_binding_set_per_draw_m;
            bindings.buffer_count = 1;
            bindings.buffers = &buffer;

#if 0
            xg_resource_binding_t bindings;
            bindings.shader_register = 0;
            bindings.stages = xg_shading_stage_bit_vertex_m;
            bindings.type = xg_resource_binding_buffer_uniform_m;
            bindings.resource = test_pass_component->vertex_cbuffer;
            xg->cmd_set_pipeline_resources ( cmd_buffer, xg_resource_binding_set_per_draw_m, &bindings, 1, key );
#else
            xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, key );
#endif
        }

        // Draw
        xg->cmd_draw ( cmd_buffer, 3, 0, key );
    }

    se->dispose_query_results();
}

static void run_se_test_2 ( void ) {
    wm_i* wm = std_module_get_m ( wm_module_name_m );
    std_assert_m ( wm );

    wm_window_params_t window_params = { .name = "se_test", .x = 0, .y = 0, .width = 600, .height = 400, .gain_focus = true, .borderless = false };
    wm_window_h window = wm->create_window ( &window_params );
    std_log_info_m ( "Creating window "std_fmt_str_m std_fmt_newline_m, window_params.name );

    xg_device_h device;
    xg_swapchain_h swapchain;
    xg_i* xg = std_module_get_m ( xg_module_name_m );
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
            .present_mode = xg_present_mode_mailbox_m,
            .debug_name = "swapchain",
        );
        swapchain = xg->create_window_swapchain ( &swapchain_params );
        std_assert_m ( swapchain != xg_null_handle_m );
    }

    xg_graphics_pipeline_state_h pipeline_state;
    xs_i* xs = std_module_get_m ( xs_module_name_m );
    {
        xs->add_database_folder ( "shader/" );
        xs->set_output_folder ( "output/shader/" );

        xs_database_build_params_t build_params;
        build_params.viewport_width = 600;
        build_params.viewport_height = 400;
        xs_database_build_result_t result = xs->build_database_shaders ( device, &build_params );
        std_log_info_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders built", result.successful_pipeline_states, result.successful_shaders );

        if ( result.failed_shaders || result.failed_pipeline_states ) {
            std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed" );
        }

        const char* pipeline_name = "main";
        xs_string_hash_t pipeline_hash = xs_hash_string_m ( pipeline_name, std_str_len ( pipeline_name ) );
        pipeline_state = xs->get_pipeline_state ( xs->lookup_pipeline_state_hash ( pipeline_hash ) );
        std_assert_m ( pipeline_state != xg_null_handle_m );
    }

    xf_i* xf = std_module_get_m ( xf_module_name_m );
    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );
    xf_graph_h graph = xf->create_graph ( device, swapchain );
    {
        xf_node_h clear_pass;
        {
            xf_node_params_t node_params = xf_default_node_params_m;
            node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m );
            node_params.execute_routine = se_clear_pass;
            node_params.user_args = std_null_buffer_m;
            std_str_copy_m ( node_params.debug_name, "clear_pass" );

            clear_pass = xf->create_node ( graph, &node_params );
        }

        xf_node_h test_pass;
        {
            xf_node_params_t node_params = xf_default_node_params_m;
            node_params.render_targets[node_params.render_targets_count++] = xf_render_target_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m );
            node_params.presentable_texture = swapchain_multi_texture;
            node_params.execute_routine = se_test_pass;
            node_params.user_args = std_null_buffer_m;
            std_str_copy_m ( node_params.debug_name, "se_pass" );

            test_pass = xf->create_node ( graph, &node_params );
        }
    }

    se_i* se = std_module_get_m ( se_module_name_m );

    se_test_pass_component_t component_data[16];
    srand ( ( unsigned int ) std_tick_now() );

    for ( uint64_t i = 0; i < std_static_array_capacity_m ( component_data ); ++i ) {
        xg_buffer_h vertex_cbuffer;
        {
            typedef struct {
                float pos_x;
                float pos_y;
                float scale;
            } vertex_cbuffer_t;

            xg_workload_h workload = xg->create_workload ( device );
            xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
            xg_buffer_params_t buffer_params = xg_buffer_params_m (
                .allocator = xg->get_default_allocator ( device, xg_memory_type_gpu_mappable_m ),
                .device = device,
                .size = sizeof ( vertex_cbuffer_t ),
                .allowed_usage = xg_buffer_usage_uniform_m,
                .debug_name = "component_cbuffer",
            );
            vertex_cbuffer = xg->cmd_create_buffer ( resource_cmd_buffer, &buffer_params );
            xg->submit_workload ( workload );

            xg_buffer_info_t cbuffer_info;
            xg->get_buffer_info ( &cbuffer_info, vertex_cbuffer );
            vertex_cbuffer_t* cbuffer_data = ( vertex_cbuffer_t* ) cbuffer_info.allocation.mapped_address;
            cbuffer_data->pos_x = ( float ) ( rand() ) / ( float ) ( RAND_MAX ) * 2.f - 1.f;
            cbuffer_data->pos_y = ( float ) ( rand() ) / ( float ) ( RAND_MAX ) * 2.f - 1.f;
            cbuffer_data->scale = 0.2f;
        }

        se_entity_params_t create_params;
        create_params.max_component_count = 8;
        se_entity_h entity = se->create_entity ( &create_params );

        component_data[i].pipeline_state = pipeline_state;
        component_data[i].vertex_cbuffer = vertex_cbuffer;
        se->add_component ( entity, se_test_pass_component_m, ( se_component_h ) &component_data[i] );
    }

    xf->debug_print_graph ( graph );

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

        xg_workload_h workload = xg->create_workload ( device );

        xg->acquire_next_swapchain_texture ( swapchain, workload );

        xf->execute_graph ( graph, workload );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );

        xf->advance_multi_texture ( swapchain_multi_texture );
    }
}

void std_main ( void ) {
    run_se_test_2();
    std_log_info_m ( "se_test_m COMPLETE!" );
}
