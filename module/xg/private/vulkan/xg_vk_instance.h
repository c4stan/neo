#pragma once

#include "xg_vk.h"

typedef struct {
    //vkCreateDebugUtilsMessengerEXT
    void ( *set_debug_callback ) ( VkInstance, const VkDebugUtilsMessengerCreateInfoEXT*, const VkAllocationCallbacks*, VkDebugUtilsMessengerEXT* );
    
    //vkDestroyDebugUtilsMessengerEXT
    void ( *destroy_debug_callback ) ( VkInstance, VkDebugUtilsMessengerEXT, const VkAllocationCallbacks* );
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

VkAllocationCallbacks*      xg_vk_cpu_allocator ( void );
