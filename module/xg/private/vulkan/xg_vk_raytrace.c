#include "xg_vk_raytrace.h"

xg_raytrace_geometry_h xg_vk_raytrace_create_geometry ( const xg_raytrace_geometry_params_t* params ) {
    xg_device_h device_handle = params->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // TODO avoid this alloc
    std_alloc_t geometries_alloc = std_virtual_heap_alloc_array_m ( VkAccelerationStructureGeometryKHR, params->geometries_count );
    std_auto_m geometries = ( VkAccelerationStructureGeometryKHR* ) geometries_alloc.buffer.base;
    std_alloc_t tri_counts_alloc = std_virtual_heap_alloc_array_m ( uint32_t, params->geometries_count );
    std_auto_m tri_counts = ( uint32_t* ) tri_counts_alloc.buffer.base;

    for ( uint32_t i = 0; i < params->geometries_count; ++i ) {
        VkDeviceAddress vertex_buffer_address = 0;
        {
            VkBufferDeviceAddressInfoKHR address_info;
            address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            address_info.buffer = params->geometries[i].vertex_buffer;
            vertex_buffer_address = vkGetBufferDeviceAddressKHR ( device_handle, &address_info );
        }
        std_assert_m ( vertex_buffer_address );

        VkDeviceAddress index_buffer_address = 0;
        {
            VkBufferDeviceAddressInfoKHR address_info;
            address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            address_info.buffer = params->geometries[i].index_buffer;
            index_buffer_address = vkGetBufferDeviceAddressKHR ( device_handle, &address_info );
        }

        VkDeviceAddress transform_buffer_address = 0;
        if ( params->geometries[i].transform_buffer != xg_null_handle_m ) {
            VkBufferDeviceAddressInfoKHR address_info;
            address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            address_info.buffer = params->geometries[i].transform_buffer;
            transform_buffer_address = vkGetBufferDeviceAddressKHR ( device_handle, &address_info );
        }

        VkAccelerationStructureGeometryKHR geometry = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {
                .triangles = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = { vertex_buffer_address + params->geometries[i].vertex_buffer_offset },
                    .maxVertex = params->geometries[i].vertex_count,
                    .vertexStride = params->geometries[i].vertex_stride,
                    .indexType = VK_INDEX_TYPE_UINT32, // TODO support u16?
                    .indexData = { index_buffer_address + params->geometries[i].index_buffer_offset },
                    .transformData = transform_buffer_address ? { transform_buffer_address + params->geometries[i].transform_buffer_offset } : { 0 },
                }
            }
        };

        geometries[i] = geometry;
        tri_counts[i] = params->geometries[i].index_count * 3;
    }

    VkAccelerationStructureBuildGeometryInfoKHR build_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = params->geometries_count,
        .pGeometries = geometries,
    };

    VkAccelerationStructureBuildSizesInfoKHR build_size_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    vkGetAccelerationStructureBuildSizesKHR ( device->vk_handle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, tri_counts, &build_size_info );

    xg_buffer_params_t as_buffer_params = xg_buffer_params_m ( 
        .allocator = xg_allocator_default ( device_handle, xg_memory_type_gpu_only_m ),
        .device = device_handle,
        .size = build_size_info.accelerationStructureSize,
        .allowed_usage = xg_vk_buffer_usage_device_addressed_m | xg_vk_buffer_usage_raytrace_geometry_m,
        .debug_name = "rt geo as",
    );
    xg_buffer_h as_buffer_handle = xg_buffer_create ( &as_buffer_params );
    const xg_vk_buffer_t* as_buffer = xg_vk_buffer_get ( as_buffer_handle );

    VkAccelerationStructureKHR as_handle;
    VkAccelerationStructureCreateInfoKHR as_create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = as_buffer->vk_handle,
        .size = build_size_info.accelerationStructureSize
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };
    vkCreateAccelerationStructureKHR ( device->vk_handle, &as_create_info, NULL, &as_handle );

    xg_buffer_params_t scratch_buffer_params = xg_buffer_params_m (
        .allocator = xg_allocator_default ( device_handle, xg_memory_type_gpu_only_m ),
        .device = device_handle,
        .size = build_size_info.buildScratchSize,
        .allowed_usage = xg_buffer_usage_bit_storage_m | xg_vk_buffer_usage_device_addressed_m,
        .debug_name = "rt geo build scratch",
    );
    xg_buffer_h scratch_buffer_handle = xg_buffer_create ( &scratch_buffer_params );
    const xg_vk_buffer_t* scratch_buffer = xg_vk_buffer_get ( scratch_buffer_handle );
    VkDeviceAddress scratch_buffer_address = 0;
    {
        VkBufferDeviceAddressInfoKHR address_info;
        address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        address_info.buffer = scratch_buffer->vk_handle;
        scratch_buffer_address = vkGetBufferDeviceAddressKHR ( device_handle, &address_info );
    }

    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.dstAccelerationStructure = as_handle;
    build_info.scratchData.deviceAddress = scratch_buffer_address;

    std_alloc_t build_ranges_alloc = std_virtual_heap_alloc_array_m ( VkAccelerationStructureBuildRangeInfoKHR, params->geometries_count );
    std_auto_m build_ranges = ( VkAccelerationStructureBuildRangeInfoKHR* ) build_ranges_alloc.buffer.base;

    for ( uint32_t i = 0; i < params->geometries_count; ++i ) {
        VkAccelerationStructureBuildRangeInfoKHR* info = &build_ranges[i];
        info->primitiveCount = tri_counts[i];
        info->primitiveOffset = 0;
        info->firstVertex = 0;
        info->transformOffset = 0;
    }

    VkResult result = vkBuildAccelerationStructuresKHR ( device->vk_handle, NULL, 1, geometries, &build_ranges );
    std_assert_m ( result == VK_SUCCESS );

    xg_buffer_destroy ( scratch_buffer_handle );

    xg_vk_raytrace_geometry_t* geometry = std_list_pop_m ( xg_vk_raytrace_state->geometries_freelist );
    geometry->vk_handle = as_handle;
    geometry->buffer = as_buffer_handle;
    geometry->params = 
}

xg_raytrace_world_h xg_vk_raytrace_create_world ( const xg_raytrace_world_params_t* params ) {

}
