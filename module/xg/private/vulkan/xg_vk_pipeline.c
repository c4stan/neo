#include <xg_enum.h>

#include "xg_vk_pipeline.h"
#include "xg_vk_instance.h"
#include "xg_vk_device.h"
#include "xg_vk_enum.h"
#include "xg_vk_buffer.h"
#include "xg_vk_texture.h"
#include "xg_vk_sampler.h"
#include "xg_vk_allocator.h"

#include <std_sort.h>

static xg_vk_pipeline_state_t* xg_vk_pipeline_state;

#define xg_vk_pipeline_handle_is_graphics_m( h ) ( std_bit_read_ms_64_m ( h, 2 ) == 0 )
#define xg_vk_pipeline_handle_is_compute_m( h ) ( std_bit_read_ms_64_m ( h, 2 ) == 1 )
#define xg_vk_pipeline_handle_is_raytrace_m( h ) ( std_bit_read_ms_64_m ( h, 2 ) == 2 )
#define xg_vk_pipeline_handle_tag_as_graphics_m( h ) std_bit_write_ms_64_m ( h, 2, 0 )
#define xg_vk_pipeline_handle_tag_as_compute_m( h ) std_bit_write_ms_64_m ( h, 2, 1 )
#define xg_vk_pipeline_handle_tag_as_raytrace_m( h ) std_bit_write_ms_64_m ( h, 2, 2 )
#define xg_vk_pipeline_handle_remove_tag_m( h ) ( std_bit_clear_ms_64_m ( h, 2 ) )

xg_vk_graphics_pipeline_t* xg_vk_graphics_pipeline_edit ( xg_graphics_pipeline_state_h pipeline_handle ) {
    std_assert_m ( xg_vk_pipeline_handle_is_graphics_m ( pipeline_handle ) );
    pipeline_handle = xg_vk_pipeline_handle_remove_tag_m ( pipeline_handle );
    xg_vk_graphics_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->graphics_pipelines_array[pipeline_handle];
    return xg_vk_pipeline;
}

xg_vk_compute_pipeline_t* xg_vk_compute_pipeline_edit ( xg_compute_pipeline_state_h pipeline_handle ) {
    std_assert_m ( xg_vk_pipeline_handle_is_compute_m ( pipeline_handle ) );
    pipeline_handle = xg_vk_pipeline_handle_remove_tag_m ( pipeline_handle );
    xg_vk_compute_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->compute_pipelines_array[pipeline_handle];
    return xg_vk_pipeline;
}

xg_vk_raytrace_pipeline_t* xg_vk_raytrace_pipeline_edit ( xg_raytrace_pipeline_state_h pipeline_handle ) {
    std_assert_m ( xg_vk_pipeline_handle_is_raytrace_m ( pipeline_handle ) );
    pipeline_handle = xg_vk_pipeline_handle_remove_tag_m ( pipeline_handle );
    xg_vk_raytrace_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->raytrace_pipelines_array[pipeline_handle];
    return xg_vk_pipeline;
}

const xg_vk_graphics_pipeline_t* xg_vk_graphics_pipeline_get ( xg_graphics_pipeline_state_h pipeline_handle ) {
    return xg_vk_graphics_pipeline_edit ( pipeline_handle );
}

const xg_vk_compute_pipeline_t* xg_vk_compute_pipeline_get ( xg_compute_pipeline_state_h pipeline_handle ) {
    return xg_vk_compute_pipeline_edit ( pipeline_handle );
}

const xg_vk_raytrace_pipeline_t* xg_vk_raytrace_pipeline_get ( xg_raytrace_pipeline_state_h pipeline_handle ) {
    return xg_vk_raytrace_pipeline_edit ( pipeline_handle );
}

xg_vk_pipeline_common_t* xg_vk_common_pipeline_get ( xg_pipeline_state_h pipeline_handle ) {
    xg_pipeline_state_h pipeline_idx = xg_vk_pipeline_handle_remove_tag_m ( pipeline_handle );
    if ( xg_vk_pipeline_handle_is_graphics_m ( pipeline_handle ) ) {
        return &xg_vk_pipeline_state->graphics_pipelines_array[pipeline_idx].common;
    } else if ( xg_vk_pipeline_handle_is_compute_m ( pipeline_handle ) ) {
        return &xg_vk_pipeline_state->compute_pipelines_array[pipeline_idx].common;
    } else if ( xg_vk_pipeline_handle_is_raytrace_m ( pipeline_handle ) ) {
        return &xg_vk_pipeline_state->raytrace_pipelines_array[pipeline_idx].common;
    } else {
        std_assert_m ( false );
        return NULL;
    }
}

xg_vk_renderpass_t* xg_vk_renderpass_edit ( xg_renderpass_h renderpass_handle ) {
    xg_vk_renderpass_t* xg_vk_renderpass = &xg_vk_pipeline_state->renderpasses_array[renderpass_handle];
    return xg_vk_renderpass;
}

const xg_vk_renderpass_t* xg_vk_renderpass_get ( xg_renderpass_h renderpass_handle ) {
    xg_vk_renderpass_t* xg_vk_renderpass = &xg_vk_pipeline_state->renderpasses_array[renderpass_handle];
    return xg_vk_renderpass;
}

static int xg_vk_pipeline_vertex_stream_cmp ( const void* a, const void* b, const void* arg ) {
    std_unused_m ( arg );
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

static int xg_vk_pipeline_vertex_attribute_cmp ( const void* a, const void* b, const void* arg ) {
    std_unused_m ( arg );
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

static int xg_vk_pipeline_render_target_blend_state_cmp ( const void* a, const void* b, const void* arg ) {
    std_unused_m ( arg );
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

#define xg_vk_renderpasses_bitset_u64_count_m std_div_ceil_m ( xg_vk_max_renderpasses_m, 64 )
#define xg_vk_resource_bindings_layouts_bitset_u64_count_m std_div_ceil_m ( xg_vk_max_resource_bindings_layouts_m, 64 )

void xg_vk_pipeline_load ( xg_vk_pipeline_state_t* state ) {
    xg_vk_pipeline_state = state;

    state->graphics_pipelines_array = std_virtual_heap_alloc_array_m ( xg_vk_graphics_pipeline_t, xg_vk_max_graphics_pipelines_m );
    state->graphics_pipelines_freelist = std_freelist_m ( state->graphics_pipelines_array, xg_vk_max_graphics_pipelines_m );
    state->graphics_pipelines_map = std_hash_map_create ( xg_vk_max_graphics_pipelines_m * 2 );

    state->compute_pipelines_array = std_virtual_heap_alloc_array_m ( xg_vk_compute_pipeline_t, xg_vk_max_compute_pipelines_m );
    state->compute_pipelines_freelist = std_freelist_m ( state->compute_pipelines_array, xg_vk_max_compute_pipelines_m );
    state->compute_pipelines_map = std_hash_map_create ( xg_vk_max_compute_pipelines_m * 2 );

    state->raytrace_pipelines_array = std_virtual_heap_alloc_array_m ( xg_vk_raytrace_pipeline_t, xg_vk_max_raytrace_pipelines_m );
    state->raytrace_pipelines_freelist = std_freelist_m ( state->raytrace_pipelines_array, xg_vk_max_raytrace_pipelines_m );
    state->raytrace_pipelines_map = std_hash_map_create ( xg_vk_max_raytrace_pipelines_m * 2 );

    state->renderpasses_array = std_virtual_heap_alloc_array_m ( xg_vk_renderpass_t, xg_vk_max_renderpasses_m );
    state->renderpasses_freelist = std_freelist_m ( state->renderpasses_array, xg_vk_max_renderpasses_m );
    state->renderpasses_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_renderpasses_bitset_u64_count_m );
    std_mem_zero ( state->renderpasses_bitset, 8 * xg_vk_renderpasses_bitset_u64_count_m );

    state->resource_bindings_layouts_array = std_virtual_heap_alloc_array_m ( xg_vk_resource_bindings_layout_t, xg_vk_max_resource_bindings_layouts_m );
    state->resource_bindings_layouts_freelist = std_freelist_m ( state->resource_bindings_layouts_array, xg_vk_max_resource_bindings_layouts_m );
    state->resource_bindings_layouts_map = std_hash_map_create ( xg_vk_max_resource_bindings_layouts_m * 2 );
    state->resource_bindings_layouts_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_resource_bindings_layouts_bitset_u64_count_m );
    std_mem_zero ( state->resource_bindings_layouts_bitset, 8 * xg_vk_resource_bindings_layouts_bitset_u64_count_m );
}

void xg_vk_pipeline_reload ( xg_vk_pipeline_state_t* state ) {
    xg_vk_pipeline_state = state;
}

void xg_vk_pipeline_unload ( void ) {
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_pipeline_state->renderpasses_bitset, idx, xg_vk_renderpasses_bitset_u64_count_m ) ) {
        const xg_vk_renderpass_t* renderpass = &xg_vk_pipeline_state->renderpasses_array[idx];
        std_log_info_m ( "Destroying renderpass " std_fmt_u64_m ": " std_fmt_str_m, idx, renderpass->params.debug_name );
        xg_renderpass_h handle = idx;
        xg_vk_renderpass_destroy ( handle );
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_pipeline_state->resource_bindings_layouts_bitset, idx, xg_vk_resource_bindings_layouts_bitset_u64_count_m ) ) {
        const xg_vk_resource_bindings_layout_t* layout = &xg_vk_pipeline_state->resource_bindings_layouts_array[idx];
        std_log_info_m ( "Destroying resource layout " std_fmt_u64_m ": " std_fmt_str_m, idx, layout->params.debug_name );
        xg_resource_bindings_layout_h handle = idx;
        xg_vk_pipeline_resource_bindings_layout_destroy ( handle );
    }

    std_virtual_heap_free ( xg_vk_pipeline_state->graphics_pipelines_array );
    std_hash_map_destroy ( &xg_vk_pipeline_state->graphics_pipelines_map );

    std_virtual_heap_free ( xg_vk_pipeline_state->compute_pipelines_array );
    std_hash_map_destroy ( &xg_vk_pipeline_state->compute_pipelines_map );

    std_virtual_heap_free ( xg_vk_pipeline_state->raytrace_pipelines_array );
    std_hash_map_destroy ( &xg_vk_pipeline_state->raytrace_pipelines_map );

    std_virtual_heap_free ( xg_vk_pipeline_state->renderpasses_array );
    std_virtual_heap_free ( xg_vk_pipeline_state->renderpasses_bitset );

    std_virtual_heap_free ( xg_vk_pipeline_state->resource_bindings_layouts_array );
    std_hash_map_destroy ( &xg_vk_pipeline_state->resource_bindings_layouts_map );
    std_virtual_heap_free ( xg_vk_pipeline_state->resource_bindings_layouts_bitset );
}

static VkFramebuffer xg_vk_framebuffer_create_vk ( xg_device_h device_handle, VkRenderPass vk_renderpass, const xg_render_textures_layout_t* render_textures_layout, const xg_render_textures_usage_t* render_textures_usage, uint32_t width, uint32_t height, const char* debug_name ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    bool use_depth_stencil = render_textures_layout->depth_stencil_enabled;

    // Framebuffer
    VkFramebuffer framebuffer;

    VkFramebufferAttachmentImageInfo attachment_image_info[xg_pipeline_output_max_color_targets_m + 1];
    VkFormat formats[xg_pipeline_output_max_color_targets_m + 1];

    for ( size_t i = 0; i < render_textures_layout->render_targets_count; ++i ) {
        formats[i] = xg_format_to_vk ( render_textures_layout->render_targets[i].format );

        attachment_image_info[i].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
        attachment_image_info[i].pNext = NULL;
        attachment_image_info[i].flags = 0;
        // TODO take these as param
        //attachment_image_info[i].usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        attachment_image_info[i].usage = render_textures_usage->render_targets[i];
        attachment_image_info[i].width = width;
        attachment_image_info[i].height = height;
        attachment_image_info[i].layerCount = 1;
        attachment_image_info[i].viewFormatCount = 1;
        attachment_image_info[i].pViewFormats = &formats[i];
    }

    // depth stencil
    if ( use_depth_stencil ) {
        size_t i = render_textures_layout->render_targets_count;

        formats[i] = xg_format_to_vk ( render_textures_layout->depth_stencil.format );

        attachment_image_info[i].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
        attachment_image_info[i].pNext = NULL;
        attachment_image_info[i].flags = 0;
        //attachment_image_info[i].usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        //attachment_image_info[i].usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        //attachment_image_info[i].usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        attachment_image_info[i].usage = render_textures_usage->depth_stencil;
        attachment_image_info[i].width = width;
        attachment_image_info[i].height = height;
        attachment_image_info[i].layerCount = 1;
        attachment_image_info[i].viewFormatCount = 1;
        attachment_image_info[i].pViewFormats = &formats[i];
    }

    VkFramebufferAttachmentsCreateInfo attachments_create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO,
        .pNext = NULL,
        .attachmentImageInfoCount = ( uint32_t ) ( render_textures_layout->render_targets_count + ( use_depth_stencil ? 1 : 0 ) ),
        .pAttachmentImageInfos = attachment_image_info,
    };

    VkFramebufferCreateInfo framebuffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext = &attachments_create_info,
        .flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT,
        .renderPass = vk_renderpass,
        .attachmentCount = ( uint32_t ) ( render_textures_layout->render_targets_count + ( use_depth_stencil ? 1 : 0 ) ),
        .pAttachments = NULL,
        .width = width,
        .height = height,
        .layers = 1,
    };

    std_verify_m ( vkCreateFramebuffer ( device->vk_handle, &framebuffer_create_info, xg_vk_cpu_allocator(), &framebuffer ) == VK_SUCCESS );

    if ( debug_name != NULL ) {
        VkDebugUtilsObjectNameInfoEXT debug_name_info;
        debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name_info.pNext = NULL;
        debug_name_info.objectType = VK_OBJECT_TYPE_FRAMEBUFFER;
        debug_name_info.objectHandle = ( uint64_t ) framebuffer;
        debug_name_info.pObjectName = debug_name;
        xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
    }

    return framebuffer;
}

static VkRenderPass xg_vk_renderpass_create_vk ( xg_device_h device_handle, const xg_render_textures_layout_t* render_textures_layout, const char* debug_name ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

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
        vkCreateRenderPass ( device->vk_handle, &info, xg_vk_cpu_allocator(), &renderpass );
    }

    if ( debug_name != NULL ) {
        VkDebugUtilsObjectNameInfoEXT debug_name_info;
        debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name_info.pNext = NULL;
        debug_name_info.objectType = VK_OBJECT_TYPE_RENDER_PASS;
        debug_name_info.objectHandle = ( uint64_t ) renderpass;
        debug_name_info.pObjectName = debug_name;
        xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
    }

    return renderpass;
}

static void xg_vk_pipeline_hash_resource_binding_layout ( std_stack_t* stack, const xg_resource_binding_layout_t* layout ) {
    std_stack_write_m ( stack, &layout->shader_register );
    std_stack_write_m ( stack, &layout->stages );
    std_stack_write_m ( stack, &layout->type );
}

static VkDescriptorSetLayoutBinding xg_vk_pipeline_vk_descriptor_set_layout_binding ( const xg_resource_binding_layout_t* layout ) {
    VkDescriptorSetLayoutBinding binding = {
        .descriptorCount = 1, // TODO
        .descriptorType = xg_descriptor_type_to_vk ( layout->type ),
        .binding = layout->shader_register,
        .stageFlags = xg_shader_stage_to_vk ( layout->stages ),
    };
    return binding;
}

xg_resource_bindings_layout_h xg_vk_pipeline_resource_bindings_layout_create ( const xg_resource_bindings_layout_params_t* params ) {
    char state_buffer[sizeof ( *params )];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );
    uint32_t resource_count = params->resource_count;
    std_stack_write_m ( &hash_allocator, &resource_count );
    for ( size_t i = 0; i < resource_count; ++i ) {
        xg_vk_pipeline_hash_resource_binding_layout ( &hash_allocator, &params->resources[i] );
    }

    uint64_t hash = std_hash_block_64_m ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );

    xg_resource_bindings_layout_h handle;
    xg_resource_bindings_layout_h* lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->resource_bindings_layouts_map, hash );

    if ( lookup ) {
        handle = *lookup;
        xg_vk_resource_bindings_layout_t* layout = &xg_vk_pipeline_state->resource_bindings_layouts_array[handle];
        layout->ref_count += 1;
    } else {
        const xg_vk_device_t* device = xg_vk_device_get ( params->device );

        VkDescriptorSetLayoutBinding vk_bindings_array[xg_pipeline_resource_max_bindings_per_set_m];
        uint32_t vk_bindings_count = 0;

        for ( size_t i = 0; i < resource_count; ++i ) {
            vk_bindings_array[vk_bindings_count++] = xg_vk_pipeline_vk_descriptor_set_layout_binding ( &params->resources[i] );
        }

        VkDescriptorSetLayout vk_handle;
        VkDescriptorSetLayoutCreateInfo descriptor_set_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .bindingCount = ( uint32_t ) vk_bindings_count,
            .pBindings = vk_bindings_array,
        };
        vkCreateDescriptorSetLayout ( device->vk_handle, &descriptor_set_info, xg_vk_cpu_allocator(), &vk_handle );

        if ( params->debug_name[0] ) {
            VkDebugUtilsObjectNameInfoEXT debug_name_info;
            debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            debug_name_info.pNext = NULL;
            debug_name_info.objectType = VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
            debug_name_info.objectHandle = ( uint64_t ) vk_handle;
            debug_name_info.pObjectName = params->debug_name;
            xg_vk_device_ext_api ( params->device )->set_debug_name ( device->vk_handle, &debug_name_info );
        }

        xg_vk_resource_bindings_layout_t* layout = std_list_pop_m ( &xg_vk_pipeline_state->resource_bindings_layouts_freelist );
        std_assert_m ( layout );
        handle = layout - xg_vk_pipeline_state->resource_bindings_layouts_array;
        std_bitset_set ( xg_vk_pipeline_state->resource_bindings_layouts_bitset, handle );

        layout->vk_handle = vk_handle,
        layout->hash = hash;
        layout->ref_count = 1;
        layout->params = *params;
        std_mem_set_static_array_m ( layout->shader_register_to_descriptor_idx, 0xff );

        for ( uint32_t i = 0; i < resource_count; ++i ) {
            layout->shader_register_to_descriptor_idx[params->resources[i].shader_register] = i;
        }

        std_hash_map_insert ( &xg_vk_pipeline_state->resource_bindings_layouts_map, hash, handle );
    }

    return handle;
}

void xg_vk_pipeline_resource_bindings_layout_destroy ( xg_resource_bindings_layout_h layout_handle ) {
    xg_vk_resource_bindings_layout_t* layout = &xg_vk_pipeline_state->resource_bindings_layouts_array[layout_handle];
    if ( --layout->ref_count == 0 ) {
        const xg_vk_device_t* device = xg_vk_device_get ( layout->params.device );
        vkDestroyDescriptorSetLayout ( device->vk_handle, layout->vk_handle, xg_vk_cpu_allocator() );
        std_verify_m ( std_hash_map_remove_hash ( &xg_vk_pipeline_state->resource_bindings_layouts_map, layout->hash ) );
        std_list_push ( &xg_vk_pipeline_state->resource_bindings_layouts_freelist, layout );
        std_bitset_clear ( xg_vk_pipeline_state->resource_bindings_layouts_bitset, layout_handle );
    }
}

xg_vk_resource_bindings_layout_t* xg_vk_pipeline_resource_bindings_layout_get ( xg_resource_bindings_layout_h handle ) {
    return &xg_vk_pipeline_state->resource_bindings_layouts_array[handle];
}

void xg_vk_pipeline_resource_binding_set_layouts_get ( xg_resource_bindings_layout_h* layouts, xg_pipeline_state_h pipeline_handle ) {
    // TODO increase refcount?
    xg_vk_pipeline_common_t* common_pipeline = xg_vk_common_pipeline_get ( pipeline_handle );
    std_mem_copy_static_array_m ( layouts, common_pipeline->resource_layouts );
}

xg_resource_bindings_layout_h xg_vk_pipeline_resource_binding_set_layout_get ( xg_pipeline_state_h pipeline_handle, xg_shader_binding_set_e set ) {
    xg_vk_pipeline_common_t* common_pipeline = xg_vk_common_pipeline_get ( pipeline_handle );
    return common_pipeline->resource_layouts[set];
}

VkPipelineLayout xg_vk_pipeline_create_pipeline_layout_vk ( xg_device_h device_handle, const char* debug_name, const xg_resource_bindings_layout_h* resource_layout, const xg_constant_bindings_layout_t* constant_layout ) {
    // TODO cache the pipeline layouts?
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    VkDescriptorSetLayout vk_sets[xg_shader_binding_set_count_m];

    for ( size_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
        xg_resource_bindings_layout_h layout_handle = resource_layout[i];
        xg_vk_resource_bindings_layout_t* layout = xg_vk_pipeline_resource_bindings_layout_get ( layout_handle );
        vk_sets[i] = layout->vk_handle;
        //++layout->ref_count;
    }

    // Fill the Vulkan push constant ranges
    VkPushConstantRange vk_constant_ranges[xg_pipeline_constant_max_bindings_m];
    {
        uint32_t offset = 0;

        for ( uint32_t i = 0; i < constant_layout->binding_points_count; ++i ) {
            const xg_constant_binding_layout_t* binding_point = &constant_layout->binding_points[i];
            vk_constant_ranges[i].offset = offset;
            vk_constant_ranges[i].size = binding_point->size;
            vk_constant_ranges[i].stageFlags = xg_shader_stage_to_vk ( binding_point->stages );

            offset += binding_point->size;
        }
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .setLayoutCount = ( uint32_t ) xg_shader_binding_set_count_m,
        .pSetLayouts = vk_sets,
        .pushConstantRangeCount = constant_layout->binding_points_count,
        .pPushConstantRanges = vk_constant_ranges,
    };

    VkPipelineLayout pipeline_layout;
    VkResult vk_result = vkCreatePipelineLayout ( device->vk_handle, &layout_info, xg_vk_cpu_allocator(), &pipeline_layout );
    std_verify_m ( vk_result == VK_SUCCESS );

    if ( debug_name != NULL ) {
        VkDebugUtilsObjectNameInfoEXT debug_name_info;
        debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name_info.pNext = NULL;
        debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE_LAYOUT;
        debug_name_info.objectHandle = ( uint64_t ) pipeline_layout;
        debug_name_info.pObjectName = debug_name;
        xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
    }

    return pipeline_layout;
}

xg_raytrace_pipeline_state_h xg_vk_raytrace_pipeline_create ( xg_device_h device_handle, const xg_raytrace_pipeline_params_t* params ) {
#if xg_enable_raytracing_m
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    if ( !( device->flags & xg_vk_device_supports_raytrace_m ) ) {
        return xg_null_handle_m;
    }

    char state_buffer[sizeof ( params->state ) + sizeof ( uint64_t ) * xg_shader_binding_set_count_m];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    std_stack_write_m ( &hash_allocator, &params->state.shader_state.shader_count );
    std_stack_write_m ( &hash_allocator, &params->state.shader_state.gen_shader_count );
    std_stack_write_m ( &hash_allocator, &params->state.shader_state.miss_shader_count );
    std_stack_write_m ( &hash_allocator, &params->state.shader_state.hit_group_count );

    // Shaders
    for ( uint32_t i = 0; i < params->state.shader_state.shader_count; ++i ) {
        std_stack_write_m ( &hash_allocator, &params->state.shader_state.shaders[i].hash );
    }

    for ( uint32_t i = 0; i < params->state.shader_state.gen_shader_count; ++i ) {
        std_stack_write_m ( &hash_allocator, &params->state.shader_state.gen_shaders[i] );
    }

    for ( uint32_t i = 0; i < params->state.shader_state.miss_shader_count; ++i ) {
        std_stack_write_m ( &hash_allocator, &params->state.shader_state.miss_shaders[i] );
    }

    for ( uint32_t i = 0; i < params->state.shader_state.hit_group_count; ++i ) {
        std_stack_write_m ( &hash_allocator, &params->state.shader_state.hit_groups[i] );
    }

    // Resources layout
    for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
        xg_vk_resource_bindings_layout_t* layout = xg_vk_pipeline_resource_bindings_layout_get ( params->resource_layouts[i] );
        std_stack_write_m ( &hash_allocator, &layout->hash );
    }

    // Pipeline
    uint64_t pipeline_hash = std_hash_block_64_m ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );
    uint64_t* pipeline_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->raytrace_pipelines_map, pipeline_hash );
    xg_raytrace_pipeline_state_h xg_vk_pipeline_handle;

    if ( pipeline_lookup ) {
        xg_vk_pipeline_handle = *pipeline_lookup;
        xg_vk_raytrace_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->raytrace_pipelines_array[xg_vk_pipeline_handle];
        xg_vk_pipeline->common.reference_count += 1;
    } else {
        // Shaders
        VkShaderModule vk_shader_handles[xg_raytrace_shader_state_max_shaders_m] = { [0 ... xg_raytrace_shader_state_max_shaders_m-1] = VK_NULL_HANDLE };
        VkPipelineShaderStageCreateInfo shader_info[xg_raytrace_shader_state_max_shaders_m];
        
        for ( uint32_t i = 0; i < params->state.shader_state.shader_count; ++i ) {
            const xg_pipeline_state_shader_t* shader = &params->state.shader_state.shaders[i];

            VkShaderModuleCreateInfo module_info = {
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .pCode = ( const uint32_t* ) shader->buffer.base,
                .codeSize = shader->buffer.size,
            };
            vkCreateShaderModule ( device->vk_handle, &module_info, xg_vk_cpu_allocator(), &vk_shader_handles[i] );

            if ( params->debug_name[0] ) {
                char module_name[xg_debug_name_size_m] = {};
                std_stack_t stack = std_static_stack_m ( module_name );
                std_stack_string_append ( &stack, params->debug_name );
                std_stack_string_append ( &stack, "-rt" ); // TODO

                VkDebugUtilsObjectNameInfoEXT debug_name_info = {
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    .pNext = NULL,
                    .objectType = VK_OBJECT_TYPE_SHADER_MODULE,
                    .objectHandle = ( uint64_t ) vk_shader_handles[i],
                    .pObjectName = module_name,
                };
                xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
            }

            shader_info[i] = ( VkPipelineShaderStageCreateInfo ) {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .stage = xg_shader_stage_to_vk ( xg_shading_stage_enum_to_bit_m ( shader->stage ) ),
                .module = vk_shader_handles[i],
                .pName = "main",
                .pSpecializationInfo = NULL,
            };
        }

        // Resource layout
        VkPipelineLayout vk_pipeline_layout = xg_vk_pipeline_create_pipeline_layout_vk ( device_handle, params->debug_name, params->resource_layouts, &params->constant_layout );

        // Pipeline
        VkPipeline pipeline;
        uint32_t group_count = 0;
        {
#if xg_vk_enable_nv_raytracing_ext_m
            // TODO have a separate define for max_groups
            VkRayTracingShaderGroupCreateInfoNV groups[xg_raytrace_shader_state_max_shaders_m] = {};

            for ( uint32_t i = 0; i < params->state.shader_state.gen_shader_count; ++i ) {
                groups[group_count++] = ( VkRayTracingShaderGroupCreateInfoNV ) {
                    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
                    .pNext = NULL,
                    .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
                    .generalShader = params->state.shader_state.gen_shaders[i].shader,
                    .closestHitShader = VK_SHADER_UNUSED_NV,
                    .anyHitShader = VK_SHADER_UNUSED_NV,
                    .intersectionShader = VK_SHADER_UNUSED_NV,
                };
            }

            for ( uint32_t i = 0; i < params->state.shader_state.miss_shader_count; ++i ) {
                groups[group_count++] = ( VkRayTracingShaderGroupCreateInfoNV ) {
                    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
                    .pNext = NULL,
                    .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV,
                    .generalShader = params->state.shader_state.miss_shaders[i].shader,
                    .closestHitShader = VK_SHADER_UNUSED_NV,
                    .anyHitShader = VK_SHADER_UNUSED_NV,
                    .intersectionShader = VK_SHADER_UNUSED_NV,
                };
            }

            for ( uint32_t i = 0; i < params->state.shader_state.hit_group_count; ++i ) {
                groups[group_count++] = ( VkRayTracingShaderGroupCreateInfoNV ) {
                    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
                    .pNext = NULL,
                    .type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_NV,
                    .generalShader = VK_SHADER_UNUSED_NV,
                    .closestHitShader = params->state.shader_state.hit_groups[i].closest_shader,
                    .anyHitShader = params->state.shader_state.hit_groups[i].any_shader != -1 ? params->state.shader_state.hit_groups[i].any_shader : VK_SHADER_UNUSED_NV,
                    .intersectionShader = params->state.shader_state.hit_groups[i].intersection_shader != -1 ? params->state.shader_state.hit_groups[i].intersection_shader : VK_SHADER_UNUSED_NV,
                };
            }

            VkRayTracingPipelineCreateInfoNV info = {
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV,
                .pNext = NULL,
                .flags = 0,
                .stageCount = params->state.shader_state.shader_count,
                .pStages = shader_info,
                .groupCount = group_count,
                .pGroups = groups,
                .maxRecursionDepth = 4,//params->state.max_recursion, // TODO
                .layout = vk_pipeline_layout,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = 0,
            };
            VkResult result = xg_vk_device_ext_api( device_handle )->create_raytrace_pipelines ( device->vk_handle, VK_NULL_HANDLE, 1, &info, xg_vk_cpu_allocator(), &pipeline );
#else
            // TODO
            VkRayTracingPipelineCreateInfoKHR info = {
                .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
                .pNext = NULL,
                .flags = 0,
                .stageCount = shader_count,
                .pStages = shader_info,
                .groupCount = 0,
                .pGroups = NULL,
                .maxPipelineRayRecursionDepth = 1, // TODO
                .pLibraryInfo = NULL,
                .pLibraryInterface = NULL,
                .pDynamicState = NULL,
                .layout = pipeline_layout_result.pipeline_layout,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = 0,
            };
            VkResult result = xg_vk_device_ext_api( device_handle )->create_raytrace_pipelines ( device->vk_handle, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, xg_vk_cpu_allocator(), &pipeline );
#endif
            std_verify_m ( result == VK_SUCCESS );

            if ( params->debug_name[0] ) {
                VkDebugUtilsObjectNameInfoEXT debug_name_info;
                debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                debug_name_info.pNext = NULL;
                debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE;
                debug_name_info.objectHandle = ( uint64_t ) pipeline;
                debug_name_info.pObjectName = params->debug_name;
                xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
            }
        }

        void* groups_buffer;
        uint32_t gen_offsets[xg_raytrace_shader_state_max_gen_shaders_m] = { -1 };
        uint32_t miss_offsets[xg_raytrace_shader_state_max_miss_shaders_m] = { -1 };
        uint32_t hit_offsets[xg_raytrace_shader_state_max_hit_groups_m] = { -1 };
        {
            uint32_t group_size = device->raytrace_properties.shaderGroupHandleSize;
            //uint32_t sbt_stride = std_align ( group_size, device->raytrace_properties.shaderGroupBaseAlignment );
            //std_assert_m ( sbt_stride <= device->raytrace_properties.maxShaderGroupStride );
            groups_buffer = std_virtual_heap_alloc_m ( group_count * group_size, 16 );
            VkResult result = xg_vk_device_ext_api ( device_handle )->get_shader_group_handles ( device->vk_handle, pipeline, 0, group_count, group_count * group_size, groups_buffer );
            std_verify_m ( result == VK_SUCCESS );

            uint32_t g = 0;
            for ( uint32_t i = 0; i < params->state.shader_state.gen_shader_count; ++i ) {
                gen_offsets[params->state.shader_state.gen_shaders[i].binding] = group_size * g++;
            }

            for ( uint32_t i = 0; i < params->state.shader_state.miss_shader_count; ++i ) {
                miss_offsets[params->state.shader_state.miss_shaders[i].binding] = group_size * g++;
            }

            for ( uint32_t i = 0; i < params->state.shader_state.hit_group_count; ++i ) {
                hit_offsets[params->state.shader_state.hit_groups[i].binding] = group_size * g++;
            }

            std_assert_m ( g == group_count );
        }

#if 0
        // Shader Binding Table
        xg_buffer_h sbt_buffer_handle;
        VkStridedDeviceAddressRegionKHR sbt_raygen_region;
        VkStridedDeviceAddressRegionKHR sbt_miss_region;
        VkStridedDeviceAddressRegionKHR sbt_hit_region;
        {
            // TODO
            uint32_t group_size = device->raytrace_properties.shaderGroupHandleSize;
            uint32_t sbt_stride = std_align ( group_size, device->raytrace_properties.shaderGroupBaseAlignment );
            std_assert_m ( sbt_stride <= device->raytrace_properties.maxShaderGroupStride );
            void* group_buffer = std_virtual_heap_alloc ( group_count * group_size, 16 );
            VkResult result = xg_vk_device_ext_api ( device_handle )->get_shader_group_handles ( device->vk_handle, pipeline, 0, group_count, group_count * group_size, group_buffer );
            std_verify_m ( result == VK_SUCCESS );
            sbt_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
                .memory_type = xg_memory_type_upload_m,
                .device = device_handle,
                .size = group_count * sbt_stride,
                .allowed_usage = xg_buffer_usage_bit_copy_source_m | xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_shader_binding_table_m,
                .debug_name = "sbt", // TODO
            ));
            const xg_vk_buffer_t* sbt_buffer = xg_vk_buffer_get ( sbt_buffer_handle );
            void* sbt_data = sbt_buffer->allocation.mapped_address;
            for ( uint32_t i = 0; i < group_count; ++i ) {
                std_mem_copy ( sbt_data, group_buffer + group_size * i, group_size );
                sbt_data += sbt_stride;
            }

            VkBufferDeviceAddressInfo address_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .pNext = NULL,
                .buffer = sbt_buffer->vk_handle,
            };
            VkDeviceAddress sbt_base = vkGetBufferDeviceAddress ( device->vk_handle, &address_info );
            sbt_raygen_region = ( VkStridedDeviceAddressRegionKHR ) {
                .deviceAddress = sbt_base + 0,
                .stride = sbt_stride,
                .size = sbt_stride,
            };
            sbt_miss_region = ( VkStridedDeviceAddressRegionKHR ) {
                .deviceAddress = sbt_base + sbt_stride,
                .stride = sbt_stride,
                .size = sbt_stride,
            };
            sbt_hit_region = ( VkStridedDeviceAddressRegionKHR ) {
                .deviceAddress = sbt_base + sbt_stride * 2,
                .stride = sbt_stride,
                .size = sbt_stride,
            };
        }
#endif

        // Allocate
        xg_vk_raytrace_pipeline_t* xg_vk_pipeline = std_list_pop_m ( &xg_vk_pipeline_state->raytrace_pipelines_freelist );
        xg_vk_pipeline_handle = ( xg_raytrace_pipeline_state_h ) ( xg_vk_pipeline - xg_vk_pipeline_state->raytrace_pipelines_array );

        // Store raytrace state
        xg_vk_pipeline->state = params->state;
        //xg_vk_pipeline->sbt_buffer = sbt_buffer_handle;
        //xg_vk_pipeline->sbt_raygen_region = sbt_raygen_region;
        //xg_vk_pipeline->sbt_miss_region = sbt_miss_region;
        //xg_vk_pipeline->sbt_hit_region = sbt_hit_region;
        xg_vk_pipeline->sbt_buffer = xg_null_handle_m;
        xg_vk_pipeline->sbt_handle_buffer = groups_buffer;
        std_mem_copy_static_array_m ( xg_vk_pipeline->gen_offsets, gen_offsets );
        std_mem_copy_static_array_m ( xg_vk_pipeline->miss_offsets, miss_offsets );
        std_mem_copy_static_array_m ( xg_vk_pipeline->hit_offsets, hit_offsets );

        // Store common state
        for ( size_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
            xg_vk_pipeline->common.resource_layouts[i] = params->resource_layouts[i];
        }

        //xg_vk_pipeline->common.push_constants_hash = bindings_layout.constant_bindings_hash;

        if ( params->debug_name[0] ) {
            std_str_copy_static_m ( xg_vk_pipeline->common.debug_name, params->debug_name );
        }

        for ( uint32_t i = 0; i < xg_shading_stage_count_m; ++i ) {
            xg_vk_pipeline->common.vk_shader_handles[i] = vk_shader_handles[i];
        }

        xg_vk_pipeline->common.vk_handle = pipeline;
        xg_vk_pipeline->common.vk_layout_handle = vk_pipeline_layout;
        xg_vk_pipeline->common.hash = pipeline_hash;
        xg_vk_pipeline->common.device_handle = device_handle;
        xg_vk_pipeline->common.reference_count = 1;
        xg_vk_pipeline->common.type = xg_pipeline_raytrace_m;

        std_verify_m ( std_hash_map_insert ( &xg_vk_pipeline_state->raytrace_pipelines_map, pipeline_hash, xg_vk_pipeline_handle ) );
    }

    xg_vk_pipeline_handle = xg_vk_pipeline_handle_tag_as_raytrace_m ( xg_vk_pipeline_handle );

    return xg_vk_pipeline_handle;
#else
    std_unused_m ( device_handle );
    std_unused_m ( params );
    return xg_null_handle_m;
#endif
}

void xg_vk_raytrace_pipeline_destroy ( xg_raytrace_pipeline_state_h pipeline_handle ) {
#if xg_enable_raytracing_m
    xg_vk_raytrace_pipeline_t* pipeline = xg_vk_raytrace_pipeline_edit ( pipeline_handle );

    if ( --pipeline->common.reference_count > 0 ) {
        return;
    }

    const xg_vk_device_t* device = xg_vk_device_get ( pipeline->common.device_handle );

    for ( uint32_t i = 0; i < xg_shading_stage_count_m; ++i ) {
        if ( pipeline->common.vk_shader_handles[i] != VK_NULL_HANDLE ) {
            vkDestroyShaderModule ( device->vk_handle, pipeline->common.vk_shader_handles[i], xg_vk_cpu_allocator() );
        }
    }
    vkDestroyPipeline ( device->vk_handle, pipeline->common.vk_handle, xg_vk_cpu_allocator() );
    vkDestroyPipelineLayout ( device->vk_handle, pipeline->common.vk_layout_handle, xg_vk_cpu_allocator() );

    //xg_buffer_destroy ( pipeline->sbt_buffer );

    //for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
    //    xg_resource_bindings_layout_h layout_handle = pipeline->common.resource_layouts[i];
    //    xg_vk_pipeline_resource_bindings_layout_destroy ( layout_handle );
    //}

    std_virtual_heap_free ( pipeline->sbt_handle_buffer );

    std_list_push ( &xg_vk_pipeline_state->raytrace_pipelines_freelist, pipeline );
    std_verify_m ( std_hash_map_remove_hash ( &xg_vk_pipeline_state->raytrace_pipelines_map, pipeline->common.hash ) );
#else
    std_unused_m ( pipeline_handle );
#endif
}

#if 1
xg_compute_pipeline_state_h xg_vk_compute_pipeline_create ( xg_device_h device_handle, const xg_compute_pipeline_params_t* params ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    char state_buffer[sizeof ( params->state ) + sizeof ( uint64_t ) * xg_shader_binding_set_count_m];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    std_stack_write_m ( &hash_allocator, &params->state.compute_shader.hash );

    // Resources layout
    for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
        xg_vk_resource_bindings_layout_t* layout = xg_vk_pipeline_resource_bindings_layout_get ( params->resource_layouts[i] );
        std_stack_write_m ( &hash_allocator, &layout->hash );
    }

    // Pipeline
    uint64_t pipeline_hash = std_hash_block_64_m ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );
    uint64_t* pipeline_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->compute_pipelines_map, pipeline_hash );
    xg_compute_pipeline_state_h xg_vk_pipeline_handle;

    if ( pipeline_lookup ) {
        xg_vk_pipeline_handle = *pipeline_lookup;
        xg_vk_compute_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->compute_pipelines_array[xg_vk_pipeline_handle];
        xg_vk_pipeline->common.reference_count += 1;
    } else {
        // Compute shader
        VkShaderModule shader;
        VkPipelineShaderStageCreateInfo shader_info;
        {
            VkShaderModuleCreateInfo module_info;

            module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            module_info.pNext = NULL;
            module_info.flags = 0;
            module_info.pCode = ( const uint32_t* ) params->state.compute_shader.buffer.base;
            module_info.codeSize = params->state.compute_shader.buffer.size;
            vkCreateShaderModule ( device->vk_handle, &module_info, xg_vk_cpu_allocator(), &shader );

            if ( params->debug_name[0] ) {
                char module_name[xg_debug_name_size_m] = {};
                std_stack_t stack = std_static_stack_m ( module_name );
                std_stack_string_append ( &stack, params->debug_name );
                std_stack_string_append ( &stack, "-cs" );

                VkDebugUtilsObjectNameInfoEXT debug_name_info;
                debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                debug_name_info.pNext = NULL;
                debug_name_info.objectType = VK_OBJECT_TYPE_SHADER_MODULE ;
                debug_name_info.objectHandle = ( uint64_t ) shader;
                debug_name_info.pObjectName = module_name;
                xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
            }

            shader_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_info.pNext = NULL;
            shader_info.flags = 0;
            shader_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            shader_info.module = shader;
            shader_info.pName = "main";
            shader_info.pSpecializationInfo = NULL;
        }

        // Pipeline layout
        VkPipelineLayout vk_pipeline_layout = xg_vk_pipeline_create_pipeline_layout_vk ( device_handle, params->debug_name, params->resource_layouts, &params->constant_layout );

        // Pipeline
        VkPipeline pipeline;
        {
            VkComputePipelineCreateInfo info = {
                .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .stage = shader_info,
                .layout = vk_pipeline_layout,
                .basePipelineHandle = VK_NULL_HANDLE,
                .basePipelineIndex = 0,
            };
            VkResult result = vkCreateComputePipelines ( device->vk_handle, VK_NULL_HANDLE, 1, &info, xg_vk_cpu_allocator(), &pipeline );
            std_verify_m ( result == VK_SUCCESS );

            if ( params->debug_name[0] ) {
                VkDebugUtilsObjectNameInfoEXT debug_name_info = {
                    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                    .pNext = NULL,
                    .objectType = VK_OBJECT_TYPE_PIPELINE,
                    .objectHandle = ( uint64_t ) pipeline,
                    .pObjectName = params->debug_name,
                };
                xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
            }
        }

        xg_vk_compute_pipeline_t* xg_vk_pipeline = std_list_pop_m ( &xg_vk_pipeline_state->compute_pipelines_freelist );
        xg_vk_pipeline_handle = ( xg_compute_pipeline_state_h ) ( xg_vk_pipeline - xg_vk_pipeline_state->compute_pipelines_array );

        // store compute state
        xg_vk_pipeline->state = params->state;

        // store common state
        for ( size_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
            xg_vk_pipeline->common.resource_layouts[i] = params->resource_layouts[i];
        }

        //xg_vk_pipeline->common.push_constants_hash = bindings_layout.constant_bindings_hash;

        if ( params->debug_name[0] ) {
            std_str_copy_static_m ( xg_vk_pipeline->common.debug_name, params->debug_name );
        }

        for ( uint32_t i = 0; i < xg_shading_stage_count_m; ++i ) {
            xg_vk_pipeline->common.vk_shader_handles[i] = VK_NULL_HANDLE;
        }

        xg_vk_pipeline->common.vk_handle = pipeline;
        xg_vk_pipeline->common.vk_layout_handle = vk_pipeline_layout;
        xg_vk_pipeline->common.vk_shader_handles[xg_shading_stage_compute_m] = shader;
        xg_vk_pipeline->common.hash = pipeline_hash;
        xg_vk_pipeline->common.device_handle = device_handle;
        xg_vk_pipeline->common.reference_count = 1;
        xg_vk_pipeline->common.type = xg_pipeline_compute_m;

        std_verify_m ( std_hash_map_insert ( &xg_vk_pipeline_state->compute_pipelines_map, pipeline_hash, xg_vk_pipeline_handle ) );
    }

    xg_vk_pipeline_handle = xg_vk_pipeline_handle_tag_as_compute_m ( xg_vk_pipeline_handle );

    return xg_vk_pipeline_handle;
}
#endif

static int xg_vk_pipeline_render_target_layout_cmp ( const void* a, const void* b, const void* arg ) {
    std_unused_m ( arg );
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

static uint64_t xg_vk_renderpass_params_hash ( const xg_render_textures_layout_t* render_textures_layout ) {
    bool use_depth_stencil = render_textures_layout->depth_stencil.format != xg_format_undefined_m;

    char state_buffer[sizeof ( *render_textures_layout )];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    // Hash format and spp of every render target. It identifies the renderpass/framebuffer and everything else is unused/left default.
    // Every pipeline will have a pointer to its shared renderpass. This way at render time on every pso change
    // it's enough to get the pso's renderpass, and begin a new renderpass if it's different from the previous.
    // Memset to zero and copy each field to guarantee padding value
    std_stack_write_m ( &hash_allocator, &use_depth_stencil );

    if ( use_depth_stencil ) {
        std_stack_write_m ( &hash_allocator, &render_textures_layout->depth_stencil.format );
        std_stack_write_m ( &hash_allocator, &render_textures_layout->depth_stencil.samples_per_pixel );
    }

    std_stack_write_m ( &hash_allocator, &render_textures_layout->render_targets_count );

    if ( render_textures_layout->render_targets_count ) {
        std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_render_target_layout_t ) );
        std_auto_m sorted_render_targets = std_stack_alloc_array_m ( &hash_allocator, xg_render_target_layout_t, render_textures_layout->render_targets_count );
        std_mem_zero_array_m ( sorted_render_targets, render_textures_layout->render_targets_count );
        std_sort_insertion_copy ( sorted_render_targets, render_textures_layout->render_targets, sizeof ( xg_render_target_layout_t ), render_textures_layout->render_targets_count, xg_vk_pipeline_render_target_layout_cmp, NULL );

        for ( size_t i = 0; i < render_textures_layout->render_targets_count; ++i ) {
            std_stack_write_m ( &hash_allocator, &sorted_render_targets[i].format );
            std_stack_write_m ( &hash_allocator, &sorted_render_targets[i].samples_per_pixel );
        }
    }

    uint64_t hash = std_hash_block_64_m ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );
    return hash;
}

xg_graphics_pipeline_state_h xg_vk_graphics_pipeline_create ( xg_device_h device_handle, const xg_graphics_pipeline_params_t* params ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    char state_buffer[sizeof ( params->state ) + sizeof ( uint64_t ) * xg_shader_binding_set_count_m];
    std_stack_t hash_allocator = std_static_stack_m ( state_buffer );

    // Shaders
    std_stack_write_m ( &hash_allocator, &params->state.vertex_shader.hash );
    std_stack_write_m ( &hash_allocator, &params->state.fragment_shader.hash );

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
        std_stack_write_m ( &hash_allocator, &params->state.input_layout.stream_count );

        if ( params->state.input_layout.stream_count ) {
            std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_vertex_stream_t ) );
            std_auto_m sorted_input_streams = std_stack_alloc_array_m ( &hash_allocator, xg_vertex_stream_t, params->state.input_layout.stream_count );
            std_mem_zero_array_m ( sorted_input_streams, params->state.input_layout.stream_count );
            std_sort_insertion_copy ( sorted_input_streams, params->state.input_layout.streams, sizeof ( xg_vertex_stream_t ), params->state.input_layout.stream_count, xg_vk_pipeline_vertex_stream_cmp, NULL );

            for ( size_t i = 0; i < params->state.input_layout.stream_count; ++i ) {
                xg_vertex_attribute_t tmp;
                std_mem_zero_m ( &tmp );
                std_sort_insertion ( sorted_input_streams[i].attributes, sizeof ( xg_vertex_attribute_t ), sorted_input_streams[i].attribute_count, xg_vk_pipeline_vertex_attribute_cmp, NULL, &tmp );
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
    bool force_dynamic_scissor = false;
    {
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.pNext = NULL;
        viewport.flags = 0;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        viewport.pViewports = NULL;
        viewport.pScissors = NULL;

        if ( params->state.dynamic_state & xg_graphics_pipeline_dynamic_state_bit_viewport_m ) {
            uint32_t u32_0 = 0;
            float f32_0 = 0;

            std_stack_write_m ( &hash_allocator, &u32_0 );
            std_stack_write_m ( &hash_allocator, &u32_0 );
            std_stack_write_m ( &hash_allocator, &u32_0 );
            std_stack_write_m ( &hash_allocator, &u32_0 );
            std_stack_write_m ( &hash_allocator, &f32_0 );
            std_stack_write_m ( &hash_allocator, &f32_0 );
        } else {
            std_mem_zero_m ( &v );

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
            viewport.pViewports = &v;

            std_stack_write_m ( &hash_allocator, &params->state.viewport_state.x );
            std_stack_write_m ( &hash_allocator, &params->state.viewport_state.y );
            std_stack_write_m ( &hash_allocator, &params->state.viewport_state.width );
            std_stack_write_m ( &hash_allocator, &params->state.viewport_state.height );
            std_stack_write_m ( &hash_allocator, &params->state.viewport_state.min_depth );
            std_stack_write_m ( &hash_allocator, &params->state.viewport_state.max_depth );
        }

        force_dynamic_scissor = params->state.scissor_state.width == xi_scissor_width_full_m && params->state.scissor_state.height == xi_scissor_height_full_m;
        if ( params->state.dynamic_state & xg_graphics_pipeline_dynamic_state_bit_scissor_m || force_dynamic_scissor ) {
            int32_t i32_0 = 0;
            uint32_t u32_0 = 0;

            std_stack_write_m ( &hash_allocator, &i32_0 );
            std_stack_write_m ( &hash_allocator, &i32_0 );
            std_stack_write_m ( &hash_allocator, &u32_0 );
            std_stack_write_m ( &hash_allocator, &u32_0 );
        } else {
            std_mem_zero_m ( &s );

            uint32_t width = params->state.scissor_state.width;
            uint32_t height = params->state.scissor_state.height;

            if ( width == xi_scissor_width_full_m ) {
                width = params->state.viewport_state.width;
            }

            if ( height == xi_scissor_height_full_m ) {
                height = params->state.viewport_state.height;
            }

            s.offset.x = 0;
            s.offset.y = 0;
            s.extent.width = width;
            s.extent.height = height;
            viewport.pScissors = &s;

            std_stack_write_m ( &hash_allocator, &params->state.scissor_state.x );
            std_stack_write_m ( &hash_allocator, &params->state.scissor_state.y );
            std_stack_write_m ( &hash_allocator, &width );
            std_stack_write_m ( &hash_allocator, &height );
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

        std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.disable_rasterization );

        if ( params->state.rasterizer_state.disable_rasterization ) {
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.cull_mode );
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.frontface_winding_mode );
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.polygon_mode );
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.line_width );
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.enable_depth_clamp );

            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.enable );

            if ( params->state.rasterizer_state.depth_bias_state.enable ) {
                std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.clamp );
                std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.const_factor );
                std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.depth_bias_state.slope_factor );
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
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.antialiasing_state.sample_count );
            std_stack_write_m ( &hash_allocator, &params->state.rasterizer_state.antialiasing_state.mode );
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

        std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.depth.enable_test );

        if ( params->state.depth_stencil_state.depth.enable_test ) {
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.depth.enable_write );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.depth.compare_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.depth.enable_bound_test );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.depth.min_bound );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.depth.max_bound );
        }

        std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.enable_test );

        if ( params->state.depth_stencil_state.stencil.enable_test ) {
            // TODO
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.stencil_pass_depth_fail_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.stencil_fail_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.stencil_depth_pass_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.compare_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.compare_mask );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.write_mask );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.front_face_op.reference );

            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.stencil_pass_depth_fail_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.stencil_fail_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.stencil_depth_pass_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.compare_op );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.compare_mask );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.write_mask );
            std_stack_write_m ( &hash_allocator, &params->state.depth_stencil_state.stencil.back_face_op.reference );
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
        std_stack_write_m ( &hash_allocator, &params->state.blend_state.render_targets_count );

        if ( params->state.blend_state.render_targets_count ) {
            std_stack_align_zero ( &hash_allocator, std_alignof_m ( xg_render_target_blend_state_t ) );
            std_auto_m sorted_render_targets = std_stack_alloc_array_m ( &hash_allocator, xg_render_target_blend_state_t, params->state.blend_state.render_targets_count );
            std_mem_zero_array_m ( sorted_render_targets, params->state.blend_state.render_targets_count );

            std_sort_insertion_copy ( sorted_render_targets, params->state.blend_state.render_targets, sizeof ( xg_render_target_blend_state_t ), params->state.blend_state.render_targets_count, xg_vk_pipeline_render_target_blend_state_cmp, NULL );

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

        std_stack_write_m ( &hash_allocator, &params->state.blend_state.enable_blend_logic_op );

        if ( params->state.blend_state.enable_blend_logic_op ) {
            std_stack_write_m ( &hash_allocator, &params->state.blend_state.blend_logic_op );
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

        if ( params->state.dynamic_state & xg_graphics_pipeline_dynamic_state_bit_viewport_m ) {
            states[states_count++] = VK_DYNAMIC_STATE_VIEWPORT;
        }

        if ( params->state.dynamic_state & xg_graphics_pipeline_dynamic_state_bit_scissor_m || force_dynamic_scissor ) {
            states[states_count++] = VK_DYNAMIC_STATE_SCISSOR;
        }

        dynamic_state.dynamicStateCount = states_count;
        dynamic_state.pDynamicStates = states;
    }

    // Renderpass
    uint64_t renderpass_hash = xg_vk_renderpass_params_hash ( &params->render_textures_layout );
    std_stack_write_m ( &hash_allocator, &renderpass_hash );

    // Resources layout
    for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
        xg_vk_resource_bindings_layout_t* layout = xg_vk_pipeline_resource_bindings_layout_get ( params->resource_layouts[i] );
        std_stack_write_m ( &hash_allocator, &layout->hash );
    }

    // Pipeline
    // TODO try to batch vkCreateGraphicsPipelines calls
    uint64_t pipeline_hash = std_hash_block_64_m ( hash_allocator.begin, hash_allocator.top - hash_allocator.begin );

    uint64_t* pipeline_lookup = std_hash_map_lookup ( &xg_vk_pipeline_state->graphics_pipelines_map, pipeline_hash );

    xg_graphics_pipeline_state_h xg_vk_pipeline_handle;

    if ( pipeline_lookup == NULL ) {
        // Shaders
        VkShaderModule vk_shader_handles[xg_shading_stage_count_m] = { [0 ... xg_shading_stage_count_m-1] = VK_NULL_HANDLE };
        VkPipelineShaderStageCreateInfo shader_info[xg_shading_stage_count_m];
        size_t shader_count = 0;
        {
            VkShaderModuleCreateInfo module_info[xg_shading_stage_count_m];

            if ( params->state.vertex_shader.enable ) {
                module_info[shader_count].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                module_info[shader_count].pNext = NULL;
                module_info[shader_count].flags = 0;
                module_info[shader_count].pCode = ( const uint32_t* ) params->state.vertex_shader.buffer.base;
                module_info[shader_count].codeSize = params->state.vertex_shader.buffer.size;
                vkCreateShaderModule ( device->vk_handle, &module_info[shader_count], xg_vk_cpu_allocator(), &vk_shader_handles[shader_count] );

                shader_info[shader_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_info[shader_count].pNext = NULL;
                shader_info[shader_count].flags = 0;
                shader_info[shader_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
                shader_info[shader_count].module = vk_shader_handles[shader_count];
                shader_info[shader_count].pName = "main";
                shader_info[shader_count].pSpecializationInfo = NULL;

                if ( params->debug_name[0] ) {
                    char module_name[xg_debug_name_size_m] = {};
                    std_stack_t stack = std_static_stack_m ( module_name );
                    std_stack_string_append ( &stack, params->debug_name );
                    std_stack_string_append ( &stack, "-vs" );

                    VkDebugUtilsObjectNameInfoEXT debug_name_info;
                    debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                    debug_name_info.pNext = NULL;
                    debug_name_info.objectType = VK_OBJECT_TYPE_SHADER_MODULE ;
                    debug_name_info.objectHandle = ( uint64_t ) vk_shader_handles[shader_count];
                    debug_name_info.pObjectName = module_name;
                    xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
                }

                ++shader_count;
            }

            if ( params->state.fragment_shader.enable ) {
                module_info[shader_count].sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                module_info[shader_count].pNext = NULL;
                module_info[shader_count].flags = 0;
                module_info[shader_count].pCode = ( const uint32_t* ) params->state.fragment_shader.buffer.base;
                module_info[shader_count].codeSize = params->state.fragment_shader.buffer.size;
                vkCreateShaderModule ( device->vk_handle, &module_info[shader_count], xg_vk_cpu_allocator(), &vk_shader_handles[shader_count] );

                shader_info[shader_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                shader_info[shader_count].pNext = NULL;
                shader_info[shader_count].flags = 0;
                shader_info[shader_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                shader_info[shader_count].module = vk_shader_handles[shader_count];
                shader_info[shader_count].pName = "main";
                shader_info[shader_count].pSpecializationInfo = NULL;

                if ( params->debug_name[0] ) {
                    char module_name[xg_debug_name_size_m] = {};
                    std_stack_t stack = std_static_stack_m ( module_name );
                    std_stack_string_append ( &stack, params->debug_name );
                    std_stack_string_append ( &stack, "-fs" );

                    VkDebugUtilsObjectNameInfoEXT debug_name_info;
                    debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                    debug_name_info.pNext = NULL;
                    debug_name_info.objectType = VK_OBJECT_TYPE_SHADER_MODULE ;
                    debug_name_info.objectHandle = ( uint64_t ) vk_shader_handles[shader_count];
                    debug_name_info.pObjectName = module_name;
                    xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
                }
                
                ++shader_count;
            }
        }

        // Pipeline layout
        // TODO hash this
        VkPipelineLayout vk_pipeline_layout = xg_vk_pipeline_create_pipeline_layout_vk ( device_handle, params->debug_name, params->resource_layouts, &params->constant_layout );
        VkRenderPass vk_renderpass = xg_vk_renderpass_create_vk ( device_handle, &params->render_textures_layout, params->debug_name );

        // Pipeline
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
            info.layout = vk_pipeline_layout;
            info.renderPass = vk_renderpass;
            info.subpass = 0;
            info.basePipelineHandle = VK_NULL_HANDLE;
            info.basePipelineIndex = 0;
            VkResult result = vkCreateGraphicsPipelines ( device->vk_handle, VK_NULL_HANDLE, 1, &info, xg_vk_cpu_allocator(), &pipeline );
            std_verify_m ( result == VK_SUCCESS );

            if ( params->debug_name[0] ) {
                VkDebugUtilsObjectNameInfoEXT debug_name_info;
                debug_name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
                debug_name_info.pNext = NULL;
                debug_name_info.objectType = VK_OBJECT_TYPE_PIPELINE;
                debug_name_info.objectHandle = ( uint64_t ) pipeline;
                debug_name_info.pObjectName = params->debug_name;
                xg_vk_device_ext_api ( device_handle )->set_debug_name ( device->vk_handle, &debug_name_info );
            }
        }

        xg_vk_graphics_pipeline_t* xg_vk_pipeline = std_list_pop_m ( &xg_vk_pipeline_state->graphics_pipelines_freelist );
        xg_vk_pipeline_handle = ( xg_graphics_pipeline_state_h ) ( xg_vk_pipeline - xg_vk_pipeline_state->graphics_pipelines_array );

        // store graphics pipeline state
        xg_vk_pipeline->state = params->state;
        xg_vk_pipeline->vk_renderpass = vk_renderpass;

        if ( force_dynamic_scissor ) {
            xg_vk_pipeline->state.dynamic_state |= xg_graphics_pipeline_dynamic_state_bit_scissor_m;
        }

        // store common pipeline state
        for ( size_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
            xg_vk_pipeline->common.resource_layouts[i] = params->resource_layouts[i];
        }

        //xg_vk_pipeline->common.push_constants_hash = bindings_layout.constant_bindings_hash;

        if ( params->debug_name[0] ) {
            std_str_copy ( xg_vk_pipeline->common.debug_name, xg_debug_name_size_m, params->debug_name );
        }

        for ( uint32_t i = 0; i < xg_shading_stage_count_m; ++i ) {
            xg_vk_pipeline->common.vk_shader_handles[i] = vk_shader_handles[i];
        }

        xg_vk_pipeline->common.vk_handle = pipeline;
        xg_vk_pipeline->common.vk_layout_handle = vk_pipeline_layout;
        xg_vk_pipeline->common.hash = pipeline_hash;
        xg_vk_pipeline->common.device_handle = device_handle;
        xg_vk_pipeline->common.reference_count = 1;
        xg_vk_pipeline->common.type = xg_pipeline_graphics_m;

        std_hash_map_insert ( &xg_vk_pipeline_state->graphics_pipelines_map, pipeline_hash, xg_vk_pipeline_handle );
    } else {
        xg_vk_pipeline_handle = *pipeline_lookup;
        xg_vk_graphics_pipeline_t* xg_vk_pipeline = &xg_vk_pipeline_state->graphics_pipelines_array[xg_vk_pipeline_handle];
        xg_vk_pipeline->common.reference_count += 1;
    }

    xg_vk_pipeline_handle = xg_vk_pipeline_handle_tag_as_graphics_m ( xg_vk_pipeline_handle );
    return xg_vk_pipeline_handle;
}

void xg_vk_graphics_pipeline_destroy ( xg_graphics_pipeline_state_h pipeline_handle ) {
    xg_vk_graphics_pipeline_t* pipeline = xg_vk_graphics_pipeline_edit ( pipeline_handle );

    if ( --pipeline->common.reference_count > 0 ) {
        return;
    }

    // TODO also ref count the framebuffer and delete it here if refcount == 0

    const xg_vk_device_t* device = xg_vk_device_get ( pipeline->common.device_handle );

    for ( uint32_t i = 0; i < xg_shading_stage_count_m; ++i ) {
        if ( pipeline->common.vk_shader_handles[i] != VK_NULL_HANDLE ) {
            vkDestroyShaderModule ( device->vk_handle, pipeline->common.vk_shader_handles[i], xg_vk_cpu_allocator() );
        }
    }

    vkDestroyPipeline ( device->vk_handle, pipeline->common.vk_handle, xg_vk_cpu_allocator() );
    vkDestroyPipelineLayout ( device->vk_handle, pipeline->common.vk_layout_handle, xg_vk_cpu_allocator() );
    vkDestroyRenderPass ( device->vk_handle, pipeline->vk_renderpass, xg_vk_cpu_allocator() );

    //for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
    //    xg_resource_bindings_layout_h layout_handle = pipeline->common.resource_layouts[i];
    //    xg_vk_pipeline_resource_bindings_layout_destroy ( layout_handle );
    //}

    std_list_push ( &xg_vk_pipeline_state->graphics_pipelines_freelist, pipeline );
    std_verify_m ( std_hash_map_remove_hash ( &xg_vk_pipeline_state->graphics_pipelines_map, pipeline->common.hash ) );
}

xg_renderpass_h xg_vk_renderpass_create ( const xg_renderpass_params_t* params ) {
    xg_vk_renderpass_t* renderpass = std_list_pop ( &xg_vk_pipeline_state->renderpasses_freelist );

    VkRenderPass vk_renderpass = xg_vk_renderpass_create_vk ( params->device, &params->render_textures_layout, params->debug_name );
    VkFramebuffer vk_framebuffer = xg_vk_framebuffer_create_vk ( params->device, vk_renderpass, &params->render_textures_layout, &params->render_textures_usage, params->resolution_x, params->resolution_y, params->debug_name );
    
    renderpass->vk_handle = vk_renderpass;
    renderpass->vk_framebuffer_handle = vk_framebuffer;
    renderpass->params = *params;

    xg_renderpass_h renderpass_handle = renderpass - xg_vk_pipeline_state->renderpasses_array;
    std_bitset_set ( xg_vk_pipeline_state->renderpasses_bitset, renderpass_handle );
    return renderpass_handle;
}

void xg_vk_renderpass_destroy ( xg_renderpass_h renderpass_handle ) {
    xg_vk_renderpass_t* renderpass = &xg_vk_pipeline_state->renderpasses_array[renderpass_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( renderpass->params.device );
    vkDestroyFramebuffer ( device->vk_handle, renderpass->vk_framebuffer_handle, xg_vk_cpu_allocator() );
    vkDestroyRenderPass ( device->vk_handle, renderpass->vk_handle, xg_vk_cpu_allocator() );
    std_list_push ( &xg_vk_pipeline_state->renderpasses_freelist, renderpass );
    std_bitset_clear ( xg_vk_pipeline_state->renderpasses_bitset, renderpass_handle );
}

void xg_vk_compute_pipeline_destroy ( xg_compute_pipeline_state_h pipeline_handle ) {
    xg_vk_compute_pipeline_t* pipeline = xg_vk_compute_pipeline_edit ( pipeline_handle );

    if ( --pipeline->common.reference_count > 0 ) {
        return;
    }

    // TODO also ref count the framebuffer and delete it here if refcount == 0

    const xg_vk_device_t* device = xg_vk_device_get ( pipeline->common.device_handle );

    vkDestroyShaderModule ( device->vk_handle, pipeline->common.vk_shader_handles[xg_shading_stage_compute_m], xg_vk_cpu_allocator() );
    
    vkDestroyPipeline ( device->vk_handle, pipeline->common.vk_handle, xg_vk_cpu_allocator() );
    vkDestroyPipelineLayout ( device->vk_handle, pipeline->common.vk_layout_handle, xg_vk_cpu_allocator() );

    //for ( uint32_t i = 0; i < xg_shader_binding_set_count_m; ++i ) {
    //    xg_resource_bindings_layout_h layout_handle = pipeline->common.resource_layouts[i];
    //    xg_vk_pipeline_resource_bindings_layout_destroy ( layout_handle );
    //}

    std_list_push ( &xg_vk_pipeline_state->compute_pipelines_freelist, pipeline );
    std_verify_m ( std_hash_map_remove_hash ( &xg_vk_pipeline_state->compute_pipelines_map, pipeline->common.hash ) );
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
#if xg_enable_raytracing_m
        info.poolSizeCount = xg_resource_binding_count_m;
#else
        info.poolSizeCount = xg_resource_binding_count_m - 1;
#endif
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
#if xg_enable_raytracing_m
#if xg_vk_enable_nv_raytracing_ext_m
        sizes[xg_resource_binding_raytrace_world_m].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
#else
        sizes[xg_resource_binding_raytrace_world_m].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
#endif
        sizes[xg_resource_binding_raytrace_world_m].descriptorCount = xg_vk_max_raytrace_world_per_descriptor_pool_m;
#endif
        info.pPoolSizes = sizes;
        VkResult result = vkCreateDescriptorPool ( device->vk_handle, &info, xg_vk_cpu_allocator(), &context->vk_desc_pool );
        std_verify_m ( result == VK_SUCCESS );
    }

    context->groups_freelist = std_static_freelist_m ( context->groups_array );
    std_mutex_init ( &context->groups_mutex );
}

void xg_vk_pipeline_deactivate_device ( xg_device_h device_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    vkDestroyDescriptorPool ( device->vk_handle, xg_vk_pipeline_state->device_contexts[device_idx].vk_desc_pool, xg_vk_cpu_allocator() );
}

xg_resource_bindings_h xg_vk_pipeline_create_resource_bindings ( xg_resource_bindings_layout_h layout_handle ) {
    xg_vk_resource_bindings_layout_t* layout = xg_vk_pipeline_resource_bindings_layout_get ( layout_handle );
    xg_device_h device_handle = layout->params.device;

    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];

    VkDescriptorSet vk_set;
    VkDescriptorSetAllocateInfo vk_set_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .pNext = NULL,
        .descriptorPool = xg_vk_pipeline_state->device_contexts[device_idx].vk_desc_pool,
        .descriptorSetCount = 1, // TODO batch
        .pSetLayouts = &layout->vk_handle,
    };
    VkResult set_alloc_result = vkAllocateDescriptorSets ( device->vk_handle, &vk_set_alloc_info, &vk_set );
    std_verify_m ( set_alloc_result == VK_SUCCESS, "Ran out of descriptor pool memory." );

    xg_vk_resource_bindings_t* group = std_list_pop_m ( &context->groups_freelist );
    group->vk_handle = vk_set;
    group->layout = layout_handle;
    xg_resource_bindings_h handle = ( xg_resource_bindings_h ) ( group - context->groups_array );
    return handle;
}

void xg_vk_pipeline_update_resource_bindings ( xg_device_h device_handle, xg_resource_bindings_h resource_bindings_handle, const xg_pipeline_resource_bindings_t* bindings ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_resource_bindings_t* resource_bindings = &xg_vk_pipeline_state->device_contexts[device_idx].groups_array[resource_bindings_handle];
    VkDescriptorSet vk_set = resource_bindings->vk_handle;
    xg_vk_resource_bindings_layout_t* resource_bindings_layout = xg_vk_pipeline_resource_bindings_layout_get ( resource_bindings->layout );

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
        const xg_buffer_resource_binding_t* binding = &bindings->buffers[i];
        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
        xg_resource_binding_layout_t* layout = &resource_bindings_layout->params.resources[binding_idx];
        xg_resource_binding_e binding_type = layout->type;
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
        write->dstBinding = binding_idx;
        write->dstArrayElement = 0; // TODO
        write->descriptorCount = 1;
        write->pBufferInfo = info;

        switch ( binding_type ) {
            case xg_resource_binding_buffer_uniform_m:
                write->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case xg_resource_binding_buffer_storage_m:
                write->descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;

            default:
                std_not_implemented_m();
        }
    }

    for ( uint32_t i = 0; i < texture_count; ++i ) {
        const xg_texture_resource_binding_t* binding = &bindings->textures[i];
        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
        xg_resource_binding_layout_t* layout = &resource_bindings_layout->params.resources[binding_idx];
        xg_resource_binding_e binding_type = layout->type;
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
        write->dstBinding = binding_idx;
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
        const xg_sampler_resource_binding_t* binding = &bindings->samplers[i];
        uint32_t binding_idx = resource_bindings_layout->shader_register_to_descriptor_idx[binding->shader_register];
        const xg_vk_sampler_t* sampler = xg_vk_sampler_get ( binding->sampler );

        VkDescriptorImageInfo* info = &vk_image_info[image_info_count++];
        info->sampler = sampler->vk_handle;
        info->imageView = VK_NULL_HANDLE;

        VkWriteDescriptorSet* write = &writes[write_count++];
        write->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write->pNext = NULL;
        write->dstSet = vk_set;
        write->dstBinding = binding_idx;
        write->dstArrayElement = 0; // TODO
        write->descriptorCount = 1;
        write->pImageInfo = info;
        write->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    }

    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    vkUpdateDescriptorSets ( device->vk_handle, write_count, writes, 0, NULL );
}

void xg_vk_pipeline_destroy_resource_bindings ( xg_device_h device_handle, xg_resource_bindings_h group_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];

    xg_vk_resource_bindings_t* group = &context->groups_array[group_handle];
    VkDescriptorSet vk_set = group->vk_handle;

    VkResult result = vkFreeDescriptorSets ( device->vk_handle, context->vk_desc_pool, 1, &vk_set );
    std_verify_m ( result == VK_SUCCESS );
    std_list_push ( &context->groups_freelist, group );
}

const xg_vk_resource_bindings_t* xg_vk_pipeline_resource_group_get ( xg_device_h device_handle, xg_resource_bindings_h group_handle ) {
    uint64_t device_idx = xg_vk_device_get_idx ( device_handle );
    xg_vk_pipeline_device_context_t* context = &xg_vk_pipeline_state->device_contexts[device_idx];
    xg_vk_resource_bindings_t* group = &context->groups_array[group_handle];
    return group;
}

void xg_vk_pipeline_get_info ( xg_pipeline_info_t* info, xg_pipeline_state_h pipeline_handle ) {
    xg_vk_pipeline_common_t* common_pipeline = xg_vk_common_pipeline_get ( pipeline_handle );
    info->type = common_pipeline->type;
    info->debug_name = common_pipeline->debug_name;
}
