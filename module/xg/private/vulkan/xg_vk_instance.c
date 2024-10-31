#include "xg_vk_instance.h"

static xg_vk_instance_state_t* xg_vk_instance_state;

static VkInstance xg_vk_instance_create ( const char** layers, size_t layers_count, const char** extensions, size_t extensions_count ) {
    std_log_info_m ( "Creating new VkInstance with " std_fmt_size_m " requested layers and " std_fmt_size_m " requested extensions...", layers_count, extensions_count );
    VkInstance instance;

    // Validate Layers
    const char* enabled_layers[xg_vk_query_max_layers_m];
    uint32_t enabled_layers_count = 0;
    {
        uint32_t supported_layers_count = 0;
        xg_vk_safecall_m ( vkEnumerateInstanceLayerProperties ( &supported_layers_count, NULL ), VK_NULL_HANDLE );
        std_assert_m ( xg_vk_query_max_layers_m >= supported_layers_count, "Device supports more layers than current cap. Increase xg_vk_query_max_layers_m." );
        VkLayerProperties supported_layers[xg_vk_query_max_layers_m];
        xg_vk_safecall_m ( vkEnumerateInstanceLayerProperties ( &supported_layers_count, supported_layers ), VK_NULL_HANDLE );
        enabled_layers_count = 0;

        for ( size_t i = 0; i < layers_count; ++i ) {
            bool found = false;
            std_log_info_m ( "Validating requested layer " std_fmt_str_m " ...", layers[i] );

            for ( uint32_t j = 0; j < supported_layers_count; ++j ) {
                if ( std_str_cmp ( layers[i], supported_layers[j].layerName ) == 0 ) {
                    enabled_layers[enabled_layers_count++] = layers[i];
                    found = true;
                    break;
                }
            }

            if ( !found ) {
                std_log_warn_m ( "Requested layer " std_fmt_str_m " is NOT supported!", layers[i] );
            }
        }
    }

    // Validate Extensions
    const char* enabled_extensions[xg_vk_query_max_instance_extensions_m];
    uint32_t enabled_extensions_count = 0;
    {
        uint32_t supported_extensions_count = 0;
        xg_vk_safecall_m ( vkEnumerateInstanceExtensionProperties ( NULL, &supported_extensions_count, NULL ), VK_NULL_HANDLE );
        std_assert_m ( xg_vk_query_max_instance_extensions_m >= supported_extensions_count, "Vulkan Instance supports more extensions than current cap. Increase xg_vk_query_max_instance_extensions_m." );
        VkExtensionProperties supported_extensions[xg_vk_query_max_instance_extensions_m];
        xg_vk_safecall_m ( vkEnumerateInstanceExtensionProperties ( NULL, &supported_extensions_count, supported_extensions ), VK_NULL_HANDLE );

        for ( uint32_t i = 0; i < extensions_count; ++i ) {
            bool found = false;
            std_log_info_m ( "Validating requested instance extension " std_fmt_str_m " ...", extensions[i] );

            for ( uint32_t j = 0; j < supported_extensions_count; ++j ) {
                if ( std_str_cmp ( extensions[i], supported_extensions[j].extensionName ) == 0 ) {
                    enabled_extensions[enabled_extensions_count++] = extensions[i];
                    found = true;
                    break;
                }
            }

            if ( !found ) {
                std_log_warn_m ( "Requested instance extension " std_fmt_str_m " is NOT supported!", extensions[i] );
            }
        }
    }

    // Create Instance
    {
    #if std_log_enabled_levels_bitflag_m & std_log_level_bit_info_m
        uint32_t instance_version;
        xg_vk_safecall_m ( vkEnumerateInstanceVersion ( &instance_version ), VK_NULL_HANDLE );
        uint32_t variant = VK_API_VERSION_VARIANT ( instance_version );
        uint32_t major = VK_API_VERSION_MAJOR ( instance_version );
        uint32_t minor = VK_API_VERSION_MINOR ( instance_version );
        uint32_t patch = VK_API_VERSION_PATCH ( instance_version );
        std_assert_m ( variant == 0 );
        std_log_info_m ( "Using Vulkan API version " std_fmt_u32_m "." std_fmt_u32_m "." std_fmt_u32_m, major, minor, patch );
    #endif

        // TODO enable in debug only
#if 0
        VkValidationFeatureEnableEXT enabled_validation_features[] = { VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT };
        VkValidationFeaturesEXT validation_features = {
            .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
            .enabledValidationFeatureCount = 1,
            .pEnabledValidationFeatures = enabled_validation_features,
        };
#endif
        
        VkApplicationInfo applicationInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = NULL,
            .pApplicationName = "XG",
            .applicationVersion = 1,
            .pEngineName = "XG",
            .engineVersion = 1,
            .apiVersion = VK_API_VERSION_1_3,//instance_version;
        };
        VkInstanceCreateInfo instanceInfo = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = NULL,//&validation_features,
            .flags = 0,
            .pApplicationInfo = &applicationInfo,
            .enabledLayerCount = enabled_layers_count,
            .ppEnabledLayerNames = enabled_layers,
            .enabledExtensionCount = enabled_extensions_count,
            .ppEnabledExtensionNames = enabled_extensions,
        };
        
        VkResult result = vkCreateInstance ( &instanceInfo, NULL, &instance );
        std_verify_m ( result == VK_SUCCESS );
    }
    return instance;
}

static void xg_vk_instance_load_ext_api ( void ) {
#define xg_vk_instance_ext_init_pfn_m(ptr, name) { *(ptr) = ( std_typeof_m ( *(ptr) ) ) ( vkGetInstanceProcAddr ( xg_vk_instance_state->vk_handle, name ) ); std_assert_m ( *ptr ); }

    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.cmd_begin_debug_region, "vkCmdBeginDebugUtilsLabelEXT" );
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.cmd_end_debug_region, "vkCmdEndDebugUtilsLabelEXT" );
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.set_debug_name, "vkSetDebugUtilsObjectNameEXT" );
#if xg_vk_enable_sync2_m
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.cmd_sync2_pipeline_barrier, "vkCmdPipelineBarrier2KHR" );
#endif
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.set_debug_callback, "vkCreateDebugUtilsMessengerEXT" );
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.destroy_debug_callback, "vkDestroyDebugUtilsMessengerEXT" );

#if xg_enable_raytracing_m
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.get_acceleration_structure_build_sizes, "vkGetAccelerationStructureBuildSizesKHR" );
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.create_acceleration_structure, "vkCreateAccelerationStructureKHR" );
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.build_acceleration_structures, "vkBuildAccelerationStructuresKHR" );
    xg_vk_instance_ext_init_pfn_m ( &xg_vk_instance_state->ext_api.get_acceleration_structure_device_address, "vkGetAccelerationStructureDeviceAddressKHR" );
#endif

#undef xg_vk_instance_ext_init_pfn_m    
}

static VKAPI_ATTR VkBool32 VKAPI_CALL xg_vk_instance_debug_callback ( VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data ) {
    std_unused_m ( type );
    std_unused_m ( user_data );

    if ( severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT || severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT ) {
        std_log_info_m ( data->pMessage );
    } else if ( severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {
        std_log_warn_m ( data->pMessage );
    } else if ( severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
#if xg_instance_crash_on_validation_error_m
        std_log_crash_m ( data->pMessage );
#else
        std_log_error_m ( data->pMessage );
#endif
    }

    if ( std_log_debugger_attached() ) {
        if ( severity != VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ) {
            std_debug_break_m();
        }
    }

    return VK_FALSE;
}

static void xg_vk_instance_destroy_debug_callback ( void ) {
    std_assert_m ( xg_vk_instance_state->callback != VK_NULL_HANDLE );
    xg_vk_instance_state->ext_api.destroy_debug_callback ( xg_vk_instance_state->vk_handle, xg_vk_instance_state->callback, NULL );
    xg_vk_instance_state->callback = VK_NULL_HANDLE;
}

static void xg_vk_instance_create_debug_callback ( void ) {
    std_assert_m ( xg_vk_instance_state->callback == VK_NULL_HANDLE );

    VkDebugUtilsMessengerCreateInfoEXT info = {0};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = xg_vk_instance_debug_callback;
    info.pUserData = NULL;

    xg_vk_instance_state->ext_api.set_debug_callback ( xg_vk_instance_state->vk_handle, &info, NULL, &xg_vk_instance_state->callback );
}

void xg_vk_instance_load ( xg_vk_instance_state_t* state, xg_runtime_layer_bit_e layers_flags ) {
    xg_vk_instance_state = state;

    /*
    LAYERS
        VK_LAYER_LUNARG_core_validation
            Validates desriptor set, pipeline state, dynamic state, GPU memory bindings
        VK_LAYER_LUNARG_standard_validation
            ?
        VK_LAYER_LUNARG_image
            Validates render target and textures formats
        VK_LAYER_LUNARG_parameter_validation
            Validates API calls parameters
        VK_LAYER_GOOGLE_unique_objects
            Makes sure each object has his own unique handle (supposedly easier to debug?)
        VK_LAYER_LUNARG_object_tracker
            Track vulkan objects, their usage and their state whenever used
        VK_LAYER_LUNARG_swapchain
            Valiadtes the ESI extension ?
        VK_LAYER_GOOGLE_threading
            Validates objects access from multiple threads
        VK_LAYER_RENDERDOC_Capture
            RenderDoc layer
    EXTENSIONS
        VK_KHR_surface
            VkSurfaceKHR types
        VK_KHR_win32_surface
            VkSurfaceKHR methods for win32
    */
    const char* base_layers[] = {
        "VK_LAYER_KHRONOS_synchronization2",
    };

    const char* debug_layers[] = {      // Additional debug info/warning/error reporting
#if std_build_debug_m
        "VK_LAYER_KHRONOS_validation",
#endif
    };

    const char* renderdoc_layers[] = {  // RenderDoc support
#if !xg_enable_raytracing_m
        // RenderDoc layer kills RT device extensions...
        //"VK_LAYER_RENDERDOC_Capture"
        //"VK_LAYER_NV_nsight"
#endif
    };

    const char* base_extensions[] = {   // Always enabled
        "VK_KHR_surface",
        "VK_KHR_display",
        "VK_EXT_debug_utils", // TODO enable only if a "debug something" define is set?
        //"VK_KHR_create_renderpass2",
        //"VK_KHR_get_physical_device_properties2", //https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_synchronization2.html lists it as required, but not enabling it doesn't trigger an error on sync2?
    };

    const char* platform_extensions[] = {
#if defined(std_platform_win32_m)
        "VK_KHR_win32_surface",
#elif defined(std_platform_linux_m)
        "VK_KHR_xlib_surface",
#endif
    };

    const char* layers[std_static_array_capacity_m ( base_layers ) + std_static_array_capacity_m ( debug_layers ) + std_static_array_capacity_m ( renderdoc_layers )];
    const char* extensions[std_static_array_capacity_m ( base_extensions ) + std_static_array_capacity_m ( platform_extensions )];

    size_t layers_count = std_static_array_capacity_m ( base_layers );
    size_t extensions_count = std_static_array_capacity_m ( base_extensions );

    // Compute layers/extensions count
    if ( layers_flags & xg_runtime_layer_bit_debug_m ) {
        layers_count += std_static_array_capacity_m ( debug_layers );
    }

    if ( layers_flags & xg_runtime_layer_bit_renderdoc_m ) {
        layers_count += std_static_array_capacity_m ( renderdoc_layers );
    }

    extensions_count += std_static_array_capacity_m ( platform_extensions );

    // Allocate layers/extensions names list
    size_t layer_i = 0;
    size_t extension_i = 0;

    char layers_buffer[xg_vk_query_instance_layers_buffer_size_m];
    char extensions_buffer[xg_vk_query_instance_extensions_buffer_size_m];
    std_stack_t layers_allocator = std_static_stack_m ( layers_buffer );
    std_stack_t extensions_allocator = std_static_stack_m ( extensions_buffer );

    for ( size_t i = 0; i < std_static_array_capacity_m ( base_layers ); ++i ) {
        layers[layer_i++] = std_stack_string_copy ( &layers_allocator, base_layers[i] );
        //size_t size = std_str_len ( base_layers[i] ) + 1;
        //layers[layer_i] = std_stack_push_array_m ( &layers_allocator, char, size );
        //std_mem_copy ( ( char* ) layers[layer_i], base_layers[i], size );
        //++layer_i;
    }

    if ( layers_flags & xg_runtime_layer_bit_debug_m ) {
        for ( size_t i = 0; i < std_static_array_capacity_m ( debug_layers ); ++i ) {
            layers[layer_i++] = std_stack_string_copy ( &layers_allocator, debug_layers[i] );
            //size_t size = std_str_len ( debug_layers[i] ) + 1;
            //layers[layer_i] = std_stack_push_array_m ( &layers_allocator, char, size );
            //std_mem_copy ( ( char* ) layers[layer_i], debug_layers[i], size );
            //++layer_i;
        }
    }

    if ( layers_flags & xg_runtime_layer_bit_renderdoc_m ) {
        for ( size_t i = 0; i < std_static_array_capacity_m ( renderdoc_layers ); ++i ) {
            layers[layer_i++] = std_stack_string_copy ( &layers_allocator, renderdoc_layers[i] );
            //size_t size = std_str_len ( renderdoc_layers[i] ) + 1;
            //layers[layer_i] = std_stack_push_array_m ( &layers_allocator, char, size );
            //std_mem_copy ( ( char* ) layers[layer_i], renderdoc_layers[i], size );
            //++layer_i;
        }
    }

    for ( size_t i = 0; i < std_static_array_capacity_m ( base_extensions ); ++i ) {
        extensions[extension_i++] = std_stack_string_copy ( &extensions_allocator, base_extensions[i] );
        //size_t size = std_str_len ( base_extensions[i] ) + 1;
        //extensions[extension_i] = std_stack_push_array_m ( &extensions_allocator, char, size );
        //std_mem_copy ( ( char* ) extensions[extension_i], base_extensions[i], size );
        //++extension_i;
    }

    for ( size_t i = 0; i < std_static_array_capacity_m ( platform_extensions ); ++i ) {
        extensions[extension_i++] = std_stack_string_copy ( &extensions_allocator, platform_extensions[i] );
        //size_t size = std_str_len ( platform_extensions[i] ) + 1;
        //extensions[extension_i] = std_stack_push_array_m ( &extensions_allocator, char, size );
        //std_mem_copy ( ( char* ) extensions[extension_i], platform_extensions[i], size );
        //++extension_i;
    }

    // Create Instance
    std_assert_m ( layer_i == layers_count );
    std_assert_m ( extension_i == extensions_count );
    xg_vk_instance_state->vk_handle = xg_vk_instance_create ( layers, layers_count, extensions, extensions_count );
    xg_vk_instance_load_ext_api();

    // Debug
    xg_vk_instance_state->callback = VK_NULL_HANDLE;
    xg_vk_instance_create_debug_callback();
}

void xg_vk_instance_reload ( xg_vk_instance_state_t* state ) {
    xg_vk_instance_state = state;

    xg_vk_instance_load_ext_api();
    xg_vk_instance_destroy_debug_callback();
    xg_vk_instance_create_debug_callback();
}

void xg_vk_instance_unload ( void ) {
    xg_vk_instance_destroy_debug_callback();
    vkDestroyInstance ( xg_vk_instance_state->vk_handle, NULL );
}

VkInstance xg_vk_instance ( void ) {
    return xg_vk_instance_state->vk_handle;
}

xg_vk_instance_ext_api_i* xg_vk_instance_ext_api ( void ) {
    return &xg_vk_instance_state->ext_api;
}
