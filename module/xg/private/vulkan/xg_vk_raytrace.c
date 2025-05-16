#include "xg_vk_raytrace.h"

#include "xg_vk_instance.h"
#include "xg_vk_device.h"
#include "xg_vk_buffer.h"
#include "xg_vk_enum.h"
#include "xg_vk_workload.h"
#include "xg_vk_allocator.h"

#if xg_enable_raytracing_m
static xg_vk_raytrace_state_t* xg_vk_raytrace_state;
#endif

#define xg_vk_raytrace_geometry_bitset_u64_count_m  std_div_ceil_m ( xg_vk_raytrace_max_geometries_m, 64 )
#define xg_vk_raytrace_world_bitset_u64_count_m     std_div_ceil_m ( xg_vk_raytrace_max_worlds_m, 64 )

void xg_vk_raytrace_load ( xg_vk_raytrace_state_t* state ) {
#if xg_enable_raytracing_m
    state->geometries_array = std_virtual_heap_alloc_array_m ( xg_vk_raytrace_geometry_t, xg_vk_raytrace_max_geometries_m );
    state->geometries_freelist = std_freelist_m ( state->geometries_array, xg_vk_raytrace_max_geometries_m );
    state->geometries_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_raytrace_geometry_bitset_u64_count_m );
    std_mem_zero_array_m ( state->geometries_bitset, xg_vk_raytrace_geometry_bitset_u64_count_m );

    state->worlds_array = std_virtual_heap_alloc_array_m ( xg_vk_raytrace_world_t, xg_vk_raytrace_max_worlds_m );
    state->worlds_freelist = std_freelist_m ( state->worlds_array, xg_vk_raytrace_max_worlds_m );
    state->worlds_bitset = std_virtual_heap_alloc_array_m ( uint64_t, xg_vk_raytrace_world_bitset_u64_count_m );
    std_mem_zero_array_m ( state->worlds_bitset, xg_vk_raytrace_world_bitset_u64_count_m );

    xg_vk_raytrace_state = state;
#endif
}

void xg_vk_raytrace_reload ( xg_vk_raytrace_state_t* state ) {
#if xg_enable_raytracing_m
    xg_vk_raytrace_state = state;
#endif
}

void xg_vk_raytrace_unload ( void ) {
#if xg_enable_raytracing_m
    uint64_t idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_raytrace_state->worlds_bitset, idx, xg_vk_raytrace_world_bitset_u64_count_m ) ) {
        xg_vk_raytrace_world_t* world = &xg_vk_raytrace_state->worlds_array[idx];
        const xg_vk_device_t* device = xg_vk_device_get ( world->params.device );
        
        std_log_info_m ( "Destroying raytrace world " std_fmt_u64_m ": " std_fmt_str_m, idx, world->params.debug_name );
        
        xg_vk_device_ext_api ( world->params.device )->destroy_acceleration_structure ( device->vk_handle, world->vk_handle, NULL );
#if xg_vk_enable_nv_raytracing_ext_m
        xg_free ( world->alloc.handle );
#else
        xg_buffer_destroy ( world->buffer );
#endif
        ++idx;
    }

    idx = 0;
    while ( std_bitset_scan ( &idx, xg_vk_raytrace_state->geometries_bitset, idx, xg_vk_raytrace_geometry_bitset_u64_count_m ) ) {
        xg_vk_raytrace_geometry_t* geometry = &xg_vk_raytrace_state->geometries_array[idx];
        const xg_vk_device_t* device = xg_vk_device_get ( geometry->params.device );

        std_log_info_m ( "Destroying raytrace geometry " std_fmt_u64_m ": " std_fmt_str_m, idx, geometry->params.debug_name );
        
        xg_vk_device_ext_api ( geometry->params.device )->destroy_acceleration_structure ( device->vk_handle, geometry->vk_handle, NULL );
#if xg_vk_enable_nv_raytracing_ext_m
        xg_free ( geometry->alloc.handle );
#else
        xg_buffer_destroy ( geometry->buffer );
#endif
        ++idx;
    }

    std_virtual_heap_free ( xg_vk_raytrace_state->geometries_array );
    std_virtual_heap_free ( xg_vk_raytrace_state->geometries_bitset );
    std_virtual_heap_free ( xg_vk_raytrace_state->worlds_array );
    std_virtual_heap_free ( xg_vk_raytrace_state->worlds_bitset );
#endif
}

xg_raytrace_geometry_h xg_vk_raytrace_geometry_create ( const xg_raytrace_geometry_params_t* params ) {
#if xg_enable_raytracing_m
    xg_device_h device_handle = params->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

#if xg_vk_enable_nv_raytracing_ext_m
    VkGeometryNV* geometries = std_virtual_heap_alloc_array_m ( VkGeometryNV, params->geometry_count );

    // fill geo data
    for ( uint32_t i = 0; i < params->geometry_count; ++i ) {
        const xg_vk_buffer_t* vertex_buffer = xg_vk_buffer_get ( params->geometries[i].vertex_buffer );
        const xg_vk_buffer_t* index_buffer = xg_vk_buffer_get ( params->geometries[i].index_buffer );
        const xg_vk_buffer_t* transform_buffer = NULL;
        if ( params->geometries[i].transform_buffer != xg_null_handle_m ) {
            transform_buffer = xg_vk_buffer_get ( params->geometries[i].transform_buffer );
        }

        VkGeometryNV geometry = ( VkGeometryNV ) {
            .sType = VK_STRUCTURE_TYPE_GEOMETRY_NV,
            .flags = VK_GEOMETRY_OPAQUE_BIT_NV,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_NV,
            .geometry = {
                .triangles = {
                    .sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = vertex_buffer->vk_handle,
                    .vertexOffset = params->geometries[i].vertex_buffer_offset,
                    .vertexCount = params->geometries[i].vertex_count,
                    .vertexStride = params->geometries[i].vertex_stride,
                    .indexType = VK_INDEX_TYPE_UINT32, // TODO support u16?
                    .indexData = index_buffer->vk_handle,
                    .indexOffset = params->geometries[i].index_buffer_offset,
                    .indexCount = params->geometries[i].index_count,
                    .transformData = transform_buffer ? transform_buffer->vk_handle : VK_NULL_HANDLE,
                    .transformOffset = transform_buffer ? params->geometries[i].transform_buffer_offset : 0,
                },
                .aabbs = {
                    .sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV,
                    .aabbData = VK_NULL_HANDLE,
                },
            }
        };

        geometries[i] = geometry;
    }

    // create
    VkAccelerationStructureInfoNV as_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV,
        .geometryCount = params->geometry_count,
        .pGeometries = geometries,
    };

    VkAccelerationStructureCreateInfoNV as_create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
        .info = as_info,
    };

    VkAccelerationStructureNV as_handle;
    xg_vk_device_ext_api( device_handle )->create_acceleration_structure ( device->vk_handle, &as_create_info, xg_vk_cpu_allocator(), &as_handle );

    // allocate
    VkAccelerationStructureMemoryRequirementsInfoNV as_memory_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
        .type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV,
        .accelerationStructure = as_handle,
    };
    VkAccelerationStructureMemoryRequirementsInfoNV scratch_memory_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
        .type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV,
        .accelerationStructure = as_handle,
    };
    VkMemoryRequirements2KHR as_memory_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    };
    VkMemoryRequirements2KHR scratch_memory_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    };
    xg_vk_device_ext_api ( device_handle )->get_acceleration_structure_memory_requirements ( device->vk_handle, &as_memory_info, &as_memory_requirements );
    xg_vk_device_ext_api ( device_handle )->get_acceleration_structure_memory_requirements ( device->vk_handle, &scratch_memory_info, &scratch_memory_requirements );

    xg_buffer_params_t scratch_buffer_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = scratch_memory_requirements.memoryRequirements.size,
        .align = scratch_memory_requirements.memoryRequirements.alignment,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_storage_m | xg_vk_buffer_usage_bit_shader_binding_table_m,
        .debug_name = "BLAS scratch",
    );
    xg_buffer_h scratch_buffer_handle = xg_buffer_create ( &scratch_buffer_params );
    const xg_vk_buffer_t* scratch_buffer = xg_vk_buffer_get ( scratch_buffer_handle );

    xg_alloc_t as_memory_alloc = xg_alloc ( &xg_alloc_params_m (
        .device = device_handle,
        .size = as_memory_requirements.memoryRequirements.size,
        .align = as_memory_requirements.memoryRequirements.alignment,
        .type = xg_memory_type_gpu_only_m,
        .debug_name = "BLAS memory"
    ) );

    VkBindAccelerationStructureMemoryInfoNV bind_info = {
        .sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
        .accelerationStructure = as_handle,
        .memory = ( VkDeviceMemory ) as_memory_alloc.base,
        .memoryOffset = as_memory_alloc.offset,
    };
    xg_vk_device_ext_api ( device_handle )->bind_acceleration_structure_memory ( device->vk_handle, 1, &bind_info );

    // build
    xg_vk_workload_context_inline_t context = xg_vk_workload_context_create_inline ( device_handle );
    vkBeginCommandBuffer ( context.cmd_buffer, &( VkCommandBufferBeginInfo ) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL,
    } );
    xg_vk_device_ext_api ( device_handle )->cmd_build_acceleration_structure ( context.cmd_buffer, &as_info, NULL, 0, VK_FALSE, as_handle, VK_NULL_HANDLE, scratch_buffer->vk_handle, 0 );
    vkEndCommandBuffer ( context.cmd_buffer );
    xg_vk_workload_context_submit_inline ( &context );
    vkDeviceWaitIdle ( device->vk_handle );

    // cleanup
    xg_buffer_destroy ( scratch_buffer_handle );
    std_virtual_heap_free ( geometries );

    // return
    xg_vk_raytrace_geometry_t* geometry = std_list_pop_m ( &xg_vk_raytrace_state->geometries_freelist );
    geometry->vk_handle = as_handle;
    geometry->alloc = as_memory_alloc;
    geometry->params = *params;

    uint64_t idx = geometry - xg_vk_raytrace_state->geometries_array;
    std_bitset_set ( xg_vk_raytrace_state->geometries_bitset, idx );

    xg_raytrace_geometry_h geometry_handle = idx;
    return geometry_handle;

#else // xg_vk_enable_nv_raytracing_ext_m

    // Fill VkAccelerationStructureGeometryKHR structs, one per param geo
    // TODO avoid this alloc
    VkAccelerationStructureGeometryKHR* geometries = std_virtual_heap_alloc_array_m ( VkAccelerationStructureGeometryKHR, params->geometry_count );
    uint32_t* tri_counts = std_virtual_heap_alloc_array_m ( uint32_t, params->geometry_count );

    for ( uint32_t i = 0; i < params->geometry_count; ++i ) {
        const xg_vk_buffer_t* vertex_buffer = xg_vk_buffer_get ( params->geometries[i].vertex_buffer );
        VkDeviceAddress vertex_buffer_address = vertex_buffer->gpu_address;

        const xg_vk_buffer_t* index_buffer = xg_vk_buffer_get ( params->geometries[i].index_buffer );
        VkDeviceAddress index_buffer_address = index_buffer->gpu_address;

        VkDeviceAddress transform_buffer_address = 0;
        if ( params->geometries[i].transform_buffer != xg_null_handle_m ) {
            const xg_vk_buffer_t* transform_buffer = xg_vk_buffer_get ( params->geometries[i].transform_buffer );
            transform_buffer_address = transform_buffer->gpu_address;
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
                    .maxVertex = params->geometries[i].vertex_count - 1,
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
        tri_counts[i] = params->geometries[i].index_count / 3;
    }

    // BLAS geometry build info
    VkAccelerationStructureBuildGeometryInfoKHR build_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = params->geometry_count,
        .pGeometries = geometries,
    };

    VkAccelerationStructureBuildSizesInfoKHR build_size_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
    };
    xg_vk_instance_ext_api()->get_acceleration_structure_build_sizes ( device->vk_handle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, tri_counts, &build_size_info );

    // Alloc BLAS memory
    xg_buffer_params_t as_buffer_params = xg_buffer_params_m ( 
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = build_size_info.accelerationStructureSize,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_acceleration_structure_storage_m,
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
        .align = device->acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_storage_m,
        .debug_name = "BLAS scratch",
    );
    xg_buffer_h scratch_buffer_handle = xg_buffer_create ( &scratch_buffer_params );
    const xg_vk_buffer_t* scratch_buffer = xg_vk_buffer_get ( scratch_buffer_handle );

    build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    build_info.dstAccelerationStructure = as_handle;
    build_info.scratchData.deviceAddress = scratch_buffer->gpu_address;

    VkAccelerationStructureBuildRangeInfoKHR* build_ranges = std_virtual_heap_alloc_array_m ( VkAccelerationStructureBuildRangeInfoKHR, params->geometry_count );
    for ( uint32_t i = 0; i < params->geometry_count; ++i ) {
        VkAccelerationStructureBuildRangeInfoKHR* info = &build_ranges[i];
        std_mem_zero_m ( info );
        info->primitiveCount = tri_counts[i];
    }

    // BLAS Build
    const VkAccelerationStructureBuildRangeInfoKHR* build_ranges_info[1] = { build_ranges };
#if 0
    //VkResult result = vkBuildAccelerationStructuresKHR ( device->vk_handle, NULL, 1, &build_info, build_ranges_info );
    VkResult result = xg_vk_instance_ext_api()->build_acceleration_structures ( device->vk_handle, NULL, 1, &build_info, build_ranges_info );
    std_assert_m ( result == VK_SUCCESS );
#else
    xg_vk_workload_context_inline_t context = xg_vk_workload_context_create_inline ( device_handle );
    vkBeginCommandBuffer ( context.cmd_buffer, &( VkCommandBufferBeginInfo ) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL,
    } );
    xg_vk_device_ext_api ( device_handle )->cmd_build_acceleration_structures ( context.cmd_buffer, 1, &build_info, build_ranges_info );
    vkEndCommandBuffer ( context.cmd_buffer );
    xg_vk_workload_context_submit_inline ( &context );
    vkDeviceWaitIdle ( device->vk_handle );
#endif

    xg_buffer_destroy ( scratch_buffer_handle );
    std_virtual_heap_free ( build_ranges );
    std_virtual_heap_free ( geometries );
    std_virtual_heap_free ( tri_counts );

    // Fill result
    xg_vk_raytrace_geometry_t* geometry = std_list_pop_m ( &xg_vk_raytrace_state->geometries_freelist );
    geometry->vk_handle = as_handle;
    geometry->buffer = as_buffer_handle;
    geometry->params = *params;

    // Flag bit
    uint64_t idx = geometry - xg_vk_raytrace_state->geometries_array;
    std_bitset_set ( xg_vk_raytrace_state->geometries_bitset, idx );

    // Return handle
    xg_raytrace_geometry_h geometry_handle = idx;
    return geometry_handle;

#endif // xg_vk_enable_nv_raytracing_ext_m

#else // xg_enable_raytracing_m
    return xg_null_handle_m;
#endif // xg_enable_raytracing_m
}

#if xg_enable_raytracing_m
static VkTransformMatrixKHR xg_vk_transform_matrix ( xg_matrix_3x4_t* matrix ) {
    VkTransformMatrixKHR vk_matrix;
    for ( uint32_t i = 0; i < 3; ++i ) {
        for ( uint32_t j = 0; j < 4; ++j ) {
            vk_matrix.matrix[i][j] = matrix->m[i][j];
        }
    }
    return vk_matrix;
}
#endif

xg_raytrace_world_h xg_vk_raytrace_world_create ( const xg_raytrace_world_params_t* params ) {
#if xg_enable_raytracing_m

#if xg_vk_enable_nv_raytracing_ext_m
    xg_device_h device_handle = params->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // fill instance data
    uint32_t instance_count = params->instance_count;
    uint64_t instance_data_size = sizeof ( VkAccelerationStructureInstanceNV ) * params->instance_count;
    xg_buffer_h instance_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_mapped_m,
        .device = device_handle,
        .size = instance_data_size,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_acceleration_structure_build_input_read_only_m | xg_vk_buffer_usage_bit_shader_binding_table_m,
        .debug_name = "TLAS instance buffer",
    ) );

    xg_buffer_info_t instance_buffer_info;
    xg_buffer_get_info ( &instance_buffer_info, instance_buffer_handle );
    std_auto_m instance_data = ( VkAccelerationStructureInstanceNV* ) instance_buffer_info.allocation.mapped_address;

    xg_vk_raytrace_pipeline_t* pipeline = ( xg_vk_raytrace_pipeline_t* ) xg_vk_raytrace_pipeline_get ( params->pipeline );
    uint32_t sbt_handle_size = device->raytrace_properties.shaderGroupHandleSize;
    uint32_t sbt_record_stride = std_align ( sbt_handle_size, device->raytrace_properties.shaderGroupBaseAlignment );
    std_assert_m ( sbt_record_stride <= device->raytrace_properties.maxShaderGroupStride );
    uint32_t sbt_hit_record_count = 0;

    for ( uint32_t i = 0; i < instance_count; ++i ) {
        xg_raytrace_geometry_instance_t* instance = &params->instance_array[i];

        xg_vk_raytrace_geometry_t* geometry = &xg_vk_raytrace_state->geometries_array[instance->geometry];
        uint64_t blas_ref = 0;
        xg_vk_device_ext_api ( device_handle )->get_acceleration_structure_reference ( device->vk_handle, geometry->vk_handle, sizeof ( blas_ref ), &blas_ref );

        VkAccelerationStructureInstanceNV* vk_instance = &instance_data[i];
        *vk_instance = ( VkAccelerationStructureInstanceNV ) {
            .transform = xg_vk_transform_matrix ( &instance->transform ),
            .instanceCustomIndex = instance->id,
            .mask = instance->visibility_mask,
            .instanceShaderBindingTableRecordOffset = i,//sbt_geo_stride * sbt_hit_record_count, //sbt_hit_offset,
            .flags = xg_raytrace_instance_flags_to_vk ( instance->flags ),
            .accelerationStructureReference = blas_ref
        };

        //sbt_hit_offset += sbt_geo_stride * geometry->params.geometry_count;
        sbt_hit_record_count += geometry->params.geometry_count;
    }

    // create
    VkAccelerationStructureInfoNV as_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_NV,
        .instanceCount = instance_count,
    };

    VkAccelerationStructureCreateInfoNV as_create_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV,
        .info = as_info,
    };

    VkAccelerationStructureNV as_handle;
    xg_vk_device_ext_api( device_handle )->create_acceleration_structure ( device->vk_handle, &as_create_info, xg_vk_cpu_allocator(), &as_handle );

    // allocate
    VkAccelerationStructureMemoryRequirementsInfoNV as_memory_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
        .type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV,
        .accelerationStructure = as_handle,
    };
    VkAccelerationStructureMemoryRequirementsInfoNV scratch_memory_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV,
        .type = VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV,
        .accelerationStructure = as_handle,
    };
    VkMemoryRequirements2KHR as_memory_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    };
    VkMemoryRequirements2KHR scratch_memory_requirements = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
    };
    xg_vk_device_ext_api ( device_handle )->get_acceleration_structure_memory_requirements ( device->vk_handle, &as_memory_info, &as_memory_requirements );
    xg_vk_device_ext_api ( device_handle )->get_acceleration_structure_memory_requirements ( device->vk_handle, &scratch_memory_info, &scratch_memory_requirements );

    xg_buffer_params_t scratch_buffer_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = scratch_memory_requirements.memoryRequirements.size,
        .align = scratch_memory_requirements.memoryRequirements.alignment,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_storage_m | xg_vk_buffer_usage_bit_shader_binding_table_m,
        .debug_name = "TLAS scratch",
    );
    xg_buffer_h scratch_buffer_handle = xg_buffer_create ( &scratch_buffer_params );
    const xg_vk_buffer_t* scratch_buffer = xg_vk_buffer_get ( scratch_buffer_handle );

    xg_alloc_t as_memory_alloc = xg_alloc ( &xg_alloc_params_m (
        .device = device_handle,
        .size = as_memory_requirements.memoryRequirements.size,
        .align = as_memory_requirements.memoryRequirements.alignment,
        .type = xg_memory_type_gpu_only_m,
        .debug_name = "TLAS memory"
    ) );

    VkBindAccelerationStructureMemoryInfoNV bind_info = {
        .sType = VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV,
        .accelerationStructure = as_handle,
        .memory = ( VkDeviceMemory ) as_memory_alloc.base,
        .memoryOffset = as_memory_alloc.offset,
    };
    xg_vk_device_ext_api ( device_handle )->bind_acceleration_structure_memory ( device->vk_handle, 1, &bind_info );

    // build
    const xg_vk_buffer_t* instance_buffer = xg_vk_buffer_get ( instance_buffer_handle );
    xg_vk_workload_context_inline_t context = xg_vk_workload_context_create_inline ( device_handle );
    vkBeginCommandBuffer ( context.cmd_buffer, &( VkCommandBufferBeginInfo ) {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL,
    } );
    xg_vk_device_ext_api ( device_handle )->cmd_build_acceleration_structure ( context.cmd_buffer, &as_info, instance_buffer->vk_handle, 0, VK_FALSE, as_handle, VK_NULL_HANDLE, scratch_buffer->vk_handle, 0 );
    vkEndCommandBuffer ( context.cmd_buffer );
    xg_vk_workload_context_submit_inline ( &context );
    vkDeviceWaitIdle ( device->vk_handle );

    // cleanup
    xg_buffer_destroy ( scratch_buffer_handle );

    // Build SBT
    uint32_t sbt_record_count = pipeline->state.shader_state.gen_shader_count + pipeline->state.shader_state.miss_shader_count + sbt_hit_record_count;
    xg_buffer_h sbt_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_upload_m,
        .device = device_handle,
        .size = sbt_record_count * sbt_record_stride,
        .allowed_usage = xg_buffer_usage_bit_copy_source_m | xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_shader_binding_table_m,
        .debug_name = "sbt", // TODO
    ));
    const xg_vk_buffer_t* sbt_buffer = xg_vk_buffer_get ( sbt_buffer_handle );
    void* sbt_data = sbt_buffer->allocation.mapped_address;
    
    for ( uint32_t i = 0; i < pipeline->state.shader_state.gen_shader_count; ++i ) {
        uint32_t handle_offset = pipeline->gen_offsets[pipeline->state.shader_state.gen_shaders[i].binding];
        std_assert_m ( handle_offset != -1 );
        std_mem_copy ( sbt_data, pipeline->sbt_handle_buffer + handle_offset, sbt_handle_size );
        sbt_data += sbt_record_stride;
    }

    for ( uint32_t i = 0; i < pipeline->state.shader_state.miss_shader_count; ++i ) {
        uint32_t handle_offset = pipeline->miss_offsets[pipeline->state.shader_state.miss_shaders[i].binding];
        std_assert_m ( handle_offset != -1 );
        std_mem_copy ( sbt_data, pipeline->sbt_handle_buffer + handle_offset, sbt_handle_size );
        sbt_data += sbt_record_stride;
    }

    for ( uint32_t instance_it = 0; instance_it < instance_count; ++instance_it ) {
        xg_raytrace_geometry_instance_t* instance = &params->instance_array[instance_it];
        xg_vk_raytrace_geometry_t* geometry = &xg_vk_raytrace_state->geometries_array[instance->geometry];
        for ( uint32_t geo_it = 0; geo_it < geometry->params.geometry_count; ++geo_it ) {
            // TODO allow different bindings per instance subgeo
            for ( uint32_t group_it = 0; group_it < pipeline->state.shader_state.hit_group_count; ++group_it ) {
                uint32_t handle_offset = pipeline->hit_offsets[instance->hit_shader_group_binding];
                std_assert_m ( handle_offset != -1 );
                std_mem_copy ( sbt_data, pipeline->sbt_handle_buffer + handle_offset, sbt_handle_size );
                sbt_data += sbt_record_stride;
            }
        }
    }

    pipeline->sbt_buffer = sbt_buffer_handle;
    VkDeviceSize offset = 0;
    pipeline->sbt_gen_offset = offset;
    offset += sbt_record_stride * pipeline->state.shader_state.gen_shader_count;
    pipeline->sbt_miss_offset = offset;
    pipeline->sbt_miss_stride = sbt_record_stride;
    offset += sbt_record_stride * pipeline->state.shader_state.miss_shader_count;
    pipeline->sbt_hit_offset = offset;
    pipeline->sbt_hit_stride = sbt_record_stride;

    // Fill result
    xg_vk_raytrace_world_t* world = std_list_pop_m ( &xg_vk_raytrace_state->worlds_freelist );
    world->alloc = as_memory_alloc;
    world->vk_handle = as_handle;
    world->params = *params;

    // Flag bitset
    uint64_t idx = world - xg_vk_raytrace_state->worlds_array;
    std_bitset_set ( xg_vk_raytrace_state->worlds_bitset, idx );

    // Return handle
    xg_raytrace_world_h world_handle = idx;
    return world_handle;

#else // xg_vk_enable_nv_raytracing_ext_m
    xg_device_h device_handle = params->device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    // Alloc instance data
    uint32_t instance_count = params->instance_count;
    uint64_t instance_data_size = sizeof ( VkAccelerationStructureInstanceKHR ) * params->instance_count;
    xg_buffer_h instance_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_mapped_m,
        .device = device_handle,
        .size = instance_data_size,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_acceleration_structure_build_input_read_only_m,
        .debug_name = "TLAS instance buffer",
    ) );

    // Write instance data
    xg_buffer_info_t instance_buffer_info;
    xg_buffer_get_info ( &instance_buffer_info, instance_buffer_handle );
    std_auto_m instance_data = ( VkAccelerationStructureInstanceKHR* ) instance_buffer_info.allocation.mapped_address;

    // TODO clean up
    xg_vk_raytrace_pipeline_t* pipeline = ( xg_vk_raytrace_pipeline_t* ) xg_vk_raytrace_pipeline_get ( params->pipeline );
    uint32_t sbt_handle_size = device->raytrace_properties.shaderGroupHandleSize;
    uint32_t sbt_record_stride = std_align ( sbt_handle_size, device->raytrace_properties.shaderGroupBaseAlignment );
    std_assert_m ( sbt_record_stride <= device->raytrace_properties.maxShaderGroupStride );
    uint32_t sbt_hit_record_count = 0;

    for ( uint32_t i = 0; i < instance_count; ++i ) {
        xg_raytrace_geometry_instance_t* instance = &params->instance_array[i];

        xg_vk_raytrace_geometry_t* geometry = &xg_vk_raytrace_state->geometries_array[instance->geometry];
        VkAccelerationStructureDeviceAddressInfoKHR blas_info = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .accelerationStructure = geometry->vk_handle,
        };
        uint64_t blas_addr = xg_vk_instance_ext_api()->get_acceleration_structure_device_address ( device->vk_handle, &blas_info );

        VkAccelerationStructureInstanceKHR* vk_instance = &instance_data[i];
        *vk_instance = ( VkAccelerationStructureInstanceKHR ) {
            .transform = xg_vk_transform_matrix ( &instance->transform ),
            .instanceCustomIndex = instance->id,
            .mask = instance->visibility_mask,
            .instanceShaderBindingTableRecordOffset = i,//sbt_geo_stride * sbt_hit_record_count, //sbt_hit_offset,
            .flags = xg_raytrace_instance_flags_to_vk ( instance->flags ),
            .accelerationStructureReference = blas_addr
        };

        //sbt_hit_offset += sbt_geo_stride * geometry->params.geometry_count;
        sbt_hit_record_count += geometry->params.geometry_count;
    }

    // Fill TLAS build info
    const xg_vk_buffer_t* instance_buffer = xg_vk_buffer_get ( instance_buffer_handle );

    VkAccelerationStructureGeometryKHR geometry = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .geometry.instances.arrayOfPointers = VK_FALSE,
        .geometry.instances.data.deviceAddress = instance_buffer->gpu_address
    };

    VkAccelerationStructureBuildGeometryInfoKHR build_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .geometryCount = 1,
        .pGeometries = &geometry
    };

    if ( params->flags & xg_raytrace_world_flag_bit_allow_update_m ) {
        build_info.flags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    }

    VkAccelerationStructureBuildSizesInfoKHR build_size_info = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    xg_vk_instance_ext_api()->get_acceleration_structure_build_sizes ( device->vk_handle, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &instance_count, &build_size_info );

    // Create TLAS buffer
    xg_buffer_h tlas_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = build_size_info.accelerationStructureSize,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_acceleration_structure_storage_m,
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
    build_info.dstAccelerationStructure = as_handle;

    // Build TLAS
    xg_buffer_params_t scratch_buffer_params = xg_buffer_params_m (
        .memory_type = xg_memory_type_gpu_only_m,
        .device = device_handle,
        .size = build_size_info.buildScratchSize,
        .allowed_usage = xg_buffer_usage_bit_shader_device_address_m | xg_buffer_usage_bit_storage_m,
        .debug_name = "TLAS scratch",
        .align = device->acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment
    );
    xg_buffer_h scratch_buffer_handle = xg_buffer_create ( &scratch_buffer_params );
    const xg_vk_buffer_t* scratch_buffer = xg_vk_buffer_get ( scratch_buffer_handle );
    build_info.scratchData.deviceAddress = scratch_buffer->gpu_address;

    VkAccelerationStructureBuildRangeInfoKHR build_range = {
        .primitiveCount = instance_count
    };
    const VkAccelerationStructureBuildRangeInfoKHR* build_ranges_info[1] = { &build_range };

#if 0    
    VkResult result = xg_vk_instance_ext_api()->build_acceleration_structures ( device->vk_handle, NULL, 1, &build_info, build_ranges_info );
    std_assert_m ( result == VK_SUCCESS );
#else
    xg_vk_workload_context_inline_t context = xg_vk_workload_context_create_inline ( device_handle );
    vkBeginCommandBuffer ( context.cmd_buffer, &( VkCommandBufferBeginInfo ) {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL,
    } );
    xg_vk_device_ext_api ( device_handle )->cmd_build_acceleration_structures ( context.cmd_buffer, 1, &build_info, build_ranges_info );
    vkEndCommandBuffer ( context.cmd_buffer );
    xg_vk_workload_context_submit_inline ( &context );
    vkDeviceWaitIdle ( device->vk_handle );
#endif

    xg_buffer_destroy ( scratch_buffer_handle );

    // Build SBT
    uint32_t sbt_record_count = pipeline->state.shader_state.gen_shader_count + pipeline->state.shader_state.miss_shader_count + sbt_hit_record_count;
    xg_buffer_h sbt_buffer_handle = xg_buffer_create ( &xg_buffer_params_m (
        .memory_type = xg_memory_type_upload_m,
        .device = device_handle,
        .size = sbt_record_count * sbt_record_stride,
        .allowed_usage = xg_buffer_usage_bit_copy_source_m | xg_buffer_usage_bit_shader_device_address_m | xg_vk_buffer_usage_bit_shader_binding_table_m,
        .debug_name = "sbt", // TODO
    ));
    const xg_vk_buffer_t* sbt_buffer = xg_vk_buffer_get ( sbt_buffer_handle );
    void* sbt_data = sbt_buffer->allocation.mapped_address;
    
    for ( uint32_t i = 0; i < pipeline->state.shader_state.gen_shader_count; ++i ) {
        uint32_t handle_offset = pipeline->gen_offsets[pipeline->state.shader_state.gen_shaders[i].binding];
        std_assert_m ( handle_offset != -1 );
        std_mem_copy ( sbt_data, pipeline->sbt_handle_buffer + handle_offset, sbt_handle_size );
        sbt_data += sbt_record_stride;
    }

    for ( uint32_t i = 0; i < pipeline->state.shader_state.miss_shader_count; ++i ) {
        uint32_t handle_offset = pipeline->miss_offsets[pipeline->state.shader_state.miss_shaders[i].binding];
        std_assert_m ( handle_offset != -1 );
        std_mem_copy ( sbt_data, pipeline->sbt_handle_buffer + handle_offset, sbt_handle_size );
        sbt_data += sbt_record_stride;
    }

    for ( uint32_t instance_it = 0; instance_it < instance_count; ++instance_it ) {
        xg_raytrace_geometry_instance_t* instance = &params->instance_array[instance_it];
        xg_vk_raytrace_geometry_t* geometry = &xg_vk_raytrace_state->geometries_array[instance->geometry];
        for ( uint32_t geo_it = 0; geo_it < geometry->params.geometry_count; ++geo_it ) {
            // TODO allow different bindings per instance subgeo
            for ( uint32_t group_it = 0; group_it < pipeline->state.shader_state.hit_group_count; ++group_it ) {
                uint32_t handle_offset = pipeline->hit_offsets[instance->hit_shader_group_binding];
                std_assert_m ( handle_offset != -1 );
                std_mem_copy ( sbt_data, pipeline->sbt_handle_buffer + handle_offset, sbt_handle_size );
                sbt_data += sbt_record_stride;
            }
        }
    }

    VkBufferDeviceAddressInfo address_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .pNext = NULL,
        .buffer = sbt_buffer->vk_handle,
    };
    VkDeviceAddress sbt_base = vkGetBufferDeviceAddress ( device->vk_handle, &address_info );
    VkStridedDeviceAddressRegionKHR sbt_gen_region = {
        .deviceAddress = sbt_base,
        .stride = sbt_record_stride,
        .size = sbt_record_stride * pipeline->state.shader_state.gen_shader_count,
    };
    sbt_base += sbt_gen_region.size;
    VkStridedDeviceAddressRegionKHR sbt_miss_region = {
        .deviceAddress = sbt_base,
        .stride = sbt_record_stride,
        .size = sbt_record_stride * pipeline->state.shader_state.miss_shader_count,
    };
    sbt_base += sbt_miss_region.size;
    VkStridedDeviceAddressRegionKHR sbt_hit_region = {
        .deviceAddress = sbt_base,
        .stride = sbt_record_stride,
        .size = sbt_record_stride * sbt_hit_record_count,
    };

    pipeline->sbt_gen_region = sbt_gen_region;
    pipeline->sbt_miss_region = sbt_miss_region;
    pipeline->sbt_hit_region = sbt_hit_region;

    // Fill result
    xg_vk_raytrace_world_t* world = std_list_pop_m ( &xg_vk_raytrace_state->worlds_freelist );
    world->buffer = tlas_buffer_handle;
    world->vk_handle = as_handle;
    world->params = *params;

    // Flag bitset
    uint64_t idx = world - xg_vk_raytrace_state->worlds_array;
    std_bitset_set ( xg_vk_raytrace_state->worlds_bitset, idx );

    // Return handle
    xg_raytrace_world_h world_handle = idx;
    return world_handle;

#endif // xg_vk_enable_nv_raytracing_ext_m

#else // xg_enable_raytracing_m
    return xg_null_handle_m;
#endif // xg_enable_raytracing_m
}

void xg_vk_raytrace_world_destroy ( xg_raytrace_world_h world_handle ) {
#if xg_enable_raytracing_m
    xg_vk_raytrace_world_t* world = &xg_vk_raytrace_state->worlds_array[world_handle];
    xg_device_h device_handle = world->params.device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_device_ext_api ( device_handle )->destroy_acceleration_structure ( device->vk_handle, world->vk_handle, NULL );
    std_bitset_clear ( xg_vk_raytrace_state->worlds_bitset, world_handle );
    std_list_push ( &xg_vk_raytrace_state->worlds_freelist, world );
#endif
}

void xg_vk_raytrace_geometry_destroy ( xg_raytrace_geometry_h geo_handle ) {
#if xg_enable_raytracing_m
    xg_vk_raytrace_geometry_t* geo = &xg_vk_raytrace_state->geometries_array[geo_handle];
    xg_device_h device_handle = geo->params.device;
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );
    xg_vk_device_ext_api ( device_handle )->destroy_acceleration_structure ( device->vk_handle, geo->vk_handle, NULL );
    std_bitset_clear ( xg_vk_raytrace_state->geometries_bitset, geo_handle );
    std_list_push ( &xg_vk_raytrace_state->geometries_freelist, geo );
#endif
}

const xg_vk_raytrace_world_t* xg_vk_raytrace_world_get ( xg_raytrace_world_h world ) {
#if xg_enable_raytracing_m
    return &xg_vk_raytrace_state->worlds_array[world];
#else
    return NULL;
#endif
}
