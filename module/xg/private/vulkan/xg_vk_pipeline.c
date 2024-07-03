#include "xg_vk.h"

#include "xg_enum.h"

#include "xg_vk_pipeline.h"
#include "xg_vk_instance.h"
#include "xg_vk_device.h"
#include "xg_vk_enum.h"
#include "xg_vk_buffer.h"
#include "xg_vk_texture.h"
#include "xg_vk_sampler.h"

#include <std_sort.h>

static int xg_vk_pipeline_resource_binding_point_cmp ( const void* a, const void* b ) {
    xg_vk_pipeline_resource_binding_point_t* p1 = ( xg_vk_pipeline_resource_binding_point_t* ) a;
    xg_vk_pipeline_resource_binding_point_t* p2 = ( xg_vk_pipeline_resource_binding_point_t* ) b;

#if 0

    if ( p1->stages < p2->stages ) {
        return -1;
    } else if ( p1->stages > p2->stages ) {
        return 1;
    } else if ( p1->type < p2->type ) {
        return -1;
    } else if ( p1->type > p2->type ) {
        return 1;
    } else if ( p1->shader_register < p2->shader_register ) {
        return -1;
    } else if ( p1->shader_register > p2->shader_register ) {
        return 1;
    } else {
        return 0;
    }

#else

    if ( p1->shader_register < p2->shader_register ) {
        return -1;
    } else if ( p1->shader_register > p2->shader_register ) {
        return 1;
    } else {
        // Not supposed to happen
        return 0;
    }

#endif
}

static int xg_vk_pipeline_constant_binding_point_cmp ( const void* a, const void* b ) {
    xg_constant_binding_layout_t* p1 = ( xg_constant_binding_layout_t* ) a;
    xg_constant_binding_layout_t* p2 = ( xg_constant_binding_layout_t* ) b;

    if ( p1->id < p2->id ) {
        return -1;
    } else if ( p1->id > p2->id ) {
        return 1;
    } else {
        return 0;
    }
}

static int xg_vk_pipeline_vertex_stream_cmp ( const void* a, const void* b ) {
    xg_vertex_stream_t* p1 = ( xg_vertex_stream_t* ) a;
    xg_vertex_stream_t* p2 = ( xg_vertex_stream_t* ) b;

    if ( p1->id < p2->id ) {
        return -1;
    } else if ( p1->id > p2->id ) {
        return 1;
    } else {
        return 0;
    }
}

static int xg_vk_pipeline_vertex_attribute_cmp ( const void* a, const void* b ) {
    xg_vertex_attribute_t* p1 = ( xg_vertex_attribute_t* ) a;
    xg_vertex_attribute_t* p2 = ( xg_vertex_attribute_t* ) b;

    if ( p1->id < p2->id ) {
        return -1;
    } else if ( p1->id > p2->id ) {
        return 1;
    } else {
        return 0;
    }
}

static int xg_vk_pipeline_render_target_blend_state_cmp ( const void* a, const void* b ) {
    xg_render_target_blend_state_t* p1 = ( xg_render_target_blend_state_t* ) a;
    xg_render_target_blend_state_t* p2 = ( xg_render_target_blend_state_t* ) b;

    if ( p1->id < p2->id ) {
        return -1;
    } else if ( p1->id > p2->id ) {
        return 1;
    } else {
        return 0;
    }
}

static int xg_vk_pipeline_render_target_layout_cmp ( const void* a, const void* b ) {
    xg_render_target_layout_t* p1 = ( xg_render_target_layout_t* ) a;
    xg_render_target_layout_t* p2 = ( xg_render_target_layout_t* ) b;

    if ( p1->slot < p2->slot ) {
        return -1;
    } else if ( p1->slot > p2->slot ) {
        return 1;
    } else {
        return 0;
    }
}

static xg_vk_pipeline_state_t* xg_vk_pipeline_state;

void xg_vk_pipeline_load ( xg_vk_pipeline_state_t* state ) {
    xg_vk_pipeline_state = state;

    state->graphics_pipelines_array = std_virtual_heap_alloc_array_m ( xg_vk_graphics_pipeline_t, xg_vk_max_graphics_pipelines_m );
    state->graphics_pipelines_freelist = std_freelist_m ( state->graphics_pipelines_array, xg_vk_max_graphics_pipelines_m );
    state->graphics_pipelines_map = std_hash_map ( 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_graphics_pipelines_m * 2 ), 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_graphics_pipelines_m * 2 ),
        xg_vk_max_graphics_pipelines_m * 2
    );
    std_mutex_init ( &state->graphics_pipelines_freelist_mutex );
    std_mutex_init ( &state->graphics_pipelines_map_mutex );

    state->compute_pipelines_array = std_virtual_heap_alloc_array_m ( xg_vk_compute_pipeline_t, xg_vk_max_compute_pipelines_m );
    state->compute_pipelines_freelist = std_freelist_m ( state->compute_pipelines_array, xg_vk_max_compute_pipelines_m );
    state->compute_pipelines_map = std_hash_map ( 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_compute_pipelines_m * 2 ), 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_compute_pipelines_m * 2 ),
        xg_vk_max_compute_pipelines_m * 2
    );
    std_mutex_init ( &state->compute_pipelines_freelist_mutex );
    std_mutex_init ( &state->compute_pipelines_map_mutex );

    state->renderpasses_array = std_virtual_heap_alloc_array_m ( xg_vk_renderpass_t, xg_vk_max_renderpasses_m );
    state->renderpasses_freelist = std_freelist_m ( state->renderpasses_array, xg_vk_max_renderpasses_m );
    state->renderpasses_map = std_hash_map ( 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_renderpasses_m * 2 ), 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_renderpasses_m * 2 ),
        xg_vk_max_renderpasses_m * 2
    );
    std_mutex_init ( &state->renderpasses_freelist_mutex );
    std_mutex_init ( &state->renderpasses_map_mutex );

    state->set_layouts_array = std_virtual_heap_alloc_array_m ( xg_vk_pipeline_resource_binding_set_layout_t, xg_vk_max_descriptor_sets_layouts_m );
    state->set_layouts_freelist = std_freelist_m ( state->set_layouts_array, xg_vk_max_descriptor_sets_layouts_m );
    state->set_layouts_map = std_hash_map ( 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_descriptor_sets_layouts_m * 2 ),
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_descriptor_sets_layouts_m * 2 ),
        xg_vk_max_descriptor_sets_layouts_m * 2
    );
    std_mutex_init ( &state->set_layouts_mutex );

    state->framebuffers_array = std_virtual_heap_alloc_array_m ( xg_vk_framebuffer_t, xg_vk_max_framebuffers_m );
    state->framebuffers_freelist = std_freelist_m ( state->framebuffers_array, xg_vk_max_framebuffers_m );
    state->framebuffers_map = std_hash_map ( 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_framebuffers_m * 2 ), 
        std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_max_framebuffers_m * 2 ),
        xg_vk_max_framebuffers_m * 2
    );
    std_mutex_init ( &state->framebuffers_freelist_mutex );
    std_mutex_init ( &state->framebuffers_map_mutex );
}

void xg_vk_pipeline_reload ( xg_vk_pipeline_state_t* state ) {
    xg_vk_pipeline_state = state;
}

void xg_vk_pipeline_unload ( void ) {
    // TODO free all vulkan objects

    std_virtual_heap_free ( xg_vk_pipeline_state->graphics_pipelines_array );
    std_virtual_heap_free ( xg_vk_pipeline_state->graphics_pipelines_map.hashes );
    std_virtual_heap_free ( xg_vk_pipeline_state->graphics_pipelines_map.payloads );

    std_virtual_heap_free ( xg_vk_pipeline_state->compute_pipelines_array );
    std_virtual_heap_free ( xg_vk_pipeline_state->compute_pipelines_map.hashes );
    std_virtual_heap_free ( xg_vk_pipeline_state->compute_pipelines_map.payloads );

    std_virtual_heap_free ( xg_vk_pipeline_state->renderpasses_array );
    std_virtual_heap_free ( xg_vk_pipeline_state->renderpasses_map.hashes );
    std_virtual_heap_free ( xg_vk_pipeline_state->renderpasses_map.payloads );

    std_virtual_heap_free ( xg_vk_pipeline_state->set_layouts_array );
    std_virtual_heap_free ( xg_vk_pipeline_state->set_layouts_map.hashes );
    std_virtual_heap_free ( xg_vk_pipeline_state->set_layouts_map.payloads );

    std_mutex_deinit ( &xg_vk_pipeline_state->graphics_pipelines_freelist_mutex );
    std_mutex_deinit ( &xg_vk_pipeline_state->graphics_pipelines_map_mutex );
    std_mutex_deinit ( &xg_vk_pipeline_state->compute_pipelines_freelist_mutex );
    std_mutex_deinit ( &xg_vk_pipeline_state->compute_pipelines_map_mutex );
    std_mutex_deinit ( &xg_vk_pipeline_state->renderpasses_freelist_mutex );
    std_mutex_deinit ( &xg_vk_pipeline_state->renderpasses_map_mutex );
    std_mutex_deinit ( &xg_vk_pipeline_state->set_layouts_mutex );
}

static uint64_t xg_vk_framebuffer_params_hash ( const xg_render_textures_layout_t* render_textures_layout, uint32_t width, uint32_t height ) {
    bool use_depth_stencil = render_textures_layout->depth_stencil.format != xg_format_undefined_m;

    char state_buffer[sizeof ( *render_textures_layout )];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    // Hash format and spp of every render target. It identifies the renderpass/framebuffer and everything else is unused/left default.
    // Every pipeline will have a pointer to its shared renderpass. This way at render time on every pso change
    // it's enough to get the pso's renderpass, and begin a new renderpass if it's different from the previous.
    // Memset to zero and copy each field to guarantee padding value
    std_stack_write_noalign_m ( &hash_allocator, &use_depth_stencil );

    if ( use_depth_stencil ) {
        std_stack_write_noalign_m ( &hash_allocator, &render_textures_layout->depth_stencil.format );
        std_stack_write_noalign_m ( &hash_allocator, &render_textures_layout->depth_stencil.samples_per_pixel );
    }

    std_stack_write_noalign_m ( &hash_allocator, &render_textures_layout->render_targets_count );

    if ( render_textures_layout->render_targets_count ) {
        std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_render_target_layout_t ) );
        std_auto_m sorted_render_targets = std_stack_alloc_array_m ( &hash_allocator, xg_render_target_layout_t, render_textures_layout->render_targets_count );
        std_mem_zero_array_m ( sorted_render_targets, render_textures_layout->render_targets_count );
        std_sort_insertion_copy ( sorted_render_targets, render_textures_layout->render_targets, sizeof ( xg_render_target_layout_t ), render_textures_layout->render_targets_count, xg_vk_pipeline_render_target_layout_cmp );

        for ( size_t i = 0; i < render_textures_layout->render_targets_count; ++i ) {
            std_stack_write_noalign_m ( &hash_allocator, &sorted_render_targets[i].format );
            std_stack_write_noalign_m ( &hash_allocator, &sorted_render_targets[i].samples_per_pixel );
        }
    }

    std_stack_write_noalign_m ( &hash_allocator, &width );
    std_stack_write_noalign_m ( &hash_allocator, &height );

    uint64_t hash = std_hash_metro ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );
    return hash;
}

static uint64_t xg_vk_renderpass_params_hash ( const xg_render_textures_layout_t* render_textures_layout ) {
    bool use_depth_stencil = render_textures_layout->depth_stencil.format != xg_format_undefined_m;

    char state_buffer[sizeof ( *render_textures_layout )];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    // Hash format and spp of every render target. It identifies the renderpass/framebuffer and everything else is unused/left default.
    // Every pipeline will have a pointer to its shared renderpass. This way at render time on every pso change
    // it's enough to get the pso's renderpass, and begin a new renderpass if it's different from the previous.
    // Memset to zero and copy each field to guarantee padding value
    std_stack_write_noalign_m ( &hash_allocator, &use_depth_stencil );

    if ( use_depth_stencil ) {
        std_stack_write_noalign_m ( &hash_allocator, &render_textures_layout->depth_stencil.format );
        std_stack_write_noalign_m ( &hash_allocator, &render_textures_layout->depth_stencil.samples_per_pixel );
    }

    std_stack_write_noalign_m ( &hash_allocator, &render_textures_layout->render_targets_count );

    if ( render_textures_layout->render_targets_count ) {
        std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_render_target_layout_t ) );
        std_auto_m sorted_render_targets = std_stack_alloc_array_m ( &hash_allocator, xg_render_target_layout_t, render_textures_layout->render_targets_count );
        std_mem_zero_array_m ( sorted_render_targets, render_textures_layout->render_targets_count );
        std_sort_insertion_copy ( sorted_render_targets, render_textures_layout->render_targets, sizeof ( xg_render_target_layout_t ), render_textures_layout->render_targets_count, xg_vk_pipeline_render_target_layout_cmp );

        for ( size_t i = 0; i < render_textures_layout->render_targets_count; ++i ) {
            std_stack_write_noalign_m ( &hash_allocator, &sorted_render_targets[i].format );
            std_stack_write_noalign_m ( &hash_allocator, &sorted_render_targets[i].samples_per_pixel );
        }
    }

    uint64_t hash = std_hash_metro ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );
    return hash;
}

static xg_vk_framebuffer_h xg_vk_framebuffer_create ( xg_device_h device_handle, const xg_vk_renderpass_t* renderpass, uint32_t width, uint32_t height, uint64_t framebuffer_hash, const char* debug_name ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    std_mutex_lock ( &xg_vk_pipeline_state->framebuffers_freelist_mutex );
    xg_vk_framebuffer_t* xg_vk_framebuffer = std_list_pop_m ( &xg_vk_pipeline_state->framebuffers_freelist );
    std_mutex_unlock ( &xg_vk_pipeline_state->framebuffers_freelist_mutex );
    xg_vk_framebuffer_h xg_vk_framebuffer_handle = ( xg_vk_framebuffer_h ) ( xg_vk_framebuffer - xg_vk_pipeline_state->framebuffers_array );

    bool use_depth_stencil = renderpass->render_textures_layout.depth_stencil_enabled;

    // Framebuffer
    VkFramebuffer framebuffer;
    {
        VkFramebufferAttachmentImageInfo attachment_image_info[xg_pipeline_output_max_color_targets_m + 1];

        VkFormat formats[xg_pipeline_output_max_color_targets_m + 1];

        for ( size_t i = 0; i < renderpass->render_textures_layout.render_targets_count; ++i ) {
            formats[i] = xg_format_to_vk ( renderpass->render_textures_layout.render_targets[i].format );

            attachment_image_info[i].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
            attachment_image_info[i].pNext = NULL;
            attachment_image_info[i].flags = 0;
            attachment_image_info[i].usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            attachment_image_info[i].usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // see xg_vk_swapchain.c swapchain creation allowed usages comment
            attachment_image_info[i].usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            attachment_image_info[i].width = width;
            attachment_image_info[i].height = height;
            attachment_image_info[i].layerCount = 1;
            attachment_image_info[i].viewFormatCount = 1;
            attachment_image_info[i].pViewFormats = &formats[i];
        }

        {
            size_t i = renderpass->render_textures_layout.render_targets_count;

            formats[i] = xg_format_to_vk ( renderpass->render_textures_layout.depth_stencil.format );

            attachment_image_info[i].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
            attachment_image_info[i].pNext = NULL;
            attachment_image_info[i].flags = 0;
            attachment_image_info[i].usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            attachment_image_info[i].usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            attachment_image_info[i].usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            attachment_image_info[i].width = width;
            attachment_image_info[i].height = height;
            attachment_image_info[i].layerCount = 1;
            attachment_image_info[i].viewFormatCount = 1;
            attachment_image_info[i].pViewFormats = &formats[i];
        }

        VkFramebufferAttachmentsCreateInfo attachments_create_info;
        attachments_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
        attachments_create_info.pNext = NULL;
        attachments_create_info.attachmentImageInfoCount = ( uint32_t ) ( renderpass->render_textures_layout.render_targets_count + ( use_depth_stencil ? 1 : 0 ) );
        attachments_create_info.pAttachmentImageInfos = attachment_image_info;

        VkFramebufferCreateInfo framebuffer_create_info;
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_create_info.pNext = &attachments_create_info;
        framebuffer_create_info.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
        framebuffer_create_info.renderPass = renderpass->vk_renderpass;
        framebuffer_create_info.attachmentCount = ( uint32_t ) ( renderpass->render_textures_layout.render_targets_count + ( use_depth_stencil ? 1 : 0 ) );
        framebuffer_create_info.pAttachments = NULL;
        framebuffer_create_info.width = width;
        framebuffer_create_info.height = height;
        framebuffer_create_info.layers = 1;

        vkCreateFramebuffer ( device->vk_handle, &framebuffer_create_info, NULL, &framebuffer );
    }

    if ( debug_name != NULL ) {
        VkDebugUtilsObjectNameInfoEXT debug_name_info;
        debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name_info.pNext = NULL;
        debug_name_info.objectType = VK_OBJECT_TYPE_FRAMEBUFFER;
        debug_name_info.objectHandle = ( uint64_t ) framebuffer;
        debug_name_info.pObjectName = debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name_info );
    }

    xg_vk_framebuffer->vk_framebuffer = framebuffer;
    xg_vk_framebuffer->hash = framebuffer_hash;
    xg_vk_framebuffer->ref_count = 1;

    std_verify_m ( std_hash_map_insert ( &xg_vk_pipeline_state->framebuffers_map, framebuffer_hash, xg_vk_framebuffer_handle ) );

    return xg_vk_framebuffer_handle;
}

static xg_vk_renderpass_h xg_vk_renderpass_create ( xg_device_h device_handle, const xg_render_textures_layout_t* render_textures_layout, uint64_t renderpass_hash, const char* debug_name ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    std_mutex_lock ( &xg_vk_pipeline_state->renderpasses_freelist_mutex );
    xg_vk_renderpass_t* xg_vk_renderpass = std_list_pop_m ( &xg_vk_pipeline_state->renderpasses_freelist );
    std_mutex_unlock ( &xg_vk_pipeline_state->renderpasses_freelist_mutex );
    xg_vk_renderpass_h xg_vk_renderpass_handle = ( xg_vk_renderpass_h ) ( xg_vk_renderpass - xg_vk_pipeline_state->renderpasses_array );

    bool use_depth_stencil = render_textures_layout->depth_stencil_enabled;

    // Attachments description
    VkAttachmentDescription attachments[xg_pipeline_output_max_color_targets_m + 1];
    {
        size_t i = 0;

        /*
        for ( ; i < render_textures_layout->render_targets_count; ++i ) {
            attachments[i].flags = 0;
            attachments[i].format = xg_format_to_vk ( render_textures_layout->render_targets[i].format );
            attachments[i].samples = xg_sample_count_to_vk ( render_textures_layout->render_targets[i].samples_per_pixel );
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }

        if ( use_depth || use_stencil ) {
            attachments[i].flags = 0;
            attachments[i].format = xg_format_to_vk ( render_textures_layout->depth_stencil.format );
            attachments[i].samples = xg_sample_count_to_vk ( render_textures_layout->depth_stencil.samples_per_pixel );
            attachments[i].loadOp = use_depth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].stencilLoadOp = use_stencil ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        */
        for ( ; i < render_textures_layout->render_targets_count; ++i ) {
            attachments[i].flags = 0;
            attachments[i].format = xg_format_to_vk ( render_textures_layout->render_targets[i].format );
            attachments[i].samples = xg_sample_count_to_vk ( render_textures_layout->render_targets[i].samples_per_pixel );
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // VK_ATTACHMENT_STORE_OP_NONE_EXT ? VK_ATTACHMENT_LOAD_OP_DONT_CARE ?
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // VK_IMAGE_LAYOUT_UNDEFINED here would accept any layout but also force discard the prev. content
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }

        if ( use_depth_stencil ) {
            attachments[i].flags = 0;
            attachments[i].format = xg_format_to_vk ( render_textures_layout->depth_stencil.format );
            attachments[i].samples = xg_sample_count_to_vk ( render_textures_layout->depth_stencil.samples_per_pixel );
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL; // VK_IMAGE_LAYOUT_UNDEFINED here would accept any layout but also force discard the prev. content
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
    }
    // Render pass
    VkAttachmentReference attachment_ref[xg_pipeline_output_max_color_targets_m + 1];
    VkSubpassDescription subpass;
    VkRenderPass renderpass;
    {
        {
            size_t i = 0;

            for ( ; i < render_textures_layout->render_targets_count; ++i ) {
                attachment_ref[i].attachment = ( uint32_t ) i;
                attachment_ref[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }

            if ( use_depth_stencil ) {
                attachment_ref[i].attachment = ( uint32_t ) i;
                attachment_ref[i].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            }
        }

        subpass.flags = 0;
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.inputAttachmentCount = 0;
        subpass.pInputAttachments = NULL;
        subpass.colorAttachmentCount = ( uint32_t ) render_textures_layout->render_targets_count;
        subpass.pColorAttachments = attachment_ref;
        subpass.preserveAttachmentCount = 0;
        subpass.pPreserveAttachments = NULL;
        subpass.pDepthStencilAttachment = use_depth_stencil ? attachment_ref + render_textures_layout->render_targets_count : NULL;
        subpass.pResolveAttachments = NULL;
        VkRenderPassCreateInfo info;
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.pNext = NULL;
        info.flags = 0;
        info.attachmentCount = ( uint32_t ) ( render_textures_layout->render_targets_count + ( use_depth_stencil ? 1 : 0 ) );
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 0;
        info.pDependencies = NULL;
        vkCreateRenderPass ( device->vk_handle, &info, NULL, &renderpass );
    }

    if ( debug_name != NULL ) {
        VkDebugUtilsObjectNameInfoEXT debug_name_info;
        debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name_info.pNext = NULL;
        debug_name_info.objectType = VK_OBJECT_TYPE_RENDER_PASS;
        debug_name_info.objectHandle = ( uint64_t ) renderpass;
        debug_name_info.pObjectName = debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name_info );
    }

    xg_vk_renderpass->vk_renderpass = renderpass;
    xg_vk_renderpass->hash = renderpass_hash;
    xg_vk_renderpass->render_textures_layout = *render_textures_layout;

    std_verify_m ( std_hash_map_insert ( &xg_vk_pipeline_state->renderpasses_map, renderpass_hash, xg_vk_renderpass_handle ) );

    return xg_vk_renderpass_handle;
}

typedef struct {
    uint64_t resource_bindings_hash;
    uint64_t constant_bindings_hash;
    VkPipelineLayout pipeline_layout;
    xg_vk_descriptor_set_layout_h resource_set_layout_handles[xg_resource_binding_set_count_m];
} xg_vk_pipeline_layout_creation_results_t;

void xg_vk_pipeline_create_pipeline_layout ( xg_vk_pipeline_layout_creation_results_t* results, const xg_vk_device_t* device, const xg_resource_bindings_layout_t* resource_bindings, const xg_constant_bindings_layout_t* constant_bindings, const char* debug_name ) {
    // Pipeline layout (resource bindings)
    //      pipeline state: sorted resource_binding_sets
    //      TODO cache the pipeline layout too in addition to the desc set layouts?
    //xg_vk_descriptor_set_layout_h resource_set_layout_handles[xg_resource_binding_set_count_m];

    // TODO allocate this inside hash_allocator?
    xg_vk_pipeline_resource_binding_set_layout_t resource_binding_sets[xg_resource_binding_set_count_m];
    std_mem_zero_m ( &resource_binding_sets );

    VkDescriptorSetLayout vk_sets[xg_resource_binding_set_count_m];
    VkPipelineLayoutCreateInfo layout_info;

    // Partition the resource bindings by set
    for ( size_t i = 0; i < resource_bindings->binding_points_count; ++i ) {
        xg_vk_pipeline_resource_binding_set_layout_t* set = &resource_binding_sets[resource_bindings->binding_points[i].set];
        set->binding_points[set->binding_points_count].shader_register = resource_bindings->binding_points[i].shader_register;
        set->binding_points[set->binding_points_count].stages = resource_bindings->binding_points[i].stages;
        set->binding_points[set->binding_points_count].type = resource_bindings->binding_points[i].type;
        ++set->binding_points_count;
        //uint32_t shader_register = resource_bindings->binding_points[i].shader_register;
        //set->binding_points[shader_register].shader_register = resource_bindings->binding_points[i].shader_register;
        //set->binding_points[shader_register].stages = resource_bindings->binding_points[i].stages;
        //set->binding_points[shader_register].type = resource_bindings->binding_points[i].type;
    }

    // Sort and hash the sets
    for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
        xg_vk_pipeline_resource_binding_set_layout_t* set = &resource_binding_sets[i];
        xg_vk_pipeline_resource_binding_point_t tmp;
        std_sort_insertion ( set->binding_points, sizeof ( xg_vk_pipeline_resource_binding_point_t ), set->binding_points_count, xg_vk_pipeline_resource_binding_point_cmp, &tmp );
        set->hash = std_hash_metro ( set->binding_points, sizeof ( xg_vk_pipeline_resource_binding_point_t ) * xg_pipeline_resource_max_bindings_per_set_m );
    }

    // Create the Vulkan sets
    for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
        xg_vk_pipeline_resource_binding_set_layout_t* set = &resource_binding_sets[i];

        std_mutex_lock ( &xg_vk_pipeline_state->set_layouts_mutex );
        xg_vk_descriptor_set_layout_h* set_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->set_layouts_map, set->hash );

        if ( set_lookup ) {
            xg_vk_descriptor_set_layout_h set_handle = *set_lookup;
            results->resource_set_layout_handles[i] = set_handle;
            xg_vk_pipeline_resource_binding_set_layout_t* existing_set = &xg_vk_pipeline_state->set_layouts_array[set_handle];
            vk_sets[i] = existing_set->vk_descriptor_set_layout;
        } else {
            VkDescriptorSetLayoutBinding vk_bindings[xg_pipeline_resource_max_bindings_per_set_m];

            for ( size_t j = 0; j < set->binding_points_count; ++j ) {
                vk_bindings[j].descriptorCount = 1; // TODO support arrays
                vk_bindings[j].descriptorType = xg_descriptor_type_to_vk ( set->binding_points[j].type );
                vk_bindings[j].binding = set->binding_points[j].shader_register;
                vk_bindings[j].stageFlags = xg_shader_stage_to_vk ( set->binding_points[j].stages );
                vk_bindings[j].pImmutableSamplers = NULL; // TODO
            }

            VkDescriptorSetLayoutCreateInfo descriptor_set_info;
            descriptor_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptor_set_info.pNext = NULL;
            descriptor_set_info.flags = 0;
            descriptor_set_info.bindingCount = ( uint32_t ) set->binding_points_count;
            descriptor_set_info.pBindings = vk_bindings;
            vkCreateDescriptorSetLayout ( device->vk_handle, &descriptor_set_info, NULL, &vk_sets[i] );

            set->vk_descriptor_set_layout = vk_sets[i];

            xg_vk_pipeline_resource_binding_set_layout_t* new_set = std_list_pop_m ( &xg_vk_pipeline_state->set_layouts_freelist );
            xg_vk_descriptor_set_layout_h set_handle = ( uint64_t ) ( new_set - xg_vk_pipeline_state->set_layouts_array );
            results->resource_set_layout_handles[i] = set_handle;

            //std_mem_copy_m ( new_set, set );
            for ( uint32_t j = 0; j < xg_pipeline_resource_max_bindings_per_set_m; ++j ) {
                new_set->binding_points[j].shader_register = UINT32_MAX;
            }

            new_set->vk_descriptor_set_layout = set->vk_descriptor_set_layout;
            new_set->hash = set->hash;
            new_set->binding_points_count = set->binding_points_count;

            for ( size_t j = 0; j < set->binding_points_count; ++j ) {
                uint32_t shader_register = set->binding_points[j].shader_register;
                new_set->binding_points[shader_register].shader_register = set->binding_points[j].shader_register;
                new_set->binding_points[shader_register].stages = set->binding_points[j].stages;
                new_set->binding_points[shader_register].type = set->binding_points[j].type;
            }

            std_hash_map_insert ( &xg_vk_pipeline_state->set_layouts_map, set->hash, set_handle );
        }

        std_mutex_unlock ( &xg_vk_pipeline_state->set_layouts_mutex );
    }

    // Sort and hash the constants
    xg_constant_binding_layout_t constant_binding_points[xg_pipeline_constant_max_bindings_m];
    std_mem_zero_m ( &constant_binding_points );

    for ( uint32_t i = 0; i < constant_bindings->binding_points_count; ++i ) {
        constant_binding_points[i].stages = constant_bindings->binding_points[i].stages;
        constant_binding_points[i].size = constant_bindings->binding_points[i].size;
        constant_binding_points[i].id = constant_bindings->binding_points[i].id;
    }

    {
        xg_constant_binding_layout_t tmp;
        std_sort_insertion ( constant_binding_points, sizeof ( xg_constant_binding_layout_t ), constant_bindings->binding_points_count, xg_vk_pipeline_constant_binding_point_cmp, &tmp );
    }

    //results->constant_bindings_hash = std_hash_metro ( constant_binding_points, sizeof ( xg_constant_binding_layout_t ) * constant_bindings->binding_points_count );

    // Create the Vulkan push constant ranges
    VkPushConstantRange vk_constant_ranges[xg_pipeline_constant_max_bindings_m];
    {
        uint32_t offset = 0;

        for ( uint32_t i = 0; i < constant_bindings->binding_points_count; ++i ) {
            vk_constant_ranges[i].offset = offset;
            vk_constant_ranges[i].size = constant_bindings->binding_points[i].size;
            vk_constant_ranges[i].stageFlags = xg_shader_stage_to_vk ( constant_bindings->binding_points[i].stages );

            offset += constant_bindings->binding_points[i].size;
        }
    }

    // Create pipeline layout
    {
        VkPipelineLayout pipeline_layout;

        layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout_info.pNext = NULL;
        layout_info.flags = 0;
        layout_info.setLayoutCount = ( uint32_t ) xg_resource_binding_set_count_m;
        layout_info.pSetLayouts = vk_sets;
        layout_info.pushConstantRangeCount = constant_bindings->binding_points_count;
        layout_info.pPushConstantRanges = vk_constant_ranges;
        VkResult result = vkCreatePipelineLayout ( device->vk_handle, &layout_info, NULL, &pipeline_layout );
        std_verify_m ( result == VK_SUCCESS );

        if ( debug_name != NULL ) {
            VkDebugUtilsObjectNameInfoEXT debug_name_info;
            debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            debug_name_info.pNext = NULL;
            debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
            debug_name_info.objectHandle = ( uint64_t ) pipeline_layout;
            debug_name_info.pObjectName = debug_name;
            xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name_info );
        }

        results->pipeline_layout = pipeline_layout;
    }

    // Hash
    // TODO hash the whole data instead of the individual sets hashes?
    {
        std_hash_metro_state_t hash_state;
        std_hash_metro_begin ( &hash_state );

        for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
            std_hash_metro_add_m ( &hash_state, &resource_binding_sets[i].hash );
        }

        //std_hash_metro_add_m ( &hash_state, &constant_bindings_hash );

        results->resource_bindings_hash = std_hash_metro_end ( &hash_state );

        //std_stack_push_copy_noalign_m ( hash_allocator, &resource_binding_sets_hash );

        results->constant_bindings_hash = std_hash_metro ( constant_binding_points, sizeof ( xg_constant_binding_layout_t ) * constant_bindings->binding_points_count );
    }
}

#if 1
xg_compute_pipeline_state_h xg_vk_compute_pipeline_create ( xg_device_h device_handle, const xg_compute_pipeline_params_t* params ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    char state_buffer[sizeof ( params->state ) + sizeof ( params->resource_bindings )];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    // Compute shader
    VkPipelineShaderStageCreateInfo shader_info;
    {
        VkShaderModule shader;
        VkShaderModuleCreateInfo module_info;

        module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module_info.pNext = NULL;
        module_info.flags = 0;
        module_info.pCode = ( const uint32_t* ) params->state.compute_shader.buffer.base;
        module_info.codeSize = params->state.compute_shader.buffer.size;
        vkCreateShaderModule ( device->vk_handle, &module_info, NULL, &shader );

        shader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shader_info.pNext = NULL;
        shader_info.flags = 0;
        shader_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        shader_info.module = shader;
        shader_info.pName = "main";
        shader_info.pSpecializationInfo = NULL;

        std_stack_write_noalign_m ( &hash_allocator, &params->state.compute_shader.hash );
    }

    xg_vk_pipeline_layout_creation_results_t pipeline_layout_results;
    xg_vk_pipeline_create_pipeline_layout ( &pipeline_layout_results, device, &params->resource_bindings, &params->constant_bindings, params->debug_name );

    std_stack_write_noalign_m ( &hash_allocator, &pipeline_layout_results.resource_bindings_hash );
    std_stack_write_noalign_m ( &hash_allocator, &pipeline_layout_results.constant_bindings_hash );

    // Pipeline
    uint64_t pipeline_hash = std_hash_metro ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );

    uint64_t* pipeline_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->compute_pipelines_map, pipeline_hash );

    xg_compute_pipeline_state_h xg_vk_pipeline_handle;

    if ( pipeline_lookup == NULL ) {
        VkPipeline pipeline;
        {
            VkComputePipelineCreateInfo info;
            info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            info.pNext = NULL;
            info.flags = 0;
            info.stage = shader_info;
            info.layout = pipeline_layout_results.pipeline_layout;
            info.basePipelineHandle = VK_NULL_HANDLE;
            info.basePipelineIndex = 0;
            VkResult result = vkCreateComputePipelines ( device->vk_handle, VK_NULL_HANDLE, 1, &info, NULL, &pipeline );
            std_verify_m ( result == VK_SUCCESS );

            if ( params->debug_name[0] ) {
                VkDebugUtilsObjectNameInfoEXT debug_name_info;
                debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                debug_name_info.pNext = NULL;
                debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE;
                debug_name_info.objectHandle = ( uint64_t ) pipeline;
                debug_name_info.pObjectName = params->debug_name;
                xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name_info );
            }
        }

        std_mutex_lock ( &xg_vk_pipeline_state->compute_pipelines_freelist_mutex );
        xg_vk_compute_pipeline_t* xg_vk_pipeline = std_list_pop_m ( &xg_vk_pipeline_state->compute_pipelines_freelist );
        std_mutex_unlock ( &xg_vk_pipeline_state->compute_pipelines_freelist_mutex );
        xg_vk_pipeline_handle = ( xg_compute_pipeline_state_h ) ( xg_vk_pipeline - xg_vk_pipeline_state->compute_pipelines_array );

        // store compute state
        xg_vk_pipeline->state = params->state;

        // store common state
        for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
            xg_vk_pipeline->common.resource_set_layout_handles[i] = pipeline_layout_results.resource_set_layout_handles[i];
        }

        xg_vk_pipeline->common.push_constants_hash = pipeline_layout_results.constant_bindings_hash;

        if ( params->debug_name[0] ) {
            std_str_copy_static_m ( xg_vk_pipeline->common.debug_name, params->debug_name );
        }

        xg_vk_pipeline->common.vk_handle = pipeline;
        xg_vk_pipeline->common.vk_layout_handle = pipeline_layout_results.pipeline_layout;
        xg_vk_pipeline->common.hash = pipeline_hash;
        xg_vk_pipeline->common.device_handle = device_handle;
        xg_vk_pipeline->common.reference_count = 1;

        std_mutex_lock ( &xg_vk_pipeline_state->compute_pipelines_map_mutex );
        std_verify_m ( std_hash_map_insert ( &xg_vk_pipeline_state->compute_pipelines_map, pipeline_hash, xg_vk_pipeline_handle ) );
        std_mutex_unlock ( &xg_vk_pipeline_state->compute_pipelines_map_mutex );
    } else {
        xg_vk_pipeline_handle = *pipeline_lookup;
        xg_vk_compute_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->compute_pipelines_array[xg_vk_pipeline_handle];
        xg_vk_pipeline->common.reference_count += 1;
    }

    std_bit_set_64 ( &xg_vk_pipeline_handle, 63 );

    return xg_vk_pipeline_handle;
}
#endif

xg_graphics_pipeline_state_h xg_vk_graphics_pipeline_create ( xg_device_h device_handle, const xg_graphics_pipeline_params_t* params ) {
    //const xg_graphics_pipeline_state_t* pipeline_state,
    //const xg_render_textures_layout_t* render_textures_layout,
    //const xg_resource_bindings_layout_t* resource_bindings_layout ) {
    // Fetch actual device handle
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // Init xg_vk_pipeline object
    //xg_vk_pipeline->resource_bindings_layout = *resource_bindings_layout;

    char state_buffer[sizeof ( params->state ) + sizeof ( params->resource_bindings )];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    // Begin constructing Vulkan pipeline
    // Shaders
    VkPipelineShaderStageCreateInfo shader_info[xg_shading_stage_count_m];
    size_t shader_count = 0;
    {
        VkShaderModule shaders[xg_shading_stage_count_m];
        VkShaderModuleCreateInfo module_info[xg_shading_stage_count_m];

        std_hash_metro_state_t hash_state;
        std_hash_metro_begin ( &hash_state );

        if ( params->state.vertex_shader.enable ) {
            module_info[shader_count].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            module_info[shader_count].pNext = NULL;
            module_info[shader_count].flags = 0;
            module_info[shader_count].pCode = ( const uint32_t* ) params->state.vertex_shader.buffer.base;
            module_info[shader_count].codeSize = params->state.vertex_shader.buffer.size;
            vkCreateShaderModule ( device->vk_handle, &module_info[shader_count], NULL, &shaders[shader_count] );

            shader_info[shader_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_info[shader_count].pNext = NULL;
            shader_info[shader_count].flags = 0;
            shader_info[shader_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
            shader_info[shader_count].module = shaders[shader_count];
            shader_info[shader_count].pName = "main";
            shader_info[shader_count].pSpecializationInfo = NULL;

            std_stack_write_noalign_m ( &hash_allocator, &params->state.vertex_shader.hash );
            ++shader_count;
        }

        if ( params->state.fragment_shader.enable ) {
            module_info[shader_count].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            module_info[shader_count].pNext = NULL;
            module_info[shader_count].flags = 0;
            module_info[shader_count].pCode = ( const uint32_t* ) params->state.fragment_shader.buffer.base;
            module_info[shader_count].codeSize = params->state.fragment_shader.buffer.size;
            vkCreateShaderModule ( device->vk_handle, &module_info[shader_count], NULL, &shaders[shader_count] );

            shader_info[shader_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_info[shader_count].pNext = NULL;
            shader_info[shader_count].flags = 0;
            shader_info[shader_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            shader_info[shader_count].module = shaders[shader_count];
            shader_info[shader_count].pName = "main";
            shader_info[shader_count].pSpecializationInfo = NULL;

            std_stack_write_noalign_m ( &hash_allocator, &params->state.fragment_shader.hash );

            ++shader_count;
        }
    }
    // Input layout
    VkPipelineVertexInputStateCreateInfo vertex_layout;
    VkVertexInputBindingDescription bindings[xg_input_layout_max_streams_m];
    VkVertexInputAttributeDescription attributes[xg_input_layout_stream_max_attributes_m];
    {
        size_t attribute_count = 0;

        for ( size_t i = 0; i < params->state.input_layout.stream_count; ++i ) {
            bindings[i].binding = ( uint32_t ) params->state.input_layout.streams[i].id;
            bindings[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            size_t size = 0;

            for ( size_t j = 0; j < params->state.input_layout.streams[i].attribute_count; ++j ) {
                // index used when binding the vertex buffer, identifies the stream
                attributes[attribute_count].binding = ( uint32_t ) i;
                //attributes[attribute_count].binding = ( uint32_t ) params->state.input_layout.streams[i].id;
                // index used when reading the data in the shader, identifies the information
                attributes[attribute_count].location = ( uint32_t ) attribute_count;
                //attributes[attribute_count].location = ( uint32_t ) params->state.input_layout.streams[i].attributes[j].id;
                attributes[attribute_count].format = xg_format_to_vk ( params->state.input_layout.streams[i].attributes[j].format );
                attributes[attribute_count].offset = ( uint32_t ) size;
                size += xg_format_size ( params->state.input_layout.streams[i].attributes[j].format );
                ++attribute_count;
            }

            bindings[i].stride = ( uint32_t ) size;
        }

        vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_layout.pNext = NULL;
        vertex_layout.flags = 0;
        vertex_layout.vertexBindingDescriptionCount = ( uint32_t ) params->state.input_layout.stream_count;
        vertex_layout.pVertexBindingDescriptions = bindings;
        vertex_layout.vertexAttributeDescriptionCount = ( uint32_t ) attribute_count;
        vertex_layout.pVertexAttributeDescriptions = attributes;

        // Compute hash
        //xg_vertex_stream_t sorted_input_streams[xg_input_layout_max_streams_m];
        //std_mem_zero_m ( &sorted_input_streams );
        std_stack_write_noalign_m ( &hash_allocator, &params->state.input_layout.stream_count );

        if ( params->state.input_layout.stream_count ) {
            std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_vertex_stream_t ) );
            std_auto_m sorted_input_streams = std_stack_alloc_array_m ( &hash_allocator, xg_vertex_stream_t, params->state.input_layout.stream_count );
            std_mem_zero_array_m ( sorted_input_streams, params->state.input_layout.stream_count );
            std_sort_insertion_copy ( sorted_input_streams, params->state.input_layout.streams, sizeof ( xg_vertex_stream_t ), params->state.input_layout.stream_count, xg_vk_pipeline_vertex_stream_cmp );

            for ( size_t i = 0; i < params->state.input_layout.stream_count; ++i ) {
                xg_vertex_attribute_t tmp;
                std_mem_zero_m ( &tmp );
                std_sort_insertion ( sorted_input_streams[i].attributes, sizeof ( xg_vertex_attribute_t ), sorted_input_streams[i].attribute_count, xg_vk_pipeline_vertex_attribute_cmp, &tmp );
            }
        }
    }
    // Input assembly
    // TODO add topology as pipeline parameter
    VkPipelineInputAssemblyStateCreateInfo input_assembly;
    {
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.pNext = NULL;
        input_assembly.flags = 0;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;
    }
    // Viewport
    //      pipeline hash: unmuted state
    VkPipelineViewportStateCreateInfo viewport;
    VkViewport v;
    VkRect2D s;
    {
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.pNext = NULL;
        viewport.flags = 0;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        viewport.pViewports = NULL;
        viewport.pScissors = NULL;

        if ( !params->state.dynamic_state.enabled_states[xg_graphics_pipeline_dynamic_state_viewport_m] ) {
            std_mem_zero_m ( &v );
            std_mem_zero_m ( &s );

            // Flip viewport y axis to point up, same as OpenGL/DX
            // https://www.saschawillems.de/blog/2019/03/29/flipping-the-vulkan-viewport
            // TODO need to query for VK_KHR_Maintenance1 extension to support Vulkan 1.0
            //      Vulkan 1.1 supports this by default
            v.x = ( float ) params->state.viewport_state.x;
            v.y = ( float ) params->state.viewport_state.y + ( float ) params->state.viewport_state.height;
            v.width = ( float ) params->state.viewport_state.width;
            //v.height = ( float ) params->state.viewport_state.height;
            v.height = - ( float ) params->state.viewport_state.height;
            v.minDepth = params->state.viewport_state.min_depth;
            v.maxDepth = params->state.viewport_state.max_depth;
            s.offset.x = 0;
            s.offset.y = 0;
            s.extent.width = params->state.viewport_state.width;
            s.extent.height = params->state.viewport_state.height;
            viewport.pViewports = &v;
            viewport.pScissors = &s;

            std_stack_write_noalign_m ( &hash_allocator, &params->state.viewport_state.x );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.viewport_state.y );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.viewport_state.width );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.viewport_state.height );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.viewport_state.min_depth );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.viewport_state.max_depth );
        } else {
            uint32_t u32_0 = 0;
            float f32_0 = 0;

            std_stack_write_noalign_m ( &hash_allocator, &u32_0 );
            std_stack_write_noalign_m ( &hash_allocator, &u32_0 );
            std_stack_write_noalign_m ( &hash_allocator, &u32_0 );
            std_stack_write_noalign_m ( &hash_allocator, &u32_0 );
            std_stack_write_noalign_m ( &hash_allocator, &f32_0 );
            std_stack_write_noalign_m ( &hash_allocator, &f32_0 );
        }
    }
    // Rasterizer
    //      pipeline hash: unmuted state
    VkPipelineRasterizationStateCreateInfo rasterizer;
    {
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.pNext = NULL;
        rasterizer.flags = 0;

        rasterizer.rasterizerDiscardEnable = params->state.rasterizer_state.disable_rasterization;

        rasterizer.cullMode = xg_cull_mode_to_vk ( params->state.rasterizer_state.cull_mode );
        rasterizer.frontFace = xg_front_face_to_vk ( params->state.rasterizer_state.frontface_winding_mode );
        rasterizer.polygonMode = xg_polygon_mode_to_vk ( params->state.rasterizer_state.polygon_mode );
        rasterizer.lineWidth = params->state.rasterizer_state.line_width;
        rasterizer.depthClampEnable = params->state.rasterizer_state.enable_depth_clamp;

        rasterizer.depthBiasEnable = params->state.rasterizer_state.depth_bias_state.enable;
        rasterizer.depthBiasClamp = params->state.rasterizer_state.depth_bias_state.clamp;
        rasterizer.depthBiasConstantFactor = params->state.rasterizer_state.depth_bias_state.const_factor;
        rasterizer.depthBiasSlopeFactor = params->state.rasterizer_state.depth_bias_state.slope_factor;

        std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.disable_rasterization );

        if ( params->state.rasterizer_state.disable_rasterization ) {
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.cull_mode );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.frontface_winding_mode );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.polygon_mode );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.line_width );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.enable_depth_clamp );

            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.enable );

            if ( params->state.rasterizer_state.depth_bias_state.enable ) {
                std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.clamp );
                std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.const_factor );
                std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.slope_factor );
            }
        }
    }
    // MSAA
    //      pipeline hash: included in rasterizer
    VkPipelineMultisampleStateCreateInfo msaa;
    {
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.pNext = NULL;
        msaa.flags = 0;
        msaa.rasterizationSamples = xg_sample_count_to_vk ( params->state.rasterizer_state.antialiasing_state.sample_count );

        if ( params->state.rasterizer_state.antialiasing_state.mode == xg_antialiasing_none_m ) {
            msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        }

        msaa.sampleShadingEnable = params->state.rasterizer_state.antialiasing_state.mode == xg_antialiasing_ssaa_m;
        msaa.minSampleShading = 1.f;    // TODO
        msaa.pSampleMask = NULL;
        msaa.alphaToCoverageEnable = VK_FALSE;
        msaa.alphaToOneEnable = VK_FALSE;

        if ( params->state.rasterizer_state.disable_rasterization ) {
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.antialiasing_state.sample_count );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.rasterizer_state.antialiasing_state.mode );
        }
    }
    // Depth stencil
    //      pipeline hash: unmuted state
    VkPipelineDepthStencilStateCreateInfo ds;
    {
        ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.pNext = NULL;
        ds.flags = 0;

        ds.depthTestEnable = params->state.depth_stencil_state.depth.enable_test;
        ds.depthWriteEnable = params->state.depth_stencil_state.depth.enable_write;
        ds.depthCompareOp = xg_compare_op_to_vk ( params->state.depth_stencil_state.depth.compare_op );
        ds.depthBoundsTestEnable = params->state.depth_stencil_state.depth.enable_bound_test;
        ds.minDepthBounds = params->state.depth_stencil_state.depth.min_bound;
        ds.maxDepthBounds = params->state.depth_stencil_state.depth.max_bound;

        ds.stencilTestEnable = params->state.depth_stencil_state.stencil.enable_test;

        ds.front.depthFailOp = xg_stencil_op_to_vk ( params->state.depth_stencil_state.stencil.front_face_op.stencil_pass_depth_fail_op );
        ds.front.failOp = xg_stencil_op_to_vk ( params->state.depth_stencil_state.stencil.front_face_op.stencil_fail_op );
        ds.front.passOp = xg_stencil_op_to_vk ( params->state.depth_stencil_state.stencil.front_face_op.stencil_depth_pass_op );
        ds.front.compareOp = xg_compare_op_to_vk ( params->state.depth_stencil_state.stencil.front_face_op.compare_op );
        ds.front.compareMask = params->state.depth_stencil_state.stencil.front_face_op.compare_mask;
        ds.front.writeMask = params->state.depth_stencil_state.stencil.front_face_op.write_mask;
        ds.front.reference = params->state.depth_stencil_state.stencil.front_face_op.reference;

        ds.back.depthFailOp = xg_stencil_op_to_vk ( params->state.depth_stencil_state.stencil.back_face_op.stencil_pass_depth_fail_op );
        ds.back.failOp = xg_stencil_op_to_vk ( params->state.depth_stencil_state.stencil.back_face_op.stencil_fail_op );
        ds.back.passOp = xg_stencil_op_to_vk ( params->state.depth_stencil_state.stencil.back_face_op.stencil_depth_pass_op );
        ds.back.compareOp = xg_compare_op_to_vk ( params->state.depth_stencil_state.stencil.back_face_op.compare_op );
        ds.back.compareMask = params->state.depth_stencil_state.stencil.back_face_op.compare_mask;
        ds.back.writeMask = params->state.depth_stencil_state.stencil.back_face_op.write_mask;
        ds.back.reference = params->state.depth_stencil_state.stencil.back_face_op.reference;

        std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.depth.enable_test );

        if ( params->state.depth_stencil_state.depth.enable_test ) {
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.depth.enable_write );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.depth.compare_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.depth.enable_bound_test );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.depth.min_bound );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.depth.max_bound );
        }

        std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.enable_test );

        if ( params->state.depth_stencil_state.stencil.enable_test ) {
            // TODO
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.stencil_pass_depth_fail_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.stencil_fail_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.stencil_depth_pass_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.compare_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.compare_mask );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.write_mask );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.reference );

            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.stencil_pass_depth_fail_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.stencil_fail_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.stencil_depth_pass_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.compare_op );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.compare_mask );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.write_mask );
            std_stack_write_noalign_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.reference );
        }
    }
    // Color blend
    //      pipeline hash: sorted blend states
    VkPipelineColorBlendStateCreateInfo bs;
    std_mem_zero_m ( &bs );
    VkPipelineColorBlendAttachmentState blends[xg_pipeline_output_max_color_targets_m];
    std_mem_zero_m ( &blends );
    {
        for ( size_t i = 0; i < params->state.blend_state.render_targets_count; ++i ) {
            size_t idx = params->state.blend_state.render_targets[i].id;
            blends[idx].blendEnable = params->state.blend_state.render_targets[i].enable_blend;
            blends[idx].alphaBlendOp = xg_blend_op_to_vk ( params->state.blend_state.render_targets[i].alpha_op );
            blends[idx].colorBlendOp = xg_blend_op_to_vk ( params->state.blend_state.render_targets[i].color_op );
            blends[idx].srcAlphaBlendFactor = xg_blend_factor_to_vk ( params->state.blend_state.render_targets[i].alpha_src );
            blends[idx].srcColorBlendFactor = xg_blend_factor_to_vk ( params->state.blend_state.render_targets[i].color_src );
            blends[idx].dstAlphaBlendFactor = xg_blend_factor_to_vk ( params->state.blend_state.render_targets[i].alpha_dst );
            blends[idx].dstColorBlendFactor = xg_blend_factor_to_vk ( params->state.blend_state.render_targets[i].color_dst );
            blends[idx].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        }

        bs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        bs.pNext = NULL;
        bs.flags = 0;
        bs.logicOpEnable = params->state.blend_state.enable_blend_logic_op;
        bs.logicOp = xg_logic_op_to_vk ( params->state.blend_state.blend_logic_op );
        bs.attachmentCount = ( uint32_t ) params->state.blend_state.render_targets_count;
        bs.pAttachments = blends;
        bs.blendConstants[0] = 0;   // TODO
        bs.blendConstants[1] = 0;
        bs.blendConstants[2] = 0;
        bs.blendConstants[3] = 0;

        //xg_render_target_blend_state_t sorted_render_targets[xg_pipeline_output_max_color_targets_m];
        std_stack_write_noalign_m ( &hash_allocator, &params->state.blend_state.render_targets_count );

        if ( params->state.blend_state.render_targets_count ) {
            std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_render_target_blend_state_t ) );
            std_auto_m sorted_render_targets = std_stack_alloc_array_m ( &hash_allocator, xg_render_target_blend_state_t, params->state.blend_state.render_targets_count );
            std_mem_zero_array_m ( sorted_render_targets, params->state.blend_state.render_targets_count );

            std_sort_insertion_copy ( sorted_render_targets, params->state.blend_state.render_targets, sizeof ( xg_render_target_blend_state_t ), params->state.blend_state.render_targets_count, xg_vk_pipeline_render_target_blend_state_cmp );

            for ( size_t i = 0; i < params->state.blend_state.render_targets_count; ++i ) {
                if ( !sorted_render_targets[i].enable_blend ) {
                    sorted_render_targets[i].alpha_op = 0;
                    sorted_render_targets[i].color_op = 0;
                    sorted_render_targets[i].alpha_src = 0;
                    sorted_render_targets[i].color_src = 0;
                    sorted_render_targets[i].alpha_dst = 0;
                    sorted_render_targets[i].color_dst = 0;
                }
            }
        }

        std_stack_write_noalign_m ( &hash_allocator, &params->state.blend_state.enable_blend_logic_op );

        if ( params->state.blend_state.enable_blend_logic_op ) {
            std_stack_write_noalign_m ( &hash_allocator, &params->state.blend_state.blend_logic_op );
        }
    }
    // Dynamic state
    //      pipeline hash: TODO
    VkPipelineDynamicStateCreateInfo dynamic_state;
    VkDynamicState states[8];
    {
        std_mem_zero_m ( &dynamic_state );
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.pNext = NULL;

        uint32_t states_count = 0;

        if ( params->state.dynamic_state.enabled_states[xg_graphics_pipeline_dynamic_state_viewport_m] ) {
            states[states_count++] = VK_DYNAMIC_STATE_VIEWPORT;
            states[states_count++] = VK_DYNAMIC_STATE_SCISSOR;
        }

        dynamic_state.dynamicStateCount = states_count;
        dynamic_state.pDynamicStates = states;
    }
    // Render pass
    //      pipeline hash: renderpass hash
    xg_vk_renderpass_h renderpass_handle;
    const xg_vk_renderpass_t* renderpass;
    {
        uint64_t renderpass_hash = xg_vk_renderpass_params_hash ( &params->render_textures );
        uint64_t* renderpass_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->renderpasses_map, renderpass_hash );

        if ( renderpass_lookup == NULL ) {
            renderpass_handle = xg_vk_renderpass_create ( device_handle, &params->render_textures, renderpass_hash, params->debug_name );
        } else {
            renderpass_handle = *renderpass_lookup;
        }

        renderpass = xg_vk_renderpass_get ( renderpass_handle );

        std_assert_m ( renderpass )
        std_assert_m ( renderpass->hash == renderpass_hash );

        std_stack_write_noalign_m ( &hash_allocator, &renderpass_hash );
    }
#if 0
    // Framebuffer
    xg_vk_framebuffer_h framebuffer_handle;
    const xg_vk_framebuffer_t* framebuffer;
    {
        uint32_t width = params->state.viewport_state.width;
        uint32_t height = params->state.viewport_state.height;

        uint64_t framebuffer_hash = xg_vk_framebuffer_params_hash ( &params->render_textures, width, height );
        uint64_t* framebuffer_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->framebuffers_map, framebuffer_hash );

        if ( framebuffer_lookup == NULL ) {
            framebuffer_handle = xg_vk_framebuffer_create ( device_handle, &params->render_textures, width, height, framebuffer_hash, params->debug_name );
        } else {
            framebuffer_handle = *framebuffer_lookup;
        }

        framebuffer = xg_vk_framebuffer_get ( framebuffer_handle );

        std_assert_m ( framebuffer );
        std_assert_m ( framebuffer->hash == framebuffer_hash );
    }
#endif
#if 0
    // Pipeline layout (resource bindings)
    //      pipeline state: sorted resource_binding_sets
    //      TODO cache the pipeline layout too in addition to the desc set layouts?
    VkPipelineLayout pipeline_layout;
    uint64_t resource_binding_sets_hash;
    xg_vk_descriptor_set_layout_h resource_set_layout_handles[xg_resource_binding_set_count_m];
    uint64_t constant_bindings_hash;
    {
        // TODO allocate this inside hash_allocator?
        xg_vk_pipeline_resource_binding_set_layout_t resource_binding_sets[xg_resource_binding_set_count_m];
        std_mem_zero_m ( &resource_binding_sets );

        VkDescriptorSetLayoutBinding vk_bindings[xg_pipeline_resource_max_bindings_m];
        VkDescriptorSetLayout vk_sets[xg_resource_binding_set_count_m];
        VkPipelineLayoutCreateInfo info;

        // Partition the resource bindings by set
        for ( size_t i = 0; i < params->resource_bindings.binding_points_count; ++i ) {
            xg_vk_pipeline_resource_binding_set_layout_t* set = &resource_binding_sets[params->resource_bindings.binding_points[i].set];
            set->binding_points[set->binding_points_count].shader_register = params->resource_bindings.binding_points[i].shader_register;
            set->binding_points[set->binding_points_count].stages = params->resource_bindings.binding_points[i].stages;
            set->binding_points[set->binding_points_count].type = params->resource_bindings.binding_points[i].type;
            ++set->binding_points_count;
        }

        // Sort and hash the sets
        for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
            xg_vk_pipeline_resource_binding_set_layout_t* set = &resource_binding_sets[i];
            xg_vk_pipeline_resource_binding_point_t tmp;
            std_sort_insertion ( set->binding_points, sizeof ( xg_vk_pipeline_resource_binding_point_t ), set->binding_points_count, xg_vk_pipeline_resource_binding_point_cmp, &tmp );
            set->hash = std_hash_metro ( set->binding_points, sizeof ( xg_vk_pipeline_resource_binding_point_t ) * set->binding_points_count );
        }

        // Create the Vulkan sets
        for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
            xg_vk_pipeline_resource_binding_set_layout_t* set = &resource_binding_sets[i];

            std_mutex_lock ( &xg_vk_pipeline_state->set_layouts_mutex );
            xg_vk_descriptor_set_layout_h* set_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->set_layouts_map, set->hash );

            if ( set_lookup ) {
                xg_vk_descriptor_set_layout_h set_handle = *set_lookup;
                resource_set_layout_handles[i] = set_handle;
                xg_vk_pipeline_resource_binding_set_layout_t* existing_set = &xg_vk_pipeline_state->set_layouts_array[set_handle];
                vk_sets[i] = existing_set->vk_descriptor_set_layout;
            } else {
                for ( size_t j = 0; j < set->binding_points_count; ++j ) {
                    vk_bindings[j].descriptorCount = 1; // TODO support arrays
                    vk_bindings[j].descriptorType = xg_descriptor_type_to_vk ( set->binding_points[j].type );
                    vk_bindings[j].binding = set->binding_points[j].shader_register;
                    vk_bindings[j].stageFlags = xg_shader_stage_to_vk ( set->binding_points[j].stages );
                    vk_bindings[j].pImmutableSamplers = NULL; // TODO
                }

                VkDescriptorSetLayoutCreateInfo descriptor_set_info;
                descriptor_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                descriptor_set_info.pNext = NULL;
                descriptor_set_info.flags = 0;
                descriptor_set_info.bindingCount = ( uint32_t ) set->binding_points_count;
                descriptor_set_info.pBindings = vk_bindings;
                vkCreateDescriptorSetLayout ( device->vk_handle, &descriptor_set_info, NULL, &vk_sets[i] );

                set->vk_descriptor_set_layout = vk_sets[i];

                xg_vk_pipeline_resource_binding_set_layout_t* new_set = std_list_pop_m ( &xg_vk_pipeline_state->set_layouts_freelist );
                xg_vk_descriptor_set_layout_h set_handle = ( uint64_t ) ( new_set - xg_vk_pipeline_state->set_layouts_array );
                resource_set_layout_handles[i] = set_handle;

                std_mem_copy_m ( new_set, set );

                std_hash_map_insert ( &xg_vk_pipeline_state->set_layouts_map, set->hash, set_handle );
            }

            std_mutex_unlock ( &xg_vk_pipeline_state->set_layouts_mutex );
        }

        // Sort and hash the constants
        xg_constant_binding_layout_t constant_binding_points[xg_pipeline_constant_max_bindings_m];
        std_mem_zero_m ( &constant_binding_points );

        for ( uint32_t i = 0; i < params->constant_bindings.binding_points_count; ++i ) {
            constant_binding_points[i].stages = params->constant_bindings.binding_points[i].stages;
            constant_binding_points[i].size = params->constant_bindings.binding_points[i].size;
            constant_binding_points[i].id = params->constant_bindings.binding_points[i].id;
        }

        {
            xg_constant_binding_layout_t tmp;
            std_sort_insertion ( constant_binding_points, sizeof ( xg_constant_binding_layout_t ), params->constant_bindings.binding_points_count, xg_vk_pipeline_constant_binding_point_cmp, &tmp );
        }

        // Create the Vulkan push constant ranges
        VkPushConstantRange vk_constant_ranges[xg_pipeline_constant_max_bindings_m];
        {
            uint32_t offset = 0;

            for ( uint32_t i = 0; i < params->constant_bindings.binding_points_count; ++i ) {
                vk_constant_ranges[i].offset = offset;
                vk_constant_ranges[i].size = params->constant_bindings.binding_points[i].size;
                vk_constant_ranges[i].stageFlags = xg_shader_stage_to_vk ( params->constant_bindings.binding_points[i].stages );

                offset += params->constant_bindings.binding_points[i].size;
            }
        }

        // Create pipeline layout
        {
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            info.pNext = NULL;
            info.flags = 0;
            info.setLayoutCount = ( uint32_t ) xg_resource_binding_set_count_m;
            info.pSetLayouts = vk_sets;
            info.pushConstantRangeCount = params->constant_bindings.binding_points_count;
            info.pPushConstantRanges = vk_constant_ranges;
            VkResult result = vkCreatePipelineLayout ( device->vk_handle, &info, NULL, &pipeline_layout );
            std_assert_m ( result == VK_SUCCESS );

            if ( params->debug_name != NULL ) {
                VkDebugUtilsObjectNameInfoEXT debug_name_info;
                debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                debug_name_info.pNext = NULL;
                debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
                debug_name_info.objectHandle = ( uint64_t ) pipeline_layout;
                debug_name_info.pObjectName = params->debug_name;
                xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name_info );
            }
        }

        // Hash
        // TODO hash the whole data instead of the individual sets hashes?
        {
            std_hash_metro_state_t hash_state;
            std_hash_metro_begin ( &hash_state );

            for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
                std_hash_metro_add_m ( &hash_state, &resource_binding_sets[i].hash );
            }

            //std_hash_metro_add_m ( &hash_state, &constant_bindings_hash );

            resource_binding_sets_hash = std_hash_metro_end ( &hash_state );

            std_stack_push_copy_noalign_m ( &hash_allocator, &resource_binding_sets_hash );
        }
        {
            constant_bindings_hash = std_hash_metro ( constant_binding_points, sizeof ( xg_constant_binding_layout_t ) * params->constant_bindings.binding_points_count );

            std_stack_push_copy_noalign_m ( &hash_allocator, &constant_bindings_hash );
        }
    }
#else
    xg_vk_pipeline_layout_creation_results_t pipeline_layout_results;
    xg_vk_pipeline_create_pipeline_layout ( &pipeline_layout_results, device, &params->resource_bindings, &params->constant_bindings, params->debug_name );

    std_stack_write_noalign_m ( &hash_allocator, &pipeline_layout_results.resource_bindings_hash );
    std_stack_write_noalign_m ( &hash_allocator, &pipeline_layout_results.constant_bindings_hash );
#endif

    // Pipeline
    // TODO try to batch vkCreateGraphicsPipelines calls
    uint64_t pipeline_hash = std_hash_metro ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );

    uint64_t* pipeline_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->graphics_pipelines_map, pipeline_hash );

    xg_graphics_pipeline_state_h xg_vk_pipeline_handle;

    if ( pipeline_lookup == NULL ) {
        VkPipeline pipeline;
        {
            VkGraphicsPipelineCreateInfo info;
            info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            info.pNext = NULL;
            info.flags = 0;
            info.stageCount = ( uint32_t ) shader_count;
            info.pStages = shader_info;
            info.pVertexInputState = &vertex_layout;
            info.pInputAssemblyState = &input_assembly;
            info.pTessellationState = NULL;
            info.pViewportState = &viewport;
            info.pRasterizationState = &rasterizer;
            info.pMultisampleState = &msaa;
            info.pDepthStencilState = &ds;
            info.pColorBlendState = &bs;
            info.pDynamicState = &dynamic_state;
            info.layout = pipeline_layout_results.pipeline_layout;
            info.renderPass = renderpass->vk_renderpass;
            info.subpass = 0;
            info.basePipelineHandle = VK_NULL_HANDLE;
            info.basePipelineIndex = 0;
            VkResult result = vkCreateGraphicsPipelines ( device->vk_handle, VK_NULL_HANDLE, 1, &info, NULL, &pipeline );
            std_verify_m ( result == VK_SUCCESS );

            if ( params->debug_name[0] ) {
                VkDebugUtilsObjectNameInfoEXT debug_name_info;
                debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                debug_name_info.pNext = NULL;
                debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE;
                debug_name_info.objectHandle = ( uint64_t ) pipeline;
                debug_name_info.pObjectName = params->debug_name;
                xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name_info );
            }
        }

        std_mutex_lock ( &xg_vk_pipeline_state->graphics_pipelines_freelist_mutex );
        xg_vk_graphics_pipeline_t* xg_vk_pipeline = std_list_pop_m ( &xg_vk_pipeline_state->graphics_pipelines_freelist );
        std_mutex_unlock ( &xg_vk_pipeline_state->graphics_pipelines_freelist_mutex );
        xg_vk_pipeline_handle = ( xg_graphics_pipeline_state_h ) ( xg_vk_pipeline - xg_vk_pipeline_state->graphics_pipelines_array );

        // store graphics pipeline state
        xg_vk_pipeline->state = params->state;
        //xg_vk_pipeline->render_textures_layout = params->render_textures;
        xg_vk_pipeline->renderpass = renderpass_handle;

        // store common pipeline state
        for ( size_t i = 0; i < xg_resource_binding_set_count_m; ++i ) {
            xg_vk_pipeline->common.resource_set_layout_handles[i] = pipeline_layout_results.resource_set_layout_handles[i];
        }

        xg_vk_pipeline->common.push_constants_hash = pipeline_layout_results.constant_bindings_hash;

        if ( params->debug_name[0] ) {
            std_str_copy ( xg_vk_pipeline->common.debug_name, xg_debug_name_size_m, params->debug_name );
        }

        xg_vk_pipeline->common.vk_handle = pipeline;
        xg_vk_pipeline->common.vk_layout_handle = pipeline_layout_results.pipeline_layout;
        xg_vk_pipeline->common.hash = pipeline_hash;
        xg_vk_pipeline->common.device_handle = device_handle;
        xg_vk_pipeline->common.reference_count = 1;

        std_mutex_lock ( &xg_vk_pipeline_state->graphics_pipelines_map_mutex );
        //std_assert_m ( 
            std_hash_map_insert ( &xg_vk_pipeline_state->graphics_pipelines_map, pipeline_hash, xg_vk_pipeline_handle ) 
        //)
            ;
        std_mutex_unlock ( &xg_vk_pipeline_state->graphics_pipelines_map_mutex );
    } else {
        xg_vk_pipeline_handle = *pipeline_lookup;
        xg_vk_graphics_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->graphics_pipelines_array[xg_vk_pipeline_handle];
        xg_vk_pipeline->common.reference_count += 1;
    }

    return xg_vk_pipeline_handle;
}

void xg_vk_graphics_pipeline_destroy ( xg_graphics_pipeline_state_h pipeline_handle ) {
    std_bit_clear_64 ( &pipeline_handle, 63 );
    xg_vk_graphics_pipeline_t* pipeline = &xg_vk_pipeline_state->graphics_pipelines_array[pipeline_handle];

    if ( --pipeline->common.reference_count > 0 ) {
        return;
    }

    // TODO also ref count the framebuffer and delete it here if refcount == 0

    const xg_vk_device_t* device = xg_vk_device_get ( pipeline->common.device_handle );

    vkDestroyPipeline ( device->vk_handle, pipeline->common.vk_handle, NULL );

    std_mutex_lock ( &xg_vk_pipeline_state->graphics_pipelines_freelist_mutex );
    std_list_push ( &xg_vk_pipeline_state->graphics_pipelines_freelist, pipeline );
    std_mutex_unlock ( &xg_vk_pipeline_state->graphics_pipelines_freelist_mutex );

    std_mutex_lock ( &xg_vk_pipeline_state->graphics_pipelines_map_mutex );
    std_verify_m ( std_hash_map_remove ( &xg_vk_pipeline_state->graphics_pipelines_map, pipeline->common.hash ) );
    std_mutex_unlock ( &xg_vk_pipeline_state->graphics_pipelines_map_mutex );
}

void xg_vk_compute_pipeline_destroy ( xg_compute_pipeline_state_h pipeline_handle ) {
    std_bit_clear_64 ( &pipeline_handle, 63 );
    xg_vk_compute_pipeline_t* pipeline = &xg_vk_pipeline_state->compute_pipelines_array[pipeline_handle];

    if ( --pipeline->common.reference_count > 0 ) {
        return;
    }

    // TODO also ref count the framebuffer and delete it here if refcount == 0

    const xg_vk_device_t* device = xg_vk_device_get ( pipeline->common.device_handle );

    vkDestroyPipeline ( device->vk_handle, pipeline->common.vk_handle, NULL );

    std_mutex_lock ( &xg_vk_pipeline_state->compute_pipelines_freelist_mutex );
    std_list_push ( &xg_vk_pipeline_state->compute_pipelines_freelist, pipeline );
    std_mutex_unlock ( &xg_vk_pipeline_state->compute_pipelines_freelist_mutex );

    std_mutex_lock ( &xg_vk_pipeline_state->compute_pipelines_map_mutex );
    std_verify_m ( std_hash_map_remove ( &xg_vk_pipeline_state->compute_pipelines_map, pipeline->common.hash ) );
    std_mutex_unlock ( &xg_vk_pipeline_state->compute_pipelines_map_mutex );
}

const xg_vk_graphics_pipeline_t* xg_vk_graphics_pipeline_get ( xg_graphics_pipeline_state_h pipeline_handle ) {
    std_bit_clear_64 ( &pipeline_handle, 63 );
    //std_mutex_lock ( &xg_vk_pipeline_state.pipelines_freelist_mutex );
    xg_vk_graphics_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->graphics_pipelines_array[pipeline_handle];
    //std_mutex_unlock ( &xg_vk_pipeline_state.pipelines_freelist_mutex );
    return xg_vk_pipeline;
}

const xg_vk_compute_pipeline_t* xg_vk_compute_pipeline_get ( xg_compute_pipeline_state_h pipeline_handle ) {
    std_bit_clear_64 ( &pipeline_handle, 63 );
    xg_vk_compute_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->compute_pipelines_array[pipeline_handle];
    return xg_vk_pipeline;
}

const xg_vk_renderpass_t* xg_vk_renderpass_get ( xg_vk_renderpass_h renderpass_handle ) {
    //std_mutex_lock ( &xg_vk_pipeline_state.renderpasses_freelist_mutex );
    xg_vk_renderpass_t* xg_vk_renderpass = &xg_vk_pipeline_state->renderpasses_array[renderpass_handle];
    //std_mutex_unlock ( &xg_vk_pipeline_state.renderpasses_freelist_mutex );
    return xg_vk_renderpass;
}

const xg_vk_framebuffer_t* xg_vk_framebuffer_get ( xg_vk_framebuffer_h framebuffer_handle ) {
    xg_vk_framebuffer_t* framebuffer = &xg_vk_pipeline_state->framebuffers_array[framebuffer_handle];
    return framebuffer;
}

xg_vk_pipeline_resource_binding_set_layout_t* xg_vk_pipeline_vk_set_layout_get ( xg_vk_descriptor_set_layout_h set_handle ) {
    xg_vk_pipeline_resource_binding_set_layout_t* set = &xg_vk_pipeline_state->set_layouts_array[set_handle];
    return set;
    //return set->vk_descriptor_set_layout;
}

xg_vk_framebuffer_h xg_vk_framebuffer_acquire ( xg_device_h device_handle, const xg_vk_renderpass_t* renderpass, uint32_t width, uint32_t height ) {

    uint64_t framebuffer_hash = xg_vk_framebuffer_params_hash ( &renderpass->render_textures_layout, width, height );
    uint64_t* framebuffer_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->framebuffers_map, framebuffer_hash );

    xg_vk_framebuffer_h framebuffer_handle;

    if ( framebuffer_lookup == NULL ) {
        framebuffer_handle = xg_vk_framebuffer_create ( device_handle, renderpass, width, height, framebuffer_hash, NULL /*TODO*/ );
    } else {
        framebuffer_handle = *framebuffer_lookup;
        xg_vk_framebuffer_t* framebuffer = &xg_vk_pipeline_state->framebuffers_array[framebuffer_handle];
        framebuffer->ref_count += 1;
    }

    return framebuffer_handle;
}

void xg_vk_framebuffer_release ( xg_vk_framebuffer_h framebuffer_handle ) {
    xg_vk_framebuffer_t* framebuffer = &xg_vk_pipeline_state->framebuffers_array[framebuffer_handle];

    uint64_t ref_count = framebuffer->ref_count;

    if ( ref_count <= 1 ) {
        // TODO
    } else {
        framebuffer->ref_count = ref_count - 1;
    }
}

void xg_vk_pipeline_activate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];

#if 0
    {
        VkPipelineCacheCreateInfo info;
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        info.pNext = NULL;
        info.flags = 0;
        info.initialDataSize = 0;
        info.pInitialData = NULL;

        vkCreatePipelineCache ( device->vk_handle, &info, NULL, &context->vk_pipeline_cache );
    }
#endif
    
    {
        VkDescriptorPoolCreateInfo info;
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.pNext = NULL;
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = 512;
        info.poolSizeCount = xg_resource_binding_count_m;
        VkDescriptorPoolSize sizes[xg_resource_binding_count_m];
        sizes[xg_resource_binding_sampler_m].type = VK_DESCRIPTOR_TYPE_SAMPLER;
        sizes[xg_resource_binding_sampler_m].descriptorCount = xg_vk_max_samplers_per_descriptor_pool_m;
        sizes[xg_resource_binding_texture_to_sample_m].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        sizes[xg_resource_binding_texture_to_sample_m].descriptorCount = xg_vk_max_sampled_texture_per_descriptor_pool_m;
        sizes[xg_resource_binding_texture_storage_m].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        sizes[xg_resource_binding_texture_storage_m].descriptorCount = xg_vk_max_storage_texture_per_descriptor_pool_m;
        sizes[xg_resource_binding_buffer_uniform_m].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[xg_resource_binding_buffer_uniform_m].descriptorCount = xg_vk_max_uniform_buffer_per_descriptor_pool_m;
        sizes[xg_resource_binding_buffer_storage_m].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[xg_resource_binding_buffer_storage_m].descriptorCount = xg_vk_max_storage_buffer_per_descriptor_pool_m;
        sizes[xg_resource_binding_buffer_texel_uniform_m].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
        sizes[xg_resource_binding_buffer_texel_uniform_m].descriptorCount = xg_vk_max_uniform_texel_buffer_per_descriptor_pool_m;
        sizes[xg_resource_binding_buffer_texel_storage_m].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
        sizes[xg_resource_binding_buffer_texel_storage_m].descriptorCount = xg_vk_max_storage_texel_buffer_per_descriptor_pool_m;
        info.pPoolSizes = sizes;
        VkResult result = vkCreateDescriptorPool ( device->vk_handle, &info, NULL, &context->vk_desc_pool );
        std_verify_m ( result == VK_SUCCESS );
    }

    context->groups_freelist = std_static_freelist_m ( context->groups_array );
    std_mutex_init ( &context->groups_mutex );
}

void xg_vk_pipeline_deactivate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    vkDestroyDescriptorPool ( device->vk_handle, xg_vk_pipeline_state->device_contexts[device_idx].vk_desc_pool, NULL );
}

xg_pipeline_resource_group_h xg_vk_pipeline_create_resource_group ( xg_device_h device_handle, xg_pipeline_state_h pipeline_handle, xg_resource_binding_set_e set ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];

    xg_vk_pipeline_common_t* pipeline;

    if ( std_bit_test_64 ( pipeline_handle, 63 ) ) {
        std_bit_clear_64 ( &pipeline_handle, 63 );
        pipeline = &xg_vk_pipeline_state->compute_pipelines_array[pipeline_handle].common;
    } else {
        pipeline = &xg_vk_pipeline_state->graphics_pipelines_array[pipeline_handle].common;
    }

    xg_vk_descriptor_set_layout_h layout_handle = pipeline->resource_set_layout_handles[set];
    xg_vk_pipeline_resource_binding_set_layout_t* layout = xg_vk_pipeline_vk_set_layout_get ( layout_handle );

    VkDescriptorSet vk_set;
    VkDescriptorSetAllocateInfo vk_set_alloc_info;
    vk_set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    vk_set_alloc_info.pNext = NULL;
    vk_set_alloc_info.descriptorPool = xg_vk_pipeline_state->device_contexts[device_idx].vk_desc_pool;
    vk_set_alloc_info.descriptorSetCount = 1;
    vk_set_alloc_info.pSetLayouts = &layout->vk_descriptor_set_layout;
    VkResult set_alloc_result = vkAllocateDescriptorSets ( device->vk_handle, &vk_set_alloc_info, &vk_set );
    std_verify_m ( set_alloc_result == VK_SUCCESS, "Ran out of descriptor pool memory." );

    std_mutex_lock ( &context->groups_mutex );
    xg_vk_pipeline_resource_group_t* group = std_list_pop_m ( &context->groups_freelist );
    std_mutex_unlock ( &context->groups_mutex );
    std_assert_m ( group );

    group->vk_set = vk_set;
    group->layout = layout_handle;
    xg_pipeline_resource_group_h group_handle = ( xg_pipeline_resource_group_h ) ( group - context->groups_array );

    return group_handle;
}

void xg_vk_pipeline_update_resource_group ( xg_device_h device_handle, xg_pipeline_resource_group_h group_handle, const xg_pipeline_resource_bindings_t* bindings ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_pipeline_resource_group_t* group = &xg_vk_pipeline_state->device_contexts[device_idx].groups_array[group_handle];
    VkDescriptorSet vk_set = group->vk_set;
    xg_vk_pipeline_resource_binding_set_layout_t* vk_set_layout = xg_vk_pipeline_vk_set_layout_get ( group->layout );

    uint32_t buffer_count = bindings->buffer_count;
    uint32_t texture_count = bindings->texture_count;
    uint32_t sampler_count = bindings->sampler_count;

    VkWriteDescriptorSet writes[xg_pipeline_resource_max_bindings_m];
    uint32_t write_count = 0;
    VkDescriptorBufferInfo vk_buffer_info[xg_pipeline_resource_max_bindings_m];
    uint32_t buffer_info_count = 0;
    VkDescriptorImageInfo vk_image_info[xg_pipeline_resource_max_bindings_m];
    uint32_t image_info_count = 0;

    for ( uint32_t i = 0; i < buffer_count; ++i ) {
        xg_buffer_resource_binding_t* binding = &bindings->buffers[i];
        xg_resource_binding_e binding_type = vk_set_layout->binding_points[binding->shader_register].type;
        const xg_vk_buffer_t* buffer = xg_vk_buffer_get ( binding->range.handle );

        VkDescriptorBufferInfo* info = &vk_buffer_info[buffer_info_count++];
        info->offset = binding->range.offset;
        // TODO check for size <= maxUniformBufferRange
        info->range = binding->range.size == xg_buffer_whole_size_m ? VK_WHOLE_SIZE : binding->range.size;
        info->buffer = buffer->vk_handle;

        VkWriteDescriptorSet* write = &writes[write_count++];
        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write->pNext = NULL;
        write->dstSet = vk_set;
        write->dstBinding = binding->shader_register;
        write->dstArrayElement = 0; // TODO
        write->descriptorCount = 1;
        write->pBufferInfo = info;

        switch ( binding_type ) {
            //case xg_buffer_binding_type_uniform_m:
            case xg_resource_binding_buffer_uniform_m:
                write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;

            default:
                std_not_implemented_m();
        }
    }

    for ( uint32_t i = 0; i < texture_count; ++i ) {
        xg_texture_resource_binding_t* binding = &bindings->textures[i];
        xg_resource_binding_e binding_type = vk_set_layout->binding_points[binding->shader_register].type;
        const xg_vk_texture_view_t* view = xg_vk_texture_get_view ( binding->texture, binding->view );

        VkDescriptorImageInfo* info = &vk_image_info[image_info_count++];
        info->sampler = VK_NULL_HANDLE;
        // TODO
        info->imageView = view->vk_handle;
        info->imageLayout = xg_image_layout_to_vk ( binding->layout );

        VkWriteDescriptorSet* write = &writes[write_count++];
        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write->pNext = NULL;
        write->dstSet = vk_set;
        write->dstBinding = binding->shader_register;
        write->dstArrayElement = 0; // TODO
        write->descriptorCount = 1;
        write->pImageInfo = info;

        switch ( binding_type ) {
            case xg_resource_binding_texture_to_sample_m:
                write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                break;

            case xg_resource_binding_texture_storage_m:
                write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                break;

            default:
                std_not_implemented_m();
        }
    }

    for ( uint32_t i = 0; i < sampler_count; ++i ) {
        xg_sampler_resource_binding_t* binding = &bindings->samplers[i];
        const xg_vk_sampler_t* sampler = xg_vk_sampler_get ( binding->sampler );

        VkDescriptorImageInfo* info = &vk_image_info[image_info_count++];
        info->sampler = sampler->vk_handle;
        info->imageView = VK_NULL_HANDLE;

        VkWriteDescriptorSet* write = &writes[write_count++];
        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write->pNext = NULL;
        write->dstSet = vk_set;
        write->dstBinding = binding->shader_register;
        write->dstArrayElement = 0; // TODO
        write->descriptorCount = 1;
        write->pImageInfo = info;
        write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    }

    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    vkUpdateDescriptorSets ( device->vk_handle, write_count, writes, 0, NULL );
}

void xg_vk_pipeline_destroy_resource_group ( xg_device_h device_handle, xg_pipeline_resource_group_h group_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];

    xg_vk_pipeline_resource_group_t* group = &context->groups_array[group_handle];
    VkDescriptorSet vk_set = group->vk_set;

    VkResult result = vkFreeDescriptorSets ( device->vk_handle, context->vk_desc_pool, 1, &vk_set );
    std_verify_m ( result == VK_SUCCESS );

    std_mutex_lock ( &context->groups_mutex );
    std_list_push ( &context->groups_freelist, group );
    std_mutex_unlock ( &context->groups_mutex );
}

const xg_vk_pipeline_resource_group_t* xg_vk_pipeline_resource_group_get ( xg_device_h device_handle, xg_pipeline_resource_group_h group_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];
    xg_vk_pipeline_resource_group_t* group = &context->groups_array[group_handle];
    return group;
}
