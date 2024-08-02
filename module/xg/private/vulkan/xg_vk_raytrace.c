#include "xg_vk_raytrace.h"

#include "xg_vk_instance.h"
#include "xg_vk_device.h"
#include "xg_vk_buffer.h"
#include "xg_vk_enum.h"

static xg_vk_raytrace_state_t* xg_vk_raytrace_state;

xg_raytrace_geometry_h xg_vk_raytrace_geometry_create ( const xg_raytrace_geometry_params_t* params ) {
    xg_device_h device_handle = params->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // Fill VkAccelerationStructureGeometryKHR structs, one per param geo
    // TODO avoid this alloc
    VkAccelerationStructureGeometryKHR* geometries = std_virtual_heap_alloc_array_m ( VkAccelerationStructureGeometryKHR, params->geometries_count );
    uint32_t* tri_counts = std_virtual_heap_alloc_array_m ( uint32_t, params->geometries_count );

    for ( uint32_t i = 0; i < params->geometries_count; ++i ) {
        VkDeviceAddress vertex_buffer_address = 0;
        {
            const xg_vk_buffer_t* vertex_buffer = xg_vk_buffer_get ( params->geometries[i].vertex_buffer );
            VkBufferDeviceAddressInfoKHR address_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .buffer = vertex_buffer->vk_handle,
            };
            vertex_buffer_address = vkGetBufferDeviceAddress ( device->vk_handle, &address_info );
        }
        std_assert_m ( vertex_buffer_address );

        VkDeviceAddress index_buffer_address = 0;
        {
            const xg_vk_buffer_t* index_buffer = xg_vk_buffer_get ( params->geometries[i].index_buffer );
            VkBufferDeviceAddressInfoKHR address_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .buffer = index_buffer->vk_handle
            };
            index_buffer_address = vkGetBufferDeviceAddress ( device->vk_handle, &address_info );
        }

        VkDeviceAddress transform_buffer_address = 0;
        if ( params->geometries[i].transform_buffer != xg_null_handle_m ) {
            const xg_vk_buffer_t* transform_buffer = xg_vk_buffer_get ( params->geometries[i].transform_buffer );
            VkBufferDeviceAddressInfoKHR address_info = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .buffer = transform_buffer->vk_handle
            };
            transform_buffer_address = vkGetBufferDeviceAddress ( device->vk_handle, &address_info );
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
                    .transformData = transform_buffer_address ? 
                        ( VkDeviceOrHostAddressConstKHR  ) { transform_buffer_address + params->geometries[i].transform_buffer_offset } : 
                        ( VkDeviceOrHostAddressConstKHR  ) { 0 },
                }
            }
        };

        geometries[i] = geometry;
        tri_counts[i] = params->geometries[i].index_count * 3;
    }

    // BLAS geometry build info
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
    //vkGetAccelerationStructureBuildSizesKHR ( device->vk_handle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, tri_counts, &build_size_info );
    xg_vk_instance_ext_api()->get_acceleration_structure_build_sizes ( device->vk_handle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, tri_counts, &build_size_info );

    // Alloc BLAS memory
    xg_buffer_params_t as_buffer_params = xg_buffer_params_m ( 
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = build_size_info.accelerationStructureSize,
        .allowed_usage = xg_buffer_usage_bit_raytrace_storage_m | xg_buffer_usage_bit_device_addressed_m,
        .debug_name = "BLAS geometry",
    );
    xg_buffer_h as_buffer_handle = xg_buffer_create ( &as_buffer_params );
    const xg_vk_buffer_t* as_buffer = xg_vk_buffer_get ( as_buffer_handle );

    // Create BLAS
    VkAccelerationStructureKHR as_handle;
    VkAccelerationStructureCreateInfoKHR as_create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = as_buffer->vk_handle,
        .size = build_size_info.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR
    };
    //vkCreateAccelerationStructureKHR ( device->vk_handle, &as_create_info, NULL, &as_handle );
    xg_vk_instance_ext_api()->create_acceleration_structure ( device->vk_handle, &as_create_info, NULL, &as_handle );

    // BLAS build info
    xg_buffer_params_t scratch_buffer_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = build_size_info.buildScratchSize,
        .allowed_usage = xg_buffer_usage_bit_storage_m | xg_buffer_usage_bit_device_addressed_m,
        .debug_name = "BLAS scratch",
    );
    xg_buffer_h scratch_buffer_handle = xg_buffer_create ( &scratch_buffer_params );
    const xg_vk_buffer_t* scratch_buffer = xg_vk_buffer_get ( scratch_buffer_handle );
    VkDeviceAddress scratch_buffer_address = 0;
    {
        VkBufferDeviceAddressInfoKHR address_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = scratch_buffer->vk_handle
        };
        scratch_buffer_address = vkGetBufferDeviceAddress ( device->vk_handle, &address_info );
    }

    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    build_info.dstAccelerationStructure = as_handle;
    build_info.scratchData.deviceAddress = scratch_buffer_address;

    VkAccelerationStructureBuildRangeInfoKHR* build_ranges = std_virtual_heap_alloc_array_m ( VkAccelerationStructureBuildRangeInfoKHR, params->geometries_count );
    for ( uint32_t i = 0; i < params->geometries_count; ++i ) {
        VkAccelerationStructureBuildRangeInfoKHR* info = &build_ranges[i];
        info->primitiveCount = tri_counts[i];
        info->primitiveOffset = 0;
        info->firstVertex = 0;
        info->transformOffset = 0;
    }

    // BLAS Build
    const VkAccelerationStructureBuildRangeInfoKHR* build_ranges_info[1] = { build_ranges };
    //VkResult result = vkBuildAccelerationStructuresKHR ( device->vk_handle, NULL, 1, &build_info, build_ranges_info );
    VkResult result = xg_vk_instance_ext_api()->build_acceleration_structures ( device->vk_handle, NULL, 1, &build_info, build_ranges_info );
    std_assert_m ( result == VK_SUCCESS );

    xg_buffer_destroy ( scratch_buffer_handle );

    // Fill result
    xg_vk_raytrace_geometry_t* geometry = std_list_pop_m ( &xg_vk_raytrace_state->geometries_freelist );
    geometry->vk_handle = as_handle;
    geometry->buffer = as_buffer_handle;
    geometry->params = *params;

    // Return handle
    xg_raytrace_geometry_h geometry_handle = geometry - xg_vk_raytrace_state->geometries_array;
    return geometry_handle;
}

static VkTransformMatrixKHR xg_vk_transform_matrix ( xg_matrix_3x4_t* matrix ) {
    VkTransformMatrixKHR vk_matrix;
    for ( uint32_t i = 0; i < 3; ++i ) {
        for ( uint32_t j = 0; j < 4; ++j ) {
            vk_matrix.matrix[i][j] = matrix->m[i][j];
        }
    }
    return vk_matrix;
}

xg_raytrace_world_h xg_vk_raytrace_world_create ( const xg_raytrace_world_params_t* params ) {
    xg_device_h device_handle = params->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // Alloc instance data
    uint32_t instance_count = params->instance_count;
    uint64_t instance_data_size = sizeof ( VkAccelerationStructureInstanceKHR ) * params->instance_count;
    xg_buffer_h instance_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_mappable_m,
        .device = device_handle,
        .size = instance_data_size,
        .allowed_usage = xg_buffer_usage_bit_raytrace_uniform_m | xg_buffer_usage_bit_device_addressed_m,
        .debug_name = "TLAS instance buffer",
    ) );

    // Write instance data
    xg_buffer_info_t instance_buffer_info;
    xg_buffer_get_info ( &instance_buffer_info, instance_buffer_handle );
    std_auto_m* instance_data = ( VkAccelerationStructureInstanceKHR* ) instance_buffer_info.allocation.mapped_address;

    for ( uint32_t i = 0; i < instance_count; ++i ) {
        VkAccelerationStructureInstanceKHR* vk_instance = &instance_data[i];
        xg_raytrace_geometry_instance_t* instance = &params->instance_array[i];
        vk_instance->transform = xg_vk_transform_matrix ( &instance->transform );
        vk_instance->instanceCustomIndex = i; // TODO
        vk_instance->mask = instance->visibility_mask;
        vk_instance->instanceShaderBindingTableRecordOffset = 0; // TODO
        vk_instance->flags = xg_raytrace_instance_flags_to_vk ( instance->flags );

        xg_vk_raytrace_geometry_t* geometry = &xg_vk_raytrace_state->geometries_array[instance->geometry];
        VkAccelerationStructureDeviceAddressInfoKHR blas_info;
        blas_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        blas_info.accelerationStructure = geometry->vk_handle;
        uint64_t blas_addr = xg_vk_instance_ext_api()->get_acceleration_structure_device_address ( device->vk_handle, &blas_info );
        vk_instance->accelerationStructureReference = blas_addr;
    }

    // Fill TLAS build info
    const xg_vk_buffer_t* instance_buffer = xg_vk_buffer_get ( instance_buffer_handle );
    VkBufferDeviceAddressInfoKHR instance_buffer_address_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = instance_buffer->vk_handle
    };
    VkDeviceAddress instance_buffer_address = vkGetBufferDeviceAddress ( device->vk_handle, &instance_buffer_address_info );

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .geometry.instances.arrayOfPointers = VK_FALSE,
        .geometry.instances.data.deviceAddress = instance_buffer_address
    };

    VkAccelerationStructureBuildGeometryInfoKHR build_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    VkAccelerationStructureBuildSizesInfoKHR build_size_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    xg_vk_instance_ext_api()->get_acceleration_structure_build_sizes ( device->vk_handle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &instance_count, &build_size_info );

    // Create TLAS buffer
    xg_buffer_h tlas_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = build_size_info.accelerationStructureSize,
        .allowed_usage = xg_buffer_usage_bit_raytrace_storage_m | xg_buffer_usage_bit_device_addressed_m,
        .debug_name = "TLAS buffer"
    ) );
    const xg_vk_buffer_t* tlas_buffer = xg_vk_buffer_get ( tlas_buffer_handle );

    // Create TLAS
    VkAccelerationStructureCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .buffer = tlas_buffer->vk_handle,
        .size = build_size_info.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };
    VkAccelerationStructureKHR as_handle;
    xg_vk_instance_ext_api()->create_acceleration_structure ( device->vk_handle, &create_info, NULL, &as_handle );

    // Build TLAS
    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build_info.dstAccelerationStructure = as_handle;

    VkAccelerationStructureBuildRangeInfoKHR build_range = {
        .primitiveCount = instance_count
    };
    const VkAccelerationStructureBuildRangeInfoKHR* build_ranges_info[1] = { &build_range };
    VkResult result = xg_vk_instance_ext_api()->build_acceleration_structures ( device->vk_handle, NULL, 1, &build_info, build_ranges_info );
    std_assert_m ( result == VK_SUCCESS );

    // Fill result
    xg_vk_raytrace_world_t* world = std_list_pop_m ( &xg_vk_raytrace_state->worlds_freelist );
    world->device = device_handle;
    world->buffer = tlas_buffer_handle;
    world->vk_handle = as_handle;

    // Return handle
    xg_raytrace_world_h world_handle = world - xg_vk_raytrace_state->worlds_array;
    return world_handle;
}
