#include <std_main.h>
#include <std_log.h>

#include <se.h>
#include <fs.h>

std_warnings_ignore_m ( "-Wunused-function" )
std_warnings_ignore_m ( "-Wunused-macros" )
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

    std_virtual_stack_t stack = std_virtual_stack_create ( 1024 * 16 );
    se_entity_params_allocator_t allocator = se_entity_params_allocator ( &stack );

    se_entity_h entity = se->create_entity();

    se_entity_params_alloc_entity ( &allocator, entity );
    se_entity_params_alloc_monostream_component_inline ( &allocator, se_test_component_0_m, &c0, sizeof ( se_component_h ) );
    se_entity_params_alloc_monostream_component_inline ( &allocator, se_test_component_1_m, &c1, sizeof ( se_component_h ) );

#if 0
    se_entity_params_t entity_params = se_entity_params_m (
        //.update = {
        .component_count = 2,
        .components = { 
            se_component_update_monostream_m ( se_test_component_0_m, &c0 ),
            se_component_update_monostream_m ( se_test_component_1_m, &c1 ),
        }
        //}
    );
    //std_bitset_set ( entity_params.mask.u64, se_test_component_0_m );
    //std_bitset_set ( entity_params.mask.u64, se_test_component_1_m );
#endif

    se->init_entities ( allocator.entities );

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m (
        .component_count = 2,
        .components = { se_test_component_0_m, se_test_component_1_m }
    ) );

    se_component_iterator_t c0_it = se_component_iterator_m ( &query_result.components[0], 0 );
    se_component_iterator_t c1_it = se_component_iterator_m ( &query_result.components[1], 0 );

    for ( uint32_t i = 0; i < query_result.entity_count; ++i ) {
        uint64_t* c0_i = ( uint64_t* ) se_component_iterator_next ( &c0_it );
        uint64_t* c1_i = ( uint64_t* ) se_component_iterator_next ( &c1_it );

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
        xg->cmd_clear_texture ( cmd_buffer, node_args->io->copy_texture_writes[0].texture, color_clear, key++ );
    }
}

typedef struct {
    float pos_x;
    float pos_y;
    float scale;
} vertex_cbuffer_data_t;

typedef struct {
    xg_graphics_pipeline_state_h pipeline_state;
    //xg_buffer_h vertex_cbuffer;
    vertex_cbuffer_data_t vertex_cbuffer_data;
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

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m (
        .component_count = 1,
        .components = { se_test_pass_component_m }
    ) );

    se_component_iterator_t it = se_component_iterator_m ( &query_result.components[0], 0 );

    for ( uint64_t i = 0; i < query_result.entity_count; ++i ) {
        se_test_pass_component_t* test_pass_component = ( se_test_pass_component_t* ) se_component_iterator_next ( &it );

        // Set pipeline
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, test_pass_component->pipeline_state, key );

#if 0
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
#else
        xg_buffer_range_t vert_uniform_range = xg->write_workload_uniform ( node_args->workload, &test_pass_component->vertex_cbuffer_data, sizeof ( vertex_cbuffer_data_t ) );

        xg_buffer_resource_binding_t buffer = xg_buffer_resource_binding_m ( .shader_register = 0, .type = xg_buffer_binding_type_uniform_m, .range = vert_uniform_range );

        xg_pipeline_resource_bindings_t bindings = xg_default_pipeline_resource_bindings_m;
        bindings.set = xg_resource_binding_set_per_draw_m;
        bindings.buffer_count = 1;
        bindings.buffers = &buffer;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, key );

#endif

        // Draw
        xg->cmd_draw ( cmd_buffer, 3, 0, key );
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

    xg_graphics_pipeline_state_h pipeline_state;
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

    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );
    xf_graph_h graph = xf->create_graph ( device, swapchain );
    {
        xf_node_h clear_pass;
        {
            xf_node_params_t node_params = xf_default_node_params_m;
            node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m );
            node_params.execute_routine = se_clear_pass;
            node_params.user_args = std_null_buffer_m;
            std_str_copy_static_m ( node_params.debug_name, "clear_pass" );

            clear_pass = xf->create_node ( graph, &node_params );
        }

        xf_node_h test_pass;
        {
            xf_node_params_t node_params = xf_default_node_params_m;
            node_params.render_targets[node_params.render_targets_count++] = xf_render_target_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m );
            node_params.presentable_texture = swapchain_multi_texture;
            node_params.execute_routine = se_test_pass;
            node_params.user_args = std_null_buffer_m;
            std_str_copy_static_m ( node_params.debug_name, "se_pass" );

            test_pass = xf->create_node ( graph, &node_params );
        }
    }

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

#define N (1024*4)

    std_virtual_stack_t stack = std_virtual_stack_create ( 1024 * 1024 );
    se_entity_params_allocator_t allocator = se_entity_params_allocator ( &stack );

    srand ( ( unsigned int ) std_tick_now() );

    for ( uint64_t i = 0; i < N; ++i ) {
        se_test_pass_component_t component_data;
        component_data.vertex_cbuffer_data.pos_x = ( float ) ( rand() ) / ( float ) ( RAND_MAX ) * 2.f - 1.f;
        component_data.vertex_cbuffer_data.pos_y = ( float ) ( rand() ) / ( float ) ( RAND_MAX ) * 2.f - 1.f;
        component_data.vertex_cbuffer_data.scale = 0.2f;
        component_data.pipeline_state = pipeline_state;

        se_entity_h entity = se->create_entity();

        se_entity_params_alloc_entity ( &allocator, entity );
        se_entity_params_alloc_monostream_component_inline_m ( &allocator, se_test_pass_component_m, &component_data );
    }

    se->init_entities ( allocator.entities );
    std_virtual_stack_destroy ( &stack );

#undef N

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

        //xg->acquire_next_swapchain_texture ( swapchain, workload );

        xf->execute_graph ( graph, workload );
        xg->submit_workload ( workload );
        xg->present_swapchain ( swapchain, workload );
    }

    std_module_unload_m ( wm_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( se_module_name_m );
}
#endif

void std_main ( void ) {
    run_se_test_2();
    std_log_info_m ( "se_test_m COMPLETE!" );
}
