#include "xg_vk_swapchain.h"

#include "xg_vk_device.h"
#include "xg_vk_texture.h"
#include "xg_vk_instance.h"
#include "xg_vk_enum.h"
#include "xg_vk_workload.h"

#include <xg_enum.h>
#include "../xg_debug_capture.h"

#include <std_list.h>
#include <std_module.h>
#include <std_time.h>

// TODO remove wm dependency and take opaque handles as creation parameter?
#include <wm.h>

static xg_vk_swapchain_state_t* xg_vk_swapchain_state;

#define xg_vk_swapchain_bitset_u64_count_m std_div_ceil_m ( xg_vk_max_swapchains_m, 64 )

void xg_vk_swapchain_load ( xg_vk_swapchain_state_t* state ) {
    xg_vk_swapchain_state = state;

    xg_vk_swapchain_state->swapchains_array = std_virtual_heap_alloc_array_m ( xg_vk_swapchain_t, xg_vk_max_swapchains_m );
    xg_vk_swapchain_state->swapchains_freelist = std_freelist_m ( xg_vk_swapchain_state->swapchains_array, xg_vk_max_swapchains_m );
    xg_vk_swapchain_state->swapchain_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_swapchain_bitset_u64_count_m );
    std_mem_zero_array_m ( xg_vk_swapchain_state->swapchain_bitset, xg_vk_swapchain_bitset_u64_count_m );
    std_mutex_init ( &xg_vk_swapchain_state->swapchains_mutex );
}

void xg_vk_swapchain_reload ( xg_vk_swapchain_state_t* state ) {
    xg_vk_swapchain_state = state;
}

void xg_vk_swapchain_unload ( void ) {
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_swapchain_state->swapchain_bitset, idx, xg_vk_swapchain_bitset_u64_count_m ) ) {
        xg_vk_swapchain_destroy ( idx );
        ++idx;
    }

    std_virtual_heap_free ( xg_vk_swapchain_state->swapchains_array );
    std_virtual_heap_free ( xg_vk_swapchain_state->swapchain_bitset );
    std_mutex_deinit ( &xg_vk_swapchain_state->swapchains_mutex );
}

xg_swapchain_h xg_vk_swapchain_create_window ( const xg_swapchain_window_params_t* params ) {
    // Pop new swapchain
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = std_list_pop_m ( &xg_vk_swapchain_state->swapchains_freelist );
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );
    std_assert_m ( swapchain );
    xg_swapchain_h swapchain_handle = ( xg_swapchain_h ) ( swapchain - xg_vk_swapchain_state->swapchains_array );
    std_bitset_set ( xg_vk_swapchain_state->swapchain_bitset, swapchain_handle );

    // Get WM info
    wm_window_info_t window_info;
    wm_i* wm = std_module_get_m ( wm_module_name_m );
    wm->get_window_info ( params->window, &window_info );

    // Get device
    const xg_vk_device_t* device = xg_vk_device_get ( params->device );
    std_assert_m ( device );

    std_log_info_m ( "Creating swapchain for window '" std_fmt_str_m "'", window_info.title );

    // Create Vk surface
#if defined(std_platform_win32_m)
    VkWin32SurfaceCreateInfoKHR surface_create_info;
    surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surface_create_info.pNext = NULL;
    surface_create_info.flags = 0;
    surface_create_info.hinstance = ( HINSTANCE ) window_info.os_handle.win32.hinstance;
    surface_create_info.hwnd = ( HWND ) window_info.os_handle.win32.hwnd;
    xg_vk_safecall_m ( vkCreateWin32SurfaceKHR ( xg_vk_instance(), &surface_create_info, NULL, &swapchain->surface ), xg_null_handle_m );
#elif defined(std_platform_linux_m)
    VkXlibSurfaceCreateInfoKHR surface_create_info;
    surface_create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surface_create_info.pNext = NULL;
    surface_create_info.flags = 0;
    surface_create_info.dpy = ( Display* ) window_info.os_handle.x11.display;
    surface_create_info.window = ( Window ) window_info.os_handle.x11.window;
    xg_vk_safecall_m ( vkCreateXlibSurfaceKHR ( xg_vk_instance(), &surface_create_info, NULL, &swapchain->surface ), xg_null_handle_m );
#endif

    // Get surface capabilities
    VkSurfaceCapabilitiesKHR surface_capabilities;
    xg_vk_safecall_m ( vkGetPhysicalDeviceSurfaceCapabilitiesKHR ( device->vk_physical_handle, swapchain->surface, &surface_capabilities ), xg_null_handle_m );

    // Print surface capabilities
    // note: maxImageCount==0 means no upper limit
    std_log_info_m ( "Window supports texture resolution between " std_fmt_u32_m "x" std_fmt_u32_m " and " std_fmt_u32_m "x" std_fmt_u32_m " and texture count between " std_fmt_u32_m " and " std_fmt_u32_m,
        surface_capabilities.minImageExtent.width, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.width, surface_capabilities.maxImageExtent.height,
        surface_capabilities.minImageCount, surface_capabilities.maxImageCount );
    swapchain->width = surface_capabilities.currentExtent.width;
    swapchain->height = surface_capabilities.currentExtent.height;

    // Get surface formats
    {
        uint32_t surface_format_count;
        VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR ( device->vk_physical_handle, swapchain->surface, &surface_format_count, NULL );
        std_assert_m ( result == VK_SUCCESS );
        std_assert_m ( xg_vk_query_max_surface_formats_m >= surface_format_count, "Device supports more surface formats than current cap. Increase xg_vk_query_max_surface_formats_m." );

        VkSurfaceFormatKHR surface_formats[xg_vk_query_max_surface_formats_m];
        result = vkGetPhysicalDeviceSurfaceFormatsKHR ( device->vk_physical_handle, swapchain->surface, &surface_format_count, surface_formats );
        std_assert_m ( result == VK_SUCCESS );

        // Print surface formats
#if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
        if ( surface_format_count == 0 ) {
            std_log_error_m ( "Window doesn't support any surface format?" );
        } else {
            std_log_info_m ( "Window supports " std_fmt_u32_m " swapchain texture formats:", surface_format_count );

            for ( uint32_t i = 0; i < surface_format_count; ++i ) {
                xg_format_e format = xg_format_from_vk ( surface_formats[i].format );
                xg_color_space_e colorspace = xg_color_space_from_vk ( surface_formats[i].colorSpace );
                std_log_info_m ( "\t" std_fmt_str_m " - " std_fmt_str_m, xg_format_str ( format ), xg_color_space_str ( colorspace ) );
            }
        }
#endif
    }

    // Get surface present modes
    {
        uint32_t surface_present_mode_count;
        xg_vk_safecall_m ( vkGetPhysicalDeviceSurfacePresentModesKHR ( device->vk_physical_handle, swapchain->surface, &surface_present_mode_count, NULL ), xg_null_handle_m );
        std_assert_m ( xg_vk_query_max_present_modes_m >= surface_present_mode_count, "Device supports more present modes than current cap. Increase xg_vk_query_max_present_modes_m." );
        VkPresentModeKHR surface_present_modes[xg_vk_query_max_present_modes_m];
        xg_vk_safecall_m ( vkGetPhysicalDeviceSurfacePresentModesKHR ( device->vk_physical_handle, swapchain->surface, &surface_present_mode_count, surface_present_modes ), xg_null_handle_m );

        // Print surface present modes
#if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
        if ( surface_present_mode_count == 0 ) {
            std_log_error_m ( "Window doesn't support any present mode?" );
        } else {
            std_log_info_m ( "Window supports " std_fmt_u32_m " present modes:", surface_present_mode_count );

            for ( uint32_t i = 0; i < surface_present_mode_count; ++i ) {
                xg_present_mode_e present_mode = xg_present_mode_from_vk ( surface_present_modes[i] );
                std_log_info_m ( "\t" std_fmt_str_m, xg_present_mode_str ( present_mode ) );
            }
        }
#endif
    }

    // Test for present support on given device
    VkBool32 is_surface_supported = VK_FALSE;
    xg_vk_safecall_m ( vkGetPhysicalDeviceSurfaceSupportKHR ( device->vk_physical_handle, device->queues[xg_cmd_queue_graphics_m].vk_family_idx, swapchain->surface, &is_surface_supported ), xg_null_handle_m );

    if ( is_surface_supported == VK_FALSE ) {
        return xg_null_handle_m;
    }

    // Create Vk swapchain
    VkFormat vk_format = xg_format_to_vk ( params->format );
    VkColorSpaceKHR vk_color_space = xg_color_space_to_vk ( params->color_space );
    VkPresentModeKHR vk_present_mode = xg_present_mode_to_vk ( params->present_mode );
    uint32_t texture_count = ( uint32_t ) params->texture_count;
    uint32_t width = ( uint32_t ) swapchain->width;
    uint32_t height = ( uint32_t ) swapchain->height;

    xg_texture_usage_bit_e allowed_usage = xg_texture_usage_bit_render_target_m | xg_texture_usage_bit_copy_dest_m; // TODO probably need more allowed usages
    allowed_usage |= xg_texture_usage_bit_copy_source_m; // Looks like this gets added automatically once copy_dest is in? and if it's not accounted for in the attachments info it will cause a warning to spam
    allowed_usage |= xg_texture_usage_bit_sampled_m; // For now defaulting all render targets to render_target/copy_source/copy_dest/shader_resource. TODO create and cache framebuffers on the fly
    allowed_usage |= xg_texture_usage_bit_storage_m; // Because storage usage was added by default to framebuffer creation, becuase of raytracing...

    VkSwapchainCreateInfoKHR swapchain_create_info;
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.pNext = NULL;
    swapchain_create_info.flags = 0;
    swapchain_create_info.surface = swapchain->surface;
    swapchain_create_info.minImageCount = texture_count;
    swapchain_create_info.imageFormat = vk_format; //VK_FORMAT_B8G8R8A8_UNORM;
    swapchain_create_info.imageColorSpace = vk_color_space; //VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapchain_create_info.imageExtent.width = width;
    swapchain_create_info.imageExtent.height = height;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = xg_image_usage_to_vk ( allowed_usage );
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices = NULL;
    swapchain_create_info.preTransform = surface_capabilities.currentTransform;//VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = vk_present_mode; //VK_PRESENT_MODE_MAILBOX_KHR;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;
    xg_vk_safecall_m ( vkCreateSwapchainKHR ( device->vk_handle, &swapchain_create_info, NULL, &swapchain->vk_handle ), xg_null_handle_m );

    if ( params->debug_name[0] != '\0' ) {
        VkDebugUtilsObjectNameInfoEXT debug_name;
        debug_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name.pNext = NULL;
        debug_name.objectType = VK_OBJECT_TYPE_SWAPCHAIN_KHR;
        debug_name.objectHandle = ( uint64_t ) swapchain->vk_handle;
        debug_name.pObjectName = params->debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );
    }

    VkImage swapchain_textures[xg_swapchain_max_textures_m];
    uint32_t acquired_texture_count = xg_swapchain_max_textures_m;
    vkGetSwapchainImagesKHR ( device->vk_handle, swapchain->vk_handle, &acquired_texture_count, swapchain_textures );
    texture_count = acquired_texture_count;

    xg_texture_params_t swapchain_texture_params = xg_texture_params_m (
        .device = params->device,
        .width = width,
        .height = height,
        .depth = 1,
        .mip_levels = 1,
        .array_layers = 1,
        .dimension = xg_texture_dimension_2d_m,
        .format = params->format,
        .allowed_usage = allowed_usage,
        .initial_layout = xg_texture_layout_undefined_m, // ?
        .samples_per_pixel = xg_sample_count_1_m,
    );
    std_str_copy_static_m ( swapchain_texture_params.debug_name, params->debug_name );

    std_log_info_m ( "Swapchain created with " std_fmt_size_m " backbuffer textures", acquired_texture_count );

    for ( size_t i = 0; i < texture_count; ++i ) {
        char id[8];
        std_u32_to_str ( id, 8, i, 0 );
        xg_texture_params_t texture_params = swapchain_texture_params;
        std_stack_t stack = std_static_stack_m ( texture_params.debug_name );
        stack.top += std_str_len ( texture_params.debug_name );
        std_stack_string_append ( &stack, "(" );
        std_stack_string_append ( &stack, id );
        std_stack_string_append ( &stack, ")" );

        swapchain->textures[i] = xg_vk_texture_register_swapchain_texture ( &texture_params, swapchain_textures[i] );

        xg_queue_event_params_t event_params = xg_queue_event_params_m ( .device = params->device );
        std_str_copy_static_m ( event_params.debug_name, texture_params.debug_name );
        swapchain->execution_complete_gpu_events[i] = xg_gpu_queue_event_create ( &event_params );
    }

    swapchain->texture_count = texture_count;
    swapchain->format = params->format;
    swapchain->color_space = params->color_space;
    swapchain->present_mode = params->present_mode;
    swapchain->device = params->device;
    swapchain->window = params->window;
    swapchain->display = xg_null_handle_m;
    swapchain->acquired = false;
    std_str_copy_static_m ( swapchain->debug_name, params->debug_name );

    return swapchain_handle;
}

xg_swapchain_h xg_vk_swapchain_create_display ( const xg_swapchain_display_params_t* params ) {
    std_unused_m ( params );
    std_log_error_m ( "TODO" );
    return xg_null_handle_m;
}

xg_swapchain_h xg_vk_swapchain_create_virtual ( const xg_swapchain_virtual_params_t* params ) {
    std_unused_m ( params );
    std_log_error_m ( "TODO" );
    return xg_null_handle_m;
}

bool xg_vk_swapchain_resize ( xg_swapchain_h swapchain_handle, size_t width, size_t height ) {
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );
    std_assert_m ( swapchain );

    const xg_vk_device_t* device = xg_vk_device_get ( swapchain->device );
    std_assert_m ( device );

    std_log_info_m ( "Resizing swapchain " std_fmt_u64_m " from " std_fmt_size_m "x" std_fmt_size_m " to " std_fmt_size_m "x" std_fmt_size_m, 
        swapchain_handle, swapchain->width, swapchain->height, width, height );

    // Wait for idle device (TODO is this ok? need a way to check if a swapchain has work queued on?)
    //vkDeviceWaitIdle ( device->vk_handle );
    xg_workload_wait_all_workload_complete ();

    // Destroy current swapchain
    vkDestroySwapchainKHR ( device->vk_handle, swapchain->vk_handle, NULL );

    //for ( size_t i = 0; i < swapchain->texture_count; ++i ) {
    //xg_vk_texture_unregister_swapchain_texture ( swapchain->textures[i] );
    //}

    // Create new swapchain
    VkSurfaceTransformFlagBitsKHR vk_transform_flags = 0;

    if ( swapchain->window != wm_null_handle_m ) {
        VkSurfaceCapabilitiesKHR surface_capabilities;
        xg_vk_safecall_m ( vkGetPhysicalDeviceSurfaceCapabilitiesKHR ( device->vk_physical_handle, swapchain->surface, &surface_capabilities ), xg_null_handle_m );

        if ( width != surface_capabilities.currentExtent.width || height != surface_capabilities.currentExtent.height ) {
            std_log_warn_m ( "Resize call for swapchain " std_fmt_u64_m " bound to window " std_fmt_u64_m " will ignore provided size of " std_fmt_size_m "x" std_fmt_size_m ", will resize instead to "std_fmt_u32_m "x" std_fmt_u32_m", matching window size.",
                swapchain_handle, swapchain->window, width, height, surface_capabilities.currentExtent.width, surface_capabilities.currentExtent.height );
            width = surface_capabilities.currentExtent.width;
            height = surface_capabilities.currentExtent.height;
        }

        vk_transform_flags = surface_capabilities.currentTransform;
    }

    VkFormat vk_format = xg_format_to_vk ( swapchain->format );
    VkColorSpaceKHR vk_color_space = xg_color_space_to_vk ( swapchain->color_space );
    VkPresentModeKHR vk_present_mode = xg_present_mode_to_vk ( swapchain->present_mode );
    uint32_t texture_count = ( uint32_t ) swapchain->texture_count;

    xg_texture_usage_bit_e allowed_usage = xg_texture_usage_bit_render_target_m | xg_texture_usage_bit_copy_dest_m; // TODO probably need more allowed usages
    // see _create
    allowed_usage |= xg_texture_usage_bit_copy_source_m;
    allowed_usage |= xg_texture_usage_bit_sampled_m;
    allowed_usage |= xg_texture_usage_bit_storage_m;

    VkSwapchainCreateInfoKHR swapchain_create_info;
    swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_create_info.pNext = NULL;
    swapchain_create_info.flags = 0;
    swapchain_create_info.surface = swapchain->surface;
    swapchain_create_info.minImageCount = texture_count;
    swapchain_create_info.imageFormat = vk_format; //VK_FORMAT_B8G8R8A8_UNORM;   // TODO why? TODO test this with supported formats
    swapchain_create_info.imageColorSpace = vk_color_space; //VK_COLORSPACE_SRGB_NONLINEAR_KHR;   // TODO test this with supported spaces
    swapchain_create_info.imageExtent.width = ( uint32_t ) width;
    swapchain_create_info.imageExtent.height = ( uint32_t ) height;
    swapchain_create_info.imageArrayLayers = 1;
    swapchain_create_info.imageUsage = xg_image_usage_to_vk ( allowed_usage );
    swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_create_info.queueFamilyIndexCount = 0;
    swapchain_create_info.pQueueFamilyIndices = NULL;
    swapchain_create_info.preTransform = vk_transform_flags; //VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_create_info.presentMode = vk_present_mode; //VK_PRESENT_MODE_MAILBOX_KHR;
    swapchain_create_info.clipped = VK_TRUE;
    swapchain_create_info.oldSwapchain = VK_NULL_HANDLE; // TODO
    xg_vk_safecall_m ( vkCreateSwapchainKHR ( device->vk_handle, &swapchain_create_info, NULL, &swapchain->vk_handle ), xg_null_handle_m );

    if ( swapchain->debug_name[0] != '\0' ) {
        VkDebugUtilsObjectNameInfoEXT debug_name;
        debug_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name.pNext = NULL;
        debug_name.objectType = VK_OBJECT_TYPE_SWAPCHAIN_KHR;
        debug_name.objectHandle = ( uint64_t ) swapchain->vk_handle;
        debug_name.pObjectName = swapchain->debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );
    }

    VkImage swapchain_textures[xg_swapchain_max_textures_m];
    uint32_t acquired_texture_count = xg_swapchain_max_textures_m;
    vkGetSwapchainImagesKHR ( device->vk_handle, swapchain->vk_handle, &acquired_texture_count, swapchain_textures );
    texture_count = acquired_texture_count;

    xg_texture_params_t swapchain_texture_params = xg_texture_params_m (
        .device = swapchain->device,
        .width = width,
        .height = height,
        .depth = 1,
        .mip_levels = 1,
        .array_layers = 1,
        .dimension = xg_texture_dimension_2d_m,
        .format = swapchain->format,
        .allowed_usage = allowed_usage,
        .initial_layout = xg_texture_layout_undefined_m, // ?
        .samples_per_pixel = xg_sample_count_1_m,
    );
    std_str_copy_static_m ( swapchain_texture_params.debug_name, swapchain->debug_name );

    swapchain->width = width;
    swapchain->height = height;

    // TODO update info.texture_count

    for ( size_t i = 0; i < texture_count; ++i ) {
        xg_vk_texture_update_swapchain_texture ( swapchain->textures[i], &swapchain_texture_params, swapchain_textures[i] );
        //swapchain->textures[i] = xg_vk_texture_register_swapchain_texture ( &swapchain_texture_params, swapchain_textures[i] );
    }

    return true;
}

bool xg_vk_swapchain_get_info ( xg_swapchain_info_t* info, xg_swapchain_h swapchain_handle ) {
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );
    std_assert_m ( swapchain );

    info->texture_count = swapchain->texture_count;
    info->format = swapchain->format;
    info->color_space = swapchain->color_space;
    info->width = swapchain->width;
    info->height = swapchain->height;
    info->present_mode = swapchain->present_mode;
    info->device = swapchain->device;
    info->window = swapchain->window;
    info->display = swapchain->display;
    info->acquired = swapchain->acquired;
    info->acquired_texture_idx = swapchain->acquired_texture_idx;
    std_str_copy_static_m ( info->debug_name, swapchain->debug_name );

    for ( uint64_t i = 0; i < swapchain->texture_count; ++i ) {
        info->textures[i] = swapchain->textures[i];
    }

    return true;
}

xg_texture_h xg_vk_swapchain_get_texture ( xg_swapchain_h swapchain_handle ) {
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );
    std_assert_m ( swapchain );

    return swapchain->textures[swapchain->acquired_texture_idx];
}

static bool xg_vk_swapchain_resize_check ( xg_swapchain_h swapchain_handle ) {
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );
    std_assert_m ( swapchain );

    wm_i* wm = std_module_get_m ( wm_module_name_m );
    std_assert_m ( wm );

    if ( !wm->is_window_alive ( swapchain->window ) ) {
        return false;
    }
    
    wm_window_info_t window_info;
    wm->get_window_info ( swapchain->window, &window_info );

    if ( swapchain->width != window_info.width || swapchain->height != window_info.height ) {
        std_log_info_m ( "Swapchain window resize detected, proceeding with the swapchain resize..." );
        xg_vk_swapchain_resize ( swapchain_handle, window_info.width, window_info.height );
        return true;
    } else {
        return false;
    }
}

void xg_vk_swapchain_acquire_next_texture ( xg_swapchain_acquire_result_t* result, xg_swapchain_h swapchain_handle, xg_workload_h workload_handle ) {
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );

    bool resize = false;
    if ( xg_vk_swapchain_resize_check ( swapchain_handle ) ) {
        resize = true;
    }

    const xg_vk_device_t* device = xg_vk_device_get ( swapchain->device );

    uint32_t texture_idx;

#if xg_debug_enable_measure_acquire_time_m
    std_tick_t acquire_start_tick = std_tick_now();
#endif

#if xg_debug_enable_disable_semaphore_frame_sync_m
    VkFence fence;
    VkFenceCreateInfo fence_create_info;
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.pNext = NULL;
    fence_create_info.flags = 0;
    VkResult vk_result = vkCreateFence ( device->vk_handle, &fence_create_info, NULL, &fence );
    std_assert_m ( vk_result == VK_SUCCESS );
    vk_result = vkAcquireNextImageKHR ( device->vk_handle, swapchain->vk_handle, UINT64_MAX, VK_NULL_HANDLE, fence, &texture_idx );
    std_assert_m ( vk_result == VK_SUCCESS );
    vk_result = vkWaitForFences ( device->vk_handle, 1, &fence, VK_TRUE, UINT64_MAX );
    std_assert_m ( vk_result == VK_SUCCESS );
    vkDestroyFence ( device->vk_handle, fence, NULL );
    swapchain->acquired_texture_idx = texture_idx;
    xg_workload_set_execution_complete_gpu_event ( workload_handle, swapchain->execution_complete_gpu_events[texture_idx] );
#else
    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    // TODO this isn't very nice? create the event here and pass it to the workload?
    xg_workload_init_swapchain_texture_acquired_gpu_event ( workload_handle );
    xg_gpu_queue_event_log_signal ( workload->swapchain_texture_acquired_event );
    const xg_vk_gpu_queue_event_t* vk_acquire_event = xg_vk_gpu_queue_event_get ( workload->swapchain_texture_acquired_event );
    VkResult vk_result = vkAcquireNextImageKHR ( device->vk_handle, swapchain->vk_handle, UINT64_MAX, vk_acquire_event->vk_semaphore, VK_NULL_HANDLE, &texture_idx );
    std_assert_msg_m ( vk_result == VK_SUCCESS, "Acquire fail: "  std_fmt_int_m, vk_result );
    swapchain->acquired_texture_idx = texture_idx;
    xg_workload_set_execution_complete_gpu_event ( workload_handle, swapchain->execution_complete_gpu_events[texture_idx] );

#endif

#if xg_debug_enable_measure_acquire_time_m
    float acquire_time_ms = std_tick_to_milli_f32 ( std_tick_now() - acquire_start_tick );

    if ( acquire_time_ms > 0.2f ) {
        std_log_warn_m ( "Busy waited for " std_fmt_f32_dec_m ( 2 ) "ms on acquire call for gpu workload " std_fmt_u64_m, acquire_time_ms, workload->id );
    }

#endif

    swapchain->acquired = true;

    result->idx = texture_idx;
    result->texture = swapchain->textures[texture_idx];
    result->resize = resize;
}

void xg_vk_swapchain_present ( xg_swapchain_h swapchain_handle, xg_workload_h workload_handle ) {
    std_mutex_lock ( &xg_vk_swapchain_state->swapchains_mutex );
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    std_mutex_unlock ( &xg_vk_swapchain_state->swapchains_mutex );
    std_assert_m ( swapchain );

    const xg_vk_device_t* device = xg_vk_device_get ( swapchain->device );
    std_assert_m ( device );

    const xg_vk_workload_t* workload = xg_vk_workload_get ( workload_handle );
    xg_gpu_queue_event_log_wait ( workload->execution_complete_gpu_event );
    const xg_vk_gpu_queue_event_t* vk_complete_event = xg_vk_gpu_queue_event_get ( workload->execution_complete_gpu_event );

    uint32_t texture_idx = ( uint32_t ) swapchain->acquired_texture_idx;

    uint64_t handles[3];

    for ( uint32_t i = 0; i < 3; ++i ) {
        xg_texture_info_t info;
        xg_texture_get_info ( &info, swapchain->textures[i] );
        handles[i] = info.os_handle;
    }

#if xg_debug_enable_measure_present_time_m
    std_tick_t present_start_tick = std_tick_now();
#endif

    // NOTE: vkQueuePresentKHR can and will (tested on win32) block when trying to call present on a swapchain image that's already in use
    // see https://github.com/KhronosGroup/Vulkan-Docs/issues/1158
    VkResult present_result;
    VkPresentInfoKHR present_info;
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = NULL;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &vk_complete_event->vk_semaphore;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain->vk_handle;
    present_info.pImageIndices = &texture_idx;
    present_info.pResults = &present_result;
#if xg_debug_enable_disable_semaphore_frame_sync_m
    present_info.waitSemaphoreCount = 0;
#endif

    swapchain->acquired = false;
    VkResult result = vkQueuePresentKHR ( device->queues[xg_cmd_queue_graphics_m].vk_handle, &present_info );

    if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
        std_log_warn_m ( "Swapchain " std_fmt_u64_m " out of date, present call failed", swapchain_handle );
    } else {
        std_assert_msg_m ( result == VK_SUCCESS, "Present fail: " std_fmt_int_m, result );

#if xg_debug_enable_flush_gpu_submissions_m || xg_debug_enable_disable_semaphore_frame_sync_m
        result = vkQueueWaitIdle ( device->queues[xg_cmd_queue_graphics_m].vk_handle );
        std_assert_m ( result == VK_SUCCESS );
#endif

        if ( present_result == VK_ERROR_OUT_OF_DATE_KHR ) {
            std_log_warn_m ( "Swapchain " std_fmt_u64_m " out of date, present call failed", swapchain_handle );
        } else {
            xg_vk_assert_m ( present_result );
        }
    }

    if ( workload->stop_debug_capture_on_present ) {
        xg_debug_capture_stop();
    }

#if xg_debug_enable_measure_present_time_m
    float present_time_ms = std_tick_to_milli_f32 ( std_tick_now() - present_start_tick );

    if ( present_time_ms > 0.2f ) {
        std_log_warn_m ( "Busy waited for " std_fmt_f32_dec_m ( 2 ) "ms on present call for gpu workload " std_fmt_u64_m, present_time_ms, workload->id );
    }

#endif
}

void xg_vk_swapchain_destroy ( xg_swapchain_h swapchain_handle ) {
    xg_vk_swapchain_t* swapchain = &xg_vk_swapchain_state->swapchains_array[swapchain_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( swapchain->device );

    for ( size_t i = 0; i < swapchain->texture_count; ++i ) {
        xg_gpu_queue_event_destroy ( swapchain->execution_complete_gpu_events[i] );
    }

    vkDestroySwapchainKHR ( device->vk_handle, swapchain->vk_handle, NULL );
    vkDestroySurfaceKHR ( xg_vk_instance(), swapchain->surface, NULL );
    std_bitset_clear ( xg_vk_swapchain_state->swapchain_bitset, swapchain_handle );
}
