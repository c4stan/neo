#include "xg_vk_enum.h"

VkFormat xg_format_to_vk ( xg_format_e format ) {
    if ( format < 0 || format > xg_format_count_m ) {
        std_log_error_m ( "Malformed format enum" );
        return 0;
    }

    // The table is copied and renamed from the first piece of the official Vk format table
    return ( VkFormat ) format;
}

VkShaderStageFlags xg_shader_stage_to_vk ( xg_shading_stage_bit_e stage ) {
    VkShaderStageFlags flags = 0;

    if ( stage & xg_shading_stage_bit_vertex_m ) {
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    }

    if ( stage & xg_shading_stage_bit_fragment_m ) {
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    if ( stage & xg_shading_stage_bit_compute_m ) {
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }

    return flags;
}

VkCullModeFlags xg_cull_mode_to_vk ( xg_cull_mode_bit_e mode ) {
    VkCullModeFlags flags = 0;

    if ( mode & xg_cull_mode_bit_front_m ) {
        flags |= VK_CULL_MODE_FRONT_BIT;
    }

    if ( mode & xg_cull_mode_bit_back_m ) {
        flags |= VK_CULL_MODE_BACK_BIT;
    }

    return flags;
}

VkFrontFace xg_front_face_to_vk ( xg_winding_mode_e mode ) {
    switch ( mode ) {
        case xg_winding_clockwise_m:
            return VK_FRONT_FACE_CLOCKWISE;

        case xg_winding_counter_clockwise_m:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;

        default:
            std_log_error_m ( "Winding mode not supported" );
            return 0;
    }
}

VkPolygonMode xg_polygon_mode_to_vk ( xg_polygon_mode_e mode ) {
    switch ( mode ) {
        case xg_polygon_mode_point_m:
            return VK_POLYGON_MODE_POINT;

        case xg_polygon_mode_line_m:
            return VK_POLYGON_MODE_LINE;

        case xg_polygon_mode_fill_m:
            return VK_POLYGON_MODE_FILL;

        default:
            std_log_error_m ( "Polygon mode not supported" );
            return 0;
    }
}

VkSampleCountFlagBits xg_sample_count_to_vk ( xg_sample_count_e count ) {
    switch ( count ) {
        case xg_sample_count_1_m:
            return VK_SAMPLE_COUNT_1_BIT;

        case xg_sample_count_2_m:
            return VK_SAMPLE_COUNT_2_BIT;

        case xg_sample_count_4_m:
            return VK_SAMPLE_COUNT_4_BIT;

        case xg_sample_count_8_m:
            return VK_SAMPLE_COUNT_8_BIT;

        case xg_sample_count_16_m:
            return VK_SAMPLE_COUNT_16_BIT;

        case xg_sample_count_32_m:
            return VK_SAMPLE_COUNT_32_BIT;

        case xg_sample_count_64_m:
            return VK_SAMPLE_COUNT_64_BIT;

        default:
            std_log_error_m ( "Sample count not supported" );
            return VK_SAMPLE_COUNT_1_BIT;   // Default to 1 sample
    }
}

VkCompareOp xg_compare_op_to_vk ( xg_compare_op_e op ) {
    switch ( op ) {
        case xg_compare_op_always_m:
            return VK_COMPARE_OP_ALWAYS;

        case xg_compare_op_greater_m:
            return VK_COMPARE_OP_GREATER;

        case xg_compare_op_equal_m:
            return VK_COMPARE_OP_EQUAL;

        case xg_compare_op_greater_or_equal_m:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;

        case xg_compare_op_less_m:
            return VK_COMPARE_OP_LESS;

        case xg_compare_op_less_or_equal_m:
            return VK_COMPARE_OP_LESS_OR_EQUAL;

        case xg_compare_op_never_m:
            return VK_COMPARE_OP_NEVER;

        case xg_compare_op_not_equal_m:
            return VK_COMPARE_OP_NOT_EQUAL;

        default:
            std_log_error_m ( "Compare op not supported" );
            return 0;
    }
}

VkStencilOp xg_stencil_op_to_vk ( xg_stencil_op_e op ) {
    switch ( op ) {
        case xg_stencil_op_keep_m:
            return VK_STENCIL_OP_KEEP;

        case xg_stencil_op_zero_m:
            return VK_STENCIL_OP_ZERO;

        case xg_stencil_op_replace_m:
            return VK_STENCIL_OP_REPLACE;

        case xg_stencil_op_inc_and_clamp_m:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;

        case xg_stencil_op_dec_and_clamp_m:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;

        case xg_stencil_op_invert_m:
            return VK_STENCIL_OP_INVERT;

        case xg_stencil_op_inc_and_wrap_m:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;

        case xg_stencil_op_dec_and_wrap_m:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;

        default:
            std_log_error_m ( "Stencil op not supported" );
            return 0;
    }
}

VkBlendOp xg_blend_op_to_vk ( xg_blend_op_e op ) {
    switch ( op ) {
        case xg_blend_op_add_m:
            return VK_BLEND_OP_ADD;

        case xg_blend_op_max_m:
            return VK_BLEND_OP_MAX;

        case xg_blend_op_min_m:
            return VK_BLEND_OP_MIN;

        case xg_blend_op_reverse_subtract_m:
            return VK_BLEND_OP_REVERSE_SUBTRACT;

        case xg_blend_op_subtract_m:
            return VK_BLEND_OP_SUBTRACT;

        default:
            std_log_error_m ( "Blend op not supported" );
            return 0;
    }
}

VkBlendFactor xg_blend_factor_to_vk ( xg_blend_factor_e factor ) {
    switch ( factor ) {
        case xg_blend_factor_dst_alpha_m:
            return VK_BLEND_FACTOR_DST_ALPHA;

        case xg_blend_factor_dst_color_m:
            return VK_BLEND_FACTOR_DST_COLOR;

        case xg_blend_factor_one_m:
            return VK_BLEND_FACTOR_ONE;

        case xg_blend_factor_one_minus_dst_alpha_m:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;

        case xg_blend_factor_one_minus_dst_color_m:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;

        case xg_blend_factor_one_minus_src_alpha_m:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

        case xg_blend_factor_one_minus_src_color_m:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;

        case xg_blend_factor_src_alpha_m:
            return VK_BLEND_FACTOR_SRC_ALPHA;

        case xg_blend_factor_src_color_m:
            return VK_BLEND_FACTOR_SRC_COLOR;

        case xg_blend_factor_zero_m:
            return VK_BLEND_FACTOR_ZERO;

        default:
            std_log_error_m ( "Blend factor not supported" );
            return 0;
    }
}

VkDescriptorType xg_descriptor_type_to_vk ( xg_resource_binding_e type ) {
    switch ( type ) {
        case xg_resource_binding_sampler_m:
            return VK_DESCRIPTOR_TYPE_SAMPLER;

        case xg_resource_binding_texture_to_sample_m:
            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;

        //case xg_resource_binding_texture_and_sampler_m:
        //    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

        case xg_resource_binding_texture_storage_m:
            return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

        case xg_resource_binding_buffer_uniform_m:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

        case xg_resource_binding_buffer_storage_m:
            return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        case xg_resource_binding_buffer_texel_uniform_m:
            return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;

        case xg_resource_binding_buffer_texel_storage_m:
            return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

        //case xg_resource_binding_pipeline_output_m:
        //    return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

        default:
            std_log_error_m ( "Resource descriptor type not supported" );
            return 0;
    }
}

VkLogicOp xg_logic_op_to_vk ( xg_blend_logic_op_e op ) {
    switch ( op ) {
        case xg_blend_logic_op_clear_m:
            return VK_LOGIC_OP_CLEAR;

        case xg_blend_logic_op_and_m:
            return VK_LOGIC_OP_AND;

        case xg_blend_logic_op_and_reverse_m:
            return VK_LOGIC_OP_AND_REVERSE;

        case xg_blend_logic_op_copy_m:
            return VK_LOGIC_OP_COPY;

        case xg_blend_logic_op_and_inverted_m:
            return VK_LOGIC_OP_AND_INVERTED;

        case xg_blend_logic_op_no_op_m:
            return VK_LOGIC_OP_NO_OP;

        case xg_blend_logic_op_xor_m:
            return VK_LOGIC_OP_XOR;

        case xg_blend_logic_op_or_m:
            return VK_LOGIC_OP_OR;

        case xg_blend_logic_op_nor_m:
            return VK_LOGIC_OP_NOR;

        case xg_blend_logic_op_equivalent_m:
            return VK_LOGIC_OP_EQUIVALENT;

        case xg_blend_logic_op_invert_m:
            return VK_LOGIC_OP_INVERT;

        case xg_blend_logic_op_or_reverse_m:
            return VK_LOGIC_OP_OR_REVERSE;

        case xg_blend_logic_op_copy_inverted_m:
            return VK_LOGIC_OP_COPY_INVERTED;

        case xg_blend_logic_op_or_inverted_m:
            return VK_LOGIC_OP_OR_INVERTED;

        case xg_blend_logic_op_nand_m:
            return VK_LOGIC_OP_NAND;

        case xg_blend_logic_op_set_m:
            return VK_LOGIC_OP_SET;

        case xg_blend_logic_op_invalid_m:
            return -1;

        default:
            std_log_error_m ( "Logic operation not supported" );
            return 0;
    }
}

VkBufferUsageFlags xg_buffer_usage_to_vk ( xg_buffer_usage_bit_e usage ) {
    VkBufferUsageFlags flags = 0;

    if ( usage & xg_buffer_usage_bit_copy_dest_m ) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }

    if ( usage & xg_buffer_usage_bit_copy_source_m ) {
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }

    if ( usage & xg_buffer_usage_bit_index_buffer_m ) {
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }

    if ( usage & xg_buffer_usage_bit_uniform_m ) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }

    if ( usage & xg_buffer_usage_bit_storage_m ) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    if ( usage & xg_buffer_usage_bit_texel_uniform_m ) {
        flags |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
    }

    if ( usage & xg_buffer_usage_bit_texel_storage_m ) {
        flags |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    }

    if ( usage & xg_buffer_usage_bit_vertex_buffer_m ) {
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }

    if ( usage & xg_vk_buffer_usage_device_addressed_m ) {
        flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }

    if ( usage & xg_vk_buffer_usage_raytrace_geometry_m ) {
        flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    return flags;
}

VkImageUsageFlags xg_image_usage_to_vk ( xg_texture_usage_bit_e usage ) {
    VkImageUsageFlags flags = 0;

    if ( usage & xg_texture_usage_bit_copy_dest_m ) {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    if ( usage & xg_texture_usage_bit_copy_source_m ) {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    if ( usage & xg_texture_usage_bit_render_target_m ) {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if ( usage & xg_texture_usage_bit_depth_stencil_m ) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }

    if ( usage & xg_texture_usage_bit_resource_m ) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    if ( usage & xg_texture_usage_bit_storage_m ) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    return flags;
}

VkImageType xg_image_type_to_vk ( xg_texture_dimension_e dimension ) {
    switch ( dimension ) {
        case xg_texture_dimension_1d_m:
            return VK_IMAGE_TYPE_1D;

        case xg_texture_dimension_2d_m:
            return VK_IMAGE_TYPE_2D;

        case xg_texture_dimension_3d_m:
            return VK_IMAGE_TYPE_3D;

        default:
            std_log_error_m ( "Texture dimension not supported" );
            return 0;
    }
}

VkImageLayout xg_image_layout_to_vk ( xg_texture_layout_e layout ) {
    switch ( layout ) {
        case xg_texture_layout_host_initialize_m:
            return VK_IMAGE_LAYOUT_PREINITIALIZED;

        case xg_texture_layout_copy_dest_m:
            return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        case xg_texture_layout_copy_source_m:
            return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        case xg_texture_layout_undefined_m:
            return VK_IMAGE_LAYOUT_UNDEFINED;

        case xg_texture_layout_all_m:
        case xg_texture_layout_shader_write_m:
            return VK_IMAGE_LAYOUT_GENERAL;

        case xg_texture_layout_present_m:
            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        case xg_texture_layout_shader_read_m:
            return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        case xg_texture_layout_render_target_m:
            return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        case xg_texture_layout_depth_stencil_target_m:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        case xg_texture_layout_depth_stencil_read_m:
            return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        case xg_texture_layout_depth_read_stencil_target_m:
            return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL;

        case xg_texture_layout_depth_target_stencil_read_m:
            return VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
    }
}

VkColorSpaceKHR xg_color_space_to_vk ( xg_color_space_e color_space ) {
    switch ( color_space ) {
        case xg_colorspace_linear_m:
            return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;

        case xg_colorspace_srgb_m:
            return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

        case xg_colorspace_hdr_hlg_m:
            return VK_COLOR_SPACE_HDR10_HLG_EXT;

        case xg_colorspace_hdr_pq_m:
            return VK_COLOR_SPACE_HDR10_ST2084_EXT;

        default:
            std_log_error_m ( "Color space not supported" );
            return 0;
    }
}

VkPresentModeKHR xg_present_mode_to_vk ( xg_present_mode_e present_mode ) {
    switch ( present_mode ) {
        case xg_present_mode_immediate_m:
            return VK_PRESENT_MODE_IMMEDIATE_KHR;

        case xg_present_mode_mailbox_m:
            return VK_PRESENT_MODE_MAILBOX_KHR;

        case xg_present_mode_fifo_m:
            return VK_PRESENT_MODE_FIFO_KHR;

        case xg_present_mode_fifo_relaxed_m:
            return VK_PRESENT_MODE_FIFO_RELAXED_KHR;

        default:
            std_log_error_m ( "Present mode not supported" );
            return 0;
    }
}

VkAccessFlags xg_memory_access_to_vk ( xg_memory_access_bit_e memory_access ) {
    return ( VkAccessFlags ) memory_access;
}

VkPipelineStageFlags xg_pipeline_stage_to_vk ( xg_pipeline_stage_bit_e pipeline_stage ) {
    return ( VkPipelineStageFlags ) pipeline_stage;
}

// --------------------------------------------------------------------------------

xg_format_e xg_format_from_vk ( VkFormat format ) {
    return ( xg_format_e ) format;
}

xg_color_space_e xg_color_space_from_vk ( VkColorSpaceKHR colorspace ) {
    switch ( colorspace ) {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
            return xg_colorspace_srgb_m;

        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
            return xg_colorspace_linear_m;

        case VK_COLOR_SPACE_HDR10_HLG_EXT:
            return xg_colorspace_hdr_hlg_m;

        case VK_COLOR_SPACE_HDR10_ST2084_EXT:
            return xg_colorspace_hdr_pq_m;

        default:
            std_log_error_m ( "Color space not supported" );
            return 0;
    }
}

xg_present_mode_e xg_present_mode_from_vk ( VkPresentModeKHR mode ) {
    switch ( mode ) {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return xg_present_mode_immediate_m;

        case VK_PRESENT_MODE_MAILBOX_KHR:
            return xg_present_mode_mailbox_m;

        case VK_PRESENT_MODE_FIFO_KHR:
            return xg_present_mode_fifo_m;

        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return xg_present_mode_fifo_relaxed_m;

        default:
            std_log_error_m ( "Present mode not supported" );
            return 0;
    }
}

VkFilter xg_sampler_filter_to_vk ( xg_sampler_filter_e filter ) {
    switch ( filter ) {
        case xg_sampler_filter_point_m:
            return VK_FILTER_NEAREST;

        case xg_sampler_filter_linear_m:
            return VK_FILTER_LINEAR;

        default:
            std_log_error_m ( "Sampler filter not supported" );
            return 0;
    }
}

VkSamplerMipmapMode xg_sampler_mipmap_filter_to_vk ( xg_sampler_filter_e filter ) {
    switch ( filter ) {
        case xg_sampler_filter_point_m:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;

        case xg_sampler_filter_linear_m:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;

        default:
            std_log_error_m ( "Sampler mipmap filter not supported" );
            return 0;
    }
}

VkSamplerAddressMode xg_sampler_address_mode_to_vk ( xg_sampler_address_mode_e address_mode ) {
    switch ( address_mode ) {
        case xg_sampler_address_mode_wrap_m:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;

        case xg_sampler_address_mode_clamp_m:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        default:
            std_log_error_m ( "Sampler address mode not supported" );
            return 0;
    }
}

VkImageAspectFlags xg_texture_aspect_to_vk ( xg_texture_aspect_e aspect ) {
    switch ( aspect ) {
        case xg_texture_aspect_default_m:
            std_log_error_m ( "Default aspect depends on the texture, need to resolve externally" );
            return 0;

        case xg_texture_aspect_color_m:
            return VK_IMAGE_ASPECT_COLOR_BIT;

        case xg_texture_aspect_depth_m:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

        case xg_texture_aspect_stencil_m:
            return VK_IMAGE_ASPECT_STENCIL_BIT;

        default:
            std_log_error_m ( "Texture aspect not supported" );
            return 0;
    }
}

VkImageAspectFlags xg_texture_flags_to_vk_aspect ( xg_texture_flag_bit_e flags ) {
    VkImageAspectFlags result = 0; // VK_IMAGE_ASPECT_NONE

    if ( flags & xg_texture_flag_bit_render_target_texture_m ) {
        result |= VK_IMAGE_ASPECT_COLOR_BIT;
    }

    if ( flags & xg_texture_flag_bit_depth_texture_m ) {
        result |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }

    if ( flags & xg_texture_flag_bit_stencil_texture_m ) {
        result |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    return result;
}

xg_memory_flag_bit_e xg_memory_flags_from_vk ( VkMemoryPropertyFlags flags ) {
    xg_memory_flag_bit_e result = 0;

    if ( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) {
        result |= xg_memory_type_bit_device_m;
    }

    if ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
        result |= xg_memory_type_bit_mappable_m;
    }

    if ( flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) {
        result |= xg_memory_type_bit_coherent_m;
    }

    if ( flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) {
        result |= xg_memory_type_bit_cached_m;
    }

    return result;
}
