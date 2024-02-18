#include "xui_font.h"

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

static xui_font_state_t* xui_font_state;

// TODO
// https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf

void xui_font_load ( xui_font_state_t* state ) {
    xui_font_state = state;

    std_alloc_t fonts_alloc = std_virtual_heap_alloc_array_m ( xui_font_t, xui_font_max_fonts_m );
    xui_font_state->fonts_memory_handle = fonts_alloc.handle;
    xui_font_state->fonts_array = ( xui_font_t* ) fonts_alloc.buffer.base;
    xui_font_state->fonts_freelist = std_freelist_array_m ( xui_font_state->fonts_array, xui_font_max_fonts_m );
    xui_font_state->uniform_data = NULL;
}

void xui_font_load_shaders ( xs_i* xs ) {
    xs_pipeline_state_h font_atlas_pipeline = xs->lookup_pipeline_state ( "xui_font_atlas" );
    std_assert_m ( font_atlas_pipeline != xs_null_handle_m );
    xui_font_state->font_atlas_pipeline = font_atlas_pipeline;
}

void xui_font_reload ( xui_font_state_t* state ) {
    xui_font_state = state;
}

void xui_font_unload ( void ) {
    std_virtual_heap_free ( xui_font_state->fonts_memory_handle );
}

xui_font_h xui_font_create_ttf ( std_buffer_t ttf_data, const xui_font_params_t* params ) {
    xg_i* xg = std_module_get_m ( xg_module_name_m );

    // TODO lock
    xui_font_t* font = std_list_pop_m ( &xui_font_state->fonts_freelist );

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

    // Atlas
    size_t atlas_size = xui_font_texture_atlas_width_m * xui_font_texture_atlas_height_m;
    std_alloc_t atlas_alloc = std_virtual_heap_alloc ( atlas_size, 16 );
#if 0
    std_alloc_t char_info_alloc = std_virtual_heap_alloc ( sizeof ( stbtt_bakedchar ) * params->char_count, 16 );

    int bake_result = stbtt_BakeFontBitmap ( ttf_data.base, 0, params->pixel_height, atlas_alloc.buffer.base, ( int ) params->atlas_width, ( int ) params->atlas_height, ( int ) params->first_char_code, ( int ) params->char_count, ( stbtt_bakedchar* ) char_info_alloc.buffer.base );

    std_assert_m ( bake_result > 0 );
#else
    stbtt_pack_context pack_context;
    std_alloc_t char_info_alloc = std_virtual_heap_alloc ( sizeof ( stbtt_packedchar ) * params->char_count, 16 );

    stbtt_PackBegin ( &pack_context, atlas_alloc.buffer.base, xui_font_texture_atlas_width_m, xui_font_texture_atlas_height_m, 0, 4, NULL );
    stbtt_PackSetOversampling ( &pack_context, 2, 2 );
    stbtt_PackFontRange ( &pack_context, ttf_data.base, 0, ( float ) params->pixel_height, ( int ) params->first_char_code, ( int ) params->char_count, ( stbtt_packedchar* ) char_info_alloc.buffer.base );
    stbtt_PackEnd ( &pack_context );
#endif

    font->char_info_handle = char_info_alloc.handle;

#if 0
    font->char_info = ( stbtt_bakedchar* ) char_info_alloc.buffer.base;
#else
    font->char_info = ( stbtt_packedchar* ) char_info_alloc.buffer.base;
#endif

    xg_texture_h raster_texture;
    {
        xg_texture_params_t texture_params = xg_default_texture_params_m;
        texture_params.allocator = xg->get_default_allocator ( params->xg_device, xg_memory_type_gpu_only_m );
        texture_params.device = params->xg_device;
        texture_params.width = xui_font_texture_atlas_width_m;
        texture_params.height = xui_font_texture_atlas_height_m;
        texture_params.format = xg_format_r8_uint_m;
        texture_params.allowed_usage = xg_texture_usage_copy_dest_m | xg_texture_usage_resource_m;
        std_str_copy_m ( texture_params.debug_name, "font atlas temp" );
        raster_texture = xg->create_texture ( &texture_params );
    }

    xg_buffer_h staging_buffer;
    {
        xg_buffer_params_t buffer_params = xg_default_buffer_params_m;
        buffer_params.allocator = xg->get_default_allocator ( params->xg_device, xg_memory_type_upload_m );
        buffer_params.device = params->xg_device;
        buffer_params.size = xui_font_texture_atlas_width_m * xui_font_texture_atlas_height_m;
        buffer_params.allowed_usage = xg_buffer_usage_copy_source_m;
        buffer_params.debug_name = "font atlas staging buffer";
        staging_buffer = xg->create_buffer ( &buffer_params );
    }

    xg_workload_h workload = xg->create_workload ( params->xg_device );
    xg_cmd_buffer_h cmd_buffer = xg->create_cmd_buffer ( workload );
    xg_resource_cmd_buffer_h resource_cmd_buffer = xg->create_resource_cmd_buffer ( workload );
    uint64_t key = 0;

    xg_buffer_info_t staging_buffer_info;
    xg->get_buffer_info ( &staging_buffer_info, staging_buffer );

    std_mem_copy ( staging_buffer_info.allocation.mapped_address, atlas_alloc.buffer.base, atlas_size );

    //xg->cmd_start_debug_capture ( cmd_buffer, xg_debug_capture_stop_time_workload_submit_m, key );

    // copy from staging to source atlas
    {
        xg_texture_memory_barrier_t barrier = xg_default_texture_memory_barrier_m;
        barrier.texture = raster_texture;
        barrier.layout.old = xg_texture_layout_undefined_m;
        barrier.layout.new = xg_texture_layout_copy_dest_m;
        barrier.memory.flushes = xg_memory_access_none_m;
        barrier.memory.invalidations = xg_memory_access_transfer_write_m;
        barrier.execution.blocker = xg_pipeline_stage_transfer_m;
        barrier.execution.blocked = xg_pipeline_stage_transfer_m;

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
        xg_texture_params_t texture_params = xg_default_texture_params_m;
        texture_params.allocator = xg->get_default_allocator ( params->xg_device, xg_memory_type_gpu_only_m );
        texture_params.device = params->xg_device;
        texture_params.width = xui_font_texture_atlas_width_m;
        texture_params.height = xui_font_texture_atlas_height_m;
        texture_params.format = xg_format_r8g8b8a8_unorm_m;
        texture_params.allowed_usage = xg_texture_usage_render_target_m | xg_texture_usage_resource_m;
        std_str_copy_m ( texture_params.debug_name, "font atlas" );
        atlas_texture = xg->create_texture ( &texture_params );
    }

    // atlas copy_dest -> shader_resource
    // final undefined -> render target
    {
        xg_texture_memory_barrier_t barriers[2];
        barriers[0] = xg_default_texture_memory_barrier_m;
        barriers[0].texture = raster_texture;
        barriers[0].layout.old = xg_texture_layout_copy_dest_m;
        barriers[0].layout.new = xg_texture_layout_shader_read_m;
        barriers[0].memory.flushes = xg_memory_access_transfer_write_m;
        barriers[0].memory.invalidations = xg_memory_access_shader_read_m;
        barriers[0].execution.blocker = xg_pipeline_stage_transfer_m;
        barriers[0].execution.blocked = xg_pipeline_stage_fragment_shader_m;
        barriers[1] = xg_default_texture_memory_barrier_m;
        barriers[1].texture = atlas_texture;
        barriers[1].layout.old = xg_texture_layout_undefined_m;
        barriers[1].layout.new = xg_texture_layout_render_target_m;
        barriers[1].memory.flushes = xg_memory_access_none_m;
        barriers[1].memory.invalidations = xg_memory_access_color_write_m;
        barriers[1].execution.blocker = xg_pipeline_stage_top_of_pipe_m;
        barriers[1].execution.blocked = xg_pipeline_stage_color_output_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 2;
        barrier_set.texture_memory_barriers = barriers;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, key );
    }

    xg_buffer_h uniform_buffer;
    {
        xg_i* xg = std_module_get_m ( xg_module_name_m );

        xg_buffer_params_t cbuffer_params;
        cbuffer_params.allocator = xg->get_default_allocator ( params->xg_device, xg_memory_type_gpu_mappable_m );
        cbuffer_params.device = params->xg_device;
        cbuffer_params.size = sizeof ( xui_font_atlas_uniform_data_t );
        cbuffer_params.allowed_usage = xg_buffer_usage_uniform_m;
        cbuffer_params.debug_name = "xui font cbuffer";
        uniform_buffer = xg->create_buffer ( &cbuffer_params );

        xg->cmd_destroy_buffer ( resource_cmd_buffer, uniform_buffer, xg_resource_cmd_buffer_time_workload_complete_m );

        xg_buffer_info_t uniform_buffer_info;
        xg->get_buffer_info ( &uniform_buffer_info, uniform_buffer );
        xui_font_state->uniform_data = ( xui_font_atlas_uniform_data_t* ) uniform_buffer_info.allocation.mapped_address;
    }

    xg->cmd_set_render_textures (
        cmd_buffer,
        &xg_render_textures_binding_m (
            .render_targets_count = 1,
            .render_targets[0] = { atlas_texture, xg_default_texture_view_m },
        ),
        key );

    xs_i* xs = std_module_get_m ( xs_module_name_m );

    xg->cmd_set_graphics_pipeline_state ( cmd_buffer, xs->get_pipeline_state ( xui_font_state->font_atlas_pipeline ), key );

    xg->cmd_set_pipeline_resources (
        cmd_buffer,
        &xg_pipeline_resource_bindings_m (
            .set = xg_resource_binding_set_per_draw_m,
            .buffer_count = 1,
            .buffers = ( xg_buffer_resource_binding_t[1] ) {
                {
                    .shader_register = 0,
                    .type = xg_buffer_binding_type_uniform_m,
                    .range = { .handle = uniform_buffer, .offset = 0, .size = sizeof ( xui_font_atlas_uniform_data_t ) }
                }
            },
            .texture_count = 1,
            .textures = &xg_texture_resource_binding_m (
                .shader_register = 1,
                .layout = xg_texture_layout_shader_read_m,
                .texture = raster_texture,
                .view = xg_default_texture_view_m
            ),
            .sampler_count = 1,
            .samplers = &xg_sampler_resource_binding_m (
                .shader_register = 2,
                .sampler = xg->get_default_sampler ( params->xg_device, xg_default_sampler_point_clamp_m )
            )
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
        buffer_resource.range.size = sizeof ( xui_font_atlas_uniform_data_t );

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

    xui_font_state->uniform_data->color[0] = 1;
    xui_font_state->uniform_data->color[1] = 1;
    xui_font_state->uniform_data->color[2] = 1;
    xui_font_state->uniform_data->outline = params->outline;
    xui_font_state->uniform_data->resolution_f32[0] = xui_font_texture_atlas_width_m;
    xui_font_state->uniform_data->resolution_f32[1] = xui_font_texture_atlas_height_m;

    xg->cmd_draw ( cmd_buffer, 3, 0, 0 );

    {
        xg_texture_memory_barrier_t barrier = xg_default_texture_memory_barrier_m;
        barrier.texture = atlas_texture;
        barrier.layout.old = xg_texture_layout_render_target_m;
        barrier.layout.new = xg_texture_layout_shader_read_m;
        barrier.memory.flushes = xg_memory_access_color_write_m;
        barrier.memory.invalidations = xg_memory_access_shader_read_m;
        barrier.execution.blocker = xg_pipeline_stage_color_output_m;
        barrier.execution.blocked = xg_pipeline_stage_fragment_shader_m;

        xg_barrier_set_t barrier_set = xg_barrier_set_m();
        barrier_set.texture_memory_barriers_count = 1;
        barrier_set.texture_memory_barriers = &barrier;

        xg->cmd_barrier_set ( cmd_buffer, &barrier_set, key );
    }

    xg->cmd_destroy_texture ( resource_cmd_buffer, raster_texture, xg_resource_cmd_buffer_time_workload_complete_m );

    xg->submit_workload ( workload );

    font->atlas_texture = atlas_texture;
    font->outline = params->outline;

    xui_font_h font_handle = ( xui_font_h ) ( font - xui_font_state->fonts_array );
    return font_handle;
}

void xui_font_destroy ( xui_font_h font ) {
    // TODO
    std_unused_m ( font );
}

xui_font_char_box_t xui_font_char_box_get ( float* fx, float* fy, xui_font_h font_handle, uint32_t character ) {
    xui_font_t* font = &xui_font_state->fonts_array[font_handle];

    uint32_t idx = character - font->params.first_char_code;

    stbtt_aligned_quad q;
    stbtt_GetPackedQuad ( font->char_info, xui_font_texture_atlas_width_m, xui_font_texture_atlas_height_m, idx, fx, fy, &q, 0 );

    //stbtt_packedchar* char_info = &font->char_info[idx];

    //float inv_width = 1.f / xui_font_texture_atlas_width_m;
    //float inv_height = 1.f / xui_font_texture_atlas_height_m;

    //uint32_t outline_pad = font->outline ? 1 : 0;

    xui_font_char_box_t box;
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

xg_texture_h xui_font_atlas_get ( xui_font_h font_handle ) {
    xui_font_t* font = &xui_font_state->fonts_array[font_handle];
    return font->atlas_texture;
}

void xui_font_get_info ( xui_font_info_t* info, xui_font_h font_handle ) {
    xui_font_t* font = &xui_font_state->fonts_array[font_handle];
    info->pixel_height = font->params.pixel_height;
}
