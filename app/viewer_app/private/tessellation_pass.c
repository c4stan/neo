#include "tessellation_pass.h"

#include <se.h>
#include <se.inl>
#include <rv.h>
#include <xg.h>
#include <xf.h>
#include <sm_matrix.h>
#include <sm_quat.h>
#include <viewapp_state.h>

typedef struct {
    uint32_t key;
    uint32_t prim_id;
} subdivision_data_t;

typedef struct {
    uint32_t count;
    uint32_t _pad0[3];
    subdivision_data_t data[];
} subdivision_buffer_t;

typedef struct {
    uint32_t workgroup_size[3];
} indirect_dispatch_buffer_t;

typedef struct {
    uint32_t vertex_count;
    uint32_t instance_count;
    uint32_t first_vertex;
    uint32_t first_instance;
} indirect_draw_buffer_t;

typedef struct {
    sm_mat_4x4f_t world;
    sm_mat_4x4f_t prev_world;
} tessellation_vertex_uniforms_t;

typedef struct {
    float base_color[3];
    float roughness;
    float emissive[3];
    float metalness;
    uint32_t object_id;
    uint32_t mat_id;
} tessellation_fragment_uniforms_t;

typedef struct {
    xs_database_pipeline_h pipeline;
    xg_buffer_h instance_vertex_buffer;
} tessellation_draw_pass_args_t;

//
// GPU Zen 2 - Adaptive GPU Tessellation with Compute Shaders
//

static void tessellation_setup_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    viewapp_state_t* state = viewapp_state_get();
    xg_i* xg = state->modules.xg;
    se_i* se = state->modules.se;

    xg_buffer_h vbuffer = node_args->io->copy_buffer_writes[0];
    xg_buffer_h ibuffer = node_args->io->copy_buffer_writes[1];
    xg_buffer_h indirect_buffer = node_args->io->copy_buffer_writes[2];
    xg_buffer_h prev_sub_buffer = node_args->io->copy_buffer_writes[3];
    xg_buffer_h culled_sub_buffer = node_args->io->copy_buffer_writes[4];

    se_query_result_t query_result;
    se->query_entities ( &query_result, &se_query_params_m ( 
        .include_component_count = 1, 
        .include_components = { viewapp_tessellation_mesh_component_id_m } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &query_result.components[0], 0 );

    uint32_t mesh_count = query_result.entity_count;
    uint32_t enabled = mesh_count > 0 ? 1 : 0;
    uint32_t tess_prim_count = 0;
    std_assert_m ( mesh_count <= 1 );
    for ( uint64_t mesh_it = 0; mesh_it < mesh_count; ++mesh_it ) {
        viewapp_tessellation_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
            .source = mesh_component->geo_gpu_data.pos_buffer,
            .destination = vbuffer, 
        ) );
        xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
            .source = mesh_component->geo_gpu_data.idx_buffer,
            .destination = ibuffer, 
        ) );
        tess_prim_count = mesh_component->geo_data.index_count / 3;
    }

    {
        xg_buffer_range_t range = xg->write_workload_staging ( node_args->workload, std_buffer_struct_m ( &enabled ), 1 );
        xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
            .destination = indirect_buffer, 
            .source = range.handle,
            .source_offset = range.offset,
            .destination_offset = 12,
            .size = range.size,
        ) );
    }

    {
        uint32_t culled_sub_buffer_write = 0;
        xg_buffer_range_t range = xg->write_workload_staging ( node_args->workload, std_buffer_struct_m ( &culled_sub_buffer_write ), 1 );
        xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
            .destination = culled_sub_buffer, 
            .source = range.handle,
            .source_offset = range.offset,
            .destination_offset = 0,
            .size = range.size,
        ) );
    }

    static bool first_run = true;
    bool was_enabled = !first_run && !state->render.clear_history;
    if ( enabled && !was_enabled ) {
        first_run = false;

        uint32_t tess_update_indirect_data[4];
        tess_update_indirect_data[0] = std_div_round_up_u32 ( tess_prim_count, 64 );
        tess_update_indirect_data[1] = 1;
        tess_update_indirect_data[2] = 1;
        tess_update_indirect_data[3] = 1; // enabled flag
        xg_buffer_range_t indirect_range = xg->write_workload_staging ( node_args->workload, std_buffer_struct_m ( tess_update_indirect_data ), 1 );
        xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
            .destination = indirect_buffer, 
            .source = indirect_range.handle,
            .source_offset = indirect_range.offset,
            .destination_offset = 0,
            .size = indirect_range.size,
        ) );

        uint32_t tess_subdivision_data_len = 4 + tess_prim_count * 2;
        uint32_t* tess_subdivision_data = std_virtual_heap_alloc_array_m ( uint32_t, tess_subdivision_data_len );
        tess_subdivision_data[0] = tess_prim_count;
        for ( uint32_t i = 0; i < tess_prim_count; ++i ) {
            tess_subdivision_data[4 + i * 2 + 0] = 1; // key
            tess_subdivision_data[4 + i * 2 + 1] = i; // prim_id
        }

        xg_buffer_range_t subdiv_range = xg->write_workload_staging ( node_args->workload, std_buffer_array_m ( tess_subdivision_data, tess_subdivision_data_len ), 1 );
        xg->cmd_copy_buffer ( node_args->cmd_buffer, node_args->base_key, &xg_buffer_copy_params_m ( 
            .destination = prev_sub_buffer, 
            .source = subdiv_range.handle,
            .source_offset = subdiv_range.offset,
            .destination_offset = 0,
            .size = subdiv_range.size,
        ) );

        std_virtual_heap_free ( tess_subdivision_data );
    }
}

xf_node_h add_tessellation_setup_pass ( xf_graph_h graph, xf_buffer_h vertex_buffer, xf_buffer_h index_buffer, xf_buffer_h indirect_dispatch_buffer, xf_buffer_h prev_subdivision_buffer, xf_buffer_h culled_subdivision_buffer ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );
    
    xf_node_h node = xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tessellation_setup",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = tessellation_setup_pass,
        ),
        .resources = xf_node_resource_params_m (
            .copy_buffer_writes_count = 5,
            .copy_buffer_writes = { vertex_buffer, index_buffer, indirect_dispatch_buffer, prev_subdivision_buffer, culled_subdivision_buffer },
        ),
    ) );

    return node;
}

static void tessellation_draw_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    xg_resource_cmd_buffer_h resource_cmd_buffer = node_args->resource_cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );
    se_i* se = std_module_get_m ( se_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );

    std_auto_m pass_args = ( tessellation_draw_pass_args_t* ) user_args;
    xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( pass_args->pipeline );

#if 0
    se_query_result_t mesh_query_result;
    se->query_entities ( &mesh_query_result, &se_query_params_m ( 
        .include_component_count = 3, 
        .include_components = { 
            viewapp_mesh_component_id_m, 
            viewapp_transform_component_id_m,
            viewapp_tessellation_mesh_component_id_m,
        } 
    ) );
    se_stream_iterator_t mesh_iterator = se_component_iterator_m ( &mesh_query_result.components[0], 0 );
    se_stream_iterator_t transform_iterator = se_component_iterator_m ( &mesh_query_result.components[1], 1 );
    se_stream_iterator_t tessellation_iterator = se_component_iterator_m ( &mesh_query_result.components[2], 0 );
    uint64_t mesh_count = mesh_query_result.entity_count;
    std_assert_m ( mesh_count == 1 ); // TODO

    for ( uint64_t i = 0; i < mesh_count; ++i ) {
        viewapp_mesh_component_t* mesh_component = se_stream_iterator_next ( &mesh_iterator );
        viewapp_tessellation_mesh_component_t* tessellation_component = se_stream_iterator_next ( &tessellation_iterator );
        viewapp_transform_t* transform_component = se_stream_iterator_next ( &transform_iterator );

        sm_mat_4x4f_t rot = sm_quat_to_4x4f ( sm_quat ( transform_component->orientation ) );
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

        sm_mat_4x4f_t prev_rot = sm_quat_to_4x4f ( sm_quat ( mesh_component->prev_transform.orientation ) );
        float prev_scale = mesh_component->prev_transform.scale;
        sm_mat_4x4f_t prev_trans = {
            .r0[0] = prev_scale,
            .r1[1] = prev_scale,
            .r2[2] = prev_scale,
            .r3[3] = 1,
            .r0[3] = mesh_component->prev_transform.position[0],
            .r1[3] = mesh_component->prev_transform.position[1],
            .r2[3] = mesh_component->prev_transform.position[2],
        };

        tessellation_vertex_uniforms_t vs = {
            .world = sm_matrix_4x4f_mul ( trans, rot ),
            .prev_world = sm_matrix_4x4f_mul ( prev_trans, prev_rot ),
        };

        tessellation_fragment_uniforms_t fs = {
            .base_color[0] = mesh_component->material.base_color[0],
            .base_color[1] = mesh_component->material.base_color[1],
            .base_color[2] = mesh_component->material.base_color[2],
            .object_id = mesh_component->object_id,
            .roughness = mesh_component->material.roughness,
            .metalness = mesh_component->material.metalness,
            .mat_id = mesh_component->material.ssr ? 1 : 0,
            .emissive[0] = mesh_component->material.emissive[0],
            .emissive[1] = mesh_component->material.emissive[1],
            .emissive[2] = mesh_component->material.emissive[2],
        };
#else
    {
        se_query_result_t query_result;
        se->query_entities ( &query_result, &se_query_params_m ( 
            .include_component_count = 1, 
            .include_components = { viewapp_tessellation_mesh_component_id_m } 
        ) );
        //if ( query_result.entity_count == 0 ) return;

        sm_mat_4x4f_t rot = sm_quat_to_4x4f ( sm_quat_identity() );
        float scale = 1;
        sm_mat_4x4f_t trans = {
            .r0[0] = scale,
            .r1[1] = scale,
            .r2[2] = scale,
            .r3[3] = 1,
            .r0[3] = 0,
            .r1[3] = 0,
            .r2[3] = 0,
        };

        sm_mat_4x4f_t prev_rot = rot;
        //float prev_scale = scale;
        sm_mat_4x4f_t prev_trans = trans;

        tessellation_vertex_uniforms_t vs = {
            .world = sm_matrix_4x4f_mul ( trans, rot ),
            .prev_world = sm_matrix_4x4f_mul ( prev_trans, prev_rot ),
        };

        tessellation_fragment_uniforms_t fs = {
            .base_color[0] = 1,
            .base_color[1] = 1,
            .base_color[2] = 1,
            .object_id = 99,
            .roughness = 1,
            .metalness = 0,
            .mat_id = 0,
            .emissive[0] = 0,
            .emissive[1] = 0,
            .emissive[2] = 0,
        }; 
#endif

#if 0
        xg_texture_h color_texture = mesh_component->material.color_texture;
        if ( color_texture == xg_null_handle_m ) {
            color_texture = xg->get_default_texture ( node_args->device, xg_default_texture_r8g8b8a8_unorm_white_m );
        }

        xg_texture_h normal_texture = mesh_component->material.normal_texture;
        if ( normal_texture == xg_null_handle_m ) {
            normal_texture = xg->get_default_texture ( node_args->device, xg_default_texture_r8g8b8a8_unorm_tbn_up_m );
        }

        // Bind draw resources
        xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 2,
                .buffers = {
                    xg_buffer_resource_binding_m (
                        .shader_register = 0,
                        .range = xg->write_workload_uniform ( workload, &vs, sizeof ( vs ) ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 1,
                        .range = xg->write_workload_uniform ( workload, &fs, sizeof ( fs ) ),
                    )
                },
                .texture_count = 2,
                .textures = {
                    xg_texture_resource_binding_m (
                        .shader_register = 2,
                        .layout = xg_texture_layout_shader_read_m,
                        .texture = color_texture,
                    ),
                    xg_texture_resource_binding_m (
                        .shader_register = 3,
                        .layout = xg_texture_layout_shader_read_m,
                        .texture = normal_texture,
                    ),
                },
                .sampler_count = 1,
                .samplers = {
                    xg_sampler_resource_binding_m (
                        .shader_register = 4,
                        .sampler = xg->get_default_sampler ( node_args->device, xg_default_sampler_linear_wrap_m ),
                    ),
                }
            )
        ) );
#else
        // Bind draw resources
        xg_resource_bindings_h draw_bindings = xg->cmd_create_workload_bindings ( resource_cmd_buffer, &xg_resource_bindings_params_m (
            .layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m ),
            .bindings = xg_pipeline_resource_bindings_m (
                .buffer_count = 5,
                .buffers = {
                    xg_buffer_resource_binding_m (
                        .shader_register = 0,
                        .range = xg->write_workload_uniform ( workload, std_buffer_struct_m ( &vs ) ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 1,
                        .range = xg->write_workload_uniform ( workload, std_buffer_struct_m ( &fs ) ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 2,
                        .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[0] ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 3,
                        .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[1] ),
                    ),
                    xg_buffer_resource_binding_m (
                        .shader_register = 4,
                        .range = xg_buffer_range_whole_buffer_m ( node_args->io->storage_buffer_reads[2] ),
                    ),
                },
            )
        ) );
#endif

        xg->cmd_draw_indirect ( cmd_buffer, key, &xg_cmd_draw_indirect_params_m (
            .pipeline = pipeline_state,
            .bindings[xg_shader_binding_set_dispatch_m] = draw_bindings,
            .vertex_buffers_count = 1,
            .vertex_buffers = { 
                pass_args->instance_vertex_buffer, 
            },
            .indirect_buffer = node_args->io->indirect_command_read,
            .indirect_count = 1,
        ) );
    }
}

xf_node_h add_tessellation_draw_pass ( xf_graph_h graph, xf_buffer_h vertex_buffer, xf_buffer_h index_buffer, xf_buffer_h subdivision_buffer, xf_buffer_h indirect_buffer, xg_buffer_h instance_vertex_buffer, const gbuffer_textures_t* gbuffer, xf_texture_h depth, xs_database_h sdb ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );
    xs_i* xs = std_module_get_m ( xs_module_name_m );

    tessellation_draw_pass_args_t args = {
        .pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "tessellation_draw" ) ),
        .instance_vertex_buffer = instance_vertex_buffer,
    };

    xf_node_h node = xf->create_node ( graph, &xf_node_params_m (
        .debug_name = "tessellation_draw",
        .type = xf_node_type_custom_pass_m,
        .pass.custom = xf_node_custom_pass_params_m (
            .routine = tessellation_draw_pass,
            .user_args = std_buffer_struct_m ( &args ),
            .auto_renderpass = true,
        ),
        .resources = xf_node_resource_params_m (
            .render_targets_count = 6,
            .render_targets = {
                xf_render_target_dependency_m ( .texture = gbuffer->color ),
                xf_render_target_dependency_m ( .texture = gbuffer->normal ),
                xf_render_target_dependency_m ( .texture = gbuffer->material ),
                xf_render_target_dependency_m ( .texture = gbuffer->radiosity ),
                xf_render_target_dependency_m ( .texture = gbuffer->object_id ),
                xf_render_target_dependency_m ( .texture = gbuffer->velocity ),
            },
            .depth_stencil_target = depth,
            .storage_buffer_reads_count = 3,
            .storage_buffer_reads = {
                xf_shader_buffer_dependency_m ( .buffer = vertex_buffer, .stage = xg_pipeline_stage_bit_vertex_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = index_buffer, .stage = xg_pipeline_stage_bit_vertex_shader_m ), 
                xf_shader_buffer_dependency_m ( .buffer = subdivision_buffer, .stage = xg_pipeline_stage_bit_vertex_shader_m ), 
            },
            .indirect_command_read = indirect_buffer,
        ),
        .passthrough = xf_node_passthrough_params_m (
            .enable = true,
            .render_targets = {
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
                xf_texture_passthrough_m ( .mode = xf_passthrough_mode_ignore_m ),
            }
        )
    ) );

    return node;
}
