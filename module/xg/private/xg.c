#include <xg.h>

#include "xg_state.h"
#include "xg_cmd_buffer.h"
#include "xg_resource_cmd_buffer.h"
#include "xg_debug_capture.h"

#if xg_backend_vulkan_enabled_m
    #include "vulkan/xg_vk.h"
    #include "vulkan/xg_vk_device.h"
    #include "vulkan/xg_vk_swapchain.h"
    #include "vulkan/xg_vk_texture.h"
    #include "vulkan/xg_vk_buffer.h"
    #include "vulkan/xg_vk_event.h"
    #include "vulkan/xg_vk_pipeline.h"
    #include "vulkan/xg_vk_allocator.h"
    #include "vulkan/xg_vk_sampler.h"
    #include "vulkan/xg_vk_workload.h"
    #include "vulkan/xg_vk_query.h"
#endif
    #include "vulkan/xg_vk_raytrace.h"

static void xg_api_init ( xg_i* xg ) {
    // Device
    xg->get_devices_count = xg_vk_device_get_count;
    xg->get_devices = xg_vk_device_get_list;
    xg->get_device_info = xg_vk_device_get_info;
    xg->activate_device = xg_vk_device_activate;
    xg->deactivate_device = xg_vk_device_deactivate;
    xg->timestamp_to_ns = xg_vk_device_timestamp_period;
    // Swapchain
    xg->create_window_swapchain = xg_vk_swapchain_create_window;
    xg->create_display_swapchain = NULL;
    xg->create_virtual_swapchain = NULL;
    xg->acquire_swapchain = xg_vk_swapchain_acquire_next_texture;
    xg->get_swapchain_texture = xg_vk_swapchain_get_texture;
    xg->present_swapchain = xg_vk_swapchain_present;
    xg->resize_swapchain = xg_vk_swapchain_resize;
    xg->get_swapchain_info = xg_vk_swapchain_get_info;
    // Pipeline
    xg->create_graphics_pipeline = xg_vk_graphics_pipeline_create;
    xg->destroy_graphics_pipeline = xg_vk_graphics_pipeline_destroy;
    xg->create_compute_pipeline = xg_vk_compute_pipeline_create;
    xg->destroy_compute_pipeline = xg_vk_compute_pipeline_destroy;
    xg->create_raytrace_pipeline = xg_vk_raytrace_pipeline_create;
    xg->destroy_raytrace_pipeline = xg_vk_raytrace_pipeline_destroy;
    xg->create_renderpass = xg_vk_renderpass_create;
    xg->destroy_renderpass = xg_vk_renderpass_destroy;
    xg->get_pipeline_resource_layouts = xg_vk_pipeline_resource_binding_set_layouts_get;
    xg->get_pipeline_resource_layout = xg_vk_pipeline_resource_binding_set_layout_get;
    xg->create_resource_layout = xg_vk_pipeline_resource_bindings_layout_create;
    xg->destroy_resource_layout = xg_vk_pipeline_resource_bindings_layout_destroy;
    xg->get_pipeline_info = xg_vk_pipeline_get_info;
    // Workload
    xg->create_workload = xg_workload_create;
    xg->create_cmd_buffer = xg_workload_add_cmd_buffer;
    xg->submit_workload = xg_workload_submit;
    xg->is_workload_complete = xg_workload_is_complete;
    xg->write_workload_uniform = xg_workload_write_uniform;
    xg->write_workload_staging = xg_workload_write_staging;
    xg->wait_all_workload_complete = xg_workload_wait_all_workload_complete;
    xg->set_workload_global_bindings = xg_workload_set_global_resource_group;
    xg->debug_capture_workload = xg_vk_workload_enable_debug_capture;
    xg->wait_for_workload = xg_workload_wait_for_workload;
    // Raytrace
    xg->create_raytrace_geometry = xg_vk_raytrace_geometry_create;
    xg->create_raytrace_world = xg_vk_raytrace_world_create;
    xg->cmd_raytrace = xg_cmd_buffer_cmd_raytrace;
    xg->destroy_raytrace_world = xg_vk_raytrace_world_destroy;
    xg->destroy_raytrace_geometry = xg_vk_raytrace_geometry_destroy;
    // Command Buffer
    xg->cmd_barrier_set = xg_cmd_buffer_barrier_set;
    xg->cmd_clear_texture = xg_cmd_buffer_clear_texture;
    xg->cmd_clear_depth_stencil_texture = xg_cmd_buffer_clear_depth_stencil_texture;
    xg->cmd_clear_buffer = xg_cmd_buffer_clear_buffer;
    xg->cmd_draw = xg_cmd_buffer_cmd_draw;
    xg->cmd_compute = xg_cmd_buffer_cmd_compute;
    xg->cmd_copy_texture = xg_cmd_buffer_copy_texture;
    xg->cmd_copy_buffer = xg_cmd_buffer_copy_buffer;
    xg->cmd_copy_buffer_to_texture = xg_cmd_buffer_copy_buffer_to_texture;
    xg->cmd_set_dynamic_viewport = xg_cmd_dynamic_viewport;
    xg->cmd_set_dynamic_scissor = xg_cmd_dynamic_scissor;
    //#if defined(std_platform_win32_m)
    xg->cmd_start_debug_capture = xg_cmd_buffer_start_debug_capture;
    xg->cmd_stop_debug_capture = xg_cmd_buffer_stop_debug_capture;
    //#endif
    xg->cmd_begin_debug_region = xg_cmd_buffer_begin_debug_region;
    xg->cmd_end_debug_region = xg_cmd_buffer_end_debug_region;
    xg->cmd_begin_renderpass = xg_cmd_buffer_cmd_renderpass_begin;
    xg->cmd_end_renderpass = xg_cmd_buffer_cmd_renderpass_end;
    xg->cmd_bind_queue = xg_cmd_bind_queue;
    // Resource command buffer
    xg->create_resource_cmd_buffer = xg_workload_add_resource_cmd_buffer;
    xg->cmd_create_buffer = xg_resource_cmd_buffer_buffer_create;
    xg->cmd_create_texture = xg_resource_cmd_buffer_texture_create;
    xg->cmd_destroy_buffer = xg_resource_cmd_buffer_buffer_destroy;
    xg->cmd_destroy_texture = xg_resource_cmd_buffer_texture_destroy;
    xg->cmd_destroy_renderpass = xg_resource_cmd_buffer_graphics_renderpass_destroy;
    xg->cmd_create_workload_bindings = xg_resource_cmd_buffer_workload_resource_bindings_create;
    xg->cmd_destroy_queue_event = xg_resource_cmd_buffer_queue_event_destroy;
    xg->create_buffer = xg_buffer_create;
    xg->create_texture = xg_texture_create;
    xg->get_buffer_info = xg_buffer_get_info;
    xg->get_texture_info = xg_texture_get_info;
    xg->create_sampler = xg_sampler_create;
    xg->get_default_sampler = xg_sampler_get_default;
    xg->create_queue_event = xg_gpu_queue_event_create;
    xg->get_default_texture = xg_texture_get_default;
    // Allocator
    xg->get_allocator_info = xg_vk_allocator_get_info;
    xg->alloc_memory = xg_alloc;
    xg->free_memory = xg_free;
    xg->get_texture_memory_requirement = xg_texture_memory_requirement;
    // Query pool
    xg->create_query_pool = xg_vk_query_pool_create;
    xg->read_query_pool = xg_vk_query_pool_read;
    xg->destroy_query_pool = xg_vk_query_pool_destroy;
    xg->cmd_query_timestamp = xg_cmd_buffer_write_timestamp;
    xg->cmd_reset_query_pool = xg_cmd_buffer_reset_query_pool;
}

void* xg_load ( void* std_runtime ) {
    std_runtime_bind ( std_runtime );

    xg_state_t* state = xg_state_alloc();

    xg_vk_instance_load ( &state->vk.instance, xg_instance_enabled_runtime_layers_m );
    xg_vk_allocator_load ( &state->vk.allocator );
    xg_vk_device_load ( &state->vk.device );
    xg_vk_query_load ( &state->vk.query );
    xg_vk_event_load ( &state->vk.event );
    xg_vk_texture_load ( &state->vk.texture );
    xg_vk_buffer_load ( &state->vk.buffer );
    xg_vk_swapchain_load ( &state->vk.swapchain );
    xg_vk_sampler_load ( &state->vk.sampler );
    xg_vk_raytrace_load ( &state->vk.raytrace );
    xg_vk_pipeline_load ( &state->vk.pipeline );
    xg_vk_workload_load ( &state->vk.workload );

    xg_cmd_buffer_load ( &state->cmd_buffer );
    xg_resource_cmd_buffer_load ( &state->resource_cmd_buffer );
    xg_debug_capture_load ( &state->debug_capture );

    xg_api_init ( &state->api );
    return &state->api;
}

void xg_reload ( void* std_runtime, void* api ) {
    std_runtime_bind ( std_runtime );

    xg_state_t* state = ( xg_state_t* ) api;

    xg_vk_instance_reload ( &state->vk.instance );
    xg_vk_device_reload ( &state->vk.device );
    xg_vk_allocator_reload ( &state->vk.allocator );
    xg_vk_query_reload ( &state->vk.query );
    xg_vk_event_reload ( &state->vk.event );
    xg_vk_texture_reload ( &state->vk.texture );
    xg_vk_buffer_reload ( &state->vk.buffer );
    xg_vk_swapchain_reload ( &state->vk.swapchain );
    xg_vk_sampler_reload ( &state->vk.sampler );
    xg_vk_raytrace_reload ( &state->vk.raytrace );
    xg_vk_pipeline_reload ( &state->vk.pipeline );
    xg_vk_workload_reload ( &state->vk.workload );

    xg_cmd_buffer_reload ( &state->cmd_buffer );
    xg_resource_cmd_buffer_reload ( &state->resource_cmd_buffer );
    xg_debug_capture_reload ( &state->debug_capture );

    xg_api_init ( &state->api );
    xg_state_bind ( state );
}

void xg_unload ( void ) {
    xg_vk_device_wait_idle_all();

    xg_debug_capture_unload();

    xg_vk_workload_unload();

    xg_resource_cmd_buffer_unload();
    xg_cmd_buffer_unload();

    xg_vk_pipeline_unload();
    xg_vk_raytrace_unload();
    xg_vk_sampler_unload();
    xg_vk_swapchain_unload();
    xg_vk_buffer_unload();
    xg_vk_texture_unload();
    xg_vk_event_unload();
    xg_vk_query_unload();
    xg_vk_device_unload();
    xg_vk_allocator_unload();
    xg_vk_instance_unload();

    xg_state_free();
}
