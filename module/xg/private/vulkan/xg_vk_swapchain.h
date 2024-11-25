#pragma once

#include <xg.h>

#include "xg_vk.h"
#include "xg_vk_workload.h"

#include "xg_vk_event.h"

#include <std_allocator.h>
#include <std_mutex.h>

typedef struct {
    VkSwapchainKHR vk_handle;
    VkSurfaceKHR surface;
    //xg_swapchain_info_t info;
    size_t texture_count;
    xg_format_e format;
    xg_color_space_e color_space;
    size_t width;
    size_t height;
    xg_present_mode_e present_mode;
    xg_device_h device;
    wm_window_h window;
    xg_display_h display;
    char debug_name[xg_debug_name_size_m];

    xg_texture_h textures[xg_swapchain_max_textures_m];
    size_t acquired_texture_idx;
    bool acquired;

    //xg_gpu_queue_event_h texture_acquire_event;
    // Signaled by vkQueueSubmit and waited on by vkQueuePresent
    xg_gpu_queue_event_h execution_complete_gpu_events[xg_swapchain_max_textures_m];
} xg_vk_swapchain_t;

typedef struct {
    xg_vk_swapchain_t* swapchains_array;
    xg_vk_swapchain_t* swapchains_freelist;
    uint64_t* swapchain_bitset;
    std_mutex_t swapchains_mutex;
} xg_vk_swapchain_state_t;

void xg_vk_swapchain_load ( xg_vk_swapchain_state_t* state );
void xg_vk_swapchain_reload ( xg_vk_swapchain_state_t* state );
void xg_vk_swapchain_unload ( void );

xg_swapchain_h  xg_vk_swapchain_create_window ( const xg_swapchain_window_params_t* desc );
xg_swapchain_h  xg_vk_swapchain_create_display ( const xg_swapchain_display_params_t* desc );
xg_swapchain_h  xg_vk_swapchain_create_virtual ( const xg_swapchain_virtual_params_t* desc );

bool            xg_vk_swapchain_resize ( xg_swapchain_h swapchain, size_t width, size_t height );

bool            xg_vk_swapchain_get_info ( xg_swapchain_info_t* info, xg_swapchain_h swapchain );

//xg_texture_h    xg_vk_swapchain_get_texture ( xg_swapchain_h swapchain, size_t idx );
void            xg_vk_swapchain_acquire_next_texture ( xg_swapchain_acquire_result_t* result, xg_swapchain_h swapchain_handle, xg_workload_h workload_handle );
xg_texture_h    xg_vk_swapchain_get_texture ( xg_swapchain_h swapchain );
void            xg_vk_swapchain_present ( xg_swapchain_h swapchain, xg_workload_h workload );

void xg_vk_swapchain_destroy ( xg_swapchain_h swapchain );


