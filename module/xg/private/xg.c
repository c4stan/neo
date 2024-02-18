#include <xg.h>

#include "xg_state.h"
#include "xg_cmd_buffer.h"
#include "xg_resource_cmd_buffer.h"
#include "xg_debug_capture.h"

#if std_enabled_m(xg_backend_vulkan_m)
    #include "vulkan/xg_vk.h"
    #include "vulkan/xg_vk_device.h"
    #include "vulkan/xg_vk_swapchain.h"
    #include "vulkan/xg_vk_texture.h"
    #include "vulkan/xg_vk_buffer.h"
    #include "vulkan/xg_vk_cmd_buffer.h"
    #include "vulkan/xg_vk_event.h"
    #include "vulkan/xg_vk_pipeline.h"
    #include "vulkan/xg_vk_allocator.h"
    #include "vulkan/xg_vk_sampler.h"
    #include "vulkan/xg_vk_workload.h"
#endif

static void xg_api_init ( xg_i* xg ) {
    // Device
    xg->get_devices_count = xg_vk_device_get_count;
    xg->get_devices = xg_vk_device_get_list;
    xg->get_device_info = xg_vk_device_get_info;
    xg->activate_device = xg_vk_device_activate;
    xg->deactivate_device = xg_vk_device_deactivate;
    //xg->map_alloc = xg_vk_device_map_alloc;
    //xg->unmap_alloc = xg_vk_device_unmap_alloc;
    // Swapchain
    xg->create_window_swapchain = xg_vk_swapchain_create_window;
    xg->create_display_swapchain = NULL;
    xg->create_virtual_swapchain = NULL;
    xg->acquire_next_swapchain_texture = xg_vk_swapchain_acquire_next_texture;
    xg->get_swapchain_texture = xg_vk_swapchain_get_texture;
    xg->present_swapchain = xg_vk_swapchain_present;
    xg->resize_swapchain = xg_vk_swapchain_resize;
    xg->get_swapchain_info = xg_vk_swapchain_get_info;
    // Pipeline
    xg->create_graphics_pipeline = xg_vk_graphics_pipeline_create;
    xg->destroy_graphics_pipeline = xg_vk_graphics_pipeline_destroy;
    xg->create_compute_pipeline = xg_vk_compute_pipeline_create;
    xg->destroy_compute_pipeline = xg_vk_compute_pipeline_destroy;
    // Workload
    xg->create_workload = xg_workload_create;
    xg->create_cmd_buffer = xg_workload_add_cmd_buffer;
    //xg->create_cmd_buffers = xg_cmd_buffer_open_n;
    //xg.close_cmd_buffers = xg_cmd_buffer_close;
    //xg.discard_cmd_buffers = xg_cmd_buffer_discard;
    xg->submit_workload = xg_vk_cmd_buffer_submit;
    xg->is_workload_complete = xg_workload_is_complete;
    xg->write_workload_uniform = xg_workload_write_uniform;
    // Command Buffer
    xg->cmd_barrier_set = xg_cmd_buffer_barrier_set;
    xg->cmd_clear_texture = xg_cmd_buffer_texture_clear;
    xg->cmd_clear_depth_stencil_texture = xg_cmd_buffer_texture_depth_stencil_clear;
    xg->cmd_set_vertex_streams = xg_cmd_buffer_graphics_streams_bind;
    xg->cmd_draw = xg_cmd_buffer_graphics_streams_submit;
    xg->cmd_draw_indexed = xg_cmd_buffer_graphics_streams_submit_indexed;
    xg->cmd_draw_instanced = xg_cmd_buffer_graphics_streams_submit_instanced;
    xg->cmd_dispatch_compute = xg_cmd_buffer_compute_dispatch;
    xg->cmd_set_graphics_pipeline_state = xg_cmd_buffer_graphics_pipeline_state_bind;
    xg->cmd_set_compute_pipeline_state = xg_cmd_buffer_compute_pipeline_state_bind;
    xg->cmd_set_render_textures = xg_cmd_buffer_graphics_render_textures_bind;
    xg->cmd_copy_texture = xg_cmd_buffer_copy_texture;
    xg->cmd_copy_buffer = xg_cmd_buffer_copy_buffer;
    xg->cmd_copy_buffer_to_texture = xg_cmd_buffer_copy_buffer_to_texture;
    xg->cmd_set_pipeline_resources = xg_cmd_buffer_pipeline_resources_bind;
    //#if defined(std_platform_win32_m)
    xg->cmd_start_debug_capture = xg_cmd_buffer_start_debug_capture;
    xg->cmd_stop_debug_capture = xg_cmd_buffer_stop_debug_capture;
    //#endif
    xg->cmd_begin_debug_region = xg_cmd_buffer_begin_debug_region;
    xg->cmd_end_debug_region = xg_cmd_buffer_end_debug_region;
    // Resource command buffer
    xg->create_resource_cmd_buffer = xg_workload_add_resource_cmd_buffer;
    //xg->create_resource_cmd_buffers = xg_resource_cmd_buffer_open_n;
    //xg.close_resource_cmd_buffers = xg_resource_cmd_buffer_close;
    xg->cmd_create_buffer = xg_resource_cmd_buffer_buffer_create;
    xg->cmd_create_texture = xg_resource_cmd_buffer_texture_create;
    xg->cmd_destroy_buffer = xg_resource_cmd_buffer_buffer_destroy;
    xg->cmd_destroy_texture = xg_resource_cmd_buffer_texture_destroy;
    xg->cmd_set_pipeline_viewport = xg_cmd_buffer_graphics_pipeline_state_set_viewport;
    xg->create_buffer = xg_buffer_create;
    xg->create_texture = xg_texture_create;
    xg->get_buffer_info = xg_buffer_get_info;
    xg->get_texture_info = xg_texture_get_info;
    xg->create_sampler = xg_sampler_create;
    xg->get_default_sampler = xg_sampler_get_default;
    // Allocator
    xg->get_default_allocator = xg_allocator_default;
    xg->get_default_allocator_info = xg_vk_allocator_get_info;

#if std_enabled_m(xg_debug_simple_frame_test_m)
    xg->debug_simple_frame = xg_debug_simple_frame;
#endif
}

void* xg_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xg_state_t* state = xg_state_alloc();

    xg_vk_instance_load ( &state->vk.instance, xg_runtime_layer_debug_m | xg_runtime_layer_renderdoc_m );
    xg_vk_device_load ( &state->vk.device );
    xg_vk_allocator_load ( &state->vk.allocator );
    xg_vk_cmd_buffer_load ( &state->vk.cmd_buffer );
    xg_vk_event_load ( &state->vk.event );
    xg_vk_texture_load ( &state->vk.texture );
    xg_vk_buffer_load ( &state->vk.buffer );
    xg_vk_swapchain_load ( &state->vk.swapchain );
    xg_vk_sampler_load ( &state->vk.sampler );
    xg_vk_pipeline_load ( &state->vk.pipeline );
    xg_vk_workload_load ( &state->vk.workload );

    xg_cmd_buffer_load ( &state->cmd_buffer );
    xg_resource_cmd_buffer_load ( &state->resource_cmd_buffer );
    //#if defined(std_platform_win32_m)
    xg_debug_capture_load ( &state->debug_capture );
    //#endif

    xg_api_init ( &state->api );
    return &state->api;
}

void xg_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    xg_state_t* state = ( xg_state_t* ) api;

    xg_vk_instance_reload ( &state->vk.instance );
    xg_vk_device_reload ( &state->vk.device );
    xg_vk_allocator_reload ( &state->vk.allocator );
    xg_vk_cmd_buffer_reload ( &state->vk.cmd_buffer );
    xg_vk_event_reload ( &state->vk.event );
    xg_vk_texture_reload ( &state->vk.texture );
    xg_vk_buffer_reload ( &state->vk.buffer );
    xg_vk_swapchain_reload ( &state->vk.swapchain );
    xg_vk_sampler_reload ( &state->vk.sampler );
    xg_vk_pipeline_reload ( &state->vk.pipeline );
    xg_vk_workload_reload ( &state->vk.workload );

    xg_cmd_buffer_reload ( &state->cmd_buffer );
    xg_resource_cmd_buffer_reload ( &state->resource_cmd_buffer );
    //#if defined(std_platform_win32_m)
    xg_debug_capture_reload ( &state->debug_capture );
    //#endif

    xg_api_init ( &state->api );
}

void xg_unload ( void ) {
    xg_vk_instance_unload();
    xg_vk_device_unload();
    xg_vk_allocator_unload();
    xg_vk_cmd_buffer_unload();
    xg_vk_event_unload();
    xg_vk_texture_unload();
    xg_vk_buffer_unload();
    xg_vk_swapchain_unload();
    xg_vk_sampler_unload();
    xg_vk_pipeline_unload();
    xg_vk_workload_unload();

    xg_cmd_buffer_unload();
    xg_resource_cmd_buffer_unload();
    //#if defined(std_platform_win32_m)
    xg_debug_capture_unload();
    //#endif

    xg_state_free();
}

#if 0
#if std_enabled_m(xg_backend_vulkan_m)
    // Backend
    xg_vk_instance_init ( xg_runtime_layer_debug_m | xg_runtime_layer_renderdoc_m );

    std_log_info_m ( "xg_vk_instance initialized." );
    xg_vk_cmd_buffer_init();
    std_log_info_m ( "xg_vk_cmd_buffer initialized." );
    xg_vk_device_init();
    std_log_info_m ( "xg_vk_device initialized." );
    xg_vk_event_init();
    std_log_info_m ( "xg_vk_event initialized." );
    xg_vk_texture_init();
    std_log_info_m ( "xg_vk_texture initialized." );
    xg_vk_buffer_init();
    std_log_info_m ( "xg_vk_buffer initialized." );
    xg_vk_swapchain_init();
    std_log_info_m ( "xg_vk_swapchain initialized." );
    xg_vk_sampler_init();
    std_log_info_m ( "xg_vk_sampler initialized." );
    xg_vk_pipeline_init();
    std_log_info_m ( "xg_vk_pipeline initialized." );
    xg_cmd_buffer_init();
    std_log_info_m ( "xg_cmd_buffer initialized." );
    xg_resource_cmd_buffer_init();
    std_log_info_m ( "xg_resource_cmd_buffer initialized." );
    xg_workload_init();
    std_log_info_m ( "xg_workload initialized." );
    xg_vk_allocator_init();
    std_log_info_m ( "xg_vk_allocator initialized." );
    #if defined(std_platform_win32_m)
        xg_debug_capture_init();
        std_log_info_m ( "xg_debug_capture initialized." );
    #endif
    // Api
    s_api = xg_vk_api();
#else
    #error "Vulkan is the only xg backend supported for now"
#endif

return &s_api;
}


void xg_unload ( void ) {
    xg_vk_device_shutdown();
}
#endif
