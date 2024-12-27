#pragma once

#include "xg_vk.h"

typedef struct {
    void ( *cmd_begin_debug_region ) ( VkCommandBuffer, const VkDebugUtilsLabelEXT* );
    void ( *cmd_end_debug_region ) ( VkCommandBuffer );

    // vkSetDebugUtilsObjectNameEXT
    void ( *set_debug_name ) ( VkDevice, const VkDebugUtilsObjectNameInfoEXT* );

#if xg_vk_enable_sync2_m
    void ( *cmd_sync2_pipeline_barrier ) ( VkCommandBuffer, const VkDependencyInfoKHR* );
#endif

    void ( *set_debug_callback ) ( VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT* );

    //vkDestroyDebugUtilsMessengerEXT
    void ( *destroy_debug_callback ) ( VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks* );

#if xg_enable_raytracing_m
    // vkGetAccelerationStructureBuildSizesKHR
    void ( *get_acceleration_structure_build_sizes ) ( VkDevice, VkAccelerationStructureBuildTypeKHR, const VkAccelerationStructureBuildGeometryInfoKHR*, const uint32_t*, VkAccelerationStructureBuildSizesInfoKHR* );

    // vkCreateAccelerationStructureKHR
    VkResult ( *create_acceleration_structure ) ( VkDevice, const VkAccelerationStructureCreateInfoKHR*, const VkAllocationCallbacks*, VkAccelerationStructureKHR* );
    
    // vkBuildAccelerationStructuresKHR
    VkResult ( *build_acceleration_structures ) ( VkDevice, VkDeferredOperationKHR, uint32_t, const VkAccelerationStructureBuildGeometryInfoKHR*, const VkAccelerationStructureBuildRangeInfoKHR* const* );
    
    // vkGetAccelerationStructureDeviceAddressKHR
    VkDeviceAddress ( *get_acceleration_structure_device_address ) ( VkDevice, const VkAccelerationStructureDeviceAddressInfoKHR* );
#endif
} xg_vk_instance_ext_api_i;

typedef struct {
    VkInstance vk_handle;
    VkDebugUtilsMessengerEXT callback;
    xg_vk_instance_ext_api_i ext_api;
    VkAllocationCallbacks   cpu_allocator;
} xg_vk_instance_state_t;

void                        xg_vk_instance_load ( xg_vk_instance_state_t* state, xg_runtime_layer_bit_e layers );
void                        xg_vk_instance_reload ( xg_vk_instance_state_t* state );
void  						xg_vk_instance_unload ( void );

VkInstance                  xg_vk_instance ( void );

xg_vk_instance_ext_api_i*   xg_vk_instance_ext_api ( void );

VkAllocationCallbacks*      xg_vk_cpu_allocator ( void );
