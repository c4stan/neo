#pragma once

#include "xg_vk.h"

VkFormat                    xg_format_to_vk ( xg_format_e format );
VkShaderStageFlags          xg_shader_stage_to_vk ( xg_shading_stage_b stage );
VkCullModeFlags             xg_cull_mode_to_vk ( xg_cull_mode_b mode );
VkFrontFace                 xg_front_face_to_vk ( xg_winding_mode_e mode );
VkPolygonMode               xg_polygon_mode_to_vk ( xg_polygon_mode_e mode );
VkSampleCountFlagBits       xg_sample_count_to_vk ( xg_sample_count_e count );
VkCompareOp                 xg_compare_op_to_vk ( xg_compare_op_e op );
VkStencilOp                 xg_stencil_op_to_vk ( xg_stencil_op_e op );
VkBlendOp                   xg_blend_op_to_vk ( xg_blend_op_e op );
VkBlendFactor               xg_blend_factor_to_vk ( xg_blend_factor_e factor );
VkDescriptorType            xg_descriptor_type_to_vk ( xg_resource_binding_e type );
VkLogicOp                   xg_logic_op_to_vk ( xg_blend_logic_op_e op );
VkBufferUsageFlags          xg_buffer_usage_to_vk ( xg_buffer_usage_f usage );
VkImageUsageFlags           xg_image_usage_to_vk ( xg_texture_usage_f usage );
VkImageType                 xg_image_type_to_vk ( xg_texture_dimension_e dimension );
VkImageLayout               xg_image_layout_to_vk ( xg_texture_layout_e layout );
VkColorSpaceKHR             xg_color_space_to_vk ( xg_color_space_e color_space );
VkPresentModeKHR            xg_present_mode_to_vk ( xg_present_mode_e present_mode );
VkAccessFlags               xg_memory_access_to_vk ( xg_memory_access_f memory_access );
VkPipelineStageFlags        xg_pipeline_stage_to_vk ( xg_pipeline_stage_f pipeline_stage );
VkFilter                    xg_sampler_filter_to_vk ( xg_sampler_filter_e filter );
VkSamplerMipmapMode         xg_sampler_mipmap_filter_to_vk ( xg_sampler_filter_e filter );
VkSamplerAddressMode        xg_sampler_address_mode_to_vk ( xg_sampler_address_mode_e address_mode );
VkImageAspectFlags          xg_texture_aspect_to_vk ( xg_texture_aspect_e aspect );
VkImageAspectFlags          xg_texture_flags_to_vk_aspect ( xg_texture_flags_b flags );

xg_format_e                 xg_format_from_vk ( VkFormat format );
xg_color_space_e            xg_color_space_from_vk ( VkColorSpaceKHR colorspace );
xg_present_mode_e           xg_present_mode_from_vk ( VkPresentModeKHR mode );
xg_memory_flags_b           xg_memory_flags_from_vk ( VkMemoryPropertyFlags flags );
