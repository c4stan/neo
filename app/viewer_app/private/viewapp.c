#include <std_log.h>
#include <std_time.h>
#include <std_app.h>
#include <std_file.h>

#include <viewapp.h>
#include <viewapp_state.h>

#include <sm.h>
#include <se.h>

#include "viewapp_ui.h"
#include "viewapp_render.h"
#include "viewapp_scene.h"

static void viewapp_boot ( void ) {
    viewapp_state_t* state = viewapp_state_get();

    viewapp_boot_scene();
    viewapp_boot_render();
    viewapp_boot_ui();

    viewapp_load_render_graph ( viewapp_render_graph_raster_m, xg_null_handle_m );
    viewapp_load_mouse_pick_graph();

    viewapp_load_scene ( state->scene.active_scene );
}

static void viewapp_update_camera ( wm_input_state_t* input_state, wm_input_state_t* new_input_state, float dt ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    xi_i* xi = state->modules.xi;
    rv_i* rv = state->modules.rv;

    se_query_result_t camera_query_result;
    se->query_entities ( &camera_query_result, &se_query_params_m ( .include_component_count = 2, .include_components = { 
        viewapp_camera_component_id_m,
        viewapp_transform_component_id_m
    } ) );

    se_stream_iterator_t camera_iterator = se_component_iterator_m ( &camera_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &camera_query_result.components[1], 0 );
    for ( uint32_t i = 0; i < camera_query_result.entity_count; ++i ) {
        viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );
        viewapp_transform_t* transform_component = se_stream_iterator_next ( &transform_iterator );

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

        // write out view transform into transform component
        // writes to local xform but the view xform is supposed to be global
        // if assumption is camera is never parented and local = global then it works out.
        std_mem_copy_static_array_m ( transform_component->position, xform.position );
        std_mem_copy_static_array_m ( transform_component->orientation, xform.orientation );
        transform_component->scale = 1;
    }
}

static void viewapp_update_global_transforms ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;

    // process transforms without parents
    // move local to global
    {
        se_query_result_t query_result;
        se->query_entities ( &query_result, &se_query_params_m (
            .include_component_count = 1,
            .include_components = { viewapp_transform_component_id_m },
            .exclude_component_count = 1,
            .exclude_components = { viewapp_parent_component_id_m },
        ) );
        se_stream_iterator_t local_transform_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
        se_stream_iterator_t global_transform_iterator = se_component_iterator_m ( &query_result.components[0], 1 );

        for ( uint32_t i = 0; i < query_result.entity_count; ++i ) {
            viewapp_transform_t* local_transform = se_stream_iterator_next ( &local_transform_iterator );
            viewapp_transform_t* global_transform = se_stream_iterator_next ( &global_transform_iterator );

            std_mem_copy_static_array_m ( global_transform->position, local_transform->position );
            std_mem_copy_static_array_m ( global_transform->orientation, local_transform->orientation );
            global_transform->scale = local_transform->scale;
        }
    }

    // process transforms with parents
    // TODO process full hierarchy, not just 1 level
    {
        se_query_result_t query_result;
        se->query_entities ( &query_result, &se_query_params_m (
            .include_component_count = 2,
            .include_components = { viewapp_transform_component_id_m, viewapp_parent_component_id_m },
        ) );
        se_stream_iterator_t local_transform_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
        se_stream_iterator_t global_transform_iterator = se_component_iterator_m ( &query_result.components[0], 1 );
        se_stream_iterator_t parent_iterator = se_component_iterator_m ( &query_result.components[1], 0 );

        for ( uint32_t i = 0; i < query_result.entity_count; ++i ) {
            viewapp_transform_t* local_transform = se_stream_iterator_next ( &local_transform_iterator );
            viewapp_transform_t* global_transform = se_stream_iterator_next ( &global_transform_iterator );
            se_entity_h* parent = se_stream_iterator_next ( &parent_iterator );

            viewapp_transform_t result_transform = *local_transform;

            se_entity_h parent_handle = *parent;
            if ( parent_handle != se_null_handle_m ) {
                viewapp_transform_t* parent_transform = se->get_entity_component ( parent_handle, viewapp_transform_component_id_m, 1 );
                // TODO multiply local xform by parent xform
                result_transform = *parent_transform;
            }

            *global_transform = result_transform;
        }
    }
}

static void viewapp_update_lights ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    rv_i* rv = state->modules.rv;

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( 
        .include_component_count = 2,
        .include_components = { viewapp_light_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t light_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &query_result.components[1], 1 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &query_result.entities );

    for ( uint32_t light_it = 0; light_it < query_result.entity_count; ++light_it ) {
        viewapp_light_component_t* light_component = se_stream_iterator_next ( &light_iterator );
        viewapp_transform_t* transform_component = se_stream_iterator_next ( &transform_iterator );

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
        .include_component_count = 2,
        .include_components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m }
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 1 );

    for ( uint32_t i = 0; i < mesh_query_result.entity_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        viewapp_transform_t* transform_component = se_stream_iterator_next ( &transform_iterator );
        mesh_component->prev_transform = *transform_component;
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

    std_tick_t new_tick = std_tick_now();
    float delta_ms = std_tick_to_milli_f32 ( new_tick - frame_tick );

    if ( delta_ms < target_frame_period ) {
        //std_thread_yield();
        std_thread_this_sleep ( 0 );
        return std_app_state_tick_m;
    }

    state->render.frame_tick = new_tick;
    state->render.frame_id += 1;
    state->render.time_ms += delta_ms;
    state->render.delta_time_ms = delta_ms;

    wm->update_window ( window );

    wm_input_state_t* input_state = &state->render.input_state;
    wm_input_state_t new_input_state;
    wm->get_window_input_state ( window, &new_input_state );

    if ( new_input_state.keyboard[wm_keyboard_state_esc_m] ) {
        return std_app_state_exit_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f2_m] && new_input_state.keyboard[wm_keyboard_state_f2_m] ) {
        return std_app_state_reload_m;
    }

    if ( !input_state->keyboard[wm_keyboard_state_f3_m] && new_input_state.keyboard[wm_keyboard_state_f3_m] ) {
        return std_app_state_reboot_m;
    }

    xg_workload_h workload = xg->create_workload ( state->render.device );

    if ( !input_state->keyboard[wm_keyboard_state_f1_m] && new_input_state.keyboard[wm_keyboard_state_f1_m] ) {
        xs->update_databases ( workload );
        if ( viewapp_render_graph_is_raytrace ( state->render.active_render_graph ) ) {
            //state->render.raytrace_world_update = true;
            viewapp_update_raytrace_world();
        }
    }

    if ( !input_state->keyboard[wm_keyboard_state_f4_m] && new_input_state.keyboard[wm_keyboard_state_f4_m] ) {
        state->render.capture_frame = true;
    }

    if ( state->render.capture_frame ) {
        //xg->debug_capture_workload ( workload );
        state->render.capture_frame = false;
    }

    if ( state->reload ) {
        viewapp_reload_graphs();
    }

    if ( state->render.new_render_graph != viewapp_render_graph_invalid_m ) {
        viewapp_load_render_graph ( state->render.new_render_graph, workload );
        state->render.new_render_graph = viewapp_render_graph_invalid_m;
    }

    wm_window_info_t new_window_info;
    wm->get_window_info ( &new_window_info, window );
    viewapp_update_ui ( &new_window_info, input_state, &new_input_state, workload );

    viewapp_update_camera ( input_state, &new_input_state, delta_ms * 1000 );

    viewapp_update_meshes();
    viewapp_update_global_transforms();

    if ( state->render.raytrace_world_update ) {
        viewapp_build_raytrace_world();
    }

    viewapp_update_lights();

    state->render.window_info = new_window_info;
    state->render.input_state = new_input_state;

    viewapp_update_workload_uniforms ( workload );

    static bool first = true;
    if ( first ) {
        //xg->debug_capture_workload ( workload );
        first = false;
    }

    xf->execute_graph ( state->render.render_graph, workload, 0 );
    xg->submit_workload ( workload );
    xg->present_swapchain ( state->render.swapchain, workload );

    return std_app_state_tick_m;
}

std_app_state_e viewapp_tick ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    if ( state->render.frame_id == 0 ) {
        viewapp_boot();
        state->render.frame_tick = std_tick_now();
        state->render.frame_id = 1;
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
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    se_i* se = state->modules.se;
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m() );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t entity_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < entity_count; ++i ) {
        se_entity_h* entity = se_stream_iterator_next ( &entity_iterator );
        viewapp_destroy_entity_resources ( *entity, workload, resource_cmd_buffer, xg_resource_cmd_buffer_time_workload_start_m );
        se->destroy_entity ( *entity );
    }

    viewapp_destroy_render_graph ( workload, resource_cmd_buffer );
    viewapp_destroy_mouse_pick_graph ( workload, resource_cmd_buffer );
    xg->destroy_swapchain ( state->render.swapchain );

    viewapp_unload_ui ( resource_cmd_buffer );

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
