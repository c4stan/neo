#include <std_log.h>
#include <std_time.h>
#include <std_app.h>

#include <viewapp.h>
#include <viewapp_state.h>

#include <sm.h>
#include <se.h>

#include "viewapp_ui.h"
#include "viewapp_render.h"
#include "viewapp_scene.h"

static void viewapp_boot ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    uint32_t resolution_x = 1920;
    uint32_t resolution_y = 1024;

    state->render.resolution_x = resolution_x;
    state->render.resolution_y = resolution_y;

    wm_i* wm = state->modules.wm;
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
    state->render.window = window;
    wm->get_window_info ( window, &state->render.window_info );
    wm->get_window_input_state ( window, &state->render.input_state );

    xg_device_h device;
    xg_device_info_t device_info;
    xg_i* xg = state->modules.xg;
    {
        size_t device_count = xg->get_devices_count();
        std_assert_m ( device_count > 0 );
        xg_device_h devices[16];
        xg->get_devices ( devices, 16 );
        device = devices[0];
        bool activate_result = xg->activate_device ( device );
        std_assert_m ( activate_result );
        xg->get_device_info ( &device_info, device );
        std_log_info_m ( "Picking device 0 (" std_fmt_str_m ") as default device", device_info.name );
    }
    xg_swapchain_h swapchain = xg->create_window_swapchain ( &xg_swapchain_window_params_m (
        .window = window,
        .device = device,
        .texture_count = 3,
        .format = xg_format_a2b10g10r10_unorm_pack32_m,
        .allowed_usage = xg_texture_usage_bit_copy_dest_m,
        .debug_name = "swapchain",
    ) );
    std_assert_m ( swapchain != xg_null_handle_m );

    state->render.device = device;
    state->render.swapchain = swapchain;
    state->render.supports_raytrace = device_info.supports_raytrace;

    se_i* se = state->modules.se;

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 1,
        .components = { se_component_layout_m (
            .id = viewapp_camera_component_id_m,
            .stream_count = 1,
            .streams = { sizeof ( viewapp_camera_component_t ) }
        ) }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 2,
        .components = { 
            se_component_layout_m (
                .id = viewapp_mesh_component_id_m,
                .streams = { sizeof ( viewapp_mesh_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_transform_component_id_m,
                .streams = { sizeof ( viewapp_transform_component_t ) }
            ),
        }
    ) );

    se->set_component_properties ( viewapp_transform_component_id_m, "Transform", &se_component_properties_params_m (
        .count = 3,
        .properties = {
            se_field_property_m ( 0, viewapp_transform_component_t, position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_transform_component_t, orientation, se_property_4f32_m ),
            se_field_property_m ( 0, viewapp_transform_component_t, scale, se_property_f32_m ),
        }
    ) );

    se->set_component_properties ( viewapp_mesh_component_id_m, "Mesh", &se_component_properties_params_m (
        .count = 5,
        .properties = {
            se_field_property_m ( 0, viewapp_mesh_component_t, material.base_color, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.emissive, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.roughness, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.metalness, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_mesh_component_t, material.ssr, se_property_bool_m ),
        }
    ) );

    se->set_component_properties ( viewapp_light_component_id_m, "Light", &se_component_properties_params_m (
        .count = 5,
        .properties = {
            se_field_property_m ( 0, viewapp_light_component_t, position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, intensity, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, color, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, radius, se_property_f32_m ),
            se_field_property_m ( 0, viewapp_light_component_t, shadow_casting, se_property_bool_m ),
        }
    ) );

    se->set_component_properties ( viewapp_camera_component_id_m, "Camera", &se_component_properties_params_m (
        .count = 1, // TODO
        .properties = {
            se_field_property_m ( 0, viewapp_camera_component_t, enabled, se_property_bool_m ),
        }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 3,
        .components = { 
            se_component_layout_m (
                .id = viewapp_light_component_id_m,
                .streams = { sizeof ( viewapp_light_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_mesh_component_id_m,
                .streams = { sizeof ( viewapp_mesh_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_transform_component_id_m,
                .streams = { sizeof ( viewapp_transform_component_t ) }
            )
        }
    ) );

    xs_i* xs = state->modules.xs;
    xs_database_h sdb = xs->create_database ( &xs_database_params_m ( .device = device, .debug_name = "viewapp_sdb" ) );
    state->render.sdb = sdb;
    xs->add_database_folder ( sdb, "shader/" );
    xs->set_output_folder ( sdb, "output/shader/" );
    xs_database_build_result_t build_result = xs->build_database ( sdb );
    std_assert_m ( build_result.failed_pipeline_states == 0 );

    viewapp_boot_ui ( device );

    xf_i* xf = state->modules.xf;
    xf->load_shaders ( device );

    viewapp_boot_workload_resources_layout();

    viewapp_load_scene ( state->scene.active_scene );

    viewapp_boot_raster_graph();
    if ( state->render.supports_raytrace ) {
        viewapp_boot_raytrace_graph();
    }
    viewapp_boot_mouse_pick_graph();

    state->render.active_graph = state->render.raster_graph;
}

static void viewapp_update_camera ( wm_input_state_t* input_state, wm_input_state_t* new_input_state, float dt ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    xi_i* xi = state->modules.xi;
    rv_i* rv = state->modules.rv;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_camera_component_id_m } ) );

    se_stream_iterator_t camera_iterator = se_component_iterator_m ( camera_query_result.components, 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );

        if ( !camera_component->enabled ) {
            continue;
        }

        rv->update_prev_frame_data ( camera_component->view );
        rv->update_proj_jitter ( camera_component->view, state->render.frame_id );

        rv_view_info_t view_info;
        rv->get_view_info ( &view_info, camera_component->view );
        rv_view_transform_t xform = view_info.transform;
        bool dirty_xform = false;

        if ( camera_component->type == viewapp_camera_type_arcball_m ) {
            // drag
            if ( new_input_state->mouse[wm_mouse_state_left_m] ) {
                float drag_scale = -1.f / 400;
                sm_vec_3f_t v = sm_vec_3f ( xform.position );

                int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
                int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

                if ( delta_x != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_quat_t q = sm_quat_axis_rotation ( up, -delta_x * drag_scale );
                    v = sm_quat_transform_f3 ( q, v );
                }

                if ( delta_y != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_vec_3f_t axis = sm_vec_3f_cross ( up, v );
                    axis = sm_vec_3f_norm ( axis );
                    sm_quat_t q = sm_quat_axis_rotation ( axis, delta_y * drag_scale );
                    v = sm_quat_transform_f3 ( q, v );
                }

                if ( delta_x != 0 || delta_y != 0 ) {
                    xform.position[0] = v.x;
                    xform.position[1] = v.y;
                    xform.position[2] = v.z;

                    sm_vec_3f_t dir = sm_vec_3f ( xform.position );
                    dir = sm_vec_3f_neg ( dir );
                    dir = sm_vec_3f_norm ( dir );
                    sm_quat_t q = sm_quat_from_vec ( dir );
                    xform.orientation[0] = q.e[0];
                    xform.orientation[1] = q.e[1];
                    xform.orientation[2] = q.e[2];
                    xform.orientation[3] = q.e[3];

                    dirty_xform = true;
                }
            }

            // zoom
            if ( xi->get_hovered_element_id() == 0 ) {
                if ( new_input_state->mouse[wm_mouse_state_wheel_up_m] || new_input_state->mouse[wm_mouse_state_wheel_down_m] ) {
                    int8_t wheel = ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_up_m] - ( int8_t ) new_input_state->mouse[wm_mouse_state_wheel_down_m];
                    float zoom_step = -0.1;
                    float zoom_min = 0.001;

                    sm_vec_3f_t v = sm_vec_3f ( xform.position );
                    float dist = sm_vec_3f_len ( v );
                    float new_dist = std_max_f32 ( zoom_min, dist + ( zoom_step * wheel ) * dist );
                    v = sm_vec_3f_mul ( v, new_dist / dist );

                    xform.position[0] = v.x;
                    xform.position[1] = v.y;
                    xform.position[2] = v.z;

                    dirty_xform = true;
                }
            }
        } else if ( camera_component->type == viewapp_camera_type_flycam_m ) {
            bool speed_up_press = new_input_state->keyboard[wm_keyboard_state_e_m];
            bool speed_down_press = new_input_state->keyboard[wm_keyboard_state_q_m];
            float speed = camera_component->move_speed;
            if ( speed_up_press ) {
                speed *= 1.1f;
                camera_component->move_speed = speed;
            }
            if ( speed_down_press ) {
                speed *= 0.9f;
                camera_component->move_speed = speed;
            }

            bool forward_press = new_input_state->keyboard[wm_keyboard_state_w_m];
            bool backward_press = new_input_state->keyboard[wm_keyboard_state_s_m];
            bool right_press = new_input_state->keyboard[wm_keyboard_state_d_m];
            bool left_press = new_input_state->keyboard[wm_keyboard_state_a_m];
            if ( ( forward_press && !backward_press ) || ( backward_press && !forward_press ) 
                || ( right_press && !left_press ) || ( left_press && !right_press ) ) {
                sm_vec_3f_t z_axis = sm_quat_to_vec ( sm_quat ( xform.orientation ) );
                sm_vec_3f_t up = { 0, 1, 0 };
                sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
                //sm_vec_3f_t y_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( x_axis, z_axis ) );

                float forward = ( forward_press ? 1.f : 0.f ) - ( backward_press ? 1.f : 0.f );
                float right = ( right_press ? 1.f : 0.f ) - ( left_press ? 1.f : 0.f );
                sm_vec_3f_t move_dir = sm_vec_3f_norm ( sm_vec_3f_add ( sm_vec_3f_mul ( x_axis, right ), sm_vec_3f_mul ( z_axis, forward) ) );
                sm_vec_3f_t move = sm_vec_3f_mul ( move_dir, speed * dt );

                xform.position[0] += move.e[0];
                xform.position[1] += move.e[1];
                xform.position[2] += move.e[2];
                dirty_xform = true;
            }

            bool up_press = new_input_state->keyboard[wm_keyboard_state_z_m];
            bool down_press = new_input_state->keyboard[wm_keyboard_state_x_m];
            if ( ( up_press && !down_press ) || ( !up_press && down_press ) ) {
                float above = ( up_press ? 1.f : 0.f ) - ( down_press ? 1.f : 0.f );
                xform.position[1] += above * speed * dt;
                dirty_xform = true;
            }

            if ( new_input_state->mouse[wm_mouse_state_right_m] ) {
                int64_t delta_x = ( int64_t ) new_input_state->cursor_x - ( int64_t ) input_state->cursor_x;
                int64_t delta_y = ( int64_t ) new_input_state->cursor_y - ( int64_t ) input_state->cursor_y;

                sm_vec_3f_t z_axis = sm_quat_to_vec ( sm_quat ( xform.orientation ) );
                sm_vec_3f_t up = { 0, 1, 0 };
                sm_vec_3f_t x_axis = sm_vec_3f_norm ( sm_vec_3f_cross ( up, z_axis ) );
                sm_vec_3f_t dir = z_axis;
                float drag_scale = -1.f / 400;

                if ( delta_x != 0 ) {
                    sm_vec_3f_t up = { 0, 1, 0 };
                    sm_quat_t q = sm_quat_axis_rotation ( up, -delta_x * drag_scale );
                    dir = sm_quat_transform_f3 ( q, dir );
                }

                if ( delta_y != 0 ) {
                    sm_quat_t q = sm_quat_axis_rotation ( x_axis, -delta_y * drag_scale );
                    dir = sm_quat_transform_f3 ( q, dir );
                }

                sm_quat_t orientation = sm_quat_from_vec ( dir );

                xform.orientation[0] = orientation.e[0];
                xform.orientation[1] = orientation.e[1];
                xform.orientation[2] = orientation.e[2];
                xform.orientation[3] = orientation.e[3];
                dirty_xform = true;
            }

        }

        if ( dirty_xform ) {
            rv->update_view_transform ( camera_component->view, &xform );
        }
    }
}


static void viewapp_update_lights ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    rv_i* rv = state->modules.rv;

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( 
        .component_count = 2,
        .components = { viewapp_light_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &query_result.components[1], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &query_result.entities );

    for ( uint32_t light_it = 0; light_it < query_result.entity_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        for ( uint32_t view_it = 0; view_it < viewapp_light_max_views_m; ++view_it ) {
            rv_view_info_t view_info;
            rv->get_view_info ( &view_info, light_component->views[view_it] );
            rv_view_transform_t transform = rv_view_transform_m (
                .position = { 
                    transform_component->position[0],
                    transform_component->position[1],
                    transform_component->position[2],
                },
                .orientation = {
                    view_info.transform.orientation[0],
                    view_info.transform.orientation[1],
                    view_info.transform.orientation[2],
                    view_info.transform.orientation[3],
                }
            );
            rv->update_view_transform ( light_component->views[view_it], &transform );
        }

        se_entity_h* entity = se_stream_iterator_next ( &entity_iterator );
        viewapp_mesh_component_t* mesh_component = se->get_entity_component ( *entity, viewapp_mesh_component_id_m, 0 );
        if ( mesh_component ) {
#if 0
            // assume sphere
            float area = 3.1415f * 4 * transform_component->scale * transform_component->scale;
            float radiant_exitance = light_component->intensity / area;
#else
            float radiant_exitance = 1;
#endif
            mesh_component->material.emissive[0] = light_component->color[0] * radiant_exitance;
            mesh_component->material.emissive[1] = light_component->color[1] * radiant_exitance;
            mesh_component->material.emissive[2] = light_component->color[2] * radiant_exitance;
        }
    }
}

static void viewapp_update_meshes ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .component_count = 2,
        .components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 0 );

    for ( uint32_t i = 0; i < mesh_query_result.entity_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        viewapp_transform_component_t* transform_component = se_stream_iterator_next ( &transform_iterator );
        mesh_component->prev_transform = *transform_component;
        // TODO update transform...
    }
}

static std_app_state_e viewapp_update ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    wm_window_h window = state->render.window;

    wm_i* wm = state->modules.wm;
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    xf_i* xf = state->modules.xf;

    if ( !wm->is_window_alive ( window ) ) {
        return std_app_state_exit_m;
    }

    float target_fps = state->render.target_fps;
    float target_frame_period = target_fps > 0.f ? 1.f / target_fps * 1000.f : 0.f;
    std_tick_t frame_tick = state->render.frame_tick;
    float time_ms = state->render.time_ms;

    std_tick_t new_tick = std_tick_now();
    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

    if ( delta_ms < target_frame_period ) {
        //std_thread_yield();
        std_thread_this_sleep( 0 );
        return std_app_state_tick_m;
    }

    time_ms += delta_ms;
    state->render.time_ms = time_ms;
    state->render.delta_time_ms = delta_ms;

    frame_tick = new_tick;
    state->render.frame_tick = frame_tick;

    wm->update_window ( window );

    wm_input_state_t* input_state = &state->render.input_state;
    wm_input_state_t new_input_state;
    wm->get_window_input_state ( window, &new_input_state );

    if ( new_input_state.keyboard[wm_keyboard_state_esc_m] ) {
        return std_app_state_exit_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
        xs->rebuild_databases();
        state->reload = true;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
        return std_app_state_reload_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f3_m] && new_input_state.keyboard[wm_keyboard_state_f3_m] ) {
        return std_app_state_reboot_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f4_m] && new_input_state.keyboard[wm_keyboard_state_f4_m] ) {
        state->render.capture_frame = true;
    }

    state->render.frame_id += 1;

    xg_workload_h workload = xg->create_workload ( state->render.device );
    if ( state->render.capture_frame ) {
        xg->debug_capture_workload ( workload );
        state->render.capture_frame = false;
    }

    if ( state->reload ) {
        bool enabled[xf_graph_max_nodes_m] = {};
        xf_graph_info_t graph_info;
        xf->get_graph_info ( &graph_info, state->render.active_graph );
        for ( uint32_t i = 0; i < graph_info.node_count; ++i ) {
            xf_node_info_t node_info;
            xf->get_node_info ( &node_info, state->render.active_graph, graph_info.nodes[i] );
            enabled[i] = node_info.enabled;
        }

        bool active_graph[2] = {};
        if ( state->render.active_graph == state->render.raster_graph ) active_graph[0] = 1;
        if ( state->render.active_graph == state->render.raytrace_graph ) active_graph[1] = 1;

        xf->destroy_graph ( state->render.raster_graph, workload );
        xf->destroy_graph ( state->render.raytrace_graph, workload );
        xf->destroy_graph ( state->render.mouse_pick_graph, workload );

        xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
        xg->cmd_destroy_texture ( resource_cmd_buffer, state->render.object_id_readback_texture, xg_resource_cmd_buffer_time_workload_complete_m );
 
        viewapp_boot_raster_graph();
        viewapp_boot_raytrace_graph();
        viewapp_boot_mouse_pick_graph();

        viewapp_build_raytrace_world ( workload );

        if ( active_graph[0] ) state->render.active_graph = state->render.raster_graph;
        if ( active_graph[1] ) state->render.active_graph = state->render.raytrace_graph;

        bool any_passthrough = false;
        for ( uint32_t i = 0; i < graph_info.node_count; ++i ) {
            if ( !enabled[i] ) {
                xf->node_set_enabled ( state->render.active_graph, graph_info.nodes[i], false );
                any_passthrough = true;
            }
        }

        if ( any_passthrough ) {
            xf->invalidate_graph ( state->render.active_graph, workload );
        }

        state->reload = false;
        state->render.graph_reload = true;
    }

    wm_window_info_t new_window_info;
    wm->get_window_info ( window, &new_window_info );

    viewapp_update_camera ( input_state, &new_input_state, delta_ms * 1000 );

    viewapp_update_meshes();
    viewapp_update_lights();
    viewapp_update_ui ( &new_window_info, input_state, &new_input_state, workload );

    state->render.window_info = new_window_info;
    state->render.input_state = new_input_state;

    viewapp_update_workload_uniforms ( workload );

    state->render.graph_reload = false;

    xf->execute_graph ( state->render.active_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->present_swapchain ( state->render.swapchain, workload );

    xs->update_pipeline_states ( workload );

    if ( state->render.raytrace_world_update ) {
        xg->wait_all_workload_complete();
        xg_workload_h workload = xg->create_workload ( state->render.device );
        viewapp_build_raytrace_world ( workload );
        xf->destroy_graph ( state->render.raytrace_graph, workload );
        viewapp_boot_raytrace_graph();
        xg->submit_workload ( workload );
        state->render.raytrace_world_update = false;
    }

    std_tick_t update_tick = std_tick_now();
    float update_ms = std_tick_to_milli_f32 ( update_tick - new_tick );
    state->render.update_time_ms = update_ms;

    return std_app_state_tick_m;
}

std_app_state_e viewapp_tick ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    if ( state->render.frame_id == 0 ) {
        viewapp_boot();
    }

    return viewapp_update();
}

void* viewer_app_load ( void* runtime ) {
    std_runtime_bind ( runtime );

    viewapp_state_t* state = viewapp_state_alloc();

    state->api.tick = viewapp_tick;
    state->reload = false;

    state->modules = ( viewapp_modules_state_t ) {
        .wm = std_module_load_m ( wm_module_name_m ),
        .xg = std_module_load_m ( xg_module_name_m ),
        .xs = std_module_load_m ( xs_module_name_m ),
        .xf = std_module_load_m ( xf_module_name_m ),
        .se = std_module_load_m ( se_module_name_m ),
        .rv = std_module_load_m ( rv_module_name_m ),
        .xi = std_module_load_m ( xi_module_name_m ),
    };
    state->render = viewapp_render_state_m();
    state->ui = viewapp_ui_state_m();
    state->scene = viewapp_scene_state_m();

    return state;
}

void viewer_app_unload ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    xg->wait_all_workload_complete();

    xg_workload_h workload = xg->create_workload ( state->render.device );

    se_i* se = state->modules.se;
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .component_count = 1, .components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg_geo_util_free_data ( &mesh_component->geo_data );
        xg_geo_util_free_gpu_data ( &mesh_component->geo_gpu_data, workload, xg_resource_cmd_buffer_time_workload_start_m );
    }

    xg->submit_workload ( workload );

    xg->destroy_resource_layout ( state->render.workload_bindings_layout );

    std_module_unload_m ( xi_module_name_m );
    std_module_unload_m ( rv_module_name_m );
    std_module_unload_m ( se_module_name_m );
    std_module_unload_m ( xf_module_name_m );
    std_module_unload_m ( xs_module_name_m );
    std_module_unload_m ( xg_module_name_m );
    std_module_unload_m ( wm_module_name_m );

    viewapp_state_free();
}

void viewer_app_reload ( void* runtime, void* api ) {
    std_runtime_bind ( runtime );

    std_auto_m state = ( viewapp_state_t* ) api;
    state->api.tick = viewapp_tick;
    state->reload = true;

    viewapp_state_bind ( state );
}
