#include "viewapp_scene.h"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>

#include "viewapp_state.h"

#include <sm.h>
#include <std_file.h>

void viewapp_boot_scene ( void ) {
    viewapp_state_t* state = viewapp_state_get();

    se_i* se = state->modules.se;

    // Properties
    se->set_component_properties ( viewapp_transform_component_id_m, "Transform", &se_component_properties_params_m (
        .count = 3,
        .properties = {
            se_field_property_m ( 0, viewapp_transform_component_t, local.position, se_property_3f32_m ),
            se_field_property_m ( 0, viewapp_transform_component_t, local.orientation, se_property_4f32_m ),
            se_field_property_m ( 0, viewapp_transform_component_t, local.scale, se_property_f32_m ),
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

    se->set_component_properties ( viewapp_tessellation_mesh_component_id_m, "Tessellation", &se_component_properties_params_m (
    ) );

    // Families
    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 2,
        .components = { 
            se_component_layout_m (
                .id = viewapp_camera_component_id_m,
                .stream_count = 1,
                .streams = { sizeof ( viewapp_camera_component_t ) },
            ),
            se_component_layout_m (
                .id = viewapp_transform_component_id_m,
                .stream_count = 2,
                .streams = { 
                    std_field_size_m ( viewapp_transform_component_t, local ), 
                    std_field_size_m ( viewapp_transform_component_t, global ) 
                }
            ),
        }
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
                .stream_count = 2,
                .streams = { 
                    std_field_size_m ( viewapp_transform_component_t, local ), 
                    std_field_size_m ( viewapp_transform_component_t, global ) 
                }
            ),
        }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 2,
        .components = { 
            se_component_layout_m (
                .id = viewapp_tessellation_mesh_component_id_m,
                .streams = { sizeof ( viewapp_tessellation_mesh_component_t ) }
            ),
            se_component_layout_m (
                .id = viewapp_transform_component_id_m,
                .stream_count = 2,
                .streams = { 
                    std_field_size_m ( viewapp_transform_component_t, local ), 
                    std_field_size_m ( viewapp_transform_component_t, global ) 
                }
            ),
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
                .stream_count = 2,
                .streams = { 
                    std_field_size_m ( viewapp_transform_component_t, local ), 
                    std_field_size_m ( viewapp_transform_component_t, global ) 
                }
            ),
        }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 4,
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
                .stream_count = 2,
                .streams = { 
                    std_field_size_m ( viewapp_transform_component_t, local ), 
                    std_field_size_m ( viewapp_transform_component_t, global ) 
                }
            ),
            se_component_layout_m (
                .id = viewapp_parent_component_id_m,
                .streams = { sizeof ( viewapp_parent_component_t ) },
            )
        }
    ) );

    se->create_entity_family ( &se_entity_family_params_m (
        .component_count = 1,
        .components = {
            se_component_layout_m (
                .id = viewapp_sky_component_id_m,
                .streams = { sizeof ( viewapp_sky_component_t ) }
            )
        }
    ) );
}

static void viewapp_build_mesh_raytrace_geo ( xg_workload_h workload, se_entity_h entity, viewapp_mesh_component_t* mesh ) {
    viewapp_state_t* state = viewapp_state_get();
    if ( !state->render.supports_raytrace ) {
        return;
    }

    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;

    se_entity_properties_t entity_properties;
    se->get_entity_properties ( &entity_properties, entity );

    xg_raytrace_geometry_data_t rt_data = xg_raytrace_geometry_data_m (
        .vertex_buffer = mesh->geo_gpu_data.pos_buffer,
        .vertex_format = xg_format_r32g32b32_sfloat_m,
        .vertex_count = mesh->geo_data.vertex_count,
        .vertex_stride = 12,
        .index_buffer = mesh->geo_gpu_data.idx_buffer,
        .index_count = mesh->geo_data.index_count,
    );
    std_str_copy_static_m ( rt_data.debug_name, entity_properties.name );

    xg_raytrace_geometry_params_t rt_geo_params = xg_raytrace_geometry_params_m ( 
        .device = state->render.device,
        .geometries = &rt_data,
        .geometry_count = 1,
    );
    std_str_copy_static_m ( rt_geo_params.debug_name, entity_properties.name );
    xg_raytrace_geometry_h rt_geo = xg->create_raytrace_geometry ( workload, 0, &rt_geo_params );
    mesh->rt_geo = rt_geo;
}

static void viewapp_build_raytrace_geo ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    if ( !state->render.supports_raytrace ) {
        return;
    }

    se_i* se = state->modules.se;

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( .include_component_count = 1, .include_components = { viewapp_mesh_component_id_m } ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &mesh_query_result.entities );
    uint64_t mesh_count = mesh_query_result.entity_count;

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        
        se_entity_h* entity_handle = se_stream_iterator_next ( &entity_iterator );
        viewapp_build_mesh_raytrace_geo ( workload, *entity_handle, mesh_component );
    }
}

void viewapp_build_raytrace_world ( void ) {
#if xg_enable_raytracing_m
    viewapp_state_t* state = viewapp_state_get();
    if ( !state->render.supports_raytrace ) {
        return;
    }

    xs_database_pipeline_h rt_pipeline = viewapp_get_render_graph_raytrace_pipeline ( state->render.active_render_graph );
    if ( rt_pipeline == xs_null_handle_m ) {
        return;
    }

    xg_device_h device = state->render.device;
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;
    se_i* se = state->modules.se;

    // Must wait for any workload using the world to finish before destroying
    // TODO cmd_destroy on resource cmd buffer?
    xg->wait_all_workload_complete();
    xg_workload_h workload = xg->create_workload ( device );

    if ( state->render.raytrace_world != xg_null_handle_m ) {
        xg->destroy_raytrace_world ( state->render.raytrace_world );
    }

    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .include_component_count = 2, 
        .include_components = { viewapp_mesh_component_id_m, viewapp_transform_component_id_m } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 1 );
    uint64_t mesh_count = mesh_query_result.entity_count;

    xg_raytrace_geometry_instance_t* rt_instances = std_virtual_heap_alloc_array_m ( xg_raytrace_geometry_instance_t, mesh_count );

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        viewapp_transform_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        sm_vec_3f_t up = sm_quat_transform_f3 ( sm_quat ( transform_component->orientation ), sm_vec_3f_set ( 0, 1, 0 ) );
        sm_vec_3f_t dir = sm_quat_transform_f3 ( sm_quat ( transform_component->orientation ), sm_vec_3f_set ( 0, 0, 1 ) );
        dir = sm_vec_3f_norm ( dir );
        sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );
        float scale = transform_component->scale;
        sm_mat_4x4f_t trans = {
            .r0[0] = scale,
            .r1[1] = scale,
            .r2[2] = scale,
            .r3[3] = 1,
            .r0[3] = transform_component->position[0],
            .r1[3] = transform_component->position[1],
            .r2[3] = transform_component->position[2],
        };

        sm_mat_4x4f_t world_matrix = sm_matrix_4x4f_mul ( trans, rot );
        xg_matrix_3x4_t transform;
        std_mem_copy ( transform.f, world_matrix.e, sizeof ( float ) * 12 );

        rt_instances[i] = xg_raytrace_geometry_instance_m (
            .geometry = mesh_component->rt_geo,
            .id = i,
            .transform = transform,
            .hit_shader_group_binding = 0,
        );
    }

    // TODO
    //xs_database_pipeline_h rt_pipeline = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "restir_di_sample" ) );
    xg_raytrace_pipeline_state_h rt_pipeline_state = xs->get_pipeline_state ( rt_pipeline );

    xg_raytrace_world_h rt_world = xg->create_raytrace_world ( workload, 0, &xg_raytrace_world_params_m (
        .device = device,
        .pipeline = rt_pipeline_state,
        .instance_array = rt_instances,
        .instance_count = mesh_count,
        .debug_name = "rt_world"
    ) );

    std_virtual_heap_free ( rt_instances );

    xg->submit_workload ( workload );

    state->render.raytrace_world = rt_world;
#endif
    state->render.raytrace_world_update = false;
}

static sm_vec_3f_t viewapp_light_view_dir ( uint32_t idx ) {
    sm_vec_3f_t dir = { 0, 0, 0 };
    switch ( idx ) {
    case 0:
        dir = sm_vec_3f_set ( 0, -1, 0 ); // up
        break;
    case 1:
        dir = sm_vec_3f_set ( 0, 1, 0 ); // down
        break;
    case 2:
        dir = sm_vec_3f_set ( 0, 0, 1 ); // fw
        break;
    case 3:
        dir = sm_vec_3f_set ( 0, 0, -1 ); // bw
        break;
    case 4:
        dir = sm_vec_3f_set ( 1, 0, 0 ); // right
        break;
    case 5:
        dir = sm_vec_3f_set ( -1, 0, 0 ); // left
        break;
    default:
        std_assert_m ( false );
    }
    return dir;
}

static void add_sky_entity ( xg_device_h device, xg_workload_h workload, uint64_t key ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;

    xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 10, 10 );
    xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

    viewapp_sky_component_t sky_component = viewapp_sky_component_m (
        .geo_data = geo,
        .geo_gpu_data = gpu_data,
    );

    se->create_entity ( &se_entity_params_m (
        .debug_name = "sky",
        .update = se_entity_update_m (
            .component_count = 1,
            .components = {
                se_component_update_m (
                    .id = viewapp_sky_component_id_m,
                    .streams = {
                        se_stream_update_m ( .data = &sky_component )
                    }
                )
            }
        ),
    ) );

    if ( state->scene.active_envmap != viewapp_envmap_none_m ) {
        viewapp_load_envmap ( workload, key, state->scene.active_envmap );
    }
}

static void viewapp_boot_scene_cornell_box ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    xs_i* xs = state->modules.xs;
    rv_i* rv = state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_device_h device = state->render.device;

    // sphere
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.01,
                .metalness = 0,
            )
        );

        viewapp_transform_t transform = viewapp_transform_m (
            .position = { -1.1, -1.45, 1 },
        );

        se->create_entity( &se_entity_params_m (
            .debug_name = "sphere",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                }
            )
        ) );
    }

    // cube
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_cube ( 1.f );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = true,
                .roughness = 0.01,
                .metalness = 0,
            )
        );

        viewapp_transform_t transform = viewapp_transform_m (
            .position = { 1, -1.5, 0.5 },
            .orientation =  { 0, -0.6, 0, 0.7 },
            .scale = 2,
        );

        se->create_entity( &se_entity_params_m (
            .debug_name = "cube",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                }
            )
        ) );
    }

    // planes
    float plane_pos[5][3] = {
        { 0, 0, 2.5 },
        { 2.5, 0, 0 },
        { -2.5, 0, 0 },
        { 0, 2.5, 0 },
        { 0, -2.5, 0 }
    };

    sm_quat_t plane_rot[5] = {
        sm_quat_axis_rotation ( sm_vec_3f_set ( 1, 0, 0 ), -sm_rad_quad_m ),
        sm_quat_axis_rotation ( sm_vec_3f_set ( 0, 0, 1 ), sm_rad_quad_m ),
        sm_quat_axis_rotation ( sm_vec_3f_set ( 0, 0, 1 ), -sm_rad_quad_m ),
        sm_quat_axis_rotation ( sm_vec_3f_set ( 0, 0, 1 ), sm_rad_semi_m ),
        sm_quat_identity(),
    };

    float plane_col[5][3] = {
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf (  67 / 255.f, 2.2 ), powf ( 149 / 255.f, 2.2 ), powf (  66 / 255.f, 2.2 ) },
        { powf ( 176 / 255.f, 2.2 ), powf (  40 / 255.f, 2.2 ), powf (  48 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
        { powf ( 240 / 255.f, 2.2 ), powf ( 240 / 255.f, 2.2 ), powf ( 250 / 255.f, 2.2 ) },
    };

    for ( uint32_t i = 0; i < 5; ++i ) {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 5.f );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    plane_col[i][0],
                    plane_col[i][1],
                    plane_col[i][2],
                },
                .ssr = true,
                .roughness = 0.01,
                .metalness = 0,
            ),
        );

        viewapp_transform_t transform = viewapp_transform_m (
            .position = {
                plane_pos[i][0],
                plane_pos[i][1],
                plane_pos[i][2],
            },
            .orientation = {
                plane_rot[i].e[0],
                plane_rot[i].e[1],
                plane_rot[i].e[2],
                plane_rot[i].e[3],
            },
        );

        se->create_entity ( &se_entity_params_m (
            .debug_name = "plane",
            .update = se_entity_update_m ( 
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                }
            )
        ) );
    }

    // light
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 100, 100 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id_pipeline = object_id_pipeline_state,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = false,
                .roughness = 0.01,
                .metalness = 0,
                .emissive = { 1, 1, 1 },
            )
        );

        viewapp_transform_t transform = viewapp_transform_m (
            .position = { 0, 1.5, 0 },
        );

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .position = { 0, 1.5, 0 },
            .intensity = 5,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
            .view_count = viewapp_light_max_views_m,
        );

        for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
            sm_vec_3f_t dir = viewapp_light_view_dir ( i );
            sm_quat_t orientation = sm_quat_from_vec ( dir );
            rv_view_params_t view_params = rv_view_params_m (
                .transform = rv_view_transform_m (
                    .position = {
                        light_component.position[0],
                        light_component.position[1],
                        light_component.position[2],
                    },
                    .orientation = {
                        orientation.e[0],
                        orientation.e[1],
                        orientation.e[2],
                        orientation.e[3],
                    },
                ),
                .proj_params.perspective = rv_perspective_projection_params_m (
                    .aspect_ratio = 1,
                    .near_z = 0.01,
                    .infinite_far_z = true,
                ),
            );
            light_component.views[i] = rv->create_view ( &view_params );
        }

        se->create_entity ( &se_entity_params_m (
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 3,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &light_component ) )
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &mesh_component ) )
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &transform ) )
                    ),
                }
            )
        ) );
    }

    add_sky_entity ( device, workload, 0 );
}

static void viewapp_boot_scene_field ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    xs_i* xs = state->modules.xs;
    rv_i* rv = state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_device_h device = state->render.device;

    // ground
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_grid ( 1000, 1000, 10.f, NULL );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        // TODO
        viewapp_tessellation_mesh_component_t tessellation_component = viewapp_tessellation_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = {
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 ) 
                },
                .ssr = true,
                .roughness = 0.01,
            ),
        );

        viewapp_transform_t transform = viewapp_transform_m ();

        se->create_entity ( &se_entity_params_m(
            .debug_name = "ground",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_tessellation_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &tessellation_component ) }
                    ),
                }
            )            
        ) );
    }

    float x = -50;

    // quads
    for ( uint32_t i = 0; i < 6; ++i ) {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 10.f );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = state->render.next_object_id++,
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

        sm_quat_t rot = sm_quat_from_vec ( sm_vec_3f_set ( 0, 1, 0 ) );
        viewapp_transform_t transform = viewapp_transform_m (
            .position = { x, 10, 50 },
            .orientation = { rot.x, rot.y, rot.z, rot.w }
        );

        se->create_entity ( &se_entity_params_m (
            .debug_name = "quad",
            .update = se_entity_update_m (
                .component_count = 2,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                }
            )
        ) );

        x += 20;
    }

    // camera light
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = false,
                .roughness = 0.01,
                .metalness = 0,
                .emissive = { 1, 1, 1 },
            )
        );

        viewapp_transform_t transform = viewapp_transform_m ();

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .intensity = 1000,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
            .view_count = viewapp_light_max_views_m,
        );
        for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
            sm_quat_t orientation = sm_quat_from_vec ( viewapp_light_view_dir ( i ) );
            rv_view_params_t view_params = rv_view_params_m (
                .transform = rv_view_transform_m (
                    .position = {
                        transform.position[0],
                        transform.position[1],
                        transform.position[2],
                    },
                    .orientation = {
                        orientation.e[0],
                        orientation.e[1],
                        orientation.e[2],
                        orientation.e[3],
                    },
                ),
                .proj_params.perspective = rv_perspective_projection_params_m (
                    .aspect_ratio = 1,
                    .near_z = 0.01,
                    .infinite_far_z = true,
                ),
            );
            light_component.views[i] = rv->create_view ( &view_params );
        }

        // get flycam
        se_entity_h flycam_entity = se_null_handle_m;
        {
            se_query_result_t query_result;
            se->query_entities ( &query_result, &se_query_params_m ( .include_component_count = 1, .include_components = { 
                viewapp_camera_component_id_m,
            } ) );

            se_stream_iterator_t entity_iterator = se_entity_iterator_m ( &query_result.entities );
            se_stream_iterator_t camera_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
            for ( uint32_t i = 0; i < query_result.entity_count; ++i ) {
                viewapp_camera_component_t* camera_component = se_stream_iterator_next ( &camera_iterator );
                se_entity_h* entity = se_stream_iterator_next ( &entity_iterator );

                if ( camera_component->type == viewapp_camera_type_flycam_m ) {
                    flycam_entity = *entity;
                    break;
                }
            }
        }

        viewapp_parent_component_t parent_component = viewapp_parent_component_m ( .parent = flycam_entity );

        se->create_entity ( &se_entity_params_m ( 
            .debug_name = "camera_light",
            .update = se_entity_update_m (
                .component_count = 4,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = { se_stream_update_m ( .data = &light_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_parent_component_id_m,
                        .streams = { se_stream_update_m ( .data = &parent_component ) }
                    ),
                }
            )
        ) );
    }

    // light
    {
        xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
        xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( device, workload, &geo );

        viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
            .geo_data = geo,
            .geo_gpu_data = gpu_data,
            .geometry_pipeline = geometry_pipeline_state,
            .shadow_pipeline = shadow_pipeline_state,
            .object_id_pipeline = object_id_pipeline_state,
            .object_id = state->render.next_object_id++,
            .material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .ssr = false,
                .roughness = 0.01,
                .metalness = 0,
                .emissive = { 1, 1, 1 },
            )
        );

        viewapp_transform_t transform = viewapp_transform_m (
            .position = { 0, 30, -10 },
        );

        viewapp_light_component_t light_component = viewapp_light_component_m (
            .position = { 0, 30, -10 },
            .intensity = 2000,
            .radius = 1000,
            .color = { 1, 1, 1 },
            .shadow_casting = true,
            .view_count = viewapp_light_max_views_m,
        );
        for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
            sm_quat_t orientation = sm_quat_from_vec ( viewapp_light_view_dir ( i ) );
            rv_view_params_t view_params = rv_view_params_m (
                .transform = rv_view_transform_m (
                    .position = {
                        transform.position[0],
                        transform.position[1],
                        transform.position[2],
                    },
                    .orientation = {
                        orientation.e[0],
                        orientation.e[1],
                        orientation.e[2],
                        orientation.e[3],
                    },
                ),
                .proj_params.perspective = rv_perspective_projection_params_m (
                    .aspect_ratio = 1,
                    .near_z = 0.01,
                    .infinite_far_z = true,
                ),
            );
            light_component.views[i] = rv->create_view ( &view_params );
        }

        se->create_entity ( &se_entity_params_m ( 
            .debug_name = "light",
            .update = se_entity_update_m (
                .component_count = 3,
                .components = { 
                    se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = { se_stream_update_m ( .data = &light_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_mesh_component_id_m,
                        .streams = { se_stream_update_m ( .data = &mesh_component ) }
                    ),
                    se_component_update_m (
                        .id = viewapp_transform_component_id_m,
                        .streams = { se_stream_update_m ( .data = &transform ) }
                    ),
                }
            )
        ) );
    }
 
    add_sky_entity ( device, workload, 0 );
}

static void viewapp_create_cameras ( void ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    rv_i* rv = state->modules.rv;
    uint32_t resolution_x = state->render.resolution_x;
    uint32_t resolution_y = state->render.resolution_y;

    // view
    rv_view_params_t view_params = rv_view_params_m (
        .transform = rv_view_transform_m (
            .position = { 0, 0, -8 },
        ),
        .proj_params.perspective = rv_perspective_projection_params_m (
            .aspect_ratio = ( float ) resolution_x / ( float ) resolution_y,
            .near_z = 0.1,
            .far_z = 10000,
            .fov_y = 50.f * rv_deg_to_rad_m,
            .jitter = { 1.f / resolution_x, 1.f / resolution_y },
            .reverse_z = viewapp_main_view_reverse_z_m,
        ),
    );

    // cameras
    viewapp_camera_component_t arcball_camera_component = viewapp_camera_component_m (
        .view = rv->create_view ( &view_params ),
        .enabled = false,
        .type = viewapp_camera_type_arcball_m
    );

    viewapp_camera_component_t flycam_camera_component = viewapp_camera_component_m (
        .view = rv->create_view ( &view_params ),
        .enabled = true,
        .type = viewapp_camera_type_flycam_m
    );

    se->create_entity ( &se_entity_params_m (
        .debug_name = "arcball_camera",
        .update = se_entity_update_m (
            .component_count = 2,
            .components = { 
                se_component_update_m (
                    .id = viewapp_camera_component_id_m,
                    .streams = { se_stream_update_m ( .data = &arcball_camera_component ) }
                ),
                se_component_update_m ( .id = viewapp_transform_component_id_m ),
            }
        )
    ) );

    se->create_entity ( &se_entity_params_m (
        .debug_name = "flycam_camera",
        .update = se_entity_update_m (
            .component_count = 2,
            .components = { 
                se_component_update_m (
                    .id = viewapp_camera_component_id_m,
                    .streams = { se_stream_update_m ( .data = &flycam_camera_component ) }
                ),
                se_component_update_m ( .id = viewapp_transform_component_id_m ),
            }
        )
    ) );
}

#include <xg_enum.h>

typedef struct {
    char* data;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    xg_format_e format;
} viewapp_texture_t;

static void* viewapp_stbi_realloc ( void* p, size_t old_size, size_t new_size ) {
    void* new = std_virtual_heap_alloc_m ( new_size, 16 );
    std_mem_copy ( new, p, old_size );
    std_virtual_heap_free ( p );
    return new;
}

#define STBI_MALLOC(sz) std_virtual_heap_alloc_m ( sz, 16 )
#define STBI_FREE(p) std_virtual_heap_free ( p )
#define STBI_REALLOC_SIZED(p,oldsz,newsz) viewapp_stbi_realloc ( p, oldsz, newsz )
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
static viewapp_texture_t viewapp_import_texture ( const struct aiScene* scene, const char* path ) {
    void* data;
    xg_format_e format;
    int width, height, channels;

    char* star = std_str_find ( path, "*" );
    if ( star ) {
        channels = 4; // TODO
        uint32_t texture_idx = std_str_to_u32 ( star );
        std_assert_m ( texture_idx < scene->mNumTextures );
        struct aiTexture* embedded_texture = scene->mTextures[texture_idx];
        if ( embedded_texture->mHeight == 0 ) {
            // Compressed texture (e.g., PNG/JPG embedded)
            data = stbi_load_from_memory ( ( stbi_uc*) embedded_texture->pcData, embedded_texture->mWidth, &width, &height, NULL, channels);
        } else {
            // Raw uncompressed texture
            data = embedded_texture->pcData;
            width = embedded_texture->mWidth;
            height = embedded_texture->mHeight;
        }
    } else {
        if ( !stbi_info ( path, &width, &height, &channels ) ) {
            std_log_error_m ( "failed to read texture " std_fmt_str_m, path );
            return ( viewapp_texture_t ) {0};
        }
        if ( channels == 3 ) channels = 4;
        data = stbi_load ( path, &width, &height, NULL, channels );
        std_assert_m ( data );
    }

    if ( channels == 4 ) {
        format = xg_format_r8g8b8a8_unorm_m;
    } else if ( channels == 2 ) {
        format = xg_format_r8g8_unorm_m;
    } else if ( channels == 1 ) {
        format = xg_format_r8_unorm_m;
    } else {
        format = xg_format_undefined_m;
    }

    viewapp_texture_t texture = {
        .data = data,
        .width = width,
        .height = height,
        .channels = channels,
        .format = format
    };
    return texture;
}

static xg_texture_h viewapp_upload_texture_to_gpu ( xg_cmd_buffer_h cmd_buffer, uint64_t key, xg_resource_cmd_buffer_h resource_cmd_buffer, const viewapp_texture_t* texture, const char* name ) {
    viewapp_state_t* state = viewapp_state_get();
    uint32_t mip_levels = texture->width > ( 1 << 7 ) ? 7 : 1;
    xg_i* xg = state->modules.xg;
    xg_texture_params_t params = xg_texture_params_m ( 
        .memory_type = xg_memory_type_gpu_only_m,
        .device = state->render.device,
        .width = texture->width,
        .height = texture->height,
        .format = texture->format,
        .allowed_usage = xg_texture_usage_bit_sampled_m | xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_copy_source_m,
        .mip_levels = mip_levels,
    );
    std_str_copy_static_m ( params.debug_name, name );
    //std_path_name ( params.debug_name, sizeof ( params.debug_name ), path );

    xg_texture_h texture_handle = xg->cmd_create_texture ( resource_cmd_buffer, &params, &xg_texture_init_m (
        .mode = xg_texture_init_mode_upload_m,
        .upload.data = texture->data,
        .final_layout = mip_levels > 1 ? xg_texture_layout_copy_dest_m : xg_texture_layout_shader_read_m,
    ) );

    if ( mip_levels > 1 ) {
        for ( uint32_t i = 1; i < mip_levels; ++i ) {
            xg->cmd_barrier_set ( cmd_buffer, key, &xg_barrier_set_m (
                .texture_memory_barriers_count = 1,
                .texture_memory_barriers = &xg_texture_memory_barrier_m (
                    .texture = texture_handle,
                    .mip_base = i - 1,
                    .mip_count = 1,
                    .layout.old = xg_texture_layout_copy_dest_m,
                    .layout.new = xg_texture_layout_copy_source_m,
                    .memory.flushes = xg_memory_access_bit_transfer_write_m,
                    .memory.invalidations = xg_memory_access_bit_transfer_read_m,
                    .execution.blocker = xg_pipeline_stage_bit_transfer_m,
                    .execution.blocked = xg_pipeline_stage_bit_transfer_m,
                ),
            ) );

            xg->cmd_copy_texture ( cmd_buffer, key, &xg_texture_copy_params_m (
                .source = xg_texture_copy_resource_m ( .texture = texture_handle, .mip_base = i - 1 ),
                .destination = xg_texture_copy_resource_m ( .texture = texture_handle, .mip_base = i ),
                .mip_count = 1,
                .filter = xg_sampler_filter_linear_m,
            ) );
        }

        xg->cmd_barrier_set ( cmd_buffer, key, &xg_barrier_set_m (
            .texture_memory_barriers_count = 2,
            .texture_memory_barriers = ( xg_texture_memory_barrier_t[2] ) { 
                xg_texture_memory_barrier_m (
                    .texture = texture_handle,
                    .mip_base = 0,
                    .mip_count = mip_levels - 1,
                    .layout.old = xg_texture_layout_copy_source_m,
                    .layout.new = xg_texture_layout_shader_read_m,
                    .memory.flushes = xg_memory_access_bit_transfer_write_m,
                    .memory.invalidations = xg_memory_access_bit_shader_read_m,
                    .execution.blocker = xg_pipeline_stage_bit_transfer_m,
                    .execution.blocked = xg_pipeline_stage_bit_fragment_shader_m,
                ),
                xg_texture_memory_barrier_m (
                    .texture = texture_handle,
                    .mip_base = mip_levels - 1,
                    .mip_count = 1,
                    .layout.old = xg_texture_layout_copy_dest_m,
                    .layout.new = xg_texture_layout_shader_read_m,
                    .memory.flushes = xg_memory_access_bit_transfer_write_m,
                    .memory.invalidations = xg_memory_access_bit_shader_read_m,
                    .execution.blocker = xg_pipeline_stage_bit_transfer_m,
                    .execution.blocked = xg_pipeline_stage_bit_fragment_shader_m,
                ),
            }
        ) );
    }

    return texture_handle;
}

static void viewapp_import_scene ( xg_workload_h workload, uint64_t key, const char* input_path ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;
    xg_i* xg = state->modules.xg;
    xs_i* xs = state->modules.xs;

    unsigned int flags = 0;
    flags |= aiProcess_MakeLeftHanded;
    flags |= aiProcess_FlipWindingOrder;
    flags |= aiProcess_FlipUVs;
    flags |= aiProcess_JoinIdenticalVertices;
    flags |= aiProcess_Triangulate;
    flags |= aiProcess_ValidateDataStructure;
    flags |= aiProcess_GenSmoothNormals;
    //flags |= aiProcess_GenNormals;
    flags |= aiProcess_FindInvalidData;
    flags |= aiProcess_PreTransformVertices;
    flags |= aiProcess_CalcTangentSpace;

    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );

    std_tick_t start_tick = std_tick_now();
    std_log_info_m ( "Importing input scene " std_fmt_str_m, input_path );
    const struct aiScene* scene = aiImportFile ( input_path, flags );

    if ( scene == NULL ) {
        const char* error = aiGetErrorString();
        std_log_error_m ( "Error importing file: " std_fmt_str_m, error );
    } else {
        xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
        xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
        xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

        for ( uint32_t mesh_it = 0; mesh_it < scene->mNumMeshes; ++mesh_it ) {
            const struct aiMesh* mesh = scene->mMeshes[mesh_it];
            xg_geo_util_geometry_data_t geo;
            geo.vertex_count = mesh->mNumVertices;
            geo.index_count = mesh->mNumFaces * 3;
            geo.pos = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.nor = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.tan = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.bitan = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 3 );
            geo.uv = std_virtual_heap_alloc_array_m ( float, geo.vertex_count * 2 );
            geo.idx = std_virtual_heap_alloc_array_m ( uint32_t, geo.index_count );

            for ( uint32_t i = 0; i < mesh->mNumVertices; i++ ) {
                geo.pos[i * 3 + 0] = mesh->mVertices[i].x;
                geo.pos[i * 3 + 1] = mesh->mVertices[i].y;
                geo.pos[i * 3 + 2] = mesh->mVertices[i].z;

                geo.nor[i * 3 + 0] = mesh->mNormals[i].x;
                geo.nor[i * 3 + 1] = mesh->mNormals[i].y;
                geo.nor[i * 3 + 2] = mesh->mNormals[i].z;

                geo.tan[i * 3 + 0] = mesh->mTangents[i].x;
                geo.tan[i * 3 + 1] = mesh->mTangents[i].y;
                geo.tan[i * 3 + 2] = mesh->mTangents[i].z;
            
                geo.bitan[i * 3 + 0] = mesh->mBitangents[i].x;
                geo.bitan[i * 3 + 1] = mesh->mBitangents[i].y;
                geo.bitan[i * 3 + 2] = mesh->mBitangents[i].z;
            }

            if ( mesh->mTextureCoords[0] ) {
                for ( uint32_t i = 0; i < mesh->mNumVertices; i++ ) {
                    geo.uv[i * 2 + 0] = mesh->mTextureCoords[0][i].x;
                    geo.uv[i * 2 + 1] = mesh->mTextureCoords[0][i].y;
                }
            } else {
                for ( uint32_t i = 0; i < mesh->mNumVertices; i += 2 ) {
                    geo.uv[i * 2 + 0] = 0;
                    geo.uv[i * 2 + 1] = 0;
                }
            }

            for ( uint32_t i = 0; i < mesh->mNumFaces; i++ ) {
                struct aiFace face = mesh->mFaces[i];
                geo.idx[i * 3 + 0] = face.mIndices[0];
                geo.idx[i * 3 + 1] = face.mIndices[1];
                geo.idx[i * 3 + 2] = face.mIndices[2];
            }

            xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( state->render.device, workload, &geo );

            viewapp_material_data_t mesh_material = viewapp_material_data_m (
                .base_color = { 
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 240 / 255.f, 2.2 ),
                    powf ( 250 / 255.f, 2.2 )
                },
                .roughness = 1,
                .metalness = 0,
                .ssr = false,
            );

            uint32_t mat_idx = mesh->mMaterialIndex;
            if ( mat_idx < scene->mNumMaterials ) {
                struct aiMaterial* material = scene->mMaterials[mat_idx];
                struct aiColor4D diffuse;
                if ( aiGetMaterialColor ( material, AI_MATKEY_BASE_COLOR, &diffuse ) == AI_SUCCESS ) {
                    mesh_material.base_color[0] = diffuse.r;
                    mesh_material.base_color[1] = diffuse.g;
                    mesh_material.base_color[2] = diffuse.b;
                }

                viewapp_texture_t color_texture = {};
                struct aiString color_texture_name;
                if ( aiGetMaterialTexture ( material, aiTextureType_DIFFUSE, 0, &color_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS ) {
                    char texture_path_buffer[256];
                    std_string_t texture_path = std_static_string_m ( texture_path_buffer );
                    std_path_normalize ( &texture_path, input_path );
                    std_path_pop ( &texture_path );
                    std_path_append ( &texture_path, color_texture_name.data );
                    color_texture = viewapp_import_texture ( scene, texture_path.str );
                }

                viewapp_texture_t normal_texture = {};
                struct aiString normal_texture_name;
                if ( aiGetMaterialTexture ( material, aiTextureType_NORMALS, 0, &normal_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS ) {
                    char texture_path_buffer[256];
                    std_string_t texture_path = std_static_string_m ( texture_path_buffer );
                    std_path_normalize ( &texture_path, input_path );
                    std_path_pop ( &texture_path );
                    std_path_append ( &texture_path, normal_texture_name.data );
                    normal_texture = viewapp_import_texture ( scene, texture_path.str );
                }

                struct aiString metalness_texture_name;
                struct aiString roughness_texture_name;
                bool has_metalness = aiGetMaterialTexture ( material, AI_MATKEY_METALLIC_TEXTURE, &metalness_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS;
                bool has_roughness = aiGetMaterialTexture ( material, AI_MATKEY_ROUGHNESS_TEXTURE, &roughness_texture_name, NULL, NULL, NULL, NULL, NULL, NULL ) == AI_SUCCESS;
                if ( has_roughness && has_metalness ) {
                    std_assert_m ( std_str_cmp ( metalness_texture_name.data, roughness_texture_name.data ) == 0 );
                    char texture_path_buffer[256];
                    std_string_t texture_path = std_static_string_m ( texture_path_buffer );
                    std_path_normalize ( &texture_path, input_path );
                    std_path_pop ( &texture_path );
                    std_path_append ( &texture_path, roughness_texture_name.data );
                    viewapp_texture_t material_texture = viewapp_import_texture ( scene, texture_path.str );
                    
                    for ( uint32_t i = 0; i < material_texture.width * material_texture.height; ++i ) {
                        // Following the glTF spec for metallicRoughnessTexture
                        char metalness = material_texture.data[i * 4 + 2];
                        char roughness = material_texture.data[i * 4 + 1];

                        if ( color_texture.data ) {
                            color_texture.data[i * 4 + 3] = metalness;
                        }
                        if ( normal_texture.data ) {
                            normal_texture.data[i * 4 + 3] = roughness;
                        }
                    }

                    STBI_FREE ( material_texture.data );
                }

                if ( color_texture.data ) {
                    mesh_material.color_texture = viewapp_upload_texture_to_gpu ( cmd_buffer, key, resource_cmd_buffer, &color_texture, color_texture_name.data );
                    STBI_FREE ( color_texture.data );
                }

                if ( normal_texture.data ) {
                    mesh_material.normal_texture = viewapp_upload_texture_to_gpu ( cmd_buffer, key, resource_cmd_buffer, &normal_texture, normal_texture_name.data );
                    STBI_FREE ( normal_texture.data );
                }

                // These are just scalars added on top of the texture values...
                // TODO need a way to take full control from debug view and ignore texture values
                aiGetMaterialFloat ( material, AI_MATKEY_ROUGHNESS_FACTOR, &mesh_material.roughness );
                aiGetMaterialFloat ( material, AI_MATKEY_METALLIC_FACTOR, &mesh_material.metalness );
            }

            viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
                .geo_data = geo,
                .geo_gpu_data = gpu_data,
                .object_id_pipeline = object_id_pipeline_state,
                .geometry_pipeline = geometry_pipeline_state,
                .shadow_pipeline = shadow_pipeline_state,
                .object_id = state->render.next_object_id++,
                .material = mesh_material,
            );

            viewapp_transform_t transform = viewapp_transform_m (
                .position = { 0, 0, 0 },
            );

            se_entity_params_t entity_params = se_entity_params_m (
                .update = se_entity_update_m (
                    .component_count = 2,
                    .components = { 
                        se_component_update_m (
                            .id = viewapp_mesh_component_id_m,
                            .streams = { se_stream_update_m ( .data = &mesh_component ) }
                        ),
                        se_component_update_m (
                            .id = viewapp_transform_component_id_m,
                            .streams = { se_stream_update_m ( .data = &transform ) }
                        ),
                    }
                )
            );
            std_str_copy_static_m ( entity_params.debug_name, mesh->mName.data );
            se->create_entity ( &entity_params );
        }

        for ( uint32_t light_it = 0; light_it < scene->mNumLights; ++light_it ) {
            const struct aiLight* light = scene->mLights[light_it];
            
            viewapp_light_component_t light_component = viewapp_light_component_m (
                .position = { light->mPosition.x, light->mPosition.y, light->mPosition.z },
                .intensity = 5,
                .color = { light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b },
                .shadow_casting = false,
                .view_count = 6,
            );

            // TOOD
            se_entity_params_t entity_params = se_entity_params_m (
                .update = se_entity_update_m (
                    .components = { se_component_update_m (
                        .id = viewapp_light_component_id_m,
                        .streams = ( se_stream_update_m ( .data = &light_component ) )
                    ) }
                )
            );
            std_str_copy_static_m ( entity_params.debug_name, light->mName.data );
            se->create_entity ( &entity_params );
        }
    }

    aiReleaseImport ( scene );

    std_tick_t end_tick = std_tick_now();
    float time_ms = std_tick_to_milli_f32 ( end_tick - start_tick );
    std_log_info_m ( "Scene imported in " std_fmt_f32_dec_m(3) "s", time_ms / 1000.f );
}

void viewapp_update_raytrace_world ( void ) {
#if xg_enable_raytracing_m
    viewapp_state_t* state = viewapp_state_get();
    state->render.raytrace_world_update = true;
#endif
}

void viewapp_destroy_entity_resources ( se_entity_h entity, xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, xg_resource_cmd_buffer_time_e time ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;

    viewapp_mesh_component_t* mesh_component = se->get_entity_component ( entity, viewapp_mesh_component_id_m, 0 );
    if ( mesh_component ) {
        xg_geo_util_free_data ( &mesh_component->geo_data );
        xg_geo_util_free_gpu_data ( &mesh_component->geo_gpu_data, workload, time );

        viewapp_material_data_t* material = &mesh_component->material;
        if ( material->color_texture != xg_null_handle_m ) {
            xg->cmd_destroy_texture ( resource_cmd_buffer, material->color_texture, time );
        }
        if ( material->normal_texture != xg_null_handle_m ) {
            xg->cmd_destroy_texture ( resource_cmd_buffer, material->normal_texture, time );
        }
        if ( material->metalness_roughness_texture != xg_null_handle_m ) {
            xg->cmd_destroy_texture ( resource_cmd_buffer, material->metalness_roughness_texture, time );
        }

        if ( mesh_component->rt_geo != xg_null_handle_m ) {
            xg->destroy_raytrace_geometry ( mesh_component->rt_geo );
        }
    }

    viewapp_sky_component_t* sky_component = se->get_entity_component ( entity, viewapp_sky_component_id_m, 0 );
    if ( sky_component ) {
        xg_geo_util_free_data ( &sky_component->geo_data );
        xg_geo_util_free_gpu_data ( &sky_component->geo_gpu_data, workload, time );
        if ( sky_component->radiance_texture != xg_null_handle_m ) {
            xg->cmd_destroy_texture ( resource_cmd_buffer, sky_component->radiance_texture, time );
        }
    }

    viewapp_tessellation_mesh_component_t* tessellation_mesh_component = se->get_entity_component ( entity, viewapp_tessellation_mesh_component_id_m, 0 );
    if ( tessellation_mesh_component ) {
        xg_geo_util_free_data ( &tessellation_mesh_component->geo_data );
        xg_geo_util_free_gpu_data ( &tessellation_mesh_component->geo_gpu_data, workload, time );
    }
}

void viewapp_load_scene ( viewapp_scene_e scene ) {
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

    viewapp_create_cameras();

    if ( scene == 0 ) {
        viewapp_boot_scene_cornell_box ( workload );
    } else if ( scene == 1 ) {
        viewapp_boot_scene_field ( workload );
    } else {
        std_assert_m ( scene == viewapp_scene_external_m );
        viewapp_import_scene ( workload, 0, state->scene.custom_scene_path );
        add_sky_entity ( state->render.device, workload, 0 );
    }

    state->scene.active_scene = scene;
    viewapp_build_raytrace_geo ( workload );
    xg->submit_workload ( workload );
    xg->wait_all_workload_complete(); // TODO

    if ( viewapp_render_graph_is_raytrace ( state->render.active_render_graph ) ) 
    {
        viewapp_update_raytrace_world();
        //viewapp_build_raytrace_world();
    }
}

// https://www.ppsloan.org/publications/shdering.pdf
static float sh_sloan_window ( float l, float w ) {
    if ( l == 0 ) {
        return 1.0f;
    }

    float x = 3.1415f * l / w;
    return sinf ( x ) / x;
}

// https://cseweb.ucsd.edu/~ravir/papers/envmap/envmap.pdf
static void compute_sh_basis ( float* out_basis, const float* dir ) {
    float x = dir[0];
    float y = dir[1];
    float z = dir[2];

    out_basis[0] = 0.282095f;

    out_basis[1] = 0.488603f * y;
    out_basis[2] = 0.488603f * z;
    out_basis[3] = 0.488603f * x;

    out_basis[4] = 1.092548f * x * y;
    out_basis[5] = 1.092548f * y * z;
    out_basis[6] = 0.315392f * ( 3.f * z * z - 1.f );
    out_basis[7] = 1.092548f * x * z;
    out_basis[8] = 0.546274f * ( x * x - y * y );
}

static void viewapp_envmap_to_sh_irradiance ( float* sh_out, const viewapp_texture_t* texture ) {
    std_mem_zero_array_m ( sh_out, 3*9 );

    // compute radiance sh
    float delta_phi = 2.f * 3.1415f / ( float ) texture->width;
    float delta_theta = 3.1415f / ( float ) texture->height;

    for ( uint32_t y = 0; y < texture->height; ++y ) {
        float theta = ( ( float ) y + 0.5f ) / ( float ) texture->height * 3.1415f;
        float sin_theta = sinf ( theta );
        float cos_theta = cosf ( theta );

        for ( uint32_t x = 0; x < texture->width; ++x ) {
            float phi = ( ( float ) x + 0.5f ) / ( float ) texture->width * 2.f * 3.1415f;
            float sin_phi = sinf ( phi );
            float cos_phi = cosf ( phi );

            sm_vec_3f_t dir = sm_vec_3f_set ( sin_theta * cos_phi, cos_theta, sin_theta * sin_phi );
            float delta_omega = delta_phi * delta_theta * sin_theta;

            uint32_t idx = ( y * texture->width + x ) * 4;
            sm_vec_3f_t radiance = sm_vec_3f ( ( ( float* ) texture->data ) + idx );

            float sh_basis[9];
            compute_sh_basis ( sh_basis, dir.e );

            for ( uint32_t i = 0; i < 9; ++i ) {
                sm_vec_3f_t contrib = sm_vec_3f_mul ( radiance, sh_basis[i] * delta_omega );
                sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
                sh = sm_vec_3f_add ( sh, contrib );
                sm_vec_3f_store ( &sh_out[i * 3], sh );
            }
        }
    }

    // convolve radiance to irradiance
    {
        float h0 = 3.1415f;
        float h1 = 2.f * 3.1415f / 3.f;
        float h2 = 3.1415f / 4.f;

        uint32_t i = 0;
        sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
        sh = sm_vec_3f_mul ( sh, h0 );
        sm_vec_3f_store ( &sh_out[i * 3], sh );
        ++i;

        for ( ; i < 4; ++i ) {
            sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
            sh = sm_vec_3f_mul ( sh, h1 );
            sm_vec_3f_store ( &sh_out[i * 3], sh );
        }

        for ( ; i < 9; ++i ) {
            sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
            sh = sm_vec_3f_mul ( sh, h2 );
            sm_vec_3f_store ( &sh_out[i * 3], sh );
        }
    }

    // Sloan windowing
    {
        float w0 = sh_sloan_window ( 0.0, 3.0 );
        float w1 = sh_sloan_window ( 1.0, 3.0 );
        float w2 = sh_sloan_window ( 2.0, 3.0 );

        uint32_t i = 0;
        sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
        sh = sm_vec_3f_mul ( sh, w0 );
        sm_vec_3f_store ( &sh_out[i * 3], sh );
        ++i;

        for ( ; i < 4; ++i ) {
            sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
            sh = sm_vec_3f_mul ( sh, w1 );
            sm_vec_3f_store ( &sh_out[i * 3], sh );
        }

        for ( ; i < 9; ++i ) {
            sm_vec_3f_t sh = sm_vec_3f ( &sh_out[i * 3] );
            sh = sm_vec_3f_mul ( sh, w2 );
            sm_vec_3f_store ( &sh_out[i * 3], sh );
        }
    }
}

typedef struct {
    xg_texture_h radiance_texture;
    float irradiance_sh[9*3];
} viewapp_envmap_import_result_t;

viewapp_envmap_import_result_t viewapp_import_envmap ( xg_workload_h workload, xg_resource_cmd_buffer_h resource_cmd_buffer, uint64_t key, const char* path ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;

    std_tick_t start_tick = std_tick_now();
    std_log_info_m ( "Importing input envmap " std_fmt_str_m, path );

    int width, height;
    int channels = 4;
    void* data = stbi_loadf ( path, &width, &height, NULL, channels );
    viewapp_texture_t texture = {
        .data = data,
        .width = width,
        .height = height,
        .channels = channels,
        .format = xg_format_r32g32b32a32_sfloat_m
    };
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_texture_h texture_handle = viewapp_upload_texture_to_gpu ( cmd_buffer, key, resource_cmd_buffer, &texture, "sky_radiance" );
    viewapp_envmap_import_result_t result;
    result.radiance_texture = texture_handle;
    viewapp_envmap_to_sh_irradiance ( result.irradiance_sh, &texture );
    STBI_FREE ( data );

    std_tick_t end_tick = std_tick_now();
    float time_ms = std_tick_to_milli_f32 ( end_tick - start_tick );
    std_log_info_m ( "Envmap imported in " std_fmt_f32_dec_m(3) "s", time_ms / 1000.f );

    return result;
}

uint64_t viewapp_load_envmap ( xg_workload_h workload, uint64_t key, viewapp_envmap_e envmap ) {
    viewapp_state_t* state = viewapp_state_get();
    se_i* se = state->modules.se;

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( .include_component_count = 1, .include_components = { viewapp_sky_component_id_m } ) );
    if ( query_result.entity_count == 0 ) {
        return key;
    }
    std_assert_m ( query_result.entity_count == 1 );
    se_stream_iterator_t sky_component_iterator = se_component_iterator_m ( &query_result.components[0], 0 );
    viewapp_sky_component_t* sky_component = se_stream_iterator_next ( &sky_component_iterator );

    xg_i* xg = state->modules.xg;
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
    if ( sky_component->radiance_texture != xg_null_handle_m ) {
        xg->cmd_destroy_texture ( resource_cmd_buffer, sky_component->radiance_texture, xg_resource_cmd_buffer_time_workload_complete_m );
        sky_component->radiance_texture = xg_null_handle_m;
    }

    if ( envmap == viewapp_envmap_none_m ) {
        std_mem_zero_static_array_m ( sky_component->irradiance_sh );
        key = viewapp_render_update_sky ( sky_component, workload, key );
    } else {
        std_assert_m ( envmap == viewapp_envmap_external_m );
        viewapp_envmap_import_result_t result = viewapp_import_envmap ( workload, resource_cmd_buffer, key, state->scene.envmap_path );
        sky_component->radiance_texture = result.radiance_texture;
        std_mem_copy_static_array_m ( sky_component->irradiance_sh, result.irradiance_sh );
        key = viewapp_render_update_sky ( sky_component, workload, key );
    }

    state->scene.active_envmap = envmap;
    return key;
}

se_entity_h spawn_plane ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    se_i* se = state->modules.se;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_geo_util_geometry_data_t geo = xg_geo_util_generate_plane ( 1.f );
    xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( state->render.device, workload, &geo );

    viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
        .geo_data = geo,
        .geo_gpu_data = gpu_data,
        .object_id_pipeline = object_id_pipeline_state,
        .geometry_pipeline = geometry_pipeline_state,
        .shadow_pipeline = shadow_pipeline_state,
        .object_id = state->render.next_object_id++,
        .material = viewapp_material_data_m (
            .base_color = {
                powf ( 240 / 255.f, 2.2 ),
                powf ( 240 / 255.f, 2.2 ),
                powf ( 250 / 255.f, 2.2 )
            },
            .ssr = false,
            .roughness = 0.01,
            .metalness = 0,
        )
    );

    viewapp_transform_t transform = viewapp_transform_m (
        .position = { 0, 0, 0 },
    );

    se_entity_h entity = se->create_entity( &se_entity_params_m (
        .debug_name = "plane",
        .update = se_entity_update_m (
            .component_count = 2,
            .components = {
                se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ),
                se_component_update_m (
                    .id = viewapp_transform_component_id_m,
                    .streams = { se_stream_update_m ( .data = &transform ) }
                ),
            }
        )
    ) );

    viewapp_mesh_component_t* mesh = se->get_entity_component ( entity, viewapp_mesh_component_id_m, 0 );
    viewapp_build_mesh_raytrace_geo ( workload, entity, mesh );
    viewapp_update_raytrace_world();
    return entity;
}

se_entity_h spawn_sphere ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    se_i* se = state->modules.se;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
    xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( state->render.device, workload, &geo );

    viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
        .geo_data = geo,
        .geo_gpu_data = gpu_data,
        .object_id_pipeline = object_id_pipeline_state,
        .geometry_pipeline = geometry_pipeline_state,
        .shadow_pipeline = shadow_pipeline_state,
        .object_id = state->render.next_object_id++,
        .material = viewapp_material_data_m (
            .base_color = {
                powf ( 240 / 255.f, 2.2 ),
                powf ( 240 / 255.f, 2.2 ),
                powf ( 250 / 255.f, 2.2 )
            },
            .ssr = false,
            .roughness = 0.01,
            .metalness = 0,
        )
    );

    viewapp_transform_t transform = viewapp_transform_m (
        .position = { 0, 0, 0 },
    );


    se_entity_h entity = se->create_entity( &se_entity_params_m (
        .debug_name = "sphere",
        .update = se_entity_update_m (
            .component_count = 2,
            .components = {
                se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ),
                se_component_update_m (
                    .id = viewapp_transform_component_id_m,
                    .streams = { se_stream_update_m ( .data = &transform ) }
                ),
            }
        )
    ) );

    viewapp_mesh_component_t* mesh = se->get_entity_component ( entity, viewapp_mesh_component_id_m, 0 );
    viewapp_build_mesh_raytrace_geo ( workload, entity, mesh );
    viewapp_update_raytrace_world();
    return entity;
}

se_entity_h spawn_light ( xg_workload_h workload ) {
    viewapp_state_t* state = viewapp_state_get();
    xs_i* xs = state->modules.xs;
    se_i* se = state->modules.se;
    rv_i* rv = state->modules.rv;

    xs_database_pipeline_h geometry_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "geometry" ) );
    xs_database_pipeline_h shadow_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "shadow" ) );
    xs_database_pipeline_h object_id_pipeline_state = xs->get_database_pipeline ( state->render.sdb, xs_hash_static_string_m ( "object_id" ) );

    xg_geo_util_geometry_data_t geo = xg_geo_util_generate_sphere ( 1.f, 300, 300 );
    xg_geo_util_geometry_gpu_data_t gpu_data = xg_geo_util_upload_geometry_to_gpu ( state->render.device, workload, &geo );

    viewapp_mesh_component_t mesh_component = viewapp_mesh_component_m (
        .geo_data = geo,
        .geo_gpu_data = gpu_data,
        .object_id_pipeline = object_id_pipeline_state,
        .geometry_pipeline = geometry_pipeline_state,
        .shadow_pipeline = shadow_pipeline_state,
        .object_id = state->render.next_object_id++,
        .material = viewapp_material_data_m (
            .base_color = { 
                powf ( 240 / 255.f, 2.2 ),
                powf ( 240 / 255.f, 2.2 ),
                powf ( 250 / 255.f, 2.2 )
            },
            .ssr = false,
            .roughness = 0.01,
            .metalness = 0,
            .emissive = { 1, 1, 1 },
        )
    );

    viewapp_transform_t transform = viewapp_transform_m (
        .position = { 0, 0, 0 },
        .scale = 0.1,
    );

    viewapp_light_component_t light_component = viewapp_light_component_m (
        .position = { 0, 0, 0 },
        .intensity = 5,
        .color = { 1, 1, 1 },
        .shadow_casting = true,
        .view_count = viewapp_light_max_views_m,
    );

    for ( uint32_t i = 0; i < viewapp_light_max_views_m; ++i ) {
        sm_quat_t orientation = sm_quat_from_vec ( viewapp_light_view_dir ( i ) );
        rv_view_params_t view_params = rv_view_params_m (
            .transform = rv_view_transform_m (
                .position = {
                    light_component.position[0],
                    light_component.position[1],
                    light_component.position[2],
                },
                .orientation = {
                    orientation.e[0],
                    orientation.e[1],
                    orientation.e[2],
                    orientation.e[3],
                },
            ),
            .proj_params.perspective = rv_perspective_projection_params_m (
                .aspect_ratio = 1,
                .near_z = 0.01,
                .infinite_far_z = true,
            ),
        );
        light_component.views[i] = rv->create_view ( &view_params );
    }

    se_entity_h entity = se->create_entity( &se_entity_params_m (
        .debug_name = "light",
        .update = se_entity_update_m (
            .component_count = 3,
            .components = { 
                se_component_update_m (
                    .id = viewapp_mesh_component_id_m,
                    .streams = { se_stream_update_m ( .data = &mesh_component ) }
                ),
                se_component_update_m (
                    .id = viewapp_transform_component_id_m,
                    .streams = { se_stream_update_m ( .data = &transform ) }
                ),
                se_component_update_m (
                    .id = viewapp_light_component_id_m,
                    .streams = { se_stream_update_m ( .data = &light_component ) }
                ),
            }
        )
    ) );

    viewapp_mesh_component_t* mesh = se->get_entity_component ( entity, viewapp_mesh_component_id_m, 0 );
    viewapp_build_mesh_raytrace_geo ( workload, entity, mesh );
    viewapp_update_raytrace_world();
    return entity;
}
