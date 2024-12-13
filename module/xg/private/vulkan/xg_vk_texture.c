#include "xg_vk_texture.h"

#include <xg_enum.h>

#include "xg_vk_device.h"
#include "xg_vk_enum.h"
#include "xg_vk_instance.h"
#include "xg_vk_allocator.h"

#include <std_list.h>

static xg_vk_texture_state_t* xg_vk_texture_state;

void xg_vk_texture_load ( xg_vk_texture_state_t* state ) {
    xg_vk_texture_state = state;

    xg_vk_texture_state->textures_array = std_virtual_heap_alloc_array_m ( xg_vk_texture_t, xg_vk_max_textures_m );
    xg_vk_texture_state->textures_freelist = std_freelist_m ( xg_vk_texture_state->textures_array, xg_vk_max_textures_m );
    xg_vk_texture_state->textures_bitset = std_virtual_heap_alloc_array_m ( uint64_t, std_div_ceil_m ( xg_vk_max_textures_m, 64 ) );
    std_mem_zero ( xg_vk_texture_state->textures_bitset, 8 * std_div_ceil_m ( xg_vk_max_textures_m, 64 ) );
    std_mutex_init ( &xg_vk_texture_state->textures_mutex );
}

void xg_vk_texture_reload ( xg_vk_texture_state_t* state ) {
    xg_vk_texture_state = state;
}

void xg_vk_texture_unload ( void ) {
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_texture_state->textures_bitset, idx, std_div_ceil_m ( xg_vk_max_textures_m, 64) ) ) {
        xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[idx];
        
        std_log_info_m ( "Destroying texture " std_fmt_u64_m ": " std_fmt_str_m, idx, texture->params.debug_name );
        xg_texture_destroy ( idx );

        ++idx;
    }

    std_virtual_heap_free ( xg_vk_texture_state->textures_array );
    std_virtual_heap_free ( xg_vk_texture_state->textures_bitset );
    std_mutex_deinit ( &xg_vk_texture_state->textures_mutex );
}

xg_texture_h xg_texture_create ( const xg_texture_params_t* params ) {
    xg_texture_h texture_handle = xg_texture_reserve ( params );
    std_verify_m ( xg_texture_alloc ( texture_handle ) );
    return texture_handle;
}

void xg_texture_create_view ( xg_vk_texture_view_t* view, xg_texture_h texture_handle, xg_texture_view_t view_desc ) {
    xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( texture->params.device );

    VkImageView vk_view;
    xg_vk_texture_view_params_t params;
    {
        xg_format_e format;

        if ( view_desc.format != xg_texture_view_default_format_m ) {
            format = view_desc.format;
        } else {
            format = texture->params.format;
        }

        uint32_t mip_count;
        uint32_t array_count;

        if ( view_desc.mip_count == xg_texture_all_mips_m ) {
            mip_count = VK_REMAINING_MIP_LEVELS;
        } else {
            mip_count = view_desc.mip_count;
        }

        if ( view_desc.array_count == xg_texture_whole_array_m ) {
            array_count = VK_REMAINING_ARRAY_LAYERS;
        } else {
            array_count = view_desc.array_count;
        }

        VkImageAspectFlags aspect = 0;

        if ( view_desc.aspect == xg_texture_aspect_default_m ) {
            if ( texture->flags & xg_texture_flag_bit_render_target_texture_m ) {
                aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            } else if ( texture->flags & xg_texture_flag_bit_depth_texture_m ) {
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            } else if ( texture->flags & xg_texture_flag_bit_stencil_texture_m ) {
                aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
            } else {
                aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            }
        } else {
            aspect = xg_texture_aspect_to_vk ( view_desc.aspect );
        }

        VkImageViewCreateInfo vk_view_info;
        vk_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vk_view_info.pNext = NULL;
        vk_view_info.flags = 0;
        vk_view_info.image = texture->vk_handle;
        vk_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vk_view_info.format = xg_format_to_vk ( format );
        vk_view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        vk_view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        vk_view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        vk_view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vk_view_info.subresourceRange.aspectMask = aspect;
        vk_view_info.subresourceRange.baseMipLevel = view_desc.mip_base;
        vk_view_info.subresourceRange.levelCount = mip_count;
        vk_view_info.subresourceRange.baseArrayLayer = view_desc.array_base;
        vk_view_info.subresourceRange.layerCount = array_count;
        xg_vk_assert_m ( vkCreateImageView ( device->vk_handle, &vk_view_info, NULL, &vk_view ) );

        if ( texture->params.debug_name[0] ) {
            char buffer[xg_debug_name_size_m + 16] = {0};
            std_stack_t stack = std_static_stack_m ( buffer );
            std_stack_string_append ( &stack, texture->params.debug_name );
            std_stack_string_append ( &stack, "view" );
            
            VkDebugUtilsObjectNameInfoEXT debug_name;
            debug_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            debug_name.pNext = NULL;
            debug_name.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
            debug_name.objectHandle = ( uint64_t ) vk_view;
            debug_name.pObjectName = buffer;
            xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );
        }

        // TODO refactor this, have the create function take params struct only and do the translation outside
        params.format = format;
        params.mip_count = mip_count;
        params.array_count = array_count;
        params.aspect = aspect;
        params.desc = view_desc;
    }

    view->vk_handle = vk_view;
    view->params = params;
}

xg_texture_h xg_texture_reserve ( const xg_texture_params_t* params ) {
    std_mutex_lock ( &xg_vk_texture_state->textures_mutex );
    xg_vk_texture_t* texture = std_list_pop_m ( &xg_vk_texture_state->textures_freelist );
    xg_texture_h texture_handle = ( xg_texture_h ) ( texture - xg_vk_texture_state->textures_array );
    std_bitset_set ( xg_vk_texture_state->textures_bitset, texture_handle );
    std_mutex_unlock ( &xg_vk_texture_state->textures_mutex );
    std_assert_m ( texture );

    xg_texture_flag_bit_e flags = 0;

    if ( params->allowed_usage & xg_texture_usage_bit_depth_stencil_m ) {
        if ( xg_format_has_depth ( params->format ) ) {
            flags |= xg_texture_flag_bit_depth_texture_m;
        }

        if ( xg_format_has_stencil ( params->format ) ) {
            flags |= xg_texture_flag_bit_stencil_texture_m;
        }
    } else if ( params->allowed_usage & xg_texture_usage_bit_render_target_m ) {
        flags = xg_texture_flag_bit_render_target_texture_m;
    }

    texture->vk_handle = VK_NULL_HANDLE;
    texture->allocation.handle = xg_null_memory_handle_m;
    texture->params = *params;
    texture->flags = flags;
    texture->default_view.vk_handle = VK_NULL_HANDLE;

    texture->state = xg_vk_texture_state_reserved_m;

    xg_texture_aspect_e default_aspect;

    // TODO make texture_aspect a bitflag, allow both stencil and depth?
    if ( texture->flags & xg_texture_flag_bit_render_target_texture_m ) {
        default_aspect = xg_texture_aspect_color_m;
    } else if ( texture->flags & xg_texture_flag_bit_depth_texture_m ) {
        default_aspect = xg_texture_aspect_depth_m;
    } else if ( texture->flags & xg_texture_flag_bit_stencil_texture_m ) {
        default_aspect = xg_texture_aspect_stencil_m;
    } else {
        default_aspect = xg_texture_aspect_color_m;
    }

    texture->default_aspect = default_aspect;

    if ( flags & xg_texture_flag_bit_render_target_texture_m ) {
        texture->params.allowed_usage |= xg_texture_usage_bit_sampled_m;
        texture->params.allowed_usage |= xg_texture_usage_bit_storage_m;
        texture->params.allowed_usage |= xg_texture_usage_bit_copy_source_m;
        texture->params.allowed_usage |= xg_texture_usage_bit_copy_dest_m;
    } else if ( flags & ( xg_texture_flag_bit_depth_texture_m | xg_texture_flag_bit_stencil_texture_m ) ) {
        texture->params.allowed_usage |= xg_texture_usage_bit_sampled_m;
        texture->params.allowed_usage |= xg_texture_usage_bit_copy_source_m;
        texture->params.allowed_usage |= xg_texture_usage_bit_copy_dest_m;
    }

    if ( params->view_access == xg_texture_view_access_default_only_m ) {
        std_mem_zero_m ( &texture->external_views );
    } else if ( params->view_access == xg_texture_view_access_separate_mips_m ) {
        texture->external_views.mips.array = std_virtual_heap_alloc_array_m ( xg_vk_texture_view_t, params->mip_levels );
        std_mem_zero ( texture->external_views.mips.array, sizeof ( xg_vk_texture_view_t ) * params->mip_levels );
    } else if ( params->view_access == xg_texture_view_access_dynamic_m ) {
        std_not_implemented_m();
    }

    return texture_handle;
}

xg_memory_requirement_t xg_texture_memory_requirement ( const xg_texture_params_t* params ) {
    const xg_vk_device_t* device = xg_vk_device_get ( params->device );
    
    // Create Vulkan image
    VkImage vk_image;
    std_assert_m ( params->width < UINT32_MAX );
    std_assert_m ( params->height < UINT32_MAX );
    std_assert_m ( params->depth < UINT32_MAX );
    std_assert_m ( params->mip_levels < UINT32_MAX );
    std_assert_m ( params->array_layers < UINT32_MAX );
    VkImageCreateInfo vk_image_info;
    vk_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    vk_image_info.pNext = NULL;
    vk_image_info.flags = 0;
    vk_image_info.imageType = xg_image_type_to_vk ( params->dimension );
    vk_image_info.format = xg_format_to_vk ( params->format );
    vk_image_info.extent.width = ( uint32_t ) params->width;
    vk_image_info.extent.height = ( uint32_t ) params->height;
    vk_image_info.extent.depth = ( uint32_t ) params->depth;
    vk_image_info.mipLevels = ( uint32_t ) params->mip_levels;
    vk_image_info.arrayLayers = ( uint32_t ) params->array_layers;
    vk_image_info.samples = xg_sample_count_to_vk ( params->samples_per_pixel );
    vk_image_info.tiling = xg_texture_tiling_to_vk ( params->tiling );
    vk_image_info.usage = xg_image_usage_to_vk ( params->allowed_usage );
    vk_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // TODO
    vk_image_info.queueFamilyIndexCount = 0;
    vk_image_info.pQueueFamilyIndices = NULL;
    vk_image_info.initialLayout = xg_image_layout_to_vk ( params->initial_layout );

    // Query for memory requiremens
    vkCreateImage ( device->vk_handle, &vk_image_info, NULL, &vk_image );

    VkMemoryDedicatedRequirements dedicated_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        .pNext = NULL,
    };
    VkMemoryRequirements2 memory_requirements_2 = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicated_requirements,
    };
    const VkImageMemoryRequirementsInfo2 image_requirements_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .pNext = NULL,
        .image = vk_image
    };
    vkGetImageMemoryRequirements2 ( device->vk_handle, &image_requirements_info, &memory_requirements_2 );
    const VkMemoryRequirements* vk_memory_requirements = &memory_requirements_2.memoryRequirements;

    // Destroy Vulkan image
    // TODO is there no way to do this without creating and destroying a temp VkImage?
    vkDestroyImage ( device->vk_handle, vk_image, NULL );

    // Return
    xg_memory_requirement_t req = {
        .size = vk_memory_requirements->size,
        .align = vk_memory_requirements->alignment,
        .flags = xg_memory_requirement_bit_none_m
    };

    if ( dedicated_requirements.requiresDedicatedAllocation ) {
        req.flags |= xg_memory_requirement_bit_dedicated_m;
    }

    return req;
}

bool xg_texture_alloc ( xg_texture_h texture_handle ) {
    xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];

    std_assert_m ( texture->state == xg_vk_texture_state_reserved_m );

    const xg_texture_params_t* params = &texture->params;

    const xg_vk_device_t* device = xg_vk_device_get ( params->device );

    // Create Vulkan image
    VkImage vk_image;
    std_assert_m ( params->width < UINT32_MAX );
    std_assert_m ( params->height < UINT32_MAX );
    std_assert_m ( params->depth < UINT32_MAX );
    std_assert_m ( params->mip_levels < UINT32_MAX );
    std_assert_m ( params->array_layers < UINT32_MAX );
    VkImageCreateInfo vk_image_info;
    vk_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    vk_image_info.pNext = NULL;
    vk_image_info.flags = 0;
    vk_image_info.imageType = xg_image_type_to_vk ( params->dimension );
    vk_image_info.format = xg_format_to_vk ( params->format );
    vk_image_info.extent.width = ( uint32_t ) params->width;
    vk_image_info.extent.height = ( uint32_t ) params->height;
    vk_image_info.extent.depth = ( uint32_t ) params->depth;
    vk_image_info.mipLevels = ( uint32_t ) params->mip_levels;
    vk_image_info.arrayLayers = ( uint32_t ) params->array_layers;
    vk_image_info.samples = xg_sample_count_to_vk ( params->samples_per_pixel );
    vk_image_info.tiling = xg_texture_tiling_to_vk ( params->tiling );
    vk_image_info.usage = xg_image_usage_to_vk ( params->allowed_usage );
    vk_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;  // TODO
    vk_image_info.queueFamilyIndexCount = 0;
    vk_image_info.pQueueFamilyIndices = NULL;
    vk_image_info.initialLayout = xg_image_layout_to_vk ( params->initial_layout );

    // Query for memory requiremens, allocate and bind
    vkCreateImage ( device->vk_handle, &vk_image_info, NULL, &vk_image );

    if ( params->debug_name[0] ) {
        VkDebugUtilsObjectNameInfoEXT debug_name;
        debug_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name.pNext = NULL;
        debug_name.objectType = VK_OBJECT_TYPE_IMAGE;
        debug_name.objectHandle = ( uint64_t ) vk_image;
        debug_name.pObjectName = params->debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );
    }

    VkDeviceMemory alloc_base;
    VkDeviceSize alloc_offset;

    if ( params->creation_address.base ) {
        alloc_base = ( VkDeviceMemory ) params->creation_address.base;
        alloc_offset = params->creation_address.offset;

        texture->allocation = xg_null_alloc_m;
    } else {
        VkMemoryDedicatedRequirements dedicated_requirements = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
            .pNext = NULL,
        };
        VkMemoryRequirements2 memory_requirements_2 = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            .pNext = &dedicated_requirements,
        };
        const VkImageMemoryRequirementsInfo2 image_requirements_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
            .pNext = NULL,
            .image = vk_image
        };
        vkGetImageMemoryRequirements2 ( device->vk_handle, &image_requirements_info, &memory_requirements_2 );
        // TODO
        std_assert_m ( !dedicated_requirements.requiresDedicatedAllocation );
        VkMemoryRequirements vk_memory_requirements = memory_requirements_2.memoryRequirements;
        xg_alloc_params_t alloc_params = xg_alloc_params_m ( 
            .device = params->device,
            .size = vk_memory_requirements.size,
            .align = vk_memory_requirements.alignment,
            .type = params->memory_type
        );
        std_str_copy_static_m ( alloc_params.debug_name, params->debug_name );
        xg_alloc_t alloc = xg_alloc ( &alloc_params );
        alloc_base = ( VkDeviceMemory ) alloc.base;
        alloc_offset = alloc.offset;

        texture->allocation = alloc;
    }

    VkResult result = vkBindImageMemory ( device->vk_handle, vk_image, alloc_base, alloc_offset );
    std_verify_m ( result == VK_SUCCESS );

    texture->vk_handle = vk_image;

    bool needs_view = params->allowed_usage & ( xg_texture_usage_bit_sampled_m | xg_texture_usage_bit_storage_m | xg_texture_usage_bit_render_target_m | xg_texture_usage_bit_depth_stencil_m );

    if ( needs_view ) {
        xg_texture_create_view ( &texture->default_view, texture_handle, xg_texture_view_m() );

        if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
            for ( uint32_t i = 0; i < texture->params.mip_levels; ++i ) {
                xg_texture_view_t view = xg_texture_view_m ( .mip_base = i, .mip_count = 1 );
                xg_texture_create_view ( &texture->external_views.mips.array[i], texture_handle, view );
            }
        } else if ( texture->params.view_access == xg_texture_view_access_dynamic_m ) {
            std_not_implemented_m();
        }
    } else {
        texture->default_view.vk_handle = VK_NULL_HANDLE;
        std_mem_zero_m ( &texture->default_view.params );
    }

    texture->state = xg_vk_texture_state_created_m;
    //texture->layout = params->initial_layout;

    return true;
}

bool xg_texture_get_info ( xg_texture_info_t* info, xg_texture_h texture_handle ) {
    const xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];

    if ( texture == NULL ) {
        return false;
    }

    info->allocation = texture->allocation;
    info->device = texture->params.device;
    info->width = texture->params.width;
    info->height = texture->params.height;
    info->depth = texture->params.depth;
    info->mip_levels = texture->params.mip_levels;
    info->array_layers = texture->params.array_layers;
    info->dimension = texture->params.dimension;
    info->format = texture->params.format;
    info->allowed_usage = texture->params.allowed_usage;
    info->samples_per_pixel = texture->params.samples_per_pixel;
    info->flags = texture->flags;
    info->default_aspect = texture->default_aspect;
    info->os_handle = ( uint64_t ) texture->vk_handle;
    std_str_copy_static_m ( info->debug_name, texture->params.debug_name );

    return true;
}

bool xg_texture_destroy ( xg_texture_h texture_handle ) {
    std_mutex_lock ( &xg_vk_texture_state->textures_mutex );
    xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];

    const xg_vk_device_t* device = xg_vk_device_get ( texture->params.device );
    
    if ( texture->state == xg_vk_texture_state_created_m ) {
        if ( ! ( texture->flags & xg_texture_flag_bit_swapchain_texture_m ) ) {
            vkDestroyImage ( device->vk_handle, texture->vk_handle, NULL );
            //texture->params.allocator.free ( texture->params.allocator.impl, texture->allocation.handle );
            xg_free ( texture->allocation.handle );
        }

        vkDestroyImageView ( device->vk_handle, texture->default_view.vk_handle, NULL );

        if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
            for ( uint32_t i = 0; i < texture->params.mip_levels; ++i ) {
                vkDestroyImageView ( device->vk_handle, texture->external_views.mips.array[i].vk_handle, NULL );
            }
        }

        if ( texture->params.view_access == xg_texture_view_access_dynamic_m ) {
            std_not_implemented_m();
        }
    }

    if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
        std_virtual_heap_free ( texture->external_views.mips.array );
    }

    std_bitset_clear ( xg_vk_texture_state->textures_bitset, texture_handle );
    std_list_push ( &xg_vk_texture_state->textures_freelist, texture );
    std_mutex_unlock ( &xg_vk_texture_state->textures_mutex );

    return true;
}

bool xg_texture_resize ( xg_texture_h texture, size_t width, size_t height ) {
    // TODO
    std_unused_m ( texture );
    std_unused_m ( width );
    std_unused_m ( height );

    return false;
}

xg_texture_h xg_vk_texture_register_swapchain_texture ( const xg_texture_params_t* params, VkImage vk_image ) {
    std_mutex_lock ( &xg_vk_texture_state->textures_mutex );
    xg_vk_texture_t* texture = std_list_pop_m ( &xg_vk_texture_state->textures_freelist );
    std_mutex_unlock ( &xg_vk_texture_state->textures_mutex );
    std_assert_m ( texture );
    xg_texture_h texture_handle = ( xg_texture_h ) ( texture - xg_vk_texture_state->textures_array );
    std_bitset_set ( xg_vk_texture_state->textures_bitset, texture_handle );

    texture->vk_handle = vk_image;
    texture->allocation = xg_null_alloc_m;
    texture->params = *params;
    texture->flags = xg_texture_flag_bit_swapchain_texture_m | xg_texture_flag_bit_render_target_texture_m;
    texture->state = xg_vk_texture_state_created_m; // TODO
    xg_texture_create_view ( &texture->default_view, texture_handle, xg_texture_view_m() );

    return texture_handle;
}

void xg_vk_texture_unregister_swapchain_texture ( xg_texture_h texture_handle ) {
    std_mutex_lock ( &xg_vk_texture_state->textures_mutex );

    xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];

    const xg_vk_device_t* device = xg_vk_device_get ( texture->params.device );
    vkDestroyImageView ( device->vk_handle, texture->default_view.vk_handle, NULL );

    std_list_push ( &xg_vk_texture_state->textures_freelist, texture );
    std_bitset_clear ( xg_vk_texture_state->textures_bitset, texture_handle );

    std_mutex_unlock ( &xg_vk_texture_state->textures_mutex );
}

void xg_vk_texture_update_swapchain_texture ( xg_texture_h texture_handle, const xg_texture_params_t* params, VkImage image ) {
    xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];

    const xg_vk_device_t* device = xg_vk_device_get ( texture->params.device );
    vkDestroyImageView ( device->vk_handle, texture->default_view.vk_handle, NULL );

    texture->vk_handle = image;
    texture->params = *params;
    xg_texture_create_view ( &texture->default_view, texture_handle, xg_texture_view_m() );
}

const xg_vk_texture_t* xg_vk_texture_get ( xg_texture_h texture_handle ) {
    return &xg_vk_texture_state->textures_array[texture_handle];
}

const xg_vk_texture_view_t* xg_vk_texture_get_view ( xg_texture_h texture_handle, xg_texture_view_t view ) {
    xg_vk_texture_t* texture = &xg_vk_texture_state->textures_array[texture_handle];

    if ( std_likely_m ( xg_texture_view_is_default_m ( view, texture->params.mip_levels, texture->params.array_layers, texture->params.format, texture->params.allowed_usage ) ) ) {
        return &texture->default_view;
    } else {
        if ( texture->params.view_access == xg_texture_view_access_separate_mips_m ) {
            uint32_t mip_idx = view.mip_base;

            std_assert_m ( view.mip_count == 1 );
            view.mip_base = 0;
            view.mip_count = xg_texture_all_mips_m;
            std_assert_m ( view.u64 == ( xg_texture_view_m() ).u64 );

            return &texture->external_views.mips.array[mip_idx];
        } else {
            std_not_implemented_m();
            return NULL;
        }
    }
}
