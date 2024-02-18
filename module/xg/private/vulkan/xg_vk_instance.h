#pragma once

#include "xg_vk.h"

typedef struct {
    void ( *cmd_begin_debug_region ) ( VkCommandBuffer, const VkDebugUtilsLabelEXT* );
    void ( *cmd_end_debug_region ) ( VkCommandBuffer );

    void ( *set_debug_name ) ( VkDevice, const VkDebugUtilsObjectNameInfoEXT* );

#if std_enabled_m(xg_vk_enable_sync2_m)
    void ( *cmd_sync2_pipeline_barrier ) ( VkCommandBuffer, const VkDependencyInfoKHR* );
#endif

    void ( *set_debug_callback ) ( VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT* );
} xg_vk_instance_ext_api_i;

typedef struct {
    VkInstance vk_handle;
    xg_vk_instance_ext_api_i ext_api;
} xg_vk_instance_state_t;

void                        xg_vk_instance_load ( xg_vk_instance_state_t* state, xg_runtime_layer_f layers );
void                        xg_vk_instance_reload ( xg_vk_instance_state_t* state );
void  						xg_vk_instance_unload ( void );

VkInstance                  xg_vk_instance ( void );

xg_vk_instance_ext_api_i*   xg_vk_instance_ext_api ( void );
