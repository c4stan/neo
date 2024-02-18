#include "xg_vk.h"

#include <std_string.h>
#include <std_atomic.h>

#if std_enabled_m(xg_debug_simple_frame_test_m)

#include "xg_vk_device.h"
#include "xg_vk_swapchain.h"

void xg_debug_simple_frame ( xg_device_h device_handle, wm_window_h window_handle ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    static bool initialized = false;
    static VkCommandPool cmd_pool;
    static VkCommandBuffer cmd_buffer;
    static VkSurfaceKHR surface;
    static VkSwapchainKHR swapchain;
    static unsigned int render_targets_count;
    static unsigned int last_framebuffer_idx;
    static VkImage* render_targets_images;

    if ( !initialized ) {
        initialized = true;

        // Create command pool
        VkCommandPoolCreateInfo command_pool_create_info;

        command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_create_info.pNext = NULL;
        command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // TODO is this needed?
        command_pool_create_info.queueFamilyIndex = device->graphics_queue.vk_family_idx;
        VkResult result = vkCreateCommandPool ( device->vk_handle, &command_pool_create_info, NULL, &cmd_pool );
        std_assert_m ( result == VK_SUCCESS );

        // Create command buffer
        VkCommandBufferAllocateInfo cmd_buffer_allocate_info;
        cmd_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmd_buffer_allocate_info.pNext = NULL;
        cmd_buffer_allocate_info.commandPool = cmd_pool;
        cmd_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmd_buffer_allocate_info.commandBufferCount = 1;
        result = vkAllocateCommandBuffers ( device->vk_handle, &cmd_buffer_allocate_info, &cmd_buffer );
        std_assert_m ( result == VK_SUCCESS );

        // Create surface
        wm_i* wm = std_module_get_m ( wm_module_name_m );
        std_assert_m ( wm );
        wm_window_info_t window_info;
        wm->get_window_info ( window_handle, &window_info );
#if defined(std_platform_win32_m)
        VkWin32SurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = NULL;
        surface_create_info.flags = 0;
        surface_create_info.hinstance = ( HINSTANCE ) window_info.os_handle.root;
        surface_create_info.hwnd = window_info.os_handle.window;
        VENUS_SAFECALL ( vkCreateWin32SurfaceKHR ( g_venus_instance, &surface_create_info, NULL, &g_venus_window.surface ) );
#elif defined(std_platform_linux_m)
        VkXlibSurfaceCreateInfoKHR surface_create_info;
        surface_create_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        surface_create_info.pNext = NULL;
        surface_create_info.flags = 0;
        surface_create_info.dpy = ( Display* ) window_info.os_handle.root;
        surface_create_info.window = ( Window ) window_info.os_handle.window;
        result = vkCreateXlibSurfaceKHR ( xg_vk_instance(), &surface_create_info, NULL, &surface );
        std_assert_m ( result == VK_SUCCESS );
#endif

        // Get surface capabilities
        VkSurfaceCapabilitiesKHR surface_capabilities;
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR ( device->vk_physical_handle, surface, &surface_capabilities );
        std_assert_m ( result == VK_SUCCESS );
        uint32_t width = surface_capabilities.currentExtent.width;
        uint32_t height = surface_capabilities.currentExtent.height;

        // Get surface formats
        unsigned int surface_format_count;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR ( device->vk_physical_handle, surface, &surface_format_count, NULL );
        std_assert_m ( result == VK_SUCCESS );
        VkSurfaceFormatKHR* surface_formats = malloc ( sizeof ( VkSurfaceFormatKHR ) * surface_format_count );
        result = vkGetPhysicalDeviceSurfaceFormatsKHR ( device->vk_physical_handle, surface, &surface_format_count, surface_formats );
        std_assert_m ( result == VK_SUCCESS );

        // Get surface present modes
        unsigned int surface_present_mode_count;
        result = vkGetPhysicalDeviceSurfacePresentModesKHR ( device->vk_physical_handle, surface, &surface_present_mode_count, NULL );
        std_assert_m ( result == VK_SUCCESS );
        VkPresentModeKHR* surface_present_modes = malloc ( sizeof ( VkPresentModeKHR ) * surface_present_mode_count );
        result = vkGetPhysicalDeviceSurfacePresentModesKHR ( device->vk_physical_handle, surface, &surface_present_mode_count, surface_present_modes );
        std_assert_m ( result == VK_SUCCESS );

        bool device_can_present = true;//false;
        unsigned int present_queue_family = 0;

#if 0

        for ( present_queue_family = 0; present_queue_family < device->queues_families_count; ++present_queue_family ) {
            if ( vkGetPhysicalDeviceXlibPresentationSupportKHR ( device->vk_handle, present_queue_family,
                    ( Display* ) window_info.os_handle.root ) == VK_TRUE ) {
                device_can_present = true;
                break;
            }
        }

#endif

        std_assert_m ( device_can_present );

        VkBool32 is_surface_supported = VK_FALSE;
        result = vkGetPhysicalDeviceSurfaceSupportKHR ( device->vk_physical_handle, present_queue_family, surface, &is_surface_supported );

        if ( result != VK_SUCCESS || is_surface_supported == VK_FALSE ) {
            std_assert_m ( false );
        }

        // Create swapchain
        VkSwapchainCreateInfoKHR swapchain_create_info;
        swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_create_info.pNext = NULL;
        swapchain_create_info.flags = 0;
        swapchain_create_info.surface = surface;
        swapchain_create_info.minImageCount = 3;
        swapchain_create_info.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
        swapchain_create_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        swapchain_create_info.imageExtent.width = width;
        swapchain_create_info.imageExtent.height = height;
        swapchain_create_info.imageArrayLayers = 1;
        swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.queueFamilyIndexCount = 0;
        swapchain_create_info.pQueueFamilyIndices = NULL;
        swapchain_create_info.preTransform = surface_capabilities.currentTransform;//VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapchain_create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
        swapchain_create_info.clipped = VK_TRUE;
        swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

        result = vkCreateSwapchainKHR ( device->vk_handle, &swapchain_create_info, NULL, &swapchain );
        std_assert_m ( result == VK_SUCCESS );

        // Get swapchain images
        result = vkGetSwapchainImagesKHR ( device->vk_handle, swapchain, &render_targets_count, NULL );
        render_targets_images = malloc ( sizeof ( VkImage ) * render_targets_count );
        vkGetSwapchainImagesKHR ( device->vk_handle, swapchain, &render_targets_count, render_targets_images );
    }

    VkResult result;

    unsigned int framebuffer_idx;
    VkFence fence;
    VkFenceCreateInfo fence_create_info;
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.pNext = NULL;
    fence_create_info.flags = 0;
    result = vkCreateFence ( device->vk_handle, &fence_create_info, NULL, &fence );
    std_assert_m ( result == VK_SUCCESS );
    result = vkAcquireNextImageKHR ( device->vk_handle, swapchain, UINT64_MAX, VK_NULL_HANDLE, fence, &framebuffer_idx );
    std_assert_m ( result == VK_SUCCESS );
    result = vkWaitForFences ( device->vk_handle, 1, &fence, VK_TRUE, UINT64_MAX );
    std_assert_m ( result == VK_SUCCESS );
    vkDestroyFence ( device->vk_handle, fence, NULL );

    // Begin recording
    VkCommandBufferBeginInfo command_begin_info;
    command_begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    command_begin_info.pNext = NULL;
    command_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    command_begin_info.pInheritanceInfo = NULL;
    result = vkBeginCommandBuffer ( cmd_buffer, &command_begin_info );
    std_assert_m ( result == VK_SUCCESS );

    // End command buffer
    result = vkEndCommandBuffer ( cmd_buffer );
    std_assert_m ( result == VK_SUCCESS );

    // Submit command buffer
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;//&g_venus_state.command_buffer_semaphore;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;//&g_venus_state.command_buffer_semaphore;
    result = vkQueueSubmit ( device->graphics_queue.vk_handle, 1, &submit_info, VK_NULL_HANDLE );
    std_assert_m ( result == VK_SUCCESS );

    vkDeviceWaitIdle ( device->vk_handle );

    last_framebuffer_idx = framebuffer_idx;

    vkResetCommandBuffer ( cmd_buffer, 0 );

    // Present
    VkResult present_result;
    VkPresentInfoKHR present_info;
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext = NULL;
    present_info.waitSemaphoreCount = 0;
    present_info.pWaitSemaphores = NULL;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &last_framebuffer_idx;
    present_info.pResults = &present_result;
    result = vkQueuePresentKHR ( device->graphics_queue.vk_handle, &present_info );
    std_assert_m ( result == VK_SUCCESS );
    std_assert_m ( present_result == VK_SUCCESS );
}

#endif // xg_debug_enable_simple_frame_test_m
