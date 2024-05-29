#include "xg_vk_buffer.h"

#include "xg_vk_device.h"
#include "xg_vk_enum.h"
#include "xg_vk_instance.h"

static xg_vk_buffer_state_t* xg_vk_buffer_state;

void xg_vk_buffer_load ( xg_vk_buffer_state_t* state ) {
    xg_vk_buffer_state = state;
    state->buffers_array = std_virtual_heap_alloc_array_m ( xg_vk_buffer_t, xg_vk_max_buffers_m );
    state->buffers_freelist = std_freelist_m ( state->buffers_array, xg_vk_max_buffers_m );
    std_mutex_init ( &state->buffers_mutex );
}

void xg_vk_buffer_reload ( xg_vk_buffer_state_t* state ) {
    xg_vk_buffer_state = state;
}

void xg_vk_buffer_unload ( void ) {
    std_virtual_heap_free ( xg_vk_buffer_state->buffers_array );
    std_mutex_deinit ( &xg_vk_buffer_state->buffers_mutex );
}

xg_buffer_h xg_buffer_create ( const xg_buffer_params_t* params ) {
#if 0
    // Get device
    const xg_vk_device_t* device = xg_vk_device_get ( params->device );

    // Create Vulkan buffer
    VkBuffer vk_buffer;
    VkBufferCreateInfo vk_buffer_info;
    vk_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vk_buffer_info.pNext = NULL;
    vk_buffer_info.flags = 0;
    vk_buffer_info.size = params->size;
    vk_buffer_info.usage = xg_buffer_usage_to_vk ( params->allowed_usage );
    vk_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_buffer_info.queueFamilyIndexCount = 0;
    vk_buffer_info.pQueueFamilyIndices = NULL;
    vkCreateBuffer ( device->vk_handle, &vk_buffer_info, NULL, &vk_buffer );

    if ( params->debug_name ) {
        VkDebugUtilsObjectNameInfoEXT debug_name;
        debug_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name.pNext = NULL;
        debug_name.objectType = VK_OBJECT_TYPE_BUFFER;
        debug_name.objectHandle = ( uint64_t ) vk_buffer;
        debug_name.pObjectName = params->debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );
    }

    // Query for memory requiremens, allocate and bind
    VkMemoryRequirements vk_buffer_memory;
    vkGetBufferMemoryRequirements ( device->vk_handle, vk_buffer, &vk_buffer_memory );
    xg_alloc_t alloc = params->allocator.alloc ( params->allocator.impl, params->device, vk_buffer_memory.size, vk_buffer_memory.alignment );
    vkBindBufferMemory ( device->vk_handle, vk_buffer, ( VkDeviceMemory ) alloc.base, ( VkDeviceSize ) alloc.offset );

    // Store xg_vk_buffer
    std_mutex_lock ( &xg_vk_buffer_state.buffers_mutex );
    xg_vk_buffer_t* buffer = std_list_pop_m ( &xg_vk_buffer_state.buffers_freelist );
    std_mutex_unlock ( &xg_vk_buffer_state.buffers_mutex );
    std_assert_m ( buffer );
    xg_buffer_h buffer_handle = ( xg_buffer_h ) ( buffer - xg_vk_buffer_state.buffers_array );

    buffer->vk_handle = vk_buffer;
    buffer->allocation = alloc;
    buffer->params = *params;
    buffer->state = xg_vk_buffer_state_created_m;

    return buffer_handle;
#else
    xg_buffer_h buffer_handle = xg_buffer_reserve ( params );
    std_verify_m ( xg_buffer_alloc ( buffer_handle ) );

    return buffer_handle;
#endif
}

xg_buffer_h xg_buffer_reserve ( const xg_buffer_params_t* params ) {
    std_mutex_lock ( &xg_vk_buffer_state->buffers_mutex );
    xg_vk_buffer_t* buffer = std_list_pop_m ( &xg_vk_buffer_state->buffers_freelist );
    std_mutex_unlock ( &xg_vk_buffer_state->buffers_mutex );
    std_assert_m ( buffer );
    xg_buffer_h buffer_handle = ( xg_buffer_h ) ( buffer - xg_vk_buffer_state->buffers_array );

    buffer->vk_handle = VK_NULL_HANDLE;
    buffer->allocation.handle = xg_null_memory_handle_m;
    buffer->params = *params;
    buffer->state = xg_vk_buffer_state_reserved_m;

    return buffer_handle;
}

bool xg_buffer_alloc ( xg_buffer_h buffer_handle ) {
    xg_vk_buffer_t* buffer = &xg_vk_buffer_state->buffers_array[buffer_handle];

    std_assert_m ( buffer->state == xg_vk_buffer_state_reserved_m );

    const xg_buffer_params_t* params = &buffer->params;

    const xg_vk_device_t* device = xg_vk_device_get ( params->device );

    // Create Vulkan buffer
    VkBuffer vk_buffer;
    VkBufferCreateInfo vk_buffer_info;
    vk_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    vk_buffer_info.pNext = NULL;
    vk_buffer_info.flags = 0;
    vk_buffer_info.size = params->size;
    vk_buffer_info.usage = xg_buffer_usage_to_vk ( params->allowed_usage );
    vk_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vk_buffer_info.queueFamilyIndexCount = 0;
    vk_buffer_info.pQueueFamilyIndices = NULL;
    VkResult result = vkCreateBuffer ( device->vk_handle, &vk_buffer_info, NULL, &vk_buffer );
    std_assert_m ( result == VK_SUCCESS );

    if ( params->debug_name[0] != '\0' ) {
        VkDebugUtilsObjectNameInfoEXT debug_name;
        debug_name.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        debug_name.pNext = NULL;
        debug_name.objectType = VK_OBJECT_TYPE_BUFFER;
        debug_name.objectHandle = ( uint64_t ) vk_buffer;
        debug_name.pObjectName = params->debug_name;
        xg_vk_instance_ext_api()->set_debug_name ( device->vk_handle, &debug_name );
    }

    // Query for memory requiremens, allocate and bind
    //VkMemoryRequirements vk_buffer_memory;
    //vkGetBufferMemoryRequirements ( device->vk_handle, vk_buffer, &vk_buffer_memory );
    VkMemoryDedicatedRequirements dedicated_requirements =
    {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS,
        .pNext = NULL,
    };
    VkMemoryRequirements2 memory_requirements_2 =
    {
        .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
        .pNext = &dedicated_requirements,
    };
    const VkBufferMemoryRequirementsInfo2 buffer_requirements_info =
    {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
        .pNext = NULL,
        .buffer = vk_buffer
    };
    vkGetBufferMemoryRequirements2 ( device->vk_handle, &buffer_requirements_info, &memory_requirements_2 );
    // TODO
    std_assert_m ( !dedicated_requirements.requiresDedicatedAllocation );
    VkMemoryRequirements vk_buffer_memory = memory_requirements_2.memoryRequirements;
    xg_alloc_t alloc = params->allocator.alloc ( params->allocator.impl, params->device, vk_buffer_memory.size, vk_buffer_memory.alignment );
    result = vkBindBufferMemory ( device->vk_handle, vk_buffer, ( VkDeviceMemory ) alloc.base, ( VkDeviceSize ) alloc.offset );
    std_assert_m ( result == VK_SUCCESS );

    buffer->vk_handle = vk_buffer;
    buffer->allocation = alloc;
    buffer->state = xg_vk_buffer_state_created_m;

    return true;
}

bool xg_buffer_get_info ( xg_buffer_info_t* info, xg_buffer_h buffer_handle ) {
    const xg_vk_buffer_t* buffer = &xg_vk_buffer_state->buffers_array[buffer_handle];

    if ( buffer == NULL ) {
        return false;
    }

    info->allocation = buffer->allocation;
    info->device = buffer->params.device;
    info->size = buffer->params.size;
    info->allowed_usage = buffer->params.allowed_usage;
    std_str_copy_m ( info->debug_name, buffer->params.debug_name );

    return true;
}

bool xg_buffer_destroy ( xg_buffer_h buffer_handle ) {
    std_mutex_lock ( &xg_vk_buffer_state->buffers_mutex );
    xg_vk_buffer_t* buffer = &xg_vk_buffer_state->buffers_array[buffer_handle];

    const xg_vk_device_t* device = xg_vk_device_get ( buffer->params.device );
    vkDestroyBuffer ( device->vk_handle, buffer->vk_handle, NULL );
    buffer->params.allocator.free ( buffer->params.allocator.impl, buffer->allocation.handle );

    std_list_push ( &xg_vk_buffer_state->buffers_freelist, buffer );
    std_mutex_unlock ( &xg_vk_buffer_state->buffers_mutex );

    return true;
}

const xg_vk_buffer_t* xg_vk_buffer_get ( xg_buffer_h buffer_handle ) {
    return &xg_vk_buffer_state->buffers_array[buffer_handle];
}
