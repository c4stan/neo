#include "xi_workload.h"

#include <xg.h>
#include <xs.h>

#include <std_queue.h>
#include <std_list.h>

#include <sm_matrix.h>

static xi_workload_state_t* xi_workload_state;

void xi_workload_load ( xi_workload_state_t* state ) {
    xi_workload_state = state;

    xi_workload_state->workloads_array = std_virtual_heap_alloc_array_m ( xi_workload_t, xi_workload_max_workloads_m );
    xi_workload_state->workloads_freelist = std_freelist_m ( xi_workload_state->workloads_array, xi_workload_max_workloads_m );
    xi_workload_state->workloads_count = 0;

    xi_workload_state->ui_renderpass_rgba8.xg_handle = xg_null_handle_m;
    xi_workload_state->ui_renderpass_bgra8.xg_handle = xg_null_handle_m;
    xi_workload_state->ui_renderpass_a2bgr10.xg_handle = xg_null_handle_m;
}

void xi_workload_reload ( xi_workload_state_t* state ) {
    xi_workload_state = state;
}

void xi_workload_unload ( void ) {
    std_virtual_heap_free ( xi_workload_state->workloads_array );
}

void xi_workload_load_shaders ( xs_i* xs, xs_database_h sdb ) {
    xi_workload_state->ui_pipeline_rgba8 = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_ui_r8g8b8a8" ) );
    xi_workload_state->ui_pipeline_bgra8 = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_ui_b8g8r8a8" ) );
    xi_workload_state->ui_pipeline_a2bgr10 = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_ui_a2b10g10r10" ) );

    xi_workload_state->geo_pipeline_rgba8 = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_geo_r8g8b8a8" ) );
    xi_workload_state->geo_pipeline_bgra8 = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_geo_b8g8r8a8" ) );
    xi_workload_state->geo_pipeline_a2bgr10 = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_geo_a2b10g10r10" ) );
}

void xi_workload_activate_device ( xg_i* xg, xg_device_h device ) {
    std_unused_m ( xg );
    std_unused_m ( device );
}

void xi_workload_deactivate_device ( xg_i* xg, xg_device_h device ) {
    std_unused_m ( xg );
    std_unused_m ( device );
}

xi_workload_h xi_workload_create ( void ) {
    xi_workload_t* workload = std_list_pop_m ( &xi_workload_state->workloads_freelist );
    std_assert_m ( workload );
    workload->rect_count = 0;
    workload->tri_count = 0;
    workload->mesh_count = 0;
    workload->vertex_buffer = xg_null_handle_m;
    workload->xg_workload = xg_null_handle_m;
    workload->scissor_count = 0;

    xi_workload_h handle = ( xi_workload_h ) ( workload - xi_workload_state->workloads_array );
    return handle;
}

xi_scissor_h xi_workload_scissor ( xi_workload_h workload_handle, uint32_t x, uint32_t y, uint32_t width, uint32_t height ) {
    xi_workload_t* workload = &xi_workload_state->workloads_array[workload_handle];
    if ( workload->scissor_count == xi_workload_max_scissors_m ) {
        return xi_null_scissor_m;
    }

    xi_scissor_t* scissor = &workload->scissor_array[workload->scissor_count++];
    scissor->x = x;
    scissor->y = y;
    scissor->width = width;
    scissor->height = height;
    return workload->scissor_array - scissor;
}

void xi_workload_cmd_draw ( xi_workload_h workload_handle, const xi_draw_rect_t* rects, uint64_t rect_count ) {
    xi_workload_t* workload = &xi_workload_state->workloads_array[workload_handle];
    std_assert_m ( workload->rect_count + rect_count < xi_workload_max_rects_m );

    for ( uint64_t i = 0; i < rect_count; ++i ) {
        workload->rect_array[workload->rect_count + i] = rects[i];
    }

    workload->rect_count += rect_count;
}

void xi_workload_cmd_draw_tri ( xi_workload_h workload_handle, const xi_draw_tri_t* tris, uint64_t tri_count ) {
    xi_workload_t* workload = &xi_workload_state->workloads_array[workload_handle];
    std_assert_m ( workload->tri_count + tri_count < xi_workload_max_tris_m );

    for ( uint64_t i = 0; i < tri_count; ++i ) {
        workload->tri_array[workload->tri_count + i] = tris[i];
    }

    workload->tri_count += tri_count;
}

void xi_workload_cmd_draw_mesh ( xi_workload_h workload_handle, const xi_draw_mesh_t* mesh ) {
    xi_workload_t* workload = &xi_workload_state->workloads_array[workload_handle];
    std_assert_m ( workload->mesh_count < xi_workload_max_meshes_m );
    workload->mesh_array[workload->mesh_count++] = *mesh;
}

void xi_workload_set_view_info ( xi_workload_h workload_handle, const rv_view_info_t* view_info ) {
    xi_workload_t* workload = &xi_workload_state->workloads_array[workload_handle];
    
    sm_mat_4x4f_t* view = ( sm_mat_4x4f_t* ) &view_info->view_matrix;
    sm_mat_4x4f_t* proj = ( sm_mat_4x4f_t* ) &view_info->proj_matrix;
    sm_mat_4x4f_t* proj_from_world = ( sm_mat_4x4f_t* ) workload->proj_from_world;

    *proj_from_world = sm_matrix_4x4f_mul ( *proj, *view );
}

uint64_t xi_workload_flush ( xi_workload_h workload_handle, const xi_flush_params_t* flush_params ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );
    
    uint64_t key = flush_params->key;
    float viewport_w = ( float ) flush_params->viewport.width;
    float viewport_h = ( float ) flush_params->viewport.height;

    xg_texture_h null_texture = xg->get_default_texture ( flush_params->device, xg_default_texture_r8g8b8a8_unorm_white_m );
    xg_sampler_h point_sampler = xg->get_default_sampler ( flush_params->device, xg_default_sampler_point_clamp_m );
    xg_sampler_h linear_sampler = xg->get_default_sampler ( flush_params->device, xg_default_sampler_linear_clamp_m );

    // setup new workload
    xi_workload_t* workload = &xi_workload_state->workloads_array[workload_handle];
    workload->xg_workload = flush_params->workload;

    // renderpass
    xi_workload_renderpass_t* renderpass;
    if ( flush_params->render_target_format == xg_format_r8g8b8a8_unorm_m ) {
        renderpass = &xi_workload_state->ui_renderpass_rgba8;
    } else if ( flush_params->render_target_format == xg_format_b8g8r8a8_unorm_m ) {
        renderpass = &xi_workload_state->ui_renderpass_bgra8;
    } else if ( flush_params->render_target_format == xg_format_a2b10g10r10_unorm_pack32_m ) {
        renderpass = &xi_workload_state->ui_renderpass_a2bgr10;
    } else {
        renderpass = NULL;
        std_not_implemented_m();
    }

    if ( renderpass->xg_handle == xg_null_handle_m || renderpass->resolution_x != viewport_w || renderpass->resolution_y != viewport_h ) {
        if ( renderpass->xg_handle != xg_null_handle_m ) {
            xg->cmd_destroy_renderpass ( flush_params->resource_cmd_buffer, renderpass->xg_handle, xg_resource_cmd_buffer_time_workload_complete_m );
        }
        renderpass->xg_handle = xg->create_renderpass ( &xg_renderpass_params_m (
            .device = flush_params->device,
            .debug_name = "xi_renderpass",
            .resolution_x = viewport_w,
            .resolution_y = viewport_h,
            .render_textures = xg_render_textures_layout_m (
                .render_targets_count = 1,
                .render_targets = { xg_render_target_layout_m ( .format = flush_params->render_target_format ) }
            ),
        ) );
        renderpass->resolution_x = viewport_w;
        renderpass->resolution_y = viewport_h;
    }

    xg->cmd_begin_renderpass ( flush_params->cmd_buffer, key, &xg_cmd_renderpass_params_m (
        .renderpass = renderpass->xg_handle,
        .render_targets_count = 1,
        .render_targets = { flush_params->render_target_binding }
    ) );

    // TODO instead of storing separate lists for quads/tris/meshes store generic draw lists, to avoid
    // imposing the arbitrary order of meshes -> quads -> tris for equal sort order items

    // draw meshes
    uint32_t max_sort_order = 0;
    {

        xs_i* xs = std_module_get_m ( xs_module_name_m );
        xg_graphics_pipeline_state_h pipeline_state;
        if ( flush_params->render_target_format == xg_format_r8g8b8a8_unorm_m ) {
            pipeline_state = xs->get_pipeline_state ( xi_workload_state->geo_pipeline_rgba8 );
        } else if ( flush_params->render_target_format == xg_format_b8g8r8a8_unorm_m ) {
            pipeline_state = xs->get_pipeline_state ( xi_workload_state->geo_pipeline_bgra8 );
        } else if ( flush_params->render_target_format == xg_format_a2b10g10r10_unorm_pack32_m ) {
            pipeline_state = xs->get_pipeline_state ( xi_workload_state->geo_pipeline_a2bgr10 );
        } else {
            pipeline_state = xg_null_handle_m;
            std_not_implemented_m();
        }

        for ( uint32_t i = 0; i < workload->mesh_count; ++i ) {
            const xi_draw_mesh_t* mesh = &workload->mesh_array[i];

            max_sort_order = std_max ( max_sort_order, mesh->sort_order );

            sm_vec_3f_t up = { 0, 1, 0 }; // TODO
            sm_vec_3f_t dir = { 0, 0, 1 }; // TODO
            dir = sm_vec_3f_norm ( dir );
            sm_mat_4x4f_t rot = sm_matrix_4x4f_dir_rotation ( dir, up );
            sm_mat_4x4f_t trans = {
                .r0[0] = mesh->scale,
                .r1[1] = mesh->scale,
                .r2[2] = mesh->scale,
                .r3[3] = 1,
                .r0[3] = mesh->traslation[0],
                .r1[3] = mesh->traslation[1],
                .r2[3] = mesh->traslation[2],
            };

            struct {
                sm_mat_4x4f_t world_from_model;
                sm_mat_4x4f_t proj_from_world;
                float color[4];
            } draw_uniforms = {
                .world_from_model = sm_matrix_4x4f_mul ( trans, rot ),
                .proj_from_world = sm_matrix_4x4f ( workload->proj_from_world ),
                .color = { mesh->color[0], mesh->color[1], mesh->color[2], mesh->color[3] },
            };

            xg_resource_bindings_layout_h layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m );

            xg_resource_bindings_h group = xg->cmd_create_workload_bindings ( flush_params->resource_cmd_buffer, &xg_resource_bindings_params_m (
                .layout = layout,
                .bindings = xg_pipeline_resource_bindings_m (
                    .buffer_count = 1,
                    .buffers = {
                        xg_buffer_resource_binding_m (
                            .shader_register = 0,
                            .range = xg->write_workload_uniform ( flush_params->workload, &draw_uniforms, sizeof ( draw_uniforms ) )
                        )
                    },
                )
            ) );

            xg->cmd_set_dynamic_scissor ( flush_params->cmd_buffer, key + mesh->sort_order, &xg_scissor_state_m() );

            xg->cmd_draw ( flush_params->cmd_buffer, key + mesh->sort_order, &xg_cmd_draw_params_m (
                .pipeline = pipeline_state,
                .bindings[xg_shader_binding_set_dispatch_m] = group,
                .primitive_count = mesh->idx_count / 3,
                .vertex_buffers_count = 1,
                .vertex_buffers = { mesh->pos_buffer },
                .index_buffer = mesh->idx_buffer
            ) );

            //xg->cmd_draw_indexed ( flush_params->cmd_buffer, mesh->idx_buffer, mesh->idx_count, 0, key + mesh->sort_order );
        }
    }

    // draw ui
    xi_scissor_h active_scissor = xi_null_scissor_m;
    {
        // translate rects and tris into vertex data
        for ( uint64_t i = 0; i < workload->rect_count; ++i ) {
            const xi_draw_rect_t* rect = &workload->rect_array[i];
            xi_workload_vertex_t* vertex;

            max_sort_order = std_max ( max_sort_order, rect->sort_order );

            // the following code remaps position values from <[0, viewport_w], [0, viewport_h]> into <[-1, 1], [-1, 1]>
            // and flips position y axis direction from bottom up to top down
            //
            // top left
            vertex = &workload->vertex_buffer_data[i * 6 + 0];
            vertex->pos[0] = ( rect->x / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - ( rect->y / viewport_h ) )  * 2 - 1;
            vertex->uv[0] = rect->uv0[0];
            vertex->uv[1] = rect->uv0[1];
            vertex->color[0] = rect->color.r / 255.f;
            vertex->color[1] = rect->color.g / 255.f;
            vertex->color[2] = rect->color.b / 255.f;
            vertex->color[3] = rect->color.a / 255.f;
            // top right
            vertex = &workload->vertex_buffer_data[i * 6 + 1];
            vertex->pos[0] = ( ( rect->x + rect->width ) / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - ( rect->y / viewport_h ) ) * 2 - 1;
            vertex->uv[0] = rect->uv1[0];
            vertex->uv[1] = rect->uv0[1];
            vertex->color[0] = rect->color.r / 255.f;
            vertex->color[1] = rect->color.g / 255.f;
            vertex->color[2] = rect->color.b / 255.f;
            vertex->color[3] = rect->color.a / 255.f;
            // bottom right
            vertex = &workload->vertex_buffer_data[i * 6 + 2];
            vertex->pos[0] = ( ( rect->x + rect->width ) / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - ( ( rect->y + rect->height ) / viewport_h ) ) * 2 - 1;
            vertex->uv[0] = rect->uv1[0];
            vertex->uv[1] = rect->uv1[1];
            vertex->color[0] = rect->color.r / 255.f;
            vertex->color[1] = rect->color.g / 255.f;
            vertex->color[2] = rect->color.b / 255.f;
            vertex->color[3] = rect->color.a / 255.f;
            // bottom right
            vertex = &workload->vertex_buffer_data[i * 6 + 3];
            *vertex = workload->vertex_buffer_data[i * 6 + 2];
            // bottom left
            vertex = &workload->vertex_buffer_data[i * 6 + 4];
            vertex->pos[0] = ( rect->x / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - ( ( rect->y + rect->height ) / viewport_h ) ) * 2 - 1;
            vertex->uv[0] = rect->uv0[0];
            vertex->uv[1] = rect->uv1[1];
            vertex->color[0] = rect->color.r / 255.f;
            vertex->color[1] = rect->color.g / 255.f;
            vertex->color[2] = rect->color.b / 255.f;
            vertex->color[3] = rect->color.a / 255.f;
            // top left
            vertex = &workload->vertex_buffer_data[i * 6 + 5];
            *vertex = workload->vertex_buffer_data[i * 6 + 0];
        }

        uint64_t tri_base = workload->rect_count * 6;

        for ( uint64_t i = 0; i < workload->tri_count; ++i ) {
            const xi_draw_tri_t* tri = &workload->tri_array[i];
            xi_workload_vertex_t* vertex;

            max_sort_order = std_max ( max_sort_order, tri->sort_order );

            vertex = &workload->vertex_buffer_data[tri_base + i * 3 + 0];
            vertex->pos[0] = ( tri->xy0[0] / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - tri->xy0[1] / viewport_h ) * 2 - 1;
            vertex->uv[0] = tri->uv0[0];
            vertex->uv[1] = tri->uv0[1];
            vertex->color[0] = tri->color.r / 255.f;
            vertex->color[1] = tri->color.g / 255.f;
            vertex->color[2] = tri->color.b / 255.f;
            vertex->color[3] = tri->color.a / 255.f;

            vertex = &workload->vertex_buffer_data[tri_base + i * 3 + 1];
            vertex->pos[0] = ( tri->xy1[0] / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - tri->xy1[1] / viewport_h ) * 2 - 1;
            vertex->uv[0] = tri->uv1[0];
            vertex->uv[1] = tri->uv1[1];
            vertex->color[0] = tri->color.r / 255.f;
            vertex->color[1] = tri->color.g / 255.f;
            vertex->color[2] = tri->color.b / 255.f;
            vertex->color[3] = tri->color.a / 255.f;

            vertex = &workload->vertex_buffer_data[tri_base + i * 3 + 2];
            vertex->pos[0] = ( tri->xy2[0] / viewport_w ) * 2 - 1;
            vertex->pos[1] = ( 1 - tri->xy2[1] / viewport_h ) * 2 - 1;
            vertex->uv[0] = tri->uv2[0];
            vertex->uv[1] = tri->uv2[1];
            vertex->color[0] = tri->color.r / 255.f;
            vertex->color[1] = tri->color.g / 255.f;
            vertex->color[2] = tri->color.b / 255.f;
            vertex->color[3] = tri->color.a / 255.f;
        }

        // create vertex buffer
        xg_buffer_params_t vertex_buffer_params = xg_buffer_params_m (
            .memory_type = xg_memory_type_gpu_mapped_m,
            .device = flush_params->device,
            .size = sizeof ( xi_workload_vertex_t ) * ( workload->rect_count * 6 + workload->tri_count * 3 ),
            .allowed_usage = xg_buffer_usage_bit_vertex_buffer_m,
            .debug_name = "xi_vertex_buffer",
        );
        xg_buffer_h vertex_buffer_handle = xg->create_buffer ( &vertex_buffer_params );
        std_assert_m ( vertex_buffer_handle != xg_null_handle_m );
        workload->vertex_buffer = vertex_buffer_handle;

        // fill vertex buffer
        xg_buffer_info_t vertex_buffer_info;
        xg->get_buffer_info ( &vertex_buffer_info, vertex_buffer_handle );
        char* vertex_buffer_data = ( char* ) vertex_buffer_info.allocation.mapped_address;
        std_mem_copy ( vertex_buffer_data, workload->vertex_buffer_data, vertex_buffer_params.size );

        // bind pipeline
        xs_i* xs = std_module_get_m ( xs_module_name_m );
        xg_graphics_pipeline_state_h pipeline_state;
        if ( flush_params->render_target_format == xg_format_r8g8b8a8_unorm_m ) {
            pipeline_state = xs->get_pipeline_state ( xi_workload_state->ui_pipeline_rgba8 );
        } else if ( flush_params->render_target_format == xg_format_b8g8r8a8_unorm_m ) {
            pipeline_state = xs->get_pipeline_state ( xi_workload_state->ui_pipeline_bgra8 );
        } else if ( flush_params->render_target_format == xg_format_a2b10g10r10_unorm_pack32_m ) {
            pipeline_state = xs->get_pipeline_state ( xi_workload_state->ui_pipeline_a2bgr10 );
        } else {
            pipeline_state = xg_null_handle_m;
            std_not_implemented_m();
        }

        // TODO single draw call?
        // draw rects
        for ( uint32_t i = 0; i < workload->rect_count; ++i ) {
            const xi_draw_rect_t* rect = &workload->rect_array[i];

            xi_scissor_h scissor_handle = rect->scissor;
            //if ( scissor_handle != active_scissor ) 
            {
                xg_scissor_state_t scissor_state = xg_scissor_state_m();
    
                if ( scissor_handle != xi_null_scissor_m ) {
                    xi_scissor_t* scissor = &workload->scissor_array[scissor_handle];
                    scissor_state.x = scissor->x;
                    scissor_state.y = scissor->y;
                    scissor_state.width = scissor->width;
                    scissor_state.height = scissor->height;
                }

                xg->cmd_set_dynamic_scissor ( flush_params->cmd_buffer, key + rect->sort_order, &scissor_state );
                active_scissor = scissor_handle;
            }

            xg_resource_bindings_layout_h layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m );

            xg_resource_bindings_h group = xg->cmd_create_workload_bindings ( flush_params->resource_cmd_buffer, &xg_resource_bindings_params_m (
                .layout = layout,
                .bindings = xg_pipeline_resource_bindings_m (
                    .texture_count = 1,
                    .textures = {
                        xg_texture_resource_binding_m (
                            .shader_register = 0,
                            .layout = xg_texture_layout_shader_read_m,
                            .texture = rect->texture != xg_null_handle_m ? rect->texture : null_texture,
                        ),
                    },
                    .sampler_count = 1,
                    .samplers = {
                        xg_sampler_resource_binding_m (
                            .shader_register = 1,
                            .sampler = rect->linear_sampler_filter ? linear_sampler : point_sampler,
                        )
                    },
                ),
            ) );

            xg->cmd_draw ( flush_params->cmd_buffer, key + rect->sort_order, &xg_cmd_draw_params_m ( 
                .pipeline = pipeline_state,
                .bindings[xg_shader_binding_set_dispatch_m] = group,
                .primitive_count = 2,
                .vertex_buffers_count = 1,
                .vertex_buffers = { workload->vertex_buffer },
                .vertex_offset = i * 6,
            ) );
        }

        // draw tris
        for ( uint32_t i = 0; i < workload->tri_count; ++i ) {
            const xi_draw_tri_t* tri = &workload->tri_array[i];

            // TODO
            xi_scissor_h scissor_handle = tri->scissor;
            //if ( scissor_handle != active_scissor ) 
            {
                xg_scissor_state_t scissor = xg_scissor_state_m();

                if ( scissor_handle != xi_null_scissor_m ) {
                    xi_scissor_t* xi_scissor = &workload->scissor_array[scissor_handle];
                    scissor.x = xi_scissor->x;
                    scissor.y = xi_scissor->y;
                    scissor.width = xi_scissor->width;
                    scissor.height = xi_scissor->height;
                }

                xg->cmd_set_dynamic_scissor ( flush_params->cmd_buffer, key + tri->sort_order, &scissor );
                active_scissor = scissor_handle;
            }

            xg_resource_bindings_layout_h layout = xg->get_pipeline_resource_layout ( pipeline_state, xg_shader_binding_set_dispatch_m );

            xg_resource_bindings_h group = xg->cmd_create_workload_bindings ( flush_params->resource_cmd_buffer, &xg_resource_bindings_params_m (
                .layout = layout,
                .bindings = xg_pipeline_resource_bindings_m (
                    .texture_count = 1,
                    .textures = { 
                        xg_texture_resource_binding_m (
                            .shader_register = 0,
                            .layout = xg_texture_layout_shader_read_m,
                            .texture = tri->texture != xg_null_handle_m ? tri->texture : null_texture,
                        ),
                    },
                    .sampler_count = 1,
                    .samplers = {
                        xg_sampler_resource_binding_m (
                            .shader_register = 1,
                            .sampler = tri->linear_sampler_filter ? linear_sampler : point_sampler,
                        ),
                    },
                ),
            ) );

            xg->cmd_draw ( flush_params->cmd_buffer, key + tri->sort_order, &xg_cmd_draw_params_m ( 
                .pipeline = pipeline_state,
                .bindings[xg_shader_binding_set_dispatch_m] = group,
                .primitive_count = 1,
                .vertex_buffers_count = 1,
                .vertex_buffers = { workload->vertex_buffer },
                .vertex_offset = tri_base + i * 3,
            ) );

        }

        // delete buffer
        xg->cmd_destroy_buffer ( flush_params->resource_cmd_buffer, vertex_buffer_handle, xg_resource_cmd_buffer_time_workload_complete_m );
    }

    // queue up the workload
    xg->cmd_end_renderpass ( flush_params->cmd_buffer, key + max_sort_order );

    std_mem_zero_m ( workload );
    std_list_push ( &xi_workload_state->workloads_freelist, workload );

    return key + max_sort_order;
}
