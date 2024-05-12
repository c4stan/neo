#include <std_allocator.h>
#include <std_log.h>
#include <std_time.h>
#include <std_app.h>

#include <viewapp.h>

#include <math.h>
#include <sm_matrix.h>

#include <geometry_pass.h>
#include <downsample_pass.h>
#include <lighting_pass.h>
#include <hiz_pass.h>
#include <ssgi_pass.h>
#include <temporal_accumulation_pass.h>
#include <blur_pass.h>
#include <ssr_pass.h>
#include <ui_pass.h>
#include <shadow_pass.h>
#include <depth_pass.h>
#include <simple_pass.h>

#include <geometry.h>
#include <viewapp_state.h>

std_warnings_ignore_m ( "-Wunused-function" )
std_warnings_ignore_m ( "-Wunused-variable" )

// ---

static viewapp_state_t* m_state;

// ---

typedef struct {
    float resolution_x_f32;
    float resolution_y_f32;
    uint32_t resolution_x_u32;
    uint32_t resolution_y_u32;
    uint32_t frame_id;
    float time_ms;
} frame_cbuffer_data_t;

typedef struct {
    uint32_t resolution_x;
    uint32_t resolution_y;
    uint32_t frame_id;
    float time_ms;
    bool capture_frame;
} frame_setup_pass_args_t;

static void frame_setup_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( node_args );
    std_unused_m ( user_args );

    frame_setup_pass_args_t data;
    data.resolution_x = m_state->render.resolution_x;
    data.resolution_y = m_state->render.resolution_y;
    data.frame_id = m_state->render.frame_id;
    data.time_ms = m_state->render.time_ms;
    data.capture_frame = m_state->render.capture_frame;
    frame_setup_pass_args_t* args = &data;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    if ( args->capture_frame ) {
        xg->cmd_start_debug_capture ( cmd_buffer, xg_debug_capture_stop_time_workload_present_m, key );
        args->capture_frame = false;
    }

    xg_buffer_range_t frame_cbuffer_range;
    {
        frame_cbuffer_data_t frame_data;
        frame_data.frame_id = args->frame_id;
        frame_data.time_ms = args->time_ms;
        frame_data.resolution_x_u32 = ( uint32_t ) args->resolution_x;
        frame_data.resolution_y_u32 = ( uint32_t ) args->resolution_y;
        frame_data.resolution_x_f32 = ( float ) args->resolution_x;
        frame_data.resolution_y_f32 = ( float ) args->resolution_y;

        frame_cbuffer_range = xg->write_workload_uniform ( node_args->workload, &frame_data, sizeof ( frame_data ) );
    }

    {
        xg_buffer_resource_binding_t buffer;
        buffer.shader_register = 0;
        buffer.type = xg_buffer_binding_type_uniform_m;
        buffer.range = frame_cbuffer_range;

        xg_pipeline_resource_bindings_t frame_bindings = xg_default_pipeline_resource_bindings_m;
        frame_bindings.set = xg_resource_binding_set_per_frame_m;
        frame_bindings.buffer_count = 1;
        frame_bindings.buffers = &buffer;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &frame_bindings, key );
    }

    std_module_release ( xg );
}

typedef struct {
    rv_matrix_4x4_t view_from_world;
    rv_matrix_4x4_t proj_from_view;
    rv_matrix_4x4_t jittered_proj_from_view;
    rv_matrix_4x4_t view_from_proj;
    rv_matrix_4x4_t world_from_view;
    rv_matrix_4x4_t prev_view_from_world;
    rv_matrix_4x4_t prev_proj_from_view;
    float z_near;
    float z_far;
} view_cbuffer_data_t;

static void view_setup_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );

    xg_i* xg = m_state->modules.xg;
    se_i* se = m_state->modules.se;
    xf_i* xf = m_state->modules.xf;

    se_query_h camera_query;
    {
        se_query_params_t query_params = se_default_query_params_m;
        std_bitset_set ( query_params.request_component_flags, CAMERA_COMPONENT_ID );
        camera_query = se->create_query ( &query_params );
    }

    se->resolve_pending_queries();
    const se_query_result_t* camera_query_result = se->get_query_result ( camera_query );

    rv_i* rv = std_module_get_m ( rv_module_name_m );
    std_assert_m ( camera_query_result->count == 1 );
    {
        se_entity_h entity = camera_query_result->entities[0];
        se_component_h component = se->get_component ( entity, CAMERA_COMPONENT_ID );
        std_auto_m camera_component = ( viewapp_camera_component_t* ) component;

        xg_buffer_range_t view_buffer_range;
        {
            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, camera_component->view );

            view_cbuffer_data_t view_data;
            view_data.view_from_world = view_info.view_matrix;
            view_data.proj_from_view = view_info.proj_matrix;
            view_data.jittered_proj_from_view = view_info.jittered_proj_matrix;
            view_data.world_from_view = view_info.inverse_view_matrix;
            view_data.view_from_proj = view_info.inverse_proj_matrix;
            view_data.prev_view_from_world = view_info.prev_frame_view_matrix;
            view_data.prev_proj_from_view = view_info.prev_frame_proj_matrix;
            view_data.z_near = view_info.proj_params.near_z;
            view_data.z_far = view_info.proj_params.far_z;

            // if TAA is off disable jittering
            xf_node_info_t taa_node_info;
            xf->get_node_info ( &taa_node_info, m_state->render.taa_node );
            if ( !taa_node_info.enabled ) {
                view_data.jittered_proj_from_view = view_info.proj_matrix;
            }

            view_buffer_range = xg->write_workload_uniform ( node_args->workload, &view_data, sizeof ( view_data ) );
        }

        // Bind view resources
        {
            xg_buffer_resource_binding_t buffer;
            buffer.shader_register = 0;
            buffer.type = xg_buffer_binding_type_uniform_m;
            buffer.range = view_buffer_range;

            xg_pipeline_resource_bindings_t view_bindings = xg_default_pipeline_resource_bindings_m;
            view_bindings.set = xg_resource_binding_set_per_view_m;
            view_bindings.buffer_count = 1;
            view_bindings.buffers = &buffer;

            xg->cmd_set_pipeline_resources ( node_args->cmd_buffer, &view_bindings, node_args->base_key );
        }
    }

    se->dispose_query_results();
}

typedef struct {
    xg_sampler_filter_e filter;
} copy_pass_args_t;

static void copy_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    copy_pass_args_t* args = ( copy_pass_args_t* ) user_args;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        xf_copy_texture_resource_t source = node_args->io->copy_texture_reads[0];
        xf_copy_texture_resource_t dest = node_args->io->copy_texture_writes[0];

        xg_texture_copy_params_t copy_params = xg_default_texture_copy_params_m;
        copy_params.source = xf_copy_texture_resource_m ( node_args->io->copy_texture_reads[0] );
        copy_params.destination = xf_copy_texture_resource_m ( node_args->io->copy_texture_writes[0] );
        copy_params.filter = args ? args->filter : xg_sampler_filter_point_m;

        xg->cmd_copy_texture ( cmd_buffer, &copy_params, key );
    }

    std_module_release ( xg );
}

typedef struct {
    xg_graphics_pipeline_state_h pipeline;
    xg_sampler_h sampler;
} combine_pass_args_t;

static void combine_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    combine_pass_args_t* args = ( combine_pass_args_t* ) user_args;

    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    {
        xg_render_textures_binding_t render_textures = xg_null_render_texture_bindings_m;
        render_textures.render_targets_count = 1;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    xs_i* xs = std_module_get_m ( xs_module_name_m );
    {
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( args->pipeline );
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        xg_texture_resource_binding_t textures[3] = {
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[0], 0 ),
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[1], 1 ),
            xf_shader_texture_binding_m ( node_args->io->shader_texture_reads[2], 2 )
        };

        xg_sampler_resource_binding_t sampler;
        sampler.shader_register = 3;
        sampler.sampler = args->sampler;

        xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
        draw_bindings.set = xg_resource_binding_set_per_draw_m;
        draw_bindings.texture_count = 3;
        draw_bindings.sampler_count = 1;
        draw_bindings.textures = textures;
        draw_bindings.samplers = &sampler;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );

        xg->cmd_draw ( cmd_buffer, 3, 0, key );
    }

    std_module_release ( xg );
    std_module_release ( xs );
}

// ---

static void viewapp_scene_cornell_box ( void ) {
    se_i* se = m_state->modules.se;
    xs_i* xs = m_state->modules.xs;
    rv_i* rv = m_state->modules.rv;

    se_entity_h object_entity;
    {
        se_entity_params_t params;
        params.max_component_count = 8;
        object_entity = se->create_entity ( &params );
    }
    m_state->components.sphere = object_entity;

    xs_pipeline_state_h geometry_pipeline_state = xs->lookup_pipeline_state ( "geometry" );
    xs_pipeline_state_h shadow_pipeline_state = xs->lookup_pipeline_state ( "shadow" );

    xg_device_h device = m_state->render.device;
    
    // sphere
    {
        geometry_data_t geo = generate_sphere ( 1.f, 300, 300 );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t* mesh_component = &m_state->components.mesh_components[0];
        *mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { -1.1, -1.45, 1 },
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
            )
        );
        se->add_component ( object_entity, MESH_COMPONENT_ID, ( se_component_h ) mesh_component );
    }

    // planes
    float plane_pos[5][3] = {
        { 0, 0, 2.5 },
        { 2.5, 0, 0 },
        { -2.5, 0, 0 },
        { 0, 2.5, 0 },
        { 0, -2.5, 0 }
    };

    float plane_dir[5][3] = {
        { 0, 1, 0 },
        { 0, 1, 0 },
        { 0, 1, 0 },
        { 0, 0, 1 },
        { 0, 0, 1 }
    };

    float plane_up[5][3] = {
        { 0, 0, -1 },
        { -1, 0, 0 },
        { 1, 0, 0 },
        { 0, 1, 0 },
        { 0, 1, 0 },
    };

    float plane_col[5][3] = {
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 176 / 255.f, 2.2 ), powf (  40 / 255.f, 2.2 ), powf (  48 / 255.f, 2.2 ) },
        { powf (  67 / 255.f, 2.2 ), powf ( 149 / 255.f, 2.2 ), powf (  66 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
    };

    for ( uint32_t i = 0; i < 5; ++i ) {
        se_entity_params_t entity_params;
        entity_params.max_component_count = 8;
        se_entity_h plane_entity = se->create_entity ( &entity_params );
        m_state->components.planes[i] = plane_entity;

        geometry_data_t geo = generate_plane ( 5.f );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t* mesh_component = &m_state->components.mesh_components[2 + i];
        *mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = {
                plane_pos[i][0],
                plane_pos[i][1],
                plane_pos[i][2],
            },
            .orientation = {
                plane_dir[i][0],
                plane_dir[i][1],
                plane_dir[i][2],
            },
            .up = {
                plane_up[i][0],
                plane_up[i][1],
                plane_up[i][2],
            },
            .material = viewapp_material_data_m (
                .base_color = {
                    plane_col[i][0],
                    plane_col[i][1],
                    plane_col[i][2],
                },
                .ssr = true,
                .roughness = 0.1,
                .metalness = 0,
            ),
        );
        se->add_component ( plane_entity, MESH_COMPONENT_ID, ( se_component_h ) mesh_component );
    }

    // light
    se_entity_h light_entity;
    {
        se_entity_params_t entity_params;
        entity_params.max_component_count = 8;
        light_entity = se->create_entity ( &entity_params );
        m_state->components.light = light_entity;

        viewapp_light_component_t* light_component = &m_state->components.light_components[0];
        *light_component = viewapp_light_component_m (
            .position = { 0, 1.45, 0 },
            .intensity = 5,
            .color = { 1, 1, 1 },
            .shadow_casting = true
        );

        rv_view_params_t view_params = rv_view_params_m (
            .position = {
                light_component->position[0],
                light_component->position[1],
                light_component->position[2],
            },
            .focus_point = {
                view_params.position[0],
                view_params.position[1] - 1,
                view_params.position[2],
            },
            .proj_params = rv_projection_params_m (
                .aspect_ratio = 1,
                .near_z = 0.1,
                .far_z = 100,
            ),
        );
        light_component->view = rv->create_view ( &view_params );

        se->add_component ( light_entity, LIGHT_COMPONENT_ID, ( se_component_h ) light_component );
    }
}

static void viewapp_scene_field ( void ) {
    se_i* se = m_state->modules.se;
    xs_i* xs = m_state->modules.xs;
    rv_i* rv = m_state->modules.rv;

    xs_pipeline_state_h geometry_pipeline_state = xs->lookup_pipeline_state ( "geometry" );
    xs_pipeline_state_h shadow_pipeline_state = xs->lookup_pipeline_state ( "shadow" );

    xg_device_h device = m_state->render.device;

    // plane
    {
        se_entity_h entity = se->create_entity ( &se_entity_params_m() );

        geometry_data_t geo = generate_plane ( 100.f );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t* mesh_component = &m_state->components.mesh_components[0];
        *mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .material = viewapp_material_data_m (
                .base_color = {
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 ) 
                },
                .ssr = true,
                .roughness = 0,
            ),          
        );
        se->add_component ( entity, MESH_COMPONENT_ID, ( se_component_h ) mesh_component );
    }

    float x = -50;

    // quads
    for ( uint32_t i = 0; i < 10; ++i ) {
        se_entity_h entity = se->create_entity ( &se_entity_params_m() );

        geometry_data_t geo = generate_plane ( 10.f );
        geometry_gpu_data_t gpu_data = upload_geometry_to_gpu ( device, &geo );

        viewapp_mesh_component_t* mesh_component = &m_state->components.mesh_components[i + 1];
        *mesh_component = viewapp_mesh_component_m (
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .pos_buffer = gpu_data.pos_buffer,
            .nor_buffer = gpu_data.nor_buffer,
            .idx_buffer = gpu_data.idx_buffer,
            .vertex_count = geo.vertex_count,
            .index_count = geo.index_count,
            .position = { x, 5, 0 },
            .orientation = { 0, 1, 0 },
            .up = { 0, 0, -1 },
            .material = viewapp_material_data_m (
                .base_color = {
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 10 / 255.f, 2.2 ),
                    powf ( 10 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.01,
            )
        );
        se->add_component ( entity, MESH_COMPONENT_ID, ( se_component_h ) mesh_component );

        x += 20;
    }

    // lights
    {
        se_entity_h entity = se->create_entity ( &se_entity_params_m() );

        viewapp_light_component_t* light_component = &m_state->components.light_components[0];
        *light_component = viewapp_light_component_m(
            .position = { 10, 10, -10 },
            .intensity = 20,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
        );
        light_component->view = rv->create_view( &rv_view_params_m (
            .position = {
                light_component->position[0],
                light_component->position[1],
                light_component->position[2],
            },
            .focus_point = {
                light_component->position[0],
                light_component->position[1] - 1.f,
                light_component->position[2],
            },
            .proj_params = rv_projection_params_m ( 
                .aspect_ratio = 1,
                .near_z = 0.1,
                .far_z = 100,
            ),
        ) );
        se->add_component ( entity, LIGHT_COMPONENT_ID, ( se_component_h ) light_component );
    }

    {
        se_entity_h entity = se->create_entity ( &se_entity_params_m() );

        viewapp_light_component_t* light_component = &m_state->components.light_components[1];
        *light_component = viewapp_light_component_m (
            .position = { -10, 10, -10 },
            .intensity = 20,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
        );        
        light_component->view = rv->create_view ( &rv_view_params_m (
            .position = {
                light_component->position[0],
                light_component->position[1],
                light_component->position[2],
            },
            .focus_point = {
                light_component->position[0],
                light_component->position[1] - 1.f,
                light_component->position[2],
            },
            .proj_params = rv_projection_params_m (
                .aspect_ratio = 1,
                .near_z = 0.1f,
                .far_z = 100.f,
            ),
        ) );
        se->add_component ( entity, LIGHT_COMPONENT_ID, ( se_component_h ) light_component );
    }
}
//#include <stdio.h>
static void viewapp_boot ( void ) {
    //{
    //    int c;
    //    do {
    //        c = getchar();
    //    } while ((c != '\n') && (c != EOF));
    //}

    uint32_t resolution_x = 1920;
    uint32_t resolution_y = 1024;

    m_state->render.resolution_x = resolution_x;
    m_state->render.resolution_y = resolution_y;

    wm_i* wm = m_state->modules.wm;
    wm_window_h window;
    {
        wm_window_params_t window_params = {
            .name = "viewer_app",
            .x = 0,
            .y = 0,
            .width = resolution_x,
            .height = resolution_y,
            .gain_focus = true,
            .borderless = false
        };
        std_log_info_m ( "Creating window "std_fmt_str_m, window_params.name );
        window = wm->create_window ( &window_params );
    }
    m_state->render.window = window;
    wm->get_window_info ( window, &m_state->render.window_info );
    wm->get_window_input_state ( window, &m_state->render.input_state );

    xg_device_h device;
    xg_i* xg = m_state->modules.xg;
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
    }
    xg_swapchain_h swapchain = xg->create_window_swapchain ( &xg_swapchain_window_params_m (
        .window = window,
        .device = device,
        .texture_count = 3,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
    ) );
    std_assert_m ( swapchain != xg_null_handle_m );

    m_state->render.device = device;
    m_state->render.swapchain = swapchain;

    se_i* se = m_state->modules.se;
    se_entity_h camera_entity = se->create_entity ( &se_entity_params_m() );
    m_state->components.camera = camera_entity;

    xs_i* xs = m_state->modules.xs;
    xui_i* xui = m_state->modules.xui;
    {
        xs->add_database_folder ( "shader/" );
        xs->set_output_folder ( "output/shader/" );

        xui->register_shaders ( xs );

        xs_database_build_params_t build_params;
        build_params.viewport_width = resolution_x;
        build_params.viewport_height = resolution_y;

        xs_database_build_result_t result = xs->build_database_shaders ( device, &build_params );

        if ( result.failed_shaders || result.failed_pipeline_states ) {
            std_log_warn_m ( "Shader database build: " std_fmt_size_m " states, " std_fmt_size_m " shaders failed" );
        }
    }
    {
        fs_i* fs = m_state->modules.fs;
        fs_file_h font_file = fs->open_file ( "assets/ProggyVector-Regular.ttf", fs_file_read_m );
        fs_file_info_t font_file_info;
        fs->get_file_info ( &font_file_info, font_file );
        void* font_data_alloc = std_virtual_heap_alloc ( font_file_info.size, 16 );
        fs->read_file ( font_data_alloc, font_file_info.size, font_file );

        m_state->ui.font = xui->create_font ( std_buffer ( font_data_alloc, font_file_info.size ),
                &xui_font_params_m (
                    .xg_device = device,
                    .pixel_height = 16,
                )
            );

        m_state->ui.window_state = xui_window_state_m (
                .title = "",
                .x = 50,
                .y = 100,
                .width = 350,
                .height = 500,
                .padding_x = 10,
                .padding_y = 2,
                .style = xui_style_m (
                    .font = m_state->ui.font
                )
            );

        m_state->ui.xf_graph_section_state = xui_section_state_m (
            .title = "xf graph",
            .height = 20,
        );

        m_state->ui.xf_alloc_section_state = xui_section_state_m (
            .title = "xg allocator",
            .height = 20,
        );
    }

    rv_i* rv = m_state->modules.rv;
    viewapp_camera_component_t* camera_component = &m_state->components.camera_component;
    {
        // view
        rv_view_params_t view_params = rv_view_params_m (
            .position = { 0, 0, -8 },
            .proj_params = rv_projection_params_m (
                .aspect_ratio = ( float ) resolution_x / ( float ) resolution_y,
                .near_z = 0.1,
                .far_z = 1000,
                .fov_y = 50.f * rv_deg_to_rad_m,
                .jitter = { 1.f / resolution_x, 1.f / resolution_y },
            ),
        );
        rv_view_h view = rv->create_view ( &view_params );

        // camera
        camera_component->view = view;
        se->add_component ( camera_entity, CAMERA_COMPONENT_ID, ( se_component_h ) camera_component );
    }

    viewapp_scene_cornell_box();
    //viewapp_scene_field();

    xf_i* xf = m_state->modules.xf;
    xf_graph_h graph = xf->create_graph ( device, xg_null_handle_m );
    m_state->render.graph = graph;

    // setup
    xf_node_h frame_setup_node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.execute_routine = frame_setup_pass;
        std_str_copy_m ( params.debug_name, "frame_setup" );
        frame_setup_node = xf->create_node ( graph, &params );
    }

    xf_texture_h shadow_texture;
    {
        xf_texture_params_t params = xf_default_texture_params_m;
        params.width = 1024;
        params.height = 1024;
        params.format = xg_format_d16_unorm_m;
        std_str_copy_m ( params.debug_name, "shadow_texture" );
        shadow_texture = xf->declare_texture ( &params );
    }

    //xf_node_h shadow_clear_node = add_depth_clear_pass ( graph, shadow_texture, "shadow_clear" );
    xf_node_h shadow_clear_node = add_simple_clear_pass ( graph, "shadow_clear", &simple_clear_pass_params_m ( 
        .depth_stencil_textures_count = 1,
        .depth_stencil_textures = { shadow_texture },
        .depth_stencil_clears = { { .depth = 1, .stencil = 0 } }
    ) );

    // shadows
    // TODO support more than one shadow...
    xf_node_h shadow_node = add_shadow_pass ( graph, shadow_texture );

    // view setup
    xf_node_h view_setup_node;
    {
        xf_node_params_t params = xf_default_node_params_m;
        params.execute_routine = view_setup_pass;
        std_str_copy_m ( params.debug_name, "view_setup" );
        view_setup_node = xf->create_node ( graph, &params );
    }

    // gbuffer laydown
    // TODO remove multi?
    xf_texture_h color_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_unorm_m,
            .debug_name = "color_texture",
        ),
        .multi_texture_count = 1,
    ) );

    xf_texture_h normal_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r8g8b8a8_unorm_m,
        .debug_name = "normal_texture",
    ) );

    xf_texture_h object_id_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_r8g8b8a8_uint_m,
            .debug_name = "gbuffer_object_id",
        ),
    ) );
    m_state->render.object_id_texture = object_id_texture;
    xf_texture_h prev_object_id_texture = xf->get_multi_texture ( object_id_texture, -1 );

    xf_texture_h depth_stencil_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_d32_sfloat_m,//xg_format_d24_unorm_s8_uint_m,
            .debug_name = "depth_stencil_texture",
        ),
    ) );
    m_state->render.depth_stencil_texture = depth_stencil_texture;

    //xf_node_h geometry_clear_node = add_geometry_clear_node ( graph, color_texture, normal_texture, object_id_texture, depth_stencil_texture );
    xf_node_h geometry_clear_node = add_simple_clear_pass ( graph, "geometry_clear", &simple_clear_pass_params_m (
        .color_textures_count = 3,
        .color_textures = { color_texture, normal_texture, object_id_texture },
        .color_clears = { xg_color_clear_m(), xg_color_clear_m(), xg_color_clear_m() },
        .depth_stencil_textures_count = 1,
        .depth_stencil_textures = { depth_stencil_texture },
        .depth_stencil_clears = { xg_depth_stencil_clear_m() }
    ) );

    xf_node_h geometry_pass = add_geometry_node ( graph, color_texture, normal_texture, object_id_texture, depth_stencil_texture );

    // lighting
    xf_texture_h lighting_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "lighting_texture",
    ) );
    // todo remove extra normal texture param
    xf_node_h lighting_node = add_lighting_pass ( graph, lighting_texture, color_texture, normal_texture, normal_texture, depth_stencil_texture, shadow_texture );

    // hi-z
    //uint32_t res_x_div2 = std_divisor_count_u32 ( resolution_x, 2 );
    //uint32_t res_y_div2 = std_divisor_count_u32 ( resolution_y, 2 );
    //std_log_info_m ( std_fmt_u32_m std_fmt_u32_m, res_x_div2, res_y_div2 );
    //uint32_t hiz_mip_count = std_min_u32 ( res_x_div2, res_y_div2 ) + 1;
    // TODO automate this
    uint32_t hiz_mip_count = 8;
    std_assert_m ( resolution_x % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    std_assert_m ( resolution_y % ( 1 << ( hiz_mip_count - 1 ) ) == 0 );
    xf_texture_h hiz_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_r32_sfloat_m,//xg_format_r16_unorm_m,
        .mip_levels = hiz_mip_count,
        .view_access = xg_texture_view_access_separate_mips_m,
        .debug_name = "hiz_texture",
    ) );

    xf_node_h hiz_mip0_gen_node = add_hiz_mip0_gen_pass ( graph, hiz_texture, depth_stencil_texture );

    for ( uint32_t i = 1; i < hiz_mip_count; ++i ) {
        add_hiz_submip_gen_pass ( graph, hiz_texture, i );
    }

    // ssgi
    // TODO use prev frame final color texture?
    //xf_texture_h prev_color_texture = color_texture;//xf->get_multi_texture ( color_texture, -1 );
    xf_texture_h ssgi_raymarch_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_raymarch_texture",
        ),
    ) );
    xf_node_h ssgi_raymarch_node = add_ssgi_raymarch_pass ( graph, "ssgi", ssgi_raymarch_texture, normal_texture, color_texture, lighting_texture, hiz_texture );

    // ssgi blur
    xf_texture_h ssgi_blur_x_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_x_texture",
    ) );
    xf_node_h ssgi_blur_x_node = add_bilateral_blur_pass ( graph, ssgi_blur_x_texture, ssgi_raymarch_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_blur_x" );

    xf_texture_h ssgi_blur_y_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_blur_y_texture",
    ) );
    xf_node_h ssgi_blur_y_node = add_bilateral_blur_pass ( graph, ssgi_blur_y_texture, ssgi_blur_x_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_blur_y" );

    // ssgi temporal accumulation
    xf_texture_h ssgi_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_accumulation_texture",
        ),
    ) );
    xf_texture_h prev_depth_stencil_texture = xf->get_multi_texture ( depth_stencil_texture, -1 );
    xg_texture_h ssgi_history_texture = xf->get_multi_texture ( ssgi_accumulation_texture, -1 );
    xf_node_h ssgi_temporal_accumulation_node = add_simple_screen_pass ( graph, "ssgi_ta", &simple_screen_pass_params_m (
        .pipeline = xs->lookup_pipeline_state ( "ta" ),
        .render_targets_count = 1,
        .render_targets = { ssgi_accumulation_texture },
        .texture_reads_count = 4,
        .texture_reads = { ssgi_blur_y_texture, ssgi_history_texture, depth_stencil_texture, prev_depth_stencil_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_node_render_target_passthrough_m (
                    .mode = xf_node_passthrough_mode_alias_m,
                    .alias = ssgi_blur_y_texture
                )
            }
        )
    ) );

    // ssgi2
    xf_texture_h ssgi_2_raymarch_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_2_raymarch_texture",
        ),
    ) );
    xf_node_h ssgi_2_raymarch_node = add_ssgi_raymarch_pass ( graph, "ssgi_2", ssgi_2_raymarch_texture, normal_texture, color_texture, ssgi_raymarch_texture, hiz_texture );

    // ssgi2 blur
    xf_texture_h ssgi_2_blur_x_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_x_texture",
    ) );
    xf_node_h ssgi_2_blur_x_node = add_bilateral_blur_pass ( graph, ssgi_2_blur_x_texture, ssgi_2_raymarch_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_horizontal_m, "ssgi_2_blur_x" );

    xf_texture_h ssgi_2_blur_y_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssgi_2_blur_y_texture",
    ) );
    xf_node_h ssgi_2_blur_y_node = add_bilateral_blur_pass ( graph, ssgi_2_blur_y_texture, ssgi_2_blur_x_texture, normal_texture, depth_stencil_texture, 11, 15, blur_pass_direction_vertical_m, "ssgi_2_blur_y" );

    // ssgi2 temporal accumulation
    xf_texture_h ssgi_2_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssgi_2_accumulation_texture",
        ),
    ) );
    xg_texture_h ssgi_2_history_texture = xf->get_multi_texture ( ssgi_2_accumulation_texture, -1 );
    xf_node_h ssgi_2_temporal_accumulation_node = add_simple_screen_pass ( graph, "ssgi_2_ta", &simple_screen_pass_params_m (
        .pipeline = xs->lookup_pipeline_state ( "ta" ),
        .render_targets_count = 1,
        .render_targets = { ssgi_2_accumulation_texture },
        .texture_reads_count = 4,
        .texture_reads = { ssgi_2_blur_y_texture, ssgi_2_history_texture, depth_stencil_texture, prev_depth_stencil_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_node_render_target_passthrough_m (
                    .mode = xf_node_passthrough_mode_alias_m,
                    .alias = ssgi_2_blur_y_texture
                )
            }
        )
    ) );

    // ssr
    xf_texture_h ssr_raymarch_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_raymarch_texture",
    ) );
    xf_node_h ssr_trace_node = add_ssr_raymarch_pass ( graph, ssr_raymarch_texture, normal_texture, lighting_texture, hiz_texture );

    // ssr blur
    xf_texture_h ssr_blur_x_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_y_texture",
    ) );
    xf_node_h ssr_blur_x_node = add_bilateral_blur_pass ( graph, ssr_blur_x_texture, ssr_raymarch_texture, normal_texture, depth_stencil_texture, 1, 5, blur_pass_direction_horizontal_m, "ssr_blur_x" );

    xf_texture_h ssr_blur_y_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format  = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "ssr_blur_y_texture",
    ) );
    xf_node_h ssr_blur_y_node = add_bilateral_blur_pass ( graph, ssr_blur_y_texture, ssr_blur_x_texture, normal_texture, depth_stencil_texture, 1, 5, blur_pass_direction_vertical_m, "ssr_blur_y" );

    // ssr ta
    xf_texture_h ssr_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "ssr_accumulation_texture",
        ),
    ) );
    xf_texture_h ssr_history_texture = xf->get_multi_texture ( ssr_accumulation_texture, -1 );
    xf_node_h ssr_ta_node = add_simple_screen_pass ( graph, "ssr_ta", &simple_screen_pass_params_m (
        .pipeline = xs->lookup_pipeline_state ( "ssr_ta" ),
        .render_targets_count = 1,
        .render_targets = { ssr_accumulation_texture },
        .texture_reads_count = 7,
        .texture_reads = { ssr_blur_y_texture, ssr_history_texture, depth_stencil_texture, prev_depth_stencil_texture, normal_texture, object_id_texture, prev_object_id_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { 
                xf_node_render_target_passthrough_m (
                    .mode = xf_node_passthrough_mode_alias_m,
                    .alias = ssr_blur_y_texture,
                )
            }
        )
    ) );

    // combine
    xf_texture_h combine_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_b10g11r11_ufloat_pack32_m,
        .debug_name = "combine_texture",
    ) );
    xf_node_h combine_node = add_simple_screen_pass ( graph, "combine", &simple_screen_pass_params_m (
        .pipeline = xs->lookup_pipeline_state ( "combine" ),
        .render_targets_count = 1,
        .render_targets = { combine_texture },
        .texture_reads_count = 4,
        .texture_reads = { lighting_texture, ssr_accumulation_texture, ssgi_accumulation_texture, ssgi_2_accumulation_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
    ) );

    // taa
    xf_texture_h taa_accumulation_texture = xf->declare_multi_texture ( &xf_multi_texture_params_m (
        .texture = xf_texture_params_m (
            .width = resolution_x,
            .height = resolution_y,
            .format = xg_format_b10g11r11_ufloat_pack32_m,
            .debug_name = "taa_accumulation_texture",
        ),
    ) );
    xg_texture_h taa_history_texture = xf->get_multi_texture ( taa_accumulation_texture, -1 );
    xf_node_h taa_node = add_simple_screen_pass ( graph, "taa", &simple_screen_pass_params_m (
        .pipeline = xs->lookup_pipeline_state ( "taa" ),
        .render_targets_count = 1,
        .render_targets = { taa_accumulation_texture },
        .texture_reads_count = 4,
        .texture_reads = { combine_texture, depth_stencil_texture, taa_history_texture, object_id_texture },
        .samplers_count = 2,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ), xg->get_default_sampler ( device, xg_default_sampler_linear_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_node_render_target_passthrough_m (
                    .mode = xf_node_passthrough_mode_alias_m,
                    .alias = combine_texture
                )
            }
        )
        ) );
    m_state->render.taa_node = taa_node;

    // tonemap
    xf_texture_h tonemap_texture = xf->declare_texture ( &xf_texture_params_m (
        .width = resolution_x,
        .height = resolution_y,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .debug_name = "tonemap_texture",
    ) );
    xf_node_h tonemap_node = add_simple_screen_pass ( graph, "tonemap", &simple_screen_pass_params_m (
        .pipeline = xs->lookup_pipeline_state ( "tonemap" ),
        .render_targets_count = 1,
        .render_targets = { tonemap_texture },
        .texture_reads_count = 1,
        .texture_reads = { taa_accumulation_texture },
        .samplers_count = 1,
        .samplers = { xg->get_default_sampler ( device, xg_default_sampler_point_clamp_m ) },
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = { xf_node_render_target_passthrough_m (
                    .mode = xf_node_passthrough_mode_copy_m,
                    .copy_source = xf_copy_texture_dependency_m ( taa_accumulation_texture, xg_default_texture_view_m ),
                )
            },
        ),
    ) );

    // ui
    xf_node_h ui_node = add_ui_pass ( graph, tonemap_texture );

    // present
    xf_texture_h swapchain_multi_texture = xf->multi_texture_from_swapchain ( swapchain );
    xf_node_h present_pass = xf->create_node ( graph, &xf_node_params_m (
        .copy_texture_writes_count = 1,
        .copy_texture_writes = { xf_copy_texture_dependency_m ( swapchain_multi_texture, xg_default_texture_view_m ) },
        .copy_texture_reads_count = 1,
        .copy_texture_reads = { xf_copy_texture_dependency_m ( tonemap_texture, xg_default_texture_view_m ) },
        .presentable_texture = swapchain_multi_texture,
        .execute_routine = copy_pass,
        .debug_name = "present",
    ) );

    xf->debug_print_graph ( graph );

    m_state->render.swapchain_multi_texture = swapchain_multi_texture;
    m_state->render.ssgi_raymarch_texture = ssgi_raymarch_texture;
    m_state->render.ssgi_accumulation_texture = ssgi_accumulation_texture;
    m_state->render.ssr_accumulation_texture = ssr_accumulation_texture;
    m_state->render.ssgi_2_raymarch_texture = ssgi_2_raymarch_texture;
    m_state->render.ssgi_2_accumulation_texture = ssgi_2_accumulation_texture;
    m_state->render.taa_accumulation_texture = taa_accumulation_texture;
}

static std_app_state_e viewapp_update ( void ) {
    wm_window_h window = m_state->render.window;

    wm_i* wm = m_state->modules.wm;
    xg_i* xg = m_state->modules.xg;
    xs_i* xs = m_state->modules.xs;
    xf_i* xf = m_state->modules.xf;

    if ( !wm->is_window_alive ( window ) ) {
        return std_app_state_exit_m;
    }

    wm_window_info_t* window_info = &m_state->render.window_info;
    wm_input_state_t* input_state = &m_state->render.input_state;

    float target_fps = 0.f;
    float target_frame_period = target_fps > 0.f ? 1.f / target_fps * 1000.f : 0.f;
    std_tick_t frame_tick = m_state->render.frame_tick;
    float time_ms = m_state->render.time_ms;

    std_tick_t new_tick = std_tick_now();
    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

    if ( delta_ms < target_frame_period ) {
        std_log_info_m ( "pass" );
        return std_app_state_tick_m;
    }

    time_ms += delta_ms;
    m_state->render.time_ms = time_ms;

    frame_tick = new_tick;
    m_state->render.frame_tick = frame_tick;

    wm->update_window ( window );

    wm_input_state_t new_input_state;
    wm->get_window_input_state ( window, &new_input_state );

    if ( new_input_state.keyboard[wm_keyboard_state_esc_m] ) {
        return std_app_state_exit_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
        xs_database_build_params_t build_params;
        build_params.viewport_width = m_state->render.resolution_x;
        build_params.viewport_height = m_state->render.resolution_y;
        xs->build_database_shaders ( m_state->render.device, &build_params );
    }

    if ( !input_state->keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
        return std_app_state_reload_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f3_m] && new_input_state.keyboard[wm_keyboard_state_f3_m] ) {
        m_state->render.capture_frame = true;
    }

    m_state->render.frame_id += 1;

    wm_window_info_t new_window_info;
    wm->get_window_info ( window, &new_window_info );

    if ( m_state->render.window_info.width != new_window_info.width || m_state->render.window_info.height != new_window_info.height ) {
        xg->resize_swapchain ( m_state->render.swapchain, new_window_info.width, new_window_info.height );
        // TODO rename this
        xf->refresh_external_texture ( m_state->render.swapchain_multi_texture );
    }

    // UI
    xui_i* xui = m_state->modules.xui;
    xui_workload_h xui_workload = xui->create_workload();
    set_ui_pass_xui_workload ( xui_workload );
    
    xui->begin_update ( &new_window_info, &new_input_state );
    xui->begin_window ( xui_workload, &m_state->ui.window_state );
    
    xui->begin_section ( xui_workload, &m_state->ui.xf_alloc_section_state );
    if ( !m_state->ui.xf_alloc_section_state.minimized ) {
        for ( uint32_t i = 0; i < xg_memory_type_count_m; ++i ) {
            xg_allocator_info_t info;
            xg->get_default_allocator_info ( &info, m_state->render.device, i );
            {
                xui_label_state_t type_label = xui_label_state_m( .height = 14 );
                std_stack_t stack = std_static_stack_m ( type_label.text );
                const char* memory_type_name;
                switch ( i ) {
                    case xg_memory_type_gpu_only_m:     memory_type_name = "gpu"; break;
                    case xg_memory_type_gpu_mappable_m: memory_type_name = "mapped"; break;
                    case xg_memory_type_upload_m:       memory_type_name = "upload"; break;
                    case xg_memory_type_readback_m:     memory_type_name = "readback"; break;
                }
                std_stack_string_append ( &stack, memory_type_name );
                xui->add_label ( xui_workload, &type_label );
            }
            {
                xui_label_state_t size_label = xui_label_state_m ( 
                    .height = 14,
                    .style.horizontal_alignment = xui_horizontal_alignment_right_to_left_m,
                );
                std_stack_t stack = std_static_stack_m ( size_label.text );
                char buffer[32];
                std_size_to_str_approx ( buffer, 32, info.allocated_size );
                std_stack_string_append ( &stack, buffer );
                std_stack_string_append ( &stack, "/" );
                std_size_to_str_approx ( buffer, 32, info.reserved_size );
                std_stack_string_append ( &stack, buffer );
                std_stack_string_append ( &stack, "/" );
                std_size_to_str_approx ( buffer, 32, info.system_size );
                std_stack_string_append ( &stack, buffer );
                xui->add_label ( xui_workload, &size_label );
            }
            xui->newline();
        }
    }
    xui->end_section ( xui_workload );
    
    xui->begin_section ( xui_workload, &m_state->ui.xf_graph_section_state );
    if ( !m_state->ui.xf_graph_section_state.minimized ) {
        xf->debug_ui_graph ( xui, xui_workload, m_state->render.graph );
    }
    xui->end_section ( xui_workload );
    
    xui->end_window ( xui_workload );
    xui->end_update();

    // Camera
    if ( xui->get_active_element_id() == 0 ) {
        rv_i* rv = m_state->modules.rv;
        viewapp_camera_component_t* camera_component = &m_state->components.camera_component;

        rv->update_prev_frame_data ( camera_component->view );
        rv->update_proj_jitter ( camera_component->view, m_state->render.frame_id );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, camera_component->view );
        rv_view_transform_t xform = view_info.transform;
        bool dirty_xform = false;

        if ( new_input_state.mouse[wm_mouse_state_left_m] ) {
            float drag_scale = -1.f / 400;
            sm_vec_3f_t v;
            v.x = xform.position[0] - xform.focus_point[0];
            v.y = xform.position[1] - xform.focus_point[1];
            v.z = xform.position[2] - xform.focus_point[2];

            int64_t delta_x = ( int64_t ) new_input_state.cursor_x - ( int64_t ) input_state->cursor_x;
            int64_t delta_y = ( int64_t ) new_input_state.cursor_y - ( int64_t ) input_state->cursor_y;

            if ( delta_x != 0 ) {
                sm_vec_3f_t up = sm_vec_3f ( 0, 1, 0 );
                sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( up, delta_x * drag_scale );

                v = sm_matrix_4x4f_transform_f3 ( mat, v );
            }

            if ( delta_y != 0 ) {
                sm_vec_3f_t up = sm_vec_3f ( 0, 1, 0 );
                sm_vec_3f_t axis = sm_vec_3f_cross ( up, v );
                axis = sm_vec_3f_norm ( axis );

                sm_mat_4x4f_t mat = sm_matrix_4x4f_axis_rotation ( axis, -delta_y * drag_scale );
                v = sm_matrix_4x4f_transform_f3 ( mat, v );
            }

            if ( delta_x != 0 || delta_y != 0 ) {
                xform.position[0] = view_info.transform.focus_point[0] + v.x;
                xform.position[1] = view_info.transform.focus_point[1] + v.y;
                xform.position[2] = view_info.transform.focus_point[2] + v.z;

                dirty_xform = true;
            }
        }

        if ( new_input_state.mouse[wm_mouse_state_wheel_up_m] || new_input_state.mouse[wm_mouse_state_wheel_down_m] ) {
            int8_t wheel = ( int8_t ) new_input_state.mouse[wm_mouse_state_wheel_up_m] - ( int8_t ) new_input_state.mouse[wm_mouse_state_wheel_down_m];
            float zoom_step = -0.1;
            float zoom_min = 0.001;

            sm_vec_3f_t v;
            v.x = xform.position[0] - xform.focus_point[0];
            v.y = xform.position[1] - xform.focus_point[1];
            v.z = xform.position[2] - xform.focus_point[2];

            float dist = sm_vec_3f_len ( v );
            float new_dist = fmaxf ( zoom_min, dist + ( zoom_step * wheel ) * dist );
            v = sm_vec_3f_mul ( v, new_dist / dist );

            xform.position[0] = view_info.transform.focus_point[0] + v.x;
            xform.position[1] = view_info.transform.focus_point[1] + v.y;
            xform.position[2] = view_info.transform.focus_point[2] + v.z;

            dirty_xform = true;
        }

        if ( dirty_xform ) {
            rv->update_view_transform ( camera_component->view, &xform );
        }
    }

    m_state->render.window_info = new_window_info;
    m_state->render.input_state = new_input_state;

    xg_workload_h workload = xg->create_workload ( m_state->render.device );

    xg->acquire_next_swapchain_texture ( m_state->render.swapchain, workload );
    xf->execute_graph ( m_state->render.graph, workload );
    xg->submit_workload ( workload );
    xg->present_swapchain ( m_state->render.swapchain, workload );

    xs->update_pipeline_states ( workload );

    return std_app_state_tick_m;
}

std_app_state_e viewapp_tick ( void ) {
    if ( m_state->render.frame_id == 0 ) {
        viewapp_boot();
    }

    return viewapp_update();
}

void* viewer_app_load ( void* runtime ) {
    std_runtime_bind ( runtime );

    viewapp_state_t* state = viewapp_state_alloc();

    state->api.tick = viewapp_tick;

    state->modules.fs = std_module_get_m ( fs_module_name_m );
    state->modules.wm = std_module_get_m ( wm_module_name_m );
    state->modules.xg = std_module_get_m ( xg_module_name_m );
    state->modules.xs = std_module_get_m ( xs_module_name_m );
    state->modules.xf = std_module_get_m ( xf_module_name_m );
    state->modules.se = std_module_get_m ( se_module_name_m );
    state->modules.rv = std_module_get_m ( rv_module_name_m );
    state->modules.xui = std_module_get_m ( xui_module_name_m );

    std_mem_zero_m ( &state->render );
    state->render.frame_id = 0;

    std_mem_zero_m ( &state->components );

    m_state = state;

    return state;
}

void viewer_app_unload ( void ) {
    viewapp_state_free();
}

void viewer_app_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );

    std_auto_m state = ( viewapp_state_t* ) api;
    state->api.tick = viewapp_tick;
    m_state = state;

    viewapp_state_bind ( state );
}
