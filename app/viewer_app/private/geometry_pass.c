#include "geometry_pass.h"

#include <std_log.h>

#include <se.h>
#include <rv.h>
#include <sm_matrix.h>

#include <viewapp_state.h>

static void clear_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_unused_m ( user_args );
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    {
        xf_copy_texture_resource_t depth_stencil = node_args->io->copy_texture_writes[0];
        xg_depth_stencil_clear_t ds_clear;
        ds_clear.depth = 1;
        ds_clear.stencil = 0;
        xg->cmd_clear_depth_stencil_texture ( cmd_buffer, depth_stencil.texture, ds_clear, key );
    }

    {
        xf_copy_texture_resource_t color_target = node_args->io->copy_texture_writes[1];
        xg_color_clear_t color_clear;
        color_clear.f32[0] = 0;
        color_clear.f32[1] = 0;
        color_clear.f32[2] = 0;
        color_clear.f32[3] = 0;
        xg->cmd_clear_texture ( cmd_buffer, color_target.texture, color_clear, key );
    }

    {
        xf_copy_texture_resource_t normal_target = node_args->io->copy_texture_writes[2];
        xg_color_clear_t normal_clear;
        normal_clear.f32[0] = 0;
        normal_clear.f32[1] = 0;
        normal_clear.f32[2] = 0;
        normal_clear.f32[3] = 0;
        xg->cmd_clear_texture ( cmd_buffer, normal_target.texture, normal_clear, key );
    }

    {
        xf_copy_texture_resource_t material_target = node_args->io->copy_texture_writes[3];
        xg_color_clear_t material_clear;
        material_clear.u32[0] = 0;
        material_clear.u32[1] = 0;
        material_clear.u32[2] = 0;
        material_clear.u32[3] = 0;
        xg->cmd_clear_texture ( cmd_buffer, material_target.texture, material_clear, key );
    }

    std_module_release ( xg );
}

typedef struct {
    sm_mat_4x4f_t world;
} draw_cbuffer_vs_t;

typedef struct {
    float base_color[3];
    uint32_t object_id;
    float roughness;
    float metalness;
} draw_cbuffer_fs_t;

typedef struct {
    uint32_t width;
    uint32_t height;
} geometry_pass_args_t;

static void geometry_pass ( const xf_node_execute_args_t* node_args, void* user_args ) {
    std_auto_m pass_args = ( geometry_pass_args_t* ) user_args;

    xg_workload_h workload = node_args->workload;
    xg_cmd_buffer_h cmd_buffer = node_args->cmd_buffer;
    uint64_t key = node_args->base_key;

    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // Bind render targets
    {
        xg_render_textures_binding_t render_textures;
        render_textures.render_targets_count = 3;
        render_textures.render_targets[0] = xf_render_target_binding_m ( node_args->io->render_targets[0] );
        render_textures.render_targets[1] = xf_render_target_binding_m ( node_args->io->render_targets[1] );
        render_textures.render_targets[2] = xf_render_target_binding_m ( node_args->io->render_targets[2] );
        render_textures.depth_stencil.texture = node_args->io->depth_stencil_target;
        xg->cmd_set_render_textures ( cmd_buffer, &render_textures, key );
    }

    se_i* se = std_module_get_m ( se_module_name_m );

    se_query_h mesh_query;
    {
        se_query_params_t query_params = se_default_query_params_m;
        std_bitset_set ( query_params.request_component_flags, MESH_COMPONENT_ID );
        mesh_query = se->create_query ( &query_params );
    }

    se->resolve_pending_queries();
    const se_query_result_t* mesh_query_result = se->get_query_result ( mesh_query );

    xg_viewport_state_t viewport = xg_default_viewport_state_m;
    viewport.width = pass_args->width;
    viewport.height = pass_args->height;
    xg->cmd_set_pipeline_viewport ( cmd_buffer, &viewport, key );

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    for ( uint64_t i = 0; i < mesh_query_result->count; ++i ) {
        se_entity_h entity = mesh_query_result->entities[i];
        se_component_h component = se->get_component ( entity, MESH_COMPONENT_ID );
        std_auto_m mesh_component = ( viewapp_mesh_component_t* ) component;

        // Set pipeline
        xg_graphics_pipeline_state_h pipeline_state = xs->get_pipeline_state ( mesh_component->geometry_pipeline );
        xg->cmd_set_graphics_pipeline_state ( cmd_buffer, pipeline_state, key );

        //uint64_t draw_buffer_vertex_offset;
        //uint64_t draw_buffer_fragment_offset;
        xg_buffer_range_t vert_draw_buffer_range;
        xg_buffer_range_t frag_draw_buffer_range;
        {
            draw_cbuffer_vs_t vs;
            std_mem_zero_m ( &vs );
            sm_vec_3f_t up;// = sm_vec_3f ( 0, 1, 0 );
            up.x = mesh_component->up[0];
            up.y = mesh_component->up[1];
            up.z = mesh_component->up[2];
            sm_vec_3f_t dir;
            dir.x = mesh_component->orientation[0];
            dir.y = mesh_component->orientation[1];
            dir.z = mesh_component->orientation[2];
            dir = sm_vec_3f_norm ( dir );
            sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );

            sm_mat_4x4f_t trans;
            std_mem_zero_m ( &trans );
            trans.r0[0] = 1;
            trans.r1[1] = 1;
            trans.r2[2] = 1;
            trans.r3[3] = 1;
            trans.r0[3] = mesh_component->position[0];
            trans.r1[3] = mesh_component->position[1];
            trans.r2[3] = mesh_component->position[2];
            vs.world = sm_matrix_4x4f_mul ( trans, rot );

            draw_cbuffer_fs_t fs;
            std_mem_zero_m ( &fs );
            fs.base_color[0] = mesh_component->material.base_color[0];
            fs.base_color[1] = mesh_component->material.base_color[1];
            fs.base_color[2] = mesh_component->material.base_color[2];
            std_assert_m ( i < 256 );
            fs.object_id = ( uint32_t ) i; //mesh_component->material.ssr;
            fs.roughness = mesh_component->material.roughness;
            fs.metalness = mesh_component->material.metalness;

            vert_draw_buffer_range = xg->write_workload_uniform ( workload, &vs, sizeof ( vs ) );
            frag_draw_buffer_range = xg->write_workload_uniform ( workload, &fs, sizeof ( fs ) );
        }

        // Bind draw resources
        {
            xg_buffer_resource_binding_t buffers[2];

            buffers[0].shader_register = 0;
            buffers[0].type = xg_buffer_binding_type_uniform_m;
            buffers[0].range = vert_draw_buffer_range;

            buffers[1].shader_register = 1;
            buffers[1].type = xg_buffer_binding_type_uniform_m;
            buffers[1].range = frag_draw_buffer_range;

            xg_pipeline_resource_bindings_t draw_bindings = xg_default_pipeline_resource_bindings_m;
            draw_bindings.set = xg_resource_binding_set_per_draw_m;
            draw_bindings.buffer_count = 2;
            draw_bindings.buffers = buffers;

            xg->cmd_set_pipeline_resources ( cmd_buffer, &draw_bindings, key );
        }

        // Set vertex streams
        {
            xg_vertex_stream_binding_t vertex_bindings[2];
            vertex_bindings[0].buffer = mesh_component->pos_buffer;
            vertex_bindings[0].stream_id = 0;
            vertex_bindings[0].offset = 0;
            vertex_bindings[1].buffer = mesh_component->nor_buffer;
            vertex_bindings[1].stream_id = 1;
            vertex_bindings[1].offset = 0;
            xg->cmd_set_vertex_streams ( cmd_buffer, vertex_bindings, 2, key );
        }

        // Draw
        xg->cmd_draw_indexed ( cmd_buffer, mesh_component->idx_buffer, mesh_component->index_count, 0, key );
    }

    se->dispose_query_results();

    std_module_release ( xg );
    std_module_release ( xs );
    std_module_release ( se );
}

xf_node_h add_geometry_clear_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_node_params_t node_params = xf_default_node_params_m;
    node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( depth, xg_default_texture_view_m );
    node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( color, xg_default_texture_view_m );
    node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( normal, xg_default_texture_view_m );
    node_params.copy_texture_writes[node_params.copy_texture_writes_count++] = xf_copy_texture_dependency_m ( material, xg_default_texture_view_m );
    node_params.execute_routine = clear_pass;
    std_str_copy_m ( node_params.debug_name, "geometry_clear" );
    xf_node_h clear_node = xf->create_node ( graph, &node_params );

    std_module_release ( xf );

    return clear_node;
}

xf_node_h add_geometry_node ( xf_graph_h graph, xf_texture_h color, xf_texture_h normal, xf_texture_h material, xf_texture_h depth ) {
    xf_i* xf = std_module_get_m ( xf_module_name_m );

    xf_texture_info_t color_info;
    xf->get_texture_info ( &color_info, color );

    geometry_pass_args_t pass_args;
    pass_args.width = color_info.width;
    pass_args.height = color_info.height;

    xf_node_params_t node_params = xf_default_node_params_m;
    node_params.render_targets[node_params.render_targets_count++] = xf_render_target_dependency_m ( color, xg_default_texture_view_m );
    node_params.render_targets[node_params.render_targets_count++] = xf_render_target_dependency_m ( normal, xg_default_texture_view_m );
    node_params.render_targets[node_params.render_targets_count++] = xf_render_target_dependency_m ( material, xg_default_texture_view_m );
    node_params.depth_stencil_target = depth;
    node_params.execute_routine = geometry_pass;
    node_params.user_args_allocator = std_virtual_heap_allocator();
    node_params.user_args_alloc_size = sizeof ( geometry_pass_args_t );
    node_params.user_args = &pass_args;
    std_str_copy_m ( node_params.debug_name, "geometry" );
    node_params.passthrough.enable = true;
    node_params.passthrough.render_targets[0].mode = xf_node_passthrough_mode_ignore_m;
    node_params.passthrough.render_targets[1].mode = xf_node_passthrough_mode_ignore_m;
    node_params.passthrough.render_targets[2].mode = xf_node_passthrough_mode_ignore_m;
    xf_node_h node = xf->create_node ( graph, &node_params );

    std_module_release ( xf );

    return node;
}
