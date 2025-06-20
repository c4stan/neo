#include "xg_vk_event.h"

#include "xg_vk.h"
#include "xg_vk_device.h"
#include "xg_vk_instance.h"

#include <std_list.h>

static xg_vk_event_state_t* xg_vk_event_state;

void xg_vk_event_load ( xg_vk_event_state_t* state ) {
    xg_vk_event_state = state;

    state->gpu_events_array = std_virtual_heap_alloc_array_m ( xg_vk_gpu_event_t, xg_vk_max_gpu_events_m );
    state->gpu_queue_events_array = std_virtual_heap_alloc_array_m ( xg_vk_gpu_queue_event_t, xg_vk_max_gpu_queue_events_m );
    state->cpu_queue_events_array = std_virtual_heap_alloc_array_m ( xg_vk_cpu_queue_event_t, xg_vk_max_cpu_queue_events_m );

    state->gpu_events_freelist = std_freelist_m ( state->gpu_events_array, xg_vk_max_gpu_events_m );
    state->gpu_queue_events_freelist = std_freelist_m ( state->gpu_queue_events_array, xg_vk_max_gpu_queue_events_m );
    state->cpu_queue_events_freelist = std_freelist_m ( state->cpu_queue_events_array, xg_vk_max_cpu_queue_events_m );

    std_mutex_init ( &state->gpu_events_mutex );
    std_mutex_init ( &state->gpu_queue_events_mutex );
    std_mutex_init ( &state->cpu_queue_events_mutex );

#if xg_debug_enable_events_log_m
    state->uid = 0;
#endif
}

void xg_vk_event_reload ( xg_vk_event_state_t* state ) {
    xg_vk_event_state = state;
}

void xg_vk_event_unload ( void ) {
    std_virtual_heap_free ( xg_vk_event_state->gpu_events_array );
    std_virtual_heap_free ( xg_vk_event_state->gpu_queue_events_array );
    std_virtual_heap_free ( xg_vk_event_state->cpu_queue_events_array );
}

#if 0
xg_gpu_event_h xg_gpu_event_create ( xg_device_h device_handle ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    std_mutex_lock ( &xg_vk_event_state->gpu_events_mutex );

    xg_vk_gpu_event_t* event = std_list_pop_m ( &xg_vk_event_state->gpu_events_freelist );
    std_assert_m ( event );

    VkEventCreateInfo event_create_info;
    event_create_info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
    event_create_info.pNext = NULL;
    event_create_info.flags = 0;
    vkCreateEvent ( device->vk_handle, &event_create_info, xg_vk_cpu_allocator(), &event->vk_event );

    event->device = device_handle;

    std_mutex_unlock ( &xg_vk_event_state->gpu_events_mutex );

    return ( xg_gpu_event_h ) ( event - xg_vk_event_state->gpu_events_array );
}

void xg_gpu_event_destroy ( xg_gpu_event_h event_handle ) {
    std_mutex_lock ( &xg_vk_event_state->gpu_events_mutex );

    xg_vk_gpu_event_t* event = &xg_vk_event_state->gpu_events_array[event_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( event->device );

    vkDestroyEvent ( device->vk_handle, event->vk_event, xg_vk_cpu_allocator() );

    std_list_push ( &xg_vk_event_state->gpu_events_freelist, event );

    std_mutex_unlock ( &xg_vk_event_state->gpu_events_mutex );
}

const xg_vk_gpu_event_t* xg_vk_gpu_event_get ( xg_gpu_event_h event_handle ) {
    return &xg_vk_event_state->gpu_events_array[event_handle];
}
#endif

// --

xg_queue_event_h xg_gpu_queue_event_create ( const xg_queue_event_params_t* params ) {
    const xg_vk_device_t* device = xg_vk_device_get ( params->device );
    std_mutex_lock ( &xg_vk_event_state->gpu_queue_events_mutex );
    xg_vk_gpu_queue_event_t* event = std_list_pop_m ( &xg_vk_event_state->gpu_queue_events_freelist );
    std_assert_m ( event )

    VkSemaphoreCreateInfo semaphore_create_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
    };
    VkResult result = vkCreateSemaphore ( device->vk_handle, &semaphore_create_info, xg_vk_cpu_allocator(), &event->vk_semaphore );
    std_verify_m ( result == VK_SUCCESS );

    xg_queue_event_h handle = event - xg_vk_event_state->gpu_queue_events_array;
    char debug_name[xg_debug_name_size_m];
    if ( params->debug_name[0] == 0 ) {
        std_u64_to_str ( debug_name, xg_debug_name_size_m, handle );
    }
    {
        VkDebugUtilsObjectNameInfoEXT debug_name_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .pNext = NULL,
            .objectType = VK_OBJECT_TYPE_SEMAPHORE,
            .objectHandle = ( uint64_t ) event->vk_semaphore,
            .pObjectName = debug_name,
        };
        xg_vk_device_ext_api ( params->device )->set_debug_name ( device->vk_handle, &debug_name_info );
    }

#if xg_debug_enable_events_log_m
    event->uid = xg_vk_event_state->uid++;
    std_log_info_m ( "[XG-VK-EVENT] Create " std_fmt_u64_m "-" std_fmt_u64_m, event->vk_semaphore, event->uid );
#endif

    event->params = *params;

    std_mutex_unlock ( &xg_vk_event_state->gpu_queue_events_mutex );

    return handle;
}

void xg_gpu_queue_event_destroy ( xg_queue_event_h event_handle ) {
    std_mutex_lock ( &xg_vk_event_state->gpu_queue_events_mutex );

    xg_vk_gpu_queue_event_t* event = &xg_vk_event_state->gpu_queue_events_array[event_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( event->params.device );

#if xg_debug_enable_events_log_m
    std_log_info_m ( "[XG-VK-EVENT] Destroy " std_fmt_u64_m "-" std_fmt_u64_m, event->vk_semaphore, event->uid );
#endif

    vkDestroySemaphore ( device->vk_handle, event->vk_semaphore, xg_vk_cpu_allocator() );

    std_list_push ( &xg_vk_event_state->gpu_queue_events_freelist, event );

    std_mutex_unlock ( &xg_vk_event_state->gpu_queue_events_mutex );
}

const xg_vk_gpu_queue_event_t* xg_vk_gpu_queue_event_get ( xg_queue_event_h event_handle ) {
    return &xg_vk_event_state->gpu_queue_events_array[event_handle];
}

void xg_gpu_queue_event_log_wait ( xg_queue_event_h event_handle ) {
#if xg_debug_enable_events_log_m
    xg_vk_gpu_queue_event_t* event = &xg_vk_event_state->gpu_queue_events_array[event_handle];
    std_log_info_m ( "[XG-VK-EVENT] Wait " std_fmt_u64_m "-" std_fmt_u64_m, event->vk_semaphore, event->uid );
#else
    std_unused_m ( event_handle );
#endif
}

void xg_gpu_queue_event_log_signal ( xg_queue_event_h event_handle ) {
#if xg_debug_enable_events_log_m
    xg_vk_gpu_queue_event_t* event = &xg_vk_event_state->gpu_queue_events_array[event_handle];
    std_log_info_m ( "[XG-VK-EVENT] Signal " std_fmt_u64_m "-" std_fmt_u64_m, event->vk_semaphore, event->uid );
#else
    std_unused_m ( event_handle );
#endif
}

// --

xg_cpu_queue_event_h xg_cpu_queue_event_create ( xg_device_h device_handle ) {
    const xg_vk_device_t* device = xg_vk_device_get ( device_handle );

    std_mutex_lock ( &xg_vk_event_state->cpu_queue_events_mutex );

    xg_vk_cpu_queue_event_t* event = std_list_pop_m ( &xg_vk_event_state->cpu_queue_events_freelist );
    std_assert_m ( event )

    VkFenceCreateInfo fence_create_info;
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.pNext = NULL;
    fence_create_info.flags = 0;
    vkCreateFence ( device->vk_handle, &fence_create_info, xg_vk_cpu_allocator(), &event->vk_fence );

    event->device = device_handle;

    std_mutex_unlock ( &xg_vk_event_state->cpu_queue_events_mutex );

    uint64_t idx = ( uint64_t ) ( event - xg_vk_event_state->cpu_queue_events_array );
    return ( xg_cpu_queue_event_h ) idx;
}

void xg_cpu_queue_event_destroy ( xg_cpu_queue_event_h event_handle ) {
    std_mutex_lock ( &xg_vk_event_state->cpu_queue_events_mutex );

    xg_vk_cpu_queue_event_t* event = &xg_vk_event_state->cpu_queue_events_array[event_handle];
    const xg_vk_device_t* device = xg_vk_device_get ( event->device );

    vkDestroyFence ( device->vk_handle, event->vk_fence, xg_vk_cpu_allocator() );

    std_list_push ( &xg_vk_event_state->cpu_queue_events_freelist, event );

    std_mutex_unlock ( &xg_vk_event_state->cpu_queue_events_mutex );
}

const xg_vk_cpu_queue_event_t* xg_vk_cpu_queue_event_get ( xg_cpu_queue_event_h event_handle ) {
    return &xg_vk_event_state->cpu_queue_events_array[event_handle];
}
