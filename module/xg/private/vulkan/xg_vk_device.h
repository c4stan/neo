#pragma once

#include <xg.h>

#include "xg_vk.h"

#include <std_allocator.h>

// ---- Public xg API ----
size_t      xg_vk_device_get_count ( void );
size_t      xg_vk_device_get_list ( xg_device_h* devices, size_t cap );
bool        xg_vk_device_get_info ( xg_device_info_t* info, xg_device_h device );

bool        xg_vk_device_activate ( xg_device_h device );
bool        xg_vk_device_deactivate ( xg_device_h device );

size_t      xg_vk_device_get_displays_count ( xg_device_h device );
size_t      xg_vk_device_get_displays_info ( xg_display_info_t* displays, size_t cap, xg_device_h device );

// ---- Public xg_vk API ----
typedef enum {
    xg_vk_device_existing_m                   = 1 << 0,
    xg_vk_device_active_m                     = 1 << 1,
    xg_vk_device_dedicated_compute_queue_m    = 1 << 3,
    xg_vk_device_dedicated_copy_queue_m       = 1 << 4,
} xg_vk_device_f;

typedef struct {
    uint32_t vk_heap_idx;
    uint32_t vk_memory_type_idx;
    xg_memory_flag_bit_e memory_flags;
    size_t size;
} xg_vk_device_memory_heap_t;

typedef struct {
    VkQueue vk_handle;
    uint32_t vk_family_idx;
} xg_vk_device_cmd_queue_t;

typedef struct {
    void ( *cmd_begin_debug_region ) ( VkCommandBuffer, const VkDebugUtilsLabelEXT* );
    void ( *cmd_end_debug_region ) ( VkCommandBuffer );

    // vkSetDebugUtilsObjectNameEXT
    void ( *set_debug_name ) ( VkDevice, const VkDebugUtilsObjectNameInfoEXT* );

#if xg_vk_enable_sync2_m
    void ( *cmd_sync2_pipeline_barrier ) ( VkCommandBuffer, const VkDependencyInfoKHR* );
#endif

//#if xg_enable_raytracing_m
    // vkGetAccelerationStructureBuildSizesKHR
    void ( *get_acceleration_structure_build_sizes ) ( VkDevice, VkAccelerationStructureBuildTypeKHR, const VkAccelerationStructureBuildGeometryInfoKHR*, const uint32_t*, VkAccelerationStructureBuildSizesInfoKHR* );

    // vkCreateAccelerationStructureKHR
    VkResult ( *create_acceleration_structure ) ( VkDevice, const VkAccelerationStructureCreateInfoKHR*, const VkAllocationCallbacks*, VkAccelerationStructureKHR* );
    
    // vkCmdBuildAccelerationStructuresKHR
    void ( *cmd_build_acceleration_structures ) (
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR* pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR* const* ppBuildRangeInfos );

    // vkGetAccelerationStructureDeviceAddressKHR
    VkDeviceAddress ( *get_acceleration_structure_device_address ) ( VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR* );

#if xg_vk_enable_nv_raytracing_ext_m
    // vkCreateRayTracingPipelinesNV
    VkResult ( *create_raytrace_pipelines ) (
        VkDevice                                    device,
        VkPipelineCache                             pipelineCache,
        uint32_t                                    createInfoCount,
        const VkRayTracingPipelineCreateInfoNV*     pCreateInfos,
        const VkAllocationCallbacks*                pAllocator,
        VkPipeline*                                 pPipelines );
#else
    // vkCreateRayTracingPipelinesKHR
    VkResult ( *create_raytrace_pipelines ) (
        VkDevice                                    device,
        VkDeferredOperationKHR                      deferredOperation,
        VkPipelineCache                             pipelineCache,
        uint32_t                                    createInfoCount,
        const VkRayTracingPipelineCreateInfoKHR*    pCreateInfos,
        const VkAllocationCallbacks*                pAllocator,
        VkPipeline*                                 pPipelines );
#endif

    // vkGetRayTracingShaderGroupHandlesKHR
    VkResult ( *get_shader_group_handles ) (
        VkDevice                                    device,
        VkPipeline                                  pipeline,
        uint32_t                                    firstGroup,
        uint32_t                                    groupCount,
        size_t                                      dataSize,
        void*                                       pData );

    // vkCmdTraceRaysKHR
    void ( *trace_rays ) (
        VkCommandBuffer                             commandBuffer,
        const VkStridedDeviceAddressRegionKHR*      pRaygenShaderBindingTable,
        const VkStridedDeviceAddressRegionKHR*      pMissShaderBindingTable,
        const VkStridedDeviceAddressRegionKHR*      pHitShaderBindingTable,
        const VkStridedDeviceAddressRegionKHR*      pCallableShaderBindingTable,
        uint32_t                                    width,
        uint32_t                                    height,
        uint32_t                                    depth );

    // vkDestroyAccelerationStructureKHR
    void ( *destroy_acceleration_structure ) (
        VkDevice                                    device,
        VkAccelerationStructureKHR                  accelerationStructure,
        const VkAllocationCallbacks*                pAllocator );

    // TODO non-nvda
    // vkGetQueueCheckpointDataNV
    void ( *get_checkpoints ) (
        VkQueue                                     queue,
        uint32_t*                                   pCheckpointDataCount,
        VkCheckpointDataNV*                         pCheckpointData );

    // vkCmdSetCheckpointNV
    void ( *cmd_set_checkpoint ) (
        VkCommandBuffer                             commandBuffer,
        const void*                                 pCheckpointMarker);

//#endif
} xg_vk_device_ext_api_i;

typedef struct {
    uint64_t                            id;
    xg_vk_device_f                      flags;
    // Handles
    VkPhysicalDevice                    vk_physical_handle;
    VkDevice                            vk_handle;
    // Properties
    VkPhysicalDeviceProperties          generic_properties;               // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkPhysicalDeviceProperties.html
    VkPhysicalDeviceFeatures            supported_features;               // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkPhysicalDeviceFeatures.html
    
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR     raytrace_properties; //
    VkPhysicalDeviceAccelerationStructurePropertiesKHR  acceleration_structure_properties;
    
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR       supported_raytrace_features; // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceRayTracingPipelineFeaturesKHR.html
    VkPhysicalDeviceBufferDeviceAddressFeatures         supported_device_address_features; // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceBufferDeviceAddressFeatures.html
    VkPhysicalDeviceAccelerationStructureFeaturesKHR    supported_acceleration_structure_features; // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceAccelerationStructureFeaturesKHR.html
    
    VkPhysicalDeviceMemoryProperties    memory_properties;                // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkPhysicalDeviceMemoryProperties.html
    
    VkQueueFamilyCheckpointPropertiesNV queue_checkpoint_properties[16];
    VkQueueFamilyProperties             queue_family_properties[16];   // https://www.khronos.org/registry/vulkan/specs/1.1-extensions/man/html/VkQueueFamilyProperties.html
    uint32_t                            queue_family_count;
    VkDeviceQueueCreateInfo             queue_create_info[16 * 3];  // We determine these when we cache vulkan properties, use them when we create the device (on activation)
    uint32_t                            queue_create_info_count;
    // Main queues
    //VkQueue                             graphics_queue;
    //uint32_t                            graphics_queue_family_index;
    //VkQueue                             compute_queue;
    //uint32_t                            compute_queue_family_index;
    //VkQueue                             copy_queue;
    //uint32_t                            copy_queue_family_index;
    // Command queues
    xg_vk_device_cmd_queue_t            queues[xg_cmd_queue_count_m];
    //xg_vk_device_cmd_queue_t            graphics_queue;
    //xg_vk_device_cmd_queue_t            compute_queue;
    //xg_vk_device_cmd_queue_t            copy_queue;
    // Memory heaps
    xg_vk_device_memory_heap_t          memory_heaps[xg_memory_type_count_m];
    //xg_vk_device_memory_heap_t          device_memory_heap;
    //xg_vk_device_memory_heap_t          device_mapped_memory_heap;
    //xg_vk_device_memory_heap_t          host_cached_memory_heap;
    //xg_vk_device_memory_heap_t          host_uncached_memory_heap;
    // Memory indexes (indexes into memory_properties.memoryTypes)
    //uint32_t                            device_memory_index;
    //uint32_t                            device_mapped_memory_index;
    //uint32_t                            staging_cached_memory_index;
    //uint32_t                            staging_uncached_memory_index;
    // Cmd pools
    // TODO remove these
    //VkCommandPool                       graphics_cmd_pool;
    //VkCommandPool                       compute_cmd_pool;
    //VkCommandPool                       copy_cmd_pool;
    xg_vk_device_ext_api_i              ext_api;
} xg_vk_device_t;

typedef struct {
    xg_vk_device_t* devices_array;
    xg_vk_device_t* devices_freelist;
    std_mutex_t devices_mutex;
    size_t hardware_device_count;
    uint64_t device_id;
} xg_vk_device_state_t;

void xg_vk_device_load ( xg_vk_device_state_t* state );
void xg_vk_device_reload ( xg_vk_device_state_t* state );
void xg_vk_device_unload ( void );

const xg_vk_device_t* xg_vk_device_get ( xg_device_h handle );

const char* xg_vk_device_memory_type_str ( VkMemoryPropertyFlags flags );
const char* xg_vk_device_memory_heap_str ( VkMemoryHeapFlags flags );
const char* xg_vk_device_queue_str ( VkQueueFlags flags );
const char* xg_vk_device_type_str ( VkPhysicalDeviceType type );
const char* xg_vk_device_vendor_name ( uint32_t id );

uint64_t xg_vk_device_get_idx ( xg_device_h device );

char* xg_vk_device_map_alloc ( const xg_alloc_t* alloc );
void xg_vk_device_unmap_alloc ( const xg_alloc_t* alloc );

//void xg_vk_device_map_host_buffer ( xg_host_buffer_t* buffer );
//void xg_vk_device_unmap_host_buffer ( xg_host_buffer_t* buffer );

void xg_vk_device_wait_idle_all ( void );

xg_vk_device_ext_api_i* xg_vk_device_ext_api ( xg_device_h device );
