#include "xg_vk_device.h"

#include "xg_vk.h"
#include "xg_vk_enum.h"
#include "xg_vk_instance.h"

#include <xg_enum.h>

#include <std_string.h>
#include <std_log.h>
#include <std_byte.h>
#include <std_atomic.h>

#if xg_enable_backend_vulkan_m
    #include "vulkan/xg_vk_allocator.h"
    #include "vulkan/xg_vk_workload.h"
    #include "vulkan/xg_vk_pipeline.h"
#endif

/*
    TODO
        create instance
            init global instance
        create device
            create device x
            init all devices
        interface displays
*/

// ---- Private -----

static xg_vk_device_state_t* xg_vk_device_state;

static uint64_t xg_vk_device_id_next ( void ) {
    return std_atomic_increment_u64 ( &xg_vk_device_state->device_id ) - 1;
}

static void xg_vk_device_load_ext_api ( xg_device_h device_handle ) {
    xg_vk_device_t* device = &xg_vk_device_state->devices_array[device_handle];

#define xg_vk_device_ext_init_pfn_m(ptr, name) { *(ptr) = ( std_typeof_m ( *(ptr) ) ) ( vkGetDeviceProcAddr ( device->vk_handle, name ) ); std_assert_m ( *ptr ); }

    xg_vk_device_ext_init_pfn_m ( &device->ext_api.cmd_begin_debug_region, "vkCmdBeginDebugUtilsLabelEXT" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.cmd_end_debug_region, "vkCmdEndDebugUtilsLabelEXT" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.set_debug_name, "vkSetDebugUtilsObjectNameEXT" );
#if xg_vk_enable_sync2_m
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.cmd_sync2_pipeline_barrier, "vkCmdPipelineBarrier2KHR" );
#endif

#if xg_enable_raytracing_m
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.get_acceleration_structure_build_sizes, "vkGetAccelerationStructureBuildSizesKHR" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.create_acceleration_structure, "vkCreateAccelerationStructureKHR" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.cmd_build_acceleration_structures, "vkCmdBuildAccelerationStructuresKHR" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.get_acceleration_structure_device_address, "vkGetAccelerationStructureDeviceAddressKHR" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.trace_rays, "vkCmdTraceRaysKHR" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.destroy_acceleration_structure, "vkDestroyAccelerationStructureKHR" );
#if xg_vk_enable_nv_raytracing_ext_m
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.create_raytrace_pipelines, "vkCreateRayTracingPipelinesNV" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.get_shader_group_handles, "vkGetRayTracingShaderGroupHandlesNV" );
#else
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.create_raytrace_pipelines, "vkCreateRayTracingPipelinesKHR" );
    xg_vk_device_ext_init_pfn_m ( &device->ext_api.get_shader_group_handles, "vkGetRayTracingShaderGroupHandlesNK" );
#endif
#endif

#undef xg_vk_instance_ext_init_pfn_m    
}

static void xg_vk_device_cache_properties ( xg_vk_device_t* device ) {
    std_log_info_m ( "Querying device " std_fmt_size_m " for properties", device->id );
    
    // Properties
    vkGetPhysicalDeviceProperties ( device->vk_physical_handle, &device->generic_properties );
    device->raytrace_properties = ( VkPhysicalDeviceRayTracingPipelinePropertiesKHR ) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
        .pNext = NULL
    };
    VkPhysicalDeviceProperties2 properties_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &device->raytrace_properties
    };
    vkGetPhysicalDeviceProperties2 ( device->vk_physical_handle, &properties_query );
    device->generic_properties = properties_query.properties;

    // Features
    device->supported_device_address_features = ( VkPhysicalDeviceBufferDeviceAddressFeatures ) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = NULL
    };
    device->supported_raytrace_features = ( VkPhysicalDeviceRayTracingPipelineFeaturesKHR ) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
        .pNext = &device->supported_device_address_features
    };
    device->supported_acceleration_structure_features = ( VkPhysicalDeviceAccelerationStructureFeaturesKHR ) {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &device->supported_raytrace_features,
    };
    VkPhysicalDeviceFeatures2 features_query = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &device->supported_acceleration_structure_features
    };
    vkGetPhysicalDeviceFeatures2 ( device->vk_physical_handle, &features_query );
    device->supported_features = features_query.features;
    
    // Memory
    vkGetPhysicalDeviceMemoryProperties ( device->vk_physical_handle, &device->memory_properties );

    // Queues
    vkGetPhysicalDeviceQueueFamilyProperties ( device->vk_physical_handle, &device->queues_families_count, NULL );
    vkGetPhysicalDeviceQueueFamilyProperties ( device->vk_physical_handle, &device->queues_families_count, device->queues_families_properties );

#if xg_enable_raytracing_m
    std_assert_m ( device->supported_raytrace_features.rayTracingPipeline );
    std_assert_m ( device->supported_device_address_features.bufferDeviceAddress );
    std_assert_m ( device->supported_acceleration_structure_features.accelerationStructure );
    //std_assert_m ( device->supported_acceleration_structure_features.accelerationStructureHostCommands );
#endif

    // needed for sv_primitiveID I think
    // TODO avoid using this? 
    //      see https://x.com/SebAaltonen/status/1821440546096689383
    //          Variable Rate Shading with Visibility Buffer Rendering - John Hable - SIGGRAPH 2024
    std_assert_m ( device->supported_features.geometryShader );

#if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
    // Print generic properties
    uint32_t api_major = VK_VERSION_MAJOR ( device->generic_properties.apiVersion );
    uint32_t api_minor = VK_VERSION_MINOR ( device->generic_properties.apiVersion );
    uint32_t api_patch = VK_VERSION_PATCH ( device->generic_properties.apiVersion );
    std_log_info_m ( std_fmt_str_m " (" std_fmt_h32_m ") " std_fmt_str_m " (" std_fmt_str_m ") - driver version " std_fmt_u32_m " - supported API version " std_fmt_u32_m "." std_fmt_u32_m "." std_fmt_u32_m,
        xg_vk_device_vendor_name ( device->generic_properties.vendorID ), device->generic_properties.vendorID, device->generic_properties.deviceName,
        xg_vk_device_type_str ( device->generic_properties.deviceType ), device->generic_properties.driverVersion, api_major, api_minor, api_patch );

    uint32_t uniform_buffer_alignment = device->generic_properties.limits.minUniformBufferOffsetAlignment;
    uint32_t texel_buffer_alignment = device->generic_properties.limits.minTexelBufferOffsetAlignment;
    uint32_t storage_buffer_alignment = device->generic_properties.limits.minStorageBufferOffsetAlignment;
    uint32_t max_uniform_range = device->generic_properties.limits.maxUniformBufferRange;
    uint32_t max_texel_elements = device->generic_properties.limits.maxTexelBufferElements;
    uint32_t max_storage_range = device->generic_properties.limits.maxStorageBufferRange;
    uint32_t max_set_bindings = device->generic_properties.limits.maxBoundDescriptorSets;
    uint32_t max_uniform_buffers_per_stage = device->generic_properties.limits.maxPerStageDescriptorUniformBuffers;
    uint32_t max_storage_buffers_per_stage = device->generic_properties.limits.maxPerStageDescriptorStorageBuffers;
    uint32_t max_sampled_images_per_stage = device->generic_properties.limits.maxPerStageDescriptorSampledImages;
    uint32_t max_resources_per_stage = device->generic_properties.limits.maxPerStageResources;
    uint32_t max_vertex_input_attributes = device->generic_properties.limits.maxVertexInputAttributes;
    uint32_t max_vertex_input_streams = device->generic_properties.limits.maxVertexInputBindings;
    uint32_t max_sampled_images_per_set = device->generic_properties.limits.maxDescriptorSetSampledImages;
#if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
    char max_uniform_range_str[32];
    char max_texel_elements_str[32];
    char max_storage_range_str[32];
    std_size_to_str_approx ( max_uniform_range_str, 32, max_uniform_range );
    std_count_to_str_approx ( max_texel_elements_str, 32, max_texel_elements );
    std_size_to_str_approx ( max_storage_range_str, 32, max_storage_range );
#endif
    std_log_info_m ( "Uniform buffer binding required alignment: " std_fmt_u32_m, uniform_buffer_alignment );
    std_log_info_m ( "Texel buffer binding required alignment: " std_fmt_u32_m, texel_buffer_alignment );
    std_log_info_m ( "Storage buffer binding required alignment: " std_fmt_u32_m, storage_buffer_alignment );
    std_log_info_m ( "Uniform buffer binding maximum range size: " std_fmt_u32_m " (" std_fmt_str_m ")", max_uniform_range, max_uniform_range_str );
    std_log_info_m ( "Texel buffer binding maximum texel count: " std_fmt_u32_m  " (" std_fmt_str_m ")", max_texel_elements, max_texel_elements_str );
    std_log_info_m ( "Storage buffer binding maximum range size: " std_fmt_u32_m  " (" std_fmt_str_m ")", max_storage_range, max_storage_range_str );
    std_log_info_m ( "Max set bindings:" std_fmt_u32_m, max_set_bindings );
    std_log_info_m ( "Max uniform buffers per stage: " std_fmt_u32_m, max_uniform_buffers_per_stage );
    std_log_info_m ( "Max storage buffers per stage: " std_fmt_u32_m, max_storage_buffers_per_stage );
    std_log_info_m ( "Max sampled images per stage: " std_fmt_u32_m, max_sampled_images_per_stage );
    std_log_info_m ( "Max sampled images per set: " std_fmt_u32_m, max_sampled_images_per_set );
    std_log_info_m ( "Max resources per stage: " std_fmt_u32_m, max_resources_per_stage );
    std_log_info_m ( "Max vertex input attributes: " std_fmt_u32_m, max_vertex_input_attributes );
    std_log_info_m ( "Max vertex input streams: " std_fmt_u32_m, max_vertex_input_streams );

    // Print list of formats that support render target
    std_log_info_m ( "Supported render target formats:" );

    for ( uint64_t i = 0; i < xg_format_count_m; ++i ) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties ( device->vk_physical_handle, xg_format_to_vk ( ( xg_format_e ) i ), &props );

        if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ) {
            std_log_info_m ( std_fmt_tab_m std_fmt_str_m, xg_format_str ( ( xg_format_e ) i ) );
        }
    }

    // Print memory properties
    for ( uint32_t i = 0; i < device->memory_properties.memoryHeapCount; ++i ) {
        VkMemoryHeapFlags flags = device->memory_properties.memoryHeaps[i].flags;
        VkDeviceSize size = device->memory_properties.memoryHeaps[i].size;
        const char* desc = xg_vk_device_memory_heap_str ( flags );

        if ( size < 1024 ) {
            std_log_info_m ( "Memory Heap " std_fmt_u32_m ": " std_fmt_str_m " - " std_fmt_u64_m " B", i, desc, size );
        } else {
            char short_size[16];
            std_size_to_str_approx ( short_size, 16, size );
            std_log_info_m ( "Memory Heap " std_fmt_u32_m ": " std_fmt_str_m " - " std_fmt_u64_m " B (" std_fmt_str_m ")", i, desc, size, short_size );
        }
    }

    // Print memory types
    for ( uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i ) {
        VkMemoryPropertyFlags flags = device->memory_properties.memoryTypes[i].propertyFlags;
        uint32_t heap = device->memory_properties.memoryTypes[i].heapIndex;
        const char* desc = xg_vk_device_memory_type_str ( flags );
        std_log_info_m ( "Memory Type " std_fmt_u32_m ": " std_fmt_str_m " - Heap " std_fmt_u32_m, i, desc, heap );
    }

    // Print queue family properties
    for ( uint32_t i = 0; i < device->queues_families_count; ++i ) {
        VkQueueFlags flags = device->queues_families_properties[i].queueFlags;
        uint32_t count = device->queues_families_properties[i].queueCount;
        uint32_t timestap_bits = device->queues_families_properties[i].timestampValidBits;
        VkExtent3D copy_granularity = device->queues_families_properties[i].minImageTransferGranularity;
        const char* desc = xg_vk_device_queue_str ( flags );
        std_log_info_m ( "Queue family " std_fmt_u32_m ": " std_fmt_u32_m " queues - " std_fmt_str_m " - <" std_fmt_u32_m
            "," std_fmt_u32_m "," std_fmt_u32_m "> copy granulatity - " std_fmt_u32_m " bits timestamps",
            i, count, desc, copy_granularity.width, copy_granularity.height, copy_granularity.depth, timestap_bits );
    }
#endif

    std_log_info_m ( "Mapping device properties to runtime objects" );

    // Map heaps
    //
    // TODO: for device mapped memory, just look for VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT and store somewhere whether it also has
    //       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT. deal with the implications at runtime.
    // TODO: rename host_uncached_memory_heap to something better?
    //xg_vk_device_memory_heap_t device_memory_heap = { .vk_heap_idx = UINT32_MAX, .vk_memory_type_idx = UINT32_MAX, .size = 0, .memory_flags = 0 };
    //xg_vk_device_memory_heap_t device_mapped_memory_heap = { .vk_heap_idx = UINT32_MAX, .vk_memory_type_idx = UINT32_MAX, .size = 0, .memory_flags = 0 };
    //xg_vk_device_memory_heap_t host_cached_memory_heap = { .vk_heap_idx = UINT32_MAX, .vk_memory_type_idx = UINT32_MAX, .size = 0, .memory_flags = 0 };
    //xg_vk_device_memory_heap_t host_uncached_memory_heap = { .vk_heap_idx = UINT32_MAX, .vk_memory_type_idx = UINT32_MAX, .size = 0, .memory_flags = 0 };
    uint32_t device_memory_type = UINT32_MAX;
    uint32_t device_mapped_memory_type = UINT32_MAX;
    uint32_t host_uncached_memory_type = UINT32_MAX;
    uint32_t host_cached_memory_type = UINT32_MAX;

    char used_memory_types[VK_MAX_MEMORY_TYPES];
    std_mem_zero_m ( &used_memory_types );

    // Pass 1. try to look for a good match
    for ( uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i ) {
        VkMemoryPropertyFlags flags = device->memory_properties.memoryTypes[i].propertyFlags;
#if std_log_enabled_levels_bitflag_m & ( std_log_level_bit_info_m | std_log_level_bit_warn_m )
        uint32_t heap = device->memory_properties.memoryTypes[i].heapIndex;
#endif

        if ( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) {
            flags &= ~ ( uint32_t ) VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

            if ( flags == 0 ) {
                if ( device_memory_type != UINT32_MAX ) {
                    std_log_warn_m ( "More than one device local memory heap found" );
                }

                device_memory_type = i;
                std_log_info_m ( "Device memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                used_memory_types[i] = 1;
                continue;
            } else if ( flags & ( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ) {
                flags &= ~ ( uint32_t ) VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                flags &= ~ ( uint32_t ) VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

                if ( flags == 0 ) {
                    if ( device_mapped_memory_type != UINT32_MAX ) {
                        std_log_warn_m ( "More than one device mapped memory heap found" );
                    }

                    device_mapped_memory_type = i;
                    std_log_info_m ( "Device mapped memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                    used_memory_types[i] = 1;
                    continue;
                }
            }
        } else if ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
            flags &= ~ ( uint32_t ) VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

            if ( flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) {
                flags &= ~ ( uint32_t ) VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

                if ( flags == 0 ) {
                    if ( host_uncached_memory_type != UINT32_MAX ) {
                        std_log_warn_m ( "More than one staging cached memory heap found" );
                    }

                    host_uncached_memory_type = i;
                    std_log_info_m ( "Host uncached memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                    used_memory_types[i] = 1;
                    continue;
                } else if ( flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) {
                    flags &= ~ ( uint32_t ) VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

                    if ( flags == 0 ) {
                        if ( host_cached_memory_type != UINT32_MAX ) {
                            std_log_warn_m ( "More than one staging uncached memory heap found" );
                        }

                        host_cached_memory_type = i;
                        std_log_info_m ( "Host cached memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                        used_memory_types[i] = 1;
                        continue;
                    }
                }
            }
        }
    }

    // Pass 2. use whatever is compatible
    for ( uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i ) {
        VkMemoryPropertyFlags flags = device->memory_properties.memoryTypes[i].propertyFlags;

#if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
        uint32_t heap = device->memory_properties.memoryTypes[i].heapIndex;
#endif

        if ( flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) {
            if ( device_memory_type == UINT32_MAX ) {
                device_memory_type = i;
                std_log_info_m ( "Device memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                used_memory_types[i] = 1;
            }

            if ( flags & ( VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) ) {
                if ( device_mapped_memory_type == UINT32_MAX ) {
                    device_mapped_memory_type = i;
                    std_log_info_m ( "Device mapped memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                    used_memory_types[i] = 1;
                }
            }
        }

        if ( flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ) {
            if ( flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT ) {
                if ( host_cached_memory_type == UINT32_MAX ) {
                    host_cached_memory_type = i;
                    std_log_info_m ( "Host cached memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                    used_memory_types[i] = 1;
                }
            }

            if ( flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) {
                if ( host_uncached_memory_type == UINT32_MAX ) {
                    host_uncached_memory_type = i;
                    std_log_info_m ( "Host uncached memory heap mapped to heap "std_fmt_u32_m" - memory type "std_fmt_u32_m, heap, i );
                    used_memory_types[i] = 1;
                }
            }
        }
    }

    // Pass 3. check that we're good to go
#if std_log_enabled_levels_bitflag_m & std_log_level_bit_warn_m
    for ( uint32_t i = 0; i < device->memory_properties.memoryTypeCount; ++i ) {
        if ( used_memory_types[i] == 0 ) {
            VkMemoryPropertyFlags flags = device->memory_properties.memoryTypes[i].propertyFlags;
            uint32_t heap = device->memory_properties.memoryTypes[i].heapIndex;

            if ( flags == 0 ) {
                // Skip warning on null memory types
                continue;
            }

            const char* desc = xg_vk_device_memory_type_str ( flags );
            std_log_warn_m ( "Device memory type left unmapped: "std_fmt_u32_m": " std_fmt_str_m" - " std_fmt_u32_m, i, desc, heap );
        }
    }
#endif

    if ( device_memory_type == UINT32_MAX ) {
        std_log_error_m ( "Device local memory heap not found!" );
    }

    if ( device_mapped_memory_type == UINT32_MAX ) {
        std_log_error_m ( "Device mapped memory heap not found!" );
    }

    if ( host_cached_memory_type == UINT32_MAX ) {
        std_log_error_m ( "Host cached memory heap not found!" );
    }

    if ( host_uncached_memory_type == UINT32_MAX ) {
        std_log_error_m ( "Host uncached memory heap not found!" );
    }

    uint32_t device_memory_heap = device->memory_properties.memoryTypes[device_memory_type].heapIndex;
    uint32_t device_mapped_memory_heap = device->memory_properties.memoryTypes[device_mapped_memory_type].heapIndex;
    uint32_t host_uncached_memory_heap = device->memory_properties.memoryTypes[host_uncached_memory_type].heapIndex;
    uint32_t host_cached_memory_heap = device->memory_properties.memoryTypes[host_cached_memory_type].heapIndex;

    device->memory_heaps[xg_memory_type_gpu_only_m] = ( xg_vk_device_memory_heap_t ) { 
        .vk_memory_type_idx = device_memory_type,
        .vk_heap_idx = device_memory_heap,
        .size = device->memory_properties.memoryHeaps[device_memory_heap].size,
        .memory_flags = xg_memory_flags_from_vk ( device->memory_properties.memoryTypes[device_memory_type].propertyFlags ),
    };
    device->memory_heaps[xg_memory_type_gpu_mappable_m] = ( xg_vk_device_memory_heap_t ) { 
        .vk_memory_type_idx = device_mapped_memory_type,
        .vk_heap_idx = device_mapped_memory_heap,
        .size = device->memory_properties.memoryHeaps[device_mapped_memory_heap].size,
        .memory_flags = xg_memory_flags_from_vk ( device->memory_properties.memoryTypes[device_mapped_memory_type].propertyFlags ),
    };
    device->memory_heaps[xg_memory_type_upload_m] = ( xg_vk_device_memory_heap_t ) { 
        .vk_memory_type_idx = host_uncached_memory_type,
        .vk_heap_idx = host_uncached_memory_heap,
        .size = device->memory_properties.memoryHeaps[host_uncached_memory_heap].size,
        .memory_flags = xg_memory_flags_from_vk ( device->memory_properties.memoryTypes[host_uncached_memory_type].propertyFlags ),
    };
    device->memory_heaps[xg_memory_type_readback_m] = ( xg_vk_device_memory_heap_t ) { 
        .vk_memory_type_idx = host_cached_memory_type,
        .vk_heap_idx = host_cached_memory_heap,
        .size = device->memory_properties.memoryHeaps[host_cached_memory_heap].size,
        .memory_flags = xg_memory_flags_from_vk ( device->memory_properties.memoryTypes[host_cached_memory_type].propertyFlags ),
    };

    // Map queue families
    // Only support one queue per type for now
    // TODO does this map properly? need to test on a multi queue device
    VkDeviceQueueCreateInfo* device_queues = device->queue_create_info;
    uint32_t device_queues_count = 0;

    // Find Graphics queue
    bool graphics_queue_found = false;
    VkQueueFlags graphics_queue_flags = 0;

    for ( uint32_t i = 0; i < device->queues_families_count; ++i ) {
        if ( device->queues_families_properties[i].queueCount > 0 && device->queues_families_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) {
            graphics_queue_found = true;
            device->graphics_queue.vk_family_idx = i;
            graphics_queue_flags = device->queues_families_properties[i].queueFlags;
            break;
        }
    }

    if ( graphics_queue_found ) {
        device_queues[device_queues_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        device_queues[device_queues_count].pNext = NULL;
        device_queues[device_queues_count].flags = 0;
        device_queues[device_queues_count].queueFamilyIndex = device->graphics_queue.vk_family_idx;
        device_queues[device_queues_count].queueCount = 1;
        ++device_queues_count;
        std_log_info_m ( "Graphics queue mapped to family " std_fmt_u32_m, device->graphics_queue.vk_family_idx );
    } else {
        std_log_warn_m ( "Graphics queue not found" );
    }

    // Find Compute queue
    bool compute_queue_found = false;
    VkQueueFlags compute_queue_flags = 0;

    for ( uint32_t i = 0; i < device->queues_families_count; ++i ) {
        if ( device->queues_families_properties[i].queueCount > 0 && device->queues_families_properties[i].queueFlags == VK_QUEUE_COMPUTE_BIT ) {
            compute_queue_found = true;
            device->compute_queue.vk_family_idx = i;
            compute_queue_flags = device->queues_families_properties[i].queueFlags;
            break;
        }
    }

    if ( compute_queue_found ) {
        device->flags |= xg_vk_device_dedicated_compute_queue_m;
        device_queues[device_queues_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        device_queues[device_queues_count].pNext = NULL;
        device_queues[device_queues_count].flags = 0;
        device_queues[device_queues_count].queueFamilyIndex = device->compute_queue.vk_family_idx;
        device_queues[device_queues_count].queueCount = 1;
        ++device_queues_count;
        std_log_info_m ( "Compute queue mapped to family " std_fmt_u32_m, device->compute_queue.vk_family_idx );
    } else {
        if ( graphics_queue_found && graphics_queue_flags & VK_QUEUE_COMPUTE_BIT ) {
            device->compute_queue = device->graphics_queue;
            device->compute_queue.vk_family_idx = device->graphics_queue.vk_family_idx;
            std_log_info_m ( "Dedicated compute queue not found - fallback to graphics queue" );
        } else {
            std_log_warn_m ( "Compute queue not found" );
        }
    }

    // Find Copy queue
    bool copy_queue_found = false;

    for ( uint32_t i = 0; i < device->queues_families_count; ++i ) {
        if ( device->queues_families_properties[i].queueCount > 0 && device->queues_families_properties[i].queueFlags == VK_QUEUE_TRANSFER_BIT ) {
            copy_queue_found = true;
            device->copy_queue.vk_family_idx = i;
            break;
        }
    }

    if ( copy_queue_found ) {
        device->flags |= xg_vk_device_dedicated_copy_queue_m;
        device_queues[device_queues_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        device_queues[device_queues_count].pNext = NULL;
        device_queues[device_queues_count].flags = 0;
        device_queues[device_queues_count].queueFamilyIndex = device->copy_queue.vk_family_idx;
        device_queues[device_queues_count].queueCount = 1;
        ++device_queues_count;
        std_log_info_m ( "Copy queue mapped to family " std_fmt_u32_m, device->copy_queue.vk_family_idx );
    } else {
        if ( graphics_queue_found && graphics_queue_flags & VK_QUEUE_TRANSFER_BIT ) {
            device->copy_queue = device->graphics_queue;
            device->copy_queue.vk_family_idx = device->graphics_queue.vk_family_idx;
            std_log_info_m ( "Dedicated copy queue not found - fallback to graphics queue" );
        } else if ( compute_queue_found && compute_queue_flags & VK_QUEUE_TRANSFER_BIT ) {
            device->copy_queue = device->compute_queue;
            device->copy_queue.vk_family_idx = device->compute_queue.vk_family_idx;
            std_log_info_m ( "Dedicated copy queue not found - fallback to compute queue" );
        } else {
            std_log_warn_m ( "Copy queue not found" );
        }
    }

    // Finalize
    std_assert_m ( device_queues_count > 0 );
    device->queue_create_info_count = device_queues_count;
}

// ---- Public xg_vk API ----

void xg_vk_device_load ( xg_vk_device_state_t* state ) {
    xg_vk_device_state = state;

    state->devices_array = std_virtual_heap_alloc_array_m ( xg_vk_device_t, xg_vk_max_devices_m );
    state->devices_freelist = std_freelist_m ( state->devices_array, xg_vk_max_devices_m );
    std_mutex_init ( &state->devices_mutex );

    // Query all physical devices
    uint32_t devices_count = 0;
    vkEnumeratePhysicalDevices ( xg_vk_instance(), &devices_count, NULL );
    VkPhysicalDevice physical_devices[xg_vk_max_devices_m];
    vkEnumeratePhysicalDevices ( xg_vk_instance(), &devices_count, physical_devices );

    //std_mutex_lock ( &state->devices_mutex );

    // Cache properties of each device
    for ( size_t i = 0; i < devices_count; ++i ) {
        xg_vk_device_t* device = std_list_pop_m ( &state->devices_freelist );
        device->vk_physical_handle = physical_devices[i];
        device->id = xg_vk_device_id_next();
        device->flags |= xg_vk_device_existing_m;
        xg_vk_device_cache_properties ( device );
    }

    xg_vk_device_state->hardware_device_count = devices_count;

    //std_mutex_unlock ( &xg_vk_device_state.devices_mutex );
}

void xg_vk_device_reload ( xg_vk_device_state_t* state ) {
    xg_vk_device_state = state;

    for ( size_t i = 0; i < xg_vk_max_devices_m; ++i ) {
        xg_vk_device_t* device = &xg_vk_device_state->devices_array[i];

        if ( device->flags & xg_vk_device_active_m ) {
            xg_vk_device_load_ext_api ( i );
        }
    }

}

void xg_vk_device_wait_idle_all ( void ) {
    for ( size_t i = 0; i < xg_vk_max_devices_m; ++i ) {
        xg_vk_device_t* device = &xg_vk_device_state->devices_array[i];

        if ( device->flags & xg_vk_device_active_m ) {
            vkDeviceWaitIdle ( device->vk_handle );
        }
    }
}

void xg_vk_device_unload ( void ) {
    for ( size_t i = 0; i < xg_vk_max_devices_m; ++i ) {
        xg_vk_device_t* device = &xg_vk_device_state->devices_array[i];

        if ( device->flags & xg_vk_device_active_m ) {
            vkDeviceWaitIdle ( device->vk_handle );

            xg_vk_device_deactivate ( i );
        }
    }

    std_virtual_heap_free ( xg_vk_device_state->devices_array );
}

const xg_vk_device_t* xg_vk_device_get ( xg_device_h device_handle ) {
    const xg_vk_device_t* device = &xg_vk_device_state->devices_array[device_handle];
    return device;
}

// avoid having external code depending on the handle format
uint64_t xg_vk_device_get_idx ( xg_device_h device_handle ) {
    return device_handle;
}

const char* xg_vk_device_memory_type_str ( VkMemoryPropertyFlags flags ) {
    // Cases from https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkPhysicalDeviceMemoryProperties.html
    switch ( flags ) {
        case 0:
            return "Null memory type";

        case VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:
            //return "Staging memory (host coherent)";
            return "Host visible | Host coherent (upload)";

        case VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT:
            //return "Staging memory (host cached)";
            return "Host visible | Host cached (uncoherent readback) ";

        case VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:
            //return "Staging memory (host cached coherent)";
            return "Host visible | Host cached | Host coherent (readback)";

        case VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT:
            //return "Device only memory";
            return "Device local (device)";

        case VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:
            //return "Device mapped memory (host coherent)";
            return "Device local | Host visible | Host coherent (device mappable)";

        //case VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT:
        //return "Device mapped memory (host cached)";

        //case VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:
        //return "Device mapped memory (host cached coherent)";

        //case VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT:
        //return "Device only memory (lazy)";

        //case VK_MEMORY_PROPERTY_PROTECTED_BIT:
        //return "Protected memory";

        //case VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_PROTECTED_BIT:
        //return "Device protected memory";

        default:
            return "Unknown memory type";
    }
}

const char* xg_vk_device_memory_heap_str ( VkMemoryHeapFlags flags ) {
    switch ( flags ) {
        case 0:
            return "Host local heap";

        case VK_MEMORY_HEAP_DEVICE_LOCAL_BIT:
            return "Device local heap";

        case VK_MEMORY_HEAP_DEVICE_LOCAL_BIT | VK_MEMORY_HEAP_MULTI_INSTANCE_BIT:
            return "Multi device local heap (replicated)";

        default:
            return "Unknown memory heap";
    }
}

const char* xg_vk_device_queue_str ( VkQueueFlags flags ) {
    switch ( flags ) {
        case ( VK_QUEUE_GRAPHICS_BIT ) :
            return "Graphics";

        case ( VK_QUEUE_COMPUTE_BIT ) :
            return "Compute";

        case ( VK_QUEUE_TRANSFER_BIT ) :
            return "Copy";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) :
            return "Graphics | Compute";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT ) :
            return "Graphics | Copy";

        case ( VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT ) :
            return "Compute | Copy";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT ) :
            return "Graphics | Compute | Copy";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Graphics | Sparse";

        case ( VK_QUEUE_COMPUTE_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Compute | Sparse";

        case ( VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Copy | Sparse";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Graphics | Compute | Sparse";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Graphics | Copy | Sparse";

        case ( VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Compute | Copy | Sparse";

        case ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT ) :
            return "Graphics | Compute | Copy | Sparse";

        default:
            return "Unknown queue";
    }
}

const char* xg_vk_device_vendor_name ( uint32_t id ) {
    switch ( id ) {
        case ( 0x1002 ) :
            return "ADM";

        case ( 0x10DE ) :
            return "NVIDIA";

        case ( 0x8086 ) :
            return "Intel";

        case ( 0x13B5 ) :
            return "ARM";

        default:
            return "Unknown Vendor";
    }
}

const char* xg_vk_device_type_str ( VkPhysicalDeviceType type ) {
    switch ( type ) {
        case ( VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ) :
            return "Integrated GPU";

        case ( VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) :
            return "Discrete GPU";

        case ( VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ) :
            return "Virtual GPU";

        case ( VK_PHYSICAL_DEVICE_TYPE_CPU ) :
            return "CPU";

        case ( VK_PHYSICAL_DEVICE_TYPE_OTHER ) :
        default:
            return "Unknown GPU type";
    }
}

// ---- Public xg API ----

bool xg_vk_device_activate ( xg_device_h device_handle ) {
    std_mutex_lock ( &xg_vk_device_state->devices_mutex );

    xg_vk_device_t* device = &xg_vk_device_state->devices_array[device_handle];

    if ( device == NULL ) {
        std_mutex_unlock ( &xg_vk_device_state->devices_mutex );
        return false;
    }

    // Fill Layers list
    // *** Device layers are deprecated ***
#if 0
    uint32_t supported_layers_count = 0;
    xg_vk_safecall_m ( vkEnumerateDeviceLayerProperties ( device->physical_id, &supported_layers_count, NULL ), false );
    VkLayerProperties* supported_layers = std_stack_push_array_m ( allocator, VkLayerProperties, supported_layers_count );
    xg_vk_safecall_m ( vkEnumerateDeviceLayerProperties ( device->physical_id, &supported_layers_count, supported_layers ), false );
    const char** enabled_layers = std_stack_push_array_m ( allocator, const char*, supported_layers_count );

    for ( uint32_t i = 0; i < supported_layers_count; ++i ) {
        enabled_layers[i] = supported_layers[i].layerName;
    }

    uint32_t enabled_layers_count = supported_layers_count;
#endif
    const char** enabled_layers = NULL;
    uint32_t enabled_layers_count = 0;

    // Fill Extensions list
    VkExtensionProperties supported_extensions[xg_vk_query_max_device_extensions_m];
    uint32_t supported_extensions_count = 0;
    vkEnumerateDeviceExtensionProperties ( device->vk_physical_handle, NULL, &supported_extensions_count, NULL );
    std_log_info_m ( "Device supports " std_fmt_u32_m " extensions", supported_extensions_count );
    std_assert_m ( xg_vk_query_max_device_extensions_m >= supported_extensions_count, "Device supports more extensions than current cap. Increase xg_vk_query_max_device_extensions_m." );
    vkEnumerateDeviceExtensionProperties ( device->vk_physical_handle, NULL, &supported_extensions_count, supported_extensions );
    const char* enabled_extensions[xg_vk_query_max_device_extensions_m];
    uint32_t enabled_extensions_count = 0;

    //for ( size_t i = 0; i < supported_extensions_count; ++i ) {
    //    std_log_info_m ( std_fmt_str_m, supported_extensions[i] );
    //}

    // TODO take a look at:
    //      VK_KHR_dynamic_rendering - https://www.khronos.org/blog/streamlining-render-passes
    //      VK_EXT_extended_dynamic_state
    //
    const char* required_extensions[] = {
        "VK_KHR_swapchain",
        "VK_KHR_synchronization2",
        "VK_KHR_imageless_framebuffer",
#if xg_enable_raytracing_m
        "VK_KHR_acceleration_structure",
        "VK_KHR_ray_tracing_pipeline",
#if xg_vk_enable_nv_raytracing_ext_m
        "VK_NV_ray_tracing",
#else
        "VK_KHR_ray_query",
#endif
        "VK_KHR_deferred_host_operations",
#endif
    };
    size_t required_extensions_count = std_static_array_capacity_m ( required_extensions );

    const bool fail_on_missing_extension = false;

    for ( size_t i = 0; i < required_extensions_count; ++i ) {
        std_log_info_m ( "Validating requested device extension "std_fmt_str_m"...", required_extensions[i] );
        bool found = false;

        for ( size_t j = 0; j < supported_extensions_count; ++j ) {
            if ( std_str_cmp ( required_extensions[i], supported_extensions[j].extensionName ) == 0 ) {
                enabled_extensions[enabled_extensions_count++] = required_extensions[i];
                found = true;
                break;
            }
        }

        if ( !found ) {
            std_log_warn_m ( "Unable to activate device extension "std_fmt_str_m" !"std_fmt_newline_m, required_extensions[i] );

            if ( fail_on_missing_extension ) {
                std_mutex_unlock ( &xg_vk_device_state->devices_mutex );
                return false;
            }
        }
    }

    // Fill queue info
    // We only support one queue per type for now
    VkDeviceQueueCreateInfo* queue_create_info = device->queue_create_info;
    float queue_priority = 1;

    for ( size_t i = 0; i < device->queue_create_info_count; ++i ) {
        device->queue_create_info[i].pQueuePriorities = &queue_priority;
    }

    // Fill Features list
    VkPhysicalDeviceFeatures enabled_features = {
        .geometryShader = VK_TRUE,
    };

    // Enable sync2 API
    // https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_synchronization2.html
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2_feature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR,
        .pNext = NULL,
        .synchronization2 = VK_TRUE,
    };

    // Enable imageless framebuffers
    // https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_imageless_framebuffer.html
    VkPhysicalDeviceImagelessFramebufferFeatures imageless_framebuffer_feature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES,
        .pNext = &sync2_feature,
        .imagelessFramebuffer = VK_TRUE,
    };

    // Enable buffer device address
    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceBufferDeviceAddressFeatures.html
    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_feature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext = &imageless_framebuffer_feature,
        .bufferDeviceAddress = VK_TRUE,
    };

    // Enable acceleration strucutre
    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceAccelerationStructureFeaturesKHR.html
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_strucutre_feature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext = &buffer_device_address_feature,
        .accelerationStructure = VK_TRUE,
        //.accelerationStructureHostCommands = VK_TRUE,
    };

    // Create Device
    VkDeviceCreateInfo device_create_info;
    device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_create_info.pNext = &acceleration_strucutre_feature;
    device_create_info.flags = 0;
    device_create_info.queueCreateInfoCount = device->queue_create_info_count;
    device_create_info.pQueueCreateInfos = queue_create_info;
    device_create_info.enabledLayerCount = enabled_layers_count;
    device_create_info.ppEnabledLayerNames = enabled_layers;
    device_create_info.enabledExtensionCount = enabled_extensions_count;
    device_create_info.ppEnabledExtensionNames = enabled_extensions;
    device_create_info.pEnabledFeatures = &enabled_features;
    vkCreateDevice ( device->vk_physical_handle, &device_create_info, NULL, &device->vk_handle );

    // Create Device queues
    // We only support one queue per type for now
    vkGetDeviceQueue ( device->vk_handle, device->graphics_queue.vk_family_idx, 0, &device->graphics_queue.vk_handle );
    vkGetDeviceQueue ( device->vk_handle, device->compute_queue.vk_family_idx, 0, &device->compute_queue.vk_handle );
    vkGetDeviceQueue ( device->vk_handle, device->copy_queue.vk_family_idx, 0, &device->copy_queue.vk_handle );

    // Flag as active
    device->flags |= xg_vk_device_active_m;

    std_mutex_unlock ( &xg_vk_device_state->devices_mutex );

    xg_vk_device_load_ext_api ( device_handle );

    xg_vk_allocator_activate_device ( device_handle );
    xg_vk_workload_activate_device ( device_handle );
    xg_vk_pipeline_activate_device ( device_handle );

    return true;
}

bool xg_vk_device_deactivate ( xg_device_h device_handle ) {
    xg_vk_workload_deactivate_device ( device_handle );
    xg_vk_pipeline_deactivate_device ( device_handle );
    xg_vk_allocator_deactivate_device ( device_handle );

    std_mutex_lock ( &xg_vk_device_state->devices_mutex );

    xg_vk_device_t* device = &xg_vk_device_state->devices_array[device_handle];

    if ( device == NULL ) {
        std_mutex_unlock ( &xg_vk_device_state->devices_mutex );
        return false;
    }

    device->flags &= ~ ( xg_vk_device_active_m );

    //vkDestroyCommandPool ( device->logical_id, device->graphics_cmd_pool, NULL );
    //vkDestroyCommandPool ( device->logical_id, device->compute_cmd_pool, NULL );
    //vkDestroyCommandPool ( device->logical_id, device->copy_cmd_pool, NULL );
    vkDestroyDevice ( device->vk_handle, NULL );

    std_mutex_unlock ( &xg_vk_device_state->devices_mutex );

    return true;
}

size_t xg_vk_device_get_count () {
    return xg_vk_device_state->hardware_device_count;
}

size_t xg_vk_device_get_list ( xg_device_h* device_handles, size_t device_handles_cap ) {
    size_t device_handles_count = 0;

    std_mutex_lock ( &xg_vk_device_state->devices_mutex );

    for ( size_t i = 0; device_handles_count < device_handles_cap && i < xg_vk_max_devices_m; ++i ) {
        xg_vk_device_t* device = &xg_vk_device_state->devices_array[i];

        if ( device->flags & xg_vk_device_existing_m ) {
            device_handles[device_handles_count] = ( xg_device_h ) i;
            ++device_handles_count;
        }
    }

    std_mutex_unlock ( &xg_vk_device_state->devices_mutex );

    return device_handles_count;
}

bool xg_vk_device_get_info ( xg_device_info_t* info, xg_device_h device_handle ) {
    std_mutex_lock ( &xg_vk_device_state->devices_mutex );

    xg_vk_device_t* device = &xg_vk_device_state->devices_array[device_handle];

    if ( device == NULL ) {
        std_mutex_unlock ( &xg_vk_device_state->devices_mutex );
        return false;
    }

    info->api_version = device->generic_properties.apiVersion;
    info->driver_version = device->generic_properties.driverVersion;
    info->vendor_id = device->generic_properties.vendorID;
    info->product_id = device->generic_properties.deviceID;

    switch ( device->generic_properties.deviceType ) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            info->type = xg_device_integrated_m;
            break;

        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            info->type = xg_device_discrete_m;
            break;

        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            info->type = xg_device_virtual_m;
            break;

        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            info->type = xg_device_cpu_m;
            break;

        default:
            info->type = xg_device_unknown_m;
            break;
    }

    std_str_copy ( info->name, xg_device_name_size_m, device->generic_properties.deviceName );
    info->dedicated_compute_queue = device->flags & xg_vk_device_dedicated_compute_queue_m;
    info->dedicated_copy_queue = device->flags & xg_vk_device_dedicated_copy_queue_m;

    info->uniform_buffer_alignment = ( uint64_t ) device->generic_properties.limits.minUniformBufferOffsetAlignment;

    // TODO
    // check xg_vk_enable_raytracing and success on extension activation/device features available
    info->supports_raytrace = false;

    std_mutex_unlock ( &xg_vk_device_state->devices_mutex );

    return true;
}

// TODO query this at init and cache this?
size_t xg_vk_device_get_displays_count ( xg_device_h handle ) {
    std_mutex_lock ( &xg_vk_device_state->devices_mutex );

    uint32_t displays_count = 0;
    xg_vk_device_t* device = &xg_vk_device_state->devices_array[handle];
    xg_vk_safecall_m ( vkGetPhysicalDeviceDisplayPropertiesKHR ( device->vk_physical_handle, &displays_count, NULL ), 0 );

    std_mutex_unlock ( &xg_vk_device_state->devices_mutex );

    return displays_count;
}

size_t xg_vk_device_get_displays_info ( xg_display_info_t* displays, size_t cap, xg_device_h handle ) {
    std_mutex_lock ( &xg_vk_device_state->devices_mutex );

    // Diplays
    uint32_t device_displays_count = 0;
    xg_vk_device_t* device = &xg_vk_device_state->devices_array[handle];
    xg_vk_safecall_m ( vkGetPhysicalDeviceDisplayPropertiesKHR ( device->vk_physical_handle, &device_displays_count, NULL ), 0 );
    uint32_t displays_count = device_displays_count;

    if ( std_unlikely_m ( device_displays_count > cap ) ) {
        std_log_warn_m ( "Found display count higher than display limit!" );
        displays_count = ( uint32_t ) cap;
    }

    std_assert_m ( xg_vk_query_max_device_displays_m >= device_displays_count, "Device is connected to more displays than current cap. Increase xg_vk_query_max_device_displays_m." );
    VkDisplayPropertiesKHR vk_displays[xg_vk_query_max_device_displays_m];
    xg_vk_safecall_m ( vkGetPhysicalDeviceDisplayPropertiesKHR ( device->vk_physical_handle, &displays_count, vk_displays ), 0 );

    for ( size_t i = 0; i < displays_count; ++i ) {
        displays[i].display_handle = ( uint64_t ) vk_displays[i].display;
        std_str_copy ( displays[i].name, xg_display_name_size_m, vk_displays[i].displayName );
        displays[i].pixel_width = vk_displays[i].physicalResolution.width;
        displays[i].pixel_height = vk_displays[i].physicalResolution.height;
        displays[i].millimeter_width = vk_displays[i].physicalDimensions.width;
        displays[i].millimeter_height = vk_displays[i].physicalDimensions.height;
        uint32_t modes_count = 0;
        xg_vk_safecall_m ( vkGetDisplayModePropertiesKHR ( device->vk_physical_handle, vk_displays[i].display, &modes_count, NULL ), 0 );
        std_assert_m ( xg_display_max_modes_m >= modes_count, "Display has more display modes than current cap. Increase xg_display_max_modes_m." );
        VkDisplayModePropertiesKHR vk_modes[xg_display_max_modes_m];
        xg_vk_safecall_m ( vkGetDisplayModePropertiesKHR ( device->vk_physical_handle, vk_displays[i].display, &modes_count, vk_modes ), 0 );

        for ( size_t j = 0; j < modes_count; ++j ) {
            displays[i].display_modes[j].id = ( uint64_t ) vk_modes[i].displayMode;
            displays[i].display_modes[j].refresh_rate = vk_modes[i].parameters.refreshRate;
            displays[i].display_modes[j].width = vk_modes[i].parameters.visibleRegion.width;
            displays[i].display_modes[j].height = vk_modes[i].parameters.visibleRegion.height;
        }
    }

    std_mutex_unlock ( &xg_vk_device_state->devices_mutex );

    return device_displays_count;
}

char* xg_vk_device_map_alloc ( const xg_alloc_t* alloc ) {
    const xg_vk_device_t* device = xg_vk_device_get ( alloc->device );
    std_assert_m ( device );
    void* data = NULL;
    VkResult result = vkMapMemory ( device->vk_handle, ( VkDeviceMemory ) alloc->base, ( VkDeviceSize ) alloc->offset, ( VkDeviceSize ) alloc->handle.size, 0, &data );
    std_verify_m ( result == VK_SUCCESS );
    std_assert_m ( data != NULL );
    return ( char* ) data;
}

void xg_vk_device_unmap_alloc ( const xg_alloc_t* alloc ) {
    const xg_vk_device_t* device = xg_vk_device_get ( alloc->device );
    std_assert_m ( device );
    vkUnmapMemory ( device->vk_handle, ( VkDeviceMemory ) alloc->base );
    // TODO call vkFlushMappedMemoryRanges if the unmapped memory is not coherent (always or expose a flag to control when?)
    // currenly the assumption when mapping heaps is that all used memory is coherent, TODO fix that too
}

xg_vk_device_ext_api_i* xg_vk_device_ext_api ( xg_device_h device_handle ) {
    xg_vk_device_t* device = &xg_vk_device_state->devices_array[device_handle];
    return &device->ext_api;
}

#if 0
void xg_vk_device_map_host_buffer ( xg_host_buffer_t* buffer ) {
    buffer->mapped_base = ( char* ) xg_vk_device_map_alloc ( &buffer->alloc );
}

void xg_vk_device_unmap_host_buffer ( xg_host_buffer_t* buffer ) {
    xg_vk_device_unmap_alloc ( &buffer->alloc );
    buffer->mapped_base = NULL;
}
#endif
