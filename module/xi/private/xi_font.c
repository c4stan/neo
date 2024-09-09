#include "xi_font.h"

//#include <xg.h>

#include <std_list.h>
#include <std_log.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STB_RECT_PACK_IMPLEMENTATION
#define STBTT_STATIC
std_warnings_save_state_m()
std_warnings_ignore_m ( "-Wsign-conversion" )
std_warnings_ignore_m ( "-Wbad-function-cast" )
std_warnings_ignore_m ( "-Wcomma" )
std_warnings_ignore_m ( "-Wunused-function" )
#include "stb_rect_pack.h"
#include "stb_truetype.h"

std_warnings_restore_state_m()

static xi_font_state_t* xi_font_state;

// TODO
// https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf

void xi_font_load ( xi_font_state_t* state ) {
    xi_font_state = state;

    xi_font_state->fonts_array = std_virtual_heap_alloc_array_m ( xi_font_t, xi_font_max_fonts_m );
    xi_font_state->fonts_freelist = std_freelist_m ( xi_font_state->fonts_array, xi_font_max_fonts_m );
    xi_font_state->uniform_data = NULL;
}

void xi_font_load_shaders ( xs_i* xs, xs_database_h sdb ) {
    xs_database_pipeline_h font_atlas_pipeline = xs->get_database_pipeline ( sdb, xs_hash_static_string_m ( "xi_font_atlas" ) );
    std_assert_m ( font_atlas_pipeline != xs_null_handle_m );
    xi_font_state->font_atlas_pipeline = font_atlas_pipeline;
}

void xi_font_reload ( xi_font_state_t* state ) {
    xi_font_state = state;
}

void xi_font_unload ( void ) {
    std_virtual_heap_free ( xi_font_state->fonts_array );
}

xi_font_h xi_font_create_ttf ( std_buffer_t ttf_data, const xi_font_params_t* params ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // TODO lock
    xi_font_t* font = std_list_pop_m ( &xi_font_state->fonts_freelist );

    font->params = *params;

    // Info
    //stbtt_fontinfo* font_info = &font->font_info;
    //stbtt_InitFont ( font_info, ttf_data.base, 0 );
    //float scale = stbtt_ScaleForPixelHeight ( font_info, ( float ) font->params.pixel_height );
    //int32_t ascent;
    //int32_t descent
    //int32_t lineGap;
    //stbtt_GetFontVMetrics ( font_info, &ascent, &descent, &lineGap );
    //float scale = font->params.pixel_height / ( ascent - descent );

    // Info
    stbtt_InitFont ( &font->font_info, ttf_data.base, 0 );
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics ( &font->font_info, &ascent, &descent, &lineGap );
    font->ascent = ascent;
    font->descent = descent;
    font->scale = stbtt_ScaleForPixelHeight ( &font->font_info, params->pixel_height );

    // Atlas
    size_t atlas_size = xi_font_texture_atlas_width_m * xi_font_texture_atlas_height_m;
    void* atlas_alloc = std_virtual_heap_alloc ( atlas_size, 16 );
#if 0
    std_alloc_t char_info_alloc = std_virtual_heap_alloc ( sizeof ( stbtt_bakedchar ) * params->char_count, 16 );

    int bake_result = stbtt_BakeFontBitmap ( ttf_data.base, 0, params->pixel_height, atlas_alloc.buffer.base, ( int ) params->atlas_width, ( int ) params->atlas_height, ( int ) params->first_char_code, ( int ) params->char_count, ( stbtt_bakedchar* ) char_info_alloc.buffer.base );

    std_assert_m ( bake_result > 0 );
#else
    stbtt_pack_context pack_context;
    font->char_info = std_virtual_heap_alloc_array_m ( stbtt_packedchar, params->char_count );

    stbtt_PackBegin ( &pack_context, ( unsigned char* ) atlas_alloc, xi_font_texture_atlas_width_m, xi_font_texture_atlas_height_m, 0, 4, NULL );
    stbtt_PackSetOversampling ( &pack_context, 2, 2 );
    stbtt_PackFontRange ( &pack_context, ( unsigned char* ) ttf_data.base, 0, ( float ) params->pixel_height, ( int ) params->first_char_code, ( int ) params->char_count, font->char_info );
    stbtt_PackEnd ( &pack_context );
#endif

#if 0
    font->char_info = ( stbtt_bakedchar* ) char_info_alloc.buffer.base;
#else
    //font->char_info = ( stbtt_packedchar* ) char_info_alloc.buffer.base;
#endif

    xg_texture_h raster_texture;
    {
        xg_texture_params_t texture_params = xg_texture_params_m (
            .memory_type = xg_memory_type_gpu_only_m,
            .device = params->xg_device,
            .width = xi_font_texture_atlas_width_m,
            .height = xi_font_texture_atlas_height_m,
            .format = xg_format_r8_uint_m,
            .allowed_usage = xg_texture_usage_bit_copy_dest_m | xg_texture_usage_bit_resource_m,
        );
        std_str_copy_static_m ( texture_params.debug_name, "font atlas temp" );
        raster_texture = xg->create_texture ( &texture_params );
    }

    xg_buffer_h staging_buffer;
    {
        xg_buffer_params_t buffer_params = xg_buffer_params_m (
            .memory_type = xg_memory_type_upload_m,
            .device = params->xg_device,
            .size = xi_font_texture_atlas_width_m * xi_font_texture_atlas_height_m,
            .allowed_usage = xg_buffer_usage_bit_copy_source_m,
            .debug_name = "font atlas staging buffer",
        );
        staging_buffer = xg->create_buffer ( &buffer_params );
    }

    xg_workload_h workload = xg->create_workload ( params->xg_device );
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
    uint64_t key = 0;

    xg_buffer_info_t staging_buffer_info;
    xg->get_buffer_info ( &staging_buffer_info, staging_buffer );

    std_mem_copy ( staging_buffer_info.allocation.mapped_address, atlas_alloc, atlas_size );

    std_virtual_heap_free ( atlas_alloc );

    //xg->cmd_start_debug_capture ( cmd_buffer, xg_debug_capture_stop_time_workload_submit_m, key );

    // copy from staging to source atlas
    {
        xg_texture_memory_barrier_t barrier = xg_texture_memory_barrier_m (
            .texture = raster_texture,
            .layout.old = xg_texture_layout_undefined_m,
            .layout.new = xg_texture_layout_copy_dest_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_transfer_write_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_transfer_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, key );
    }

    {
        xg_buffer_to_texture_copy_params_t copy_params = xg_default_buffer_to_texture_copy_params_m;
        copy_params.source = staging_buffer;
        copy_params.destination = raster_texture;
        xg->cmd_copy_buffer_to_texture ( cmd_buffer, &copy_params, key );
    }

    xg->cmd_destroy_buffer ( resource_cmd_buffer, staging_buffer, xg_resource_cmd_buffer_time_workload_complete_m );

    xg_texture_h atlas_texture;
    {
        xg_texture_params_t texture_params = xg_texture_params_m (
            .memory_type = xg_memory_type_gpu_only_m,
            .device = params->xg_device,
            .width = xi_font_texture_atlas_width_m,
            .height = xi_font_texture_atlas_height_m,
            .format = xg_format_r8g8b8a8_unorm_m,
            .allowed_usage = xg_texture_usage_bit_render_target_m | xg_texture_usage_bit_resource_m,
        );
        std_str_copy_static_m ( texture_params.debug_name, "font atlas" );
        atlas_texture = xg->create_texture ( &texture_params );
    }

    // atlas copy_dest -> shader_resource
    // final undefined -> render target
    {
        xg_texture_memory_barrier_t barriers[2];
        barriers[0] = xg_texture_memory_barrier_m (
            .texture = raster_texture,
            .layout.old = xg_texture_layout_copy_dest_m,
            .layout.new = xg_texture_layout_shader_read_m,
            .memory.flushes = xg_memory_access_bit_transfer_write_m,
            .memory.invalidations = xg_memory_access_bit_shader_read_m,
            .execution.blocker = xg_pipeline_stage_bit_transfer_m,
            .execution.blocked = xg_pipeline_stage_bit_fragment_shader_m,
        );
        barriers[1] = xg_texture_memory_barrier_m (
            .texture = atlas_texture,
            .layout.old = xg_texture_layout_undefined_m,
            .layout.new = xg_texture_layout_render_target_m,
            .memory.flushes = xg_memory_access_bit_none_m,
            .memory.invalidations = xg_memory_access_bit_color_write_m,
            .execution.blocker = xg_pipeline_stage_bit_top_of_pipe_m,
            .execution.blocked = xg_pipeline_stage_bit_color_output_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 2;
        barrier_set.texture_memory_barriers = barriers;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, key );
    }

    xg_buffer_h uniform_buffer;
    {
        xg_i* xg = std_module_get_m ( xg_module_name_m );

        xg_buffer_params_t cbuffer_params = xg_buffer_params_m (
            .memory_type = xg_memory_type_gpu_mappable_m,
            .device = params->xg_device,
            .size = sizeof ( xi_font_atlas_uniform_data_t ),
            .allowed_usage = xg_buffer_usage_bit_uniform_m,
            .debug_name = "xi font cbuffer",
        );
        uniform_buffer = xg->create_buffer ( &cbuffer_params );

        xg->cmd_destroy_buffer ( resource_cmd_buffer, uniform_buffer, xg_resource_cmd_buffer_time_workload_complete_m );

        xg_buffer_info_t uniform_buffer_info;
        xg->get_buffer_info ( &uniform_buffer_info, uniform_buffer );
        xi_font_state->uniform_data = ( xi_font_atlas_uniform_data_t* ) uniform_buffer_info.allocation.mapped_address;
    }

    xg->cmd_set_render_textures (
        cmd_buffer,
        &xg_render_textures_binding_m (
            .render_targets_count = 1,
            .render_targets[0] = { atlas_texture, xg_default_texture_view_m },
        ),
        key );

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, xs->get_pipeline_state ( xi_font_state->font_atlas_pipeline ), key );

    xg->cmd_set_pipeline_resources (
        cmd_buffer,
        &xg_pipeline_resource_bindings_m (
            .set = xg_resource_binding_set_per_draw_m,
            .buffer_count = 1,
            .buffers = {
                xg_buffer_resource_binding_m (
                    .shader_register = 0,
                    .type = xg_buffer_binding_type_uniform_m,
                    .range = { .handle = uniform_buffer, .offset = 0, .size = sizeof ( xi_font_atlas_uniform_data_t ) }
                ),
            },
            .texture_count = 1,
            .textures = {
                xg_texture_resource_binding_m (
                    .shader_register = 1,
                    .layout = xg_texture_layout_shader_read_m,
                    .texture = raster_texture,
                    .view = xg_default_texture_view_m
                ),
            },
            .sampler_count = 1,
            .samplers = {
                xg_sampler_resource_binding_m (
                    .shader_register = 2,
                    .sampler = xg->get_default_sampler ( params->xg_device, xg_default_sampler_point_clamp_m )
                )
            }
        ), 
        key );

#if 0
    {
        xg_buffer_resource_binding_t buffer_resource;
        buffer_resource.shader_register = 0;
        buffer_resource.type = xg_buffer_binding_type_uniform_m;
        //buffer_resource.buffer = uniform_buffer;
        buffer_resource.range.handle = uniform_buffer;
        buffer_resource.range.offset = 0;
        buffer_resource.range.size = sizeof ( xi_font_atlas_uniform_data_t );

        xg_texture_resource_binding_t texture_resource;
        texture_resource.shader_register = 1;
        texture_resource.layout = xg_texture_layout_shader_read_m;
        texture_resource.texture = raster_texture;
        texture_resource.view = xg_default_texture_view_m;

        xg_sampler_resource_binding_t sampler_resource;
        sampler_resource.shader_register = 2;
        sampler_resource.sampler = xg->get_default_sampler ( params->xg_device, xg_default_sampler_point_clamp_m );

        xg_pipeline_resource_bindings_t bindings;
        bindings.set = xg_resource_binding_set_per_draw_m;
        bindings.buffer_count = 1;
        bindings.texture_count = 1;
        bindings.sampler_count = 1;
        bindings.buffers = &buffer_resource;
        bindings.textures = &texture_resource;
        bindings.samplers = &sampler_resource;

        xg->cmd_set_pipeline_resources ( cmd_buffer, &bindings, key );
    }
#endif

    xi_font_state->uniform_data->color[0] = 1;
    xi_font_state->uniform_data->color[1] = 1;
    xi_font_state->uniform_data->color[2] = 1;
    xi_font_state->uniform_data->outline = params->outline;
    xi_font_state->uniform_data->resolution_f32[0] = xi_font_texture_atlas_width_m;
    xi_font_state->uniform_data->resolution_f32[1] = xi_font_texture_atlas_height_m;

    xg->cmd_draw ( cmd_buffer, 3, 0, 0 );

    {
        xg_texture_memory_barrier_t barrier = xg_texture_memory_barrier_m (
            .texture = atlas_texture,
            .layout.old = xg_texture_layout_render_target_m,
            .layout.new = xg_texture_layout_shader_read_m,
            .memory.flushes = xg_memory_access_bit_color_write_m,
            .memory.invalidations = xg_memory_access_bit_shader_read_m,
            .execution.blocker = xg_pipeline_stage_bit_color_output_m,
            .execution.blocked = xg_pipeline_stage_bit_fragment_shader_m,
        );

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, key );
    }

    xg->cmd_destroy_texture ( resource_cmd_buffer, raster_texture, xg_resource_cmd_buffer_time_workload_complete_m );

    xg->submit_workload ( workload );

    font->atlas_texture = atlas_texture;
    font->outline = params->outline;

    xi_font_h font_handle = ( xi_font_h ) ( font - xi_font_state->fonts_array );
    return font_handle;
}

void xi_font_destroy ( xi_font_h font ) {
    // TODO
    std_unused_m ( font );
}

xi_font_char_box_t xi_font_char_box_get ( float* fx, float* fy, xi_font_h font_handle, uint32_t character ) {
    xi_font_t* font = &xi_font_state->fonts_array[font_handle];

    uint32_t idx = character - font->params.first_char_code;

    stbtt_aligned_quad q;
    stbtt_GetPackedQuad ( font->char_info, xi_font_texture_atlas_width_m, xi_font_texture_atlas_height_m, idx, fx, fy, &q, 0 );

    //stbtt_packedchar* char_info = &font->char_info[idx];

    //float inv_width = 1.f / xi_font_texture_atlas_width_m;
    //float inv_height = 1.f / xi_font_texture_atlas_height_m;

    //uint32_t outline_pad = font->outline ? 1 : 0;

    xi_font_char_box_t box;
    //box.uv0[0] = ( char_info->x0 - outline_pad ) * inv_width;
    //box.uv0[1] = ( char_info->y0 - outline_pad ) * inv_height;
    //box.uv1[0] = ( char_info->x1 + outline_pad ) * inv_width;
    //box.uv1[1] = ( char_info->y1 + outline_pad ) * inv_height;
    //uint32_t xy0[2];
    //uint32_t xy1[2];
    //xy0[0] = char_info->x0 - outline_pad;
    //xy0[1] = char_info->y0 - outline_pad;
    //xy1[0] = char_info->x1 + outline_pad;
    //xy1[1] = char_info->y1 + outline_pad;
    //box.width = xy1[0] - xy0[0];
    //box.width = char_info->xadvance;
    //box.height = xy1[1] - xy0[1];

    box.uv0[0] = q.s0;
    box.uv0[1] = q.t0;
    box.uv1[0] = q.s1;
    box.uv1[1] = q.t1;
    box.xy0[0] = q.x0;
    box.xy0[1] = q.y0;
    box.xy1[0] = q.x1;
    box.xy1[1] = q.y1;
    return box;
}

xg_texture_h xi_font_atlas_get ( xi_font_h font_handle ) {
    xi_font_t* font = &xi_font_state->fonts_array[font_handle];
    return font->atlas_texture;
}

void xi_font_get_info ( xi_font_info_t* info, xi_font_h font_handle ) {
    xi_font_t* font = &xi_font_state->fonts_array[font_handle];
    info->pixel_height = font->params.pixel_height;
    info->ascent = font->ascent * font->scale;
    info->descent = font->descent * font->scale;
}
